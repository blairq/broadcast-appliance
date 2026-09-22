// ============================================================================
// src/app/ui/StudioViewModel.cpp — ver StudioViewModel.hpp
// ============================================================================

#include "ui/StudioViewModel.hpp"

#if defined(BA_HAVE_CLAP)
#include "audio/AudioViewModel.hpp"
#include "audio/RackManager.hpp"
#endif

#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QStorageInfo>
#include <QUrl>

#include <util/platform.h>

#include <algorithm>
#include <cstdlib>

#include "AccountGuides.hpp"
#include "agent/AgentViewModel.hpp"
#include "agent/AlertEngine.hpp"
#include "agent/Despachador.hpp"
#include "agent/Roles.hpp"
#include "AppRuntime.hpp"
#include "net/HttpsClient.hpp"
#include "ui/ChatOverlayRenderer.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/EventoViewModel.hpp"
#include "diag/DiarioDeArranque.hpp"
#include "diag/Registro.hpp"
#include "graficos/Emisor.hpp"
#include "graficos/Vinculos.hpp"
#include "ui/GraficoViewModel.hpp"
#include "ui/ExtensionesModel.hpp"
#include "ui/RegistroViewModel.hpp"
#include "ui/RielConfig.hpp"
#include "ui/RotuladorViewModel.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/DashboardModel.hpp"
#include "ui/FacebookViewModel.hpp"
#include "ui/FrameService.hpp"
#include "ui/InputsViewModel.hpp"
#include "ui/ListModels.hpp"
#include "ui/OperatorMessages.hpp"
#include "ui/PropertiesModel.hpp"
#include "ui/SettingsModel.hpp"
#include "ui/SystemViewModel.hpp"
#include "ui/TwitchViewModel.hpp"
#include "ui/YouTubeViewModel.hpp"
#include <QCoreApplication>

namespace ba::app {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

QString hms(std::chrono::steady_clock::time_point since) {
    const auto s =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - since).count();
    return QString("%1:%2:%3")
        .arg(s / 3600, 2, 10, QChar('0'))
        .arg((s / 60) % 60, 2, 10, QChar('0'))
        .arg(s % 60, 2, 10, QChar('0'));
}

} // namespace

StudioViewModel::StudioViewModel(AppRuntime& rt, QObject* parent) : QObject(parent), rt_(rt) {
    // Modo seguro (el circuit breaker del launcher, tras varias caidas
    // seguidas): NADA que venga del disco. Aquel apaga todo; el diario apaga al
    // culpable. Juntos cubren los dos casos: el que rompe una cosa y el que
    // rompe algo que no sabemos cual es.
    if (rt.config().safeMode) {
        ba::diag::diario().setModoSeguro(true);
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "arranque", 0,
                                   "modo seguro: no se carga nada del disco en esta corrida");
    }
    scenes_ = std::make_unique<ScenesModel>(rt);
    sources_ = std::make_unique<SourcesModel>(rt);
    sources_->setScene(currentScene());
    mixer_ = std::make_unique<MixerModel>(rt);
    types_ = std::make_unique<SourceTypesModel>(rt);
    settings_ = std::make_unique<SettingsModel>(rt);
    {
        // La disposicion del dashboard sale de un archivo que se edita solo y
        // que tambien toca el asistente: si queda roto, no puede impedir que el
        // programa abra.
        ba::diag::Cargando c("dashboard");
        dashboard_ = std::make_unique<DashboardModel>(
            c.saltear() ? std::filesystem::path{} : rt.dataRoot() / "Config" / "ui.json");
    }
    frames_ = std::make_unique<FrameService>(rt.engine(), rt.config().guests);
    frames_->setLobbySlots(rt.gateway().lobbySlots);
    props_ = std::make_unique<PropertiesModel>(rt);
    conference_ = std::make_unique<ConferenceViewModel>(rt);
    // El encargo al asistente vive aca (necesita el MCP): la sala solo avisa.
    conference_->setIntakeSink(
        [this](const QString& id, const QString& name) { return assistantIntake(id, name); });
    connect(conference_.get(), &ConferenceViewModel::notify, this, &StudioViewModel::notify);
    // Un solo renderizador de gráficos para todos los que disparan uno: el
    // guion, el editor, el rotulador y el asistente.
    emisor_ = std::make_unique<graficos::Emisor>(rt.dataRoot());
    // El guion del evento necesita la sala para los cues de orador.
    {
        // El guion abre solo el ultimo evento que se toco, que es un archivo
        // del disco y puede venir de cualquier lado.
        ba::diag::Cargando c("evento");
        evento_ = std::make_unique<EventoViewModel>(rt, conference_.get(), emisor_.get(),
                                                    c.saltear());
    }
    connect(evento_.get(), &EventoViewModel::notify, this, &StudioViewModel::notify);
    grafico_ = std::make_unique<GraficoViewModel>(rt, *emisor_);
    // El rotulador mira la escena que está al aire, así que la sigue.
    rotulador_ = std::make_unique<RotuladorViewModel>(rt, *emisor_, conference_.get());
    connect(rotulador_.get(), &RotuladorViewModel::notify, this, &StudioViewModel::notify);
    connect(this, &StudioViewModel::sceneChanged, rotulador_.get(), &RotuladorViewModel::refrescar);

    // Los vínculos: «el nombre del zócalo sale del orador que está al aire».
    // El contexto se arma acá porque es el único lugar que ve todo junto: la
    // sala sabe quién habla, el guion sabe quién sigue, el motor sabe qué
    // escena está al aire.
    ba::diag::Cargando cargandoVinculos("vinculos");
    vinculos_ = std::make_unique<graficos::Vinculos>(
        cargandoVinculos.saltear() ? std::filesystem::path{}
                                   : rt.dataRoot() / "Config" / "vinculos.json",
        *emisor_);
    vinculos_->setContexto([this]() { return contextoDeVinculos(); });
    rotulador_->setVinculos(vinculos_.get());
    evento_->setVinculos(vinculos_.get());

    {
        // Mirar la carpeta es leer disco y parsear JSON ajeno: si eso se
        // llevara puesto el arranque, la proxima vez se saltea.
        ba::diag::Cargando c("extensiones");
        extensiones_ = std::make_unique<ExtensionesModel>(rt);
        if (c.saltear()) extensiones_.reset();
    }
    if (extensiones_)
        connect(extensiones_.get(), &ExtensionesModel::notify, this, &StudioViewModel::notify);

    // El riel se lee siempre, tambien en modo seguro: es COMO SE NAVEGA. Si
    // no se cargara, el operador quedaria sin poder llegar a Configuracion
    // justo cuando algo anda mal.
    riel_ = std::make_unique<RielConfig>(rt.dataRoot() / "Config" / "riel.json");

    registro_ = std::make_unique<RegistroViewModel>(rt);
    connect(registro_.get(), &RegistroViewModel::notify, this, &StudioViewModel::notify);
    connect(vinculos_.get(), &graficos::Vinculos::actuo, this,
            [this](const QString&, const QString& texto, bool error) {
                if (error) emit notify("Un rótulo automático no salió: " + texto, "error");
            });
    connect(grafico_.get(), &GraficoViewModel::notify, this, &StudioViewModel::notify);
    // HTTPS propio (plataformas): raices de certificados del bundle primero.
    net::HttpsClient::setDefaultCaSearch(
        {(rt.dataRoot() / "data" / "cacert.pem").string(),
         (QCoreApplication::applicationDirPath() + "/../../data/cacert.pem").toStdString()});
    youtube_ = std::make_unique<YouTubeViewModel>(rt);
    connect(youtube_.get(), &YouTubeViewModel::notify, this, &StudioViewModel::notify);
    twitch_ = std::make_unique<TwitchViewModel>(rt);
    connect(twitch_.get(), &TwitchViewModel::notify, this, &StudioViewModel::notify);
    facebook_ = std::make_unique<FacebookViewModel>(rt);
    connect(facebook_.get(), &FacebookViewModel::notify, this, &StudioViewModel::notify);
    system_ = std::make_unique<SystemViewModel>();
    connect(system_.get(), &SystemViewModel::quitForUpdate, this, [this]() {
        emit notify("Cerrando para actualizar: el sistema vuelve a abrir solo en unos segundos.", "info");
        QTimer::singleShot(800, this, [this]() { emit quitRequested(); });
    });
    chatOverlay_ = std::make_unique<ChatOverlayRenderer>(*youtube_, *conference_, *twitch_, *facebook_);
    // Vigilancias (alertas) contra el estado real de la app, y la pestaña Asistente.
    {
        agent::AlertInputs in;
        in.audio = [this]() {
            std::vector<agent::AlertInputs::Audio> out;
            for (const std::string& n : rt_.engine().audioSources())
                out.push_back({n, rt_.engine().sourcePeakDb(n), rt_.engine().sourceMuted(n)});
            return out;
        };
        in.media = [this]() {
            std::vector<agent::AlertInputs::Media> out;
            for (const auto& m : rt_.engine().mediaSources())
                out.push_back({m.name, m.state, m.durationMs, m.timeMs});
            return out;
        };
        in.room = [this]() {
            return nlohmann::json::parse(
                QJsonDocument(conference_->client().snapshot()).toJson(QJsonDocument::Compact).toStdString(),
                nullptr, false);
        };
        in.chat = [this]() {
            std::vector<nlohmann::json> out;
            for (const auto& m : chatOverlay_->collect())
                out.push_back({{"key", m.key.toStdString()},
                               {"platform", m.source.toStdString()},
                               {"author", m.author.toStdString()},
                               {"text", m.text.toStdString()}});
            return out;
        };
        in.roomChat = [this]() {
            return nlohmann::json::parse(
                QJsonDocument(static_cast<JsonListModel*>(conference_->chat())->items())
                    .toJson(QJsonDocument::Compact)
                    .toStdString(),
                nullptr, false);
        };
        in.status = [this]() {
            const EngineStatus s = rt_.engine().status();
            return agent::AlertInputs::Status{s.streaming, s.recording, s.droppedFrames, s.totalFrames};
        };
        in.diskFreeGb = [this]() {
            const QStorageInfo si(QString::fromStdWString((rt_.dataRoot() / "Recordings").wstring()));
            return si.isValid() ? static_cast<double>(si.bytesAvailable()) / 1e9 : -1.0;
        };
        alerts_ = std::make_unique<agent::AlertEngine>(std::move(in),
                                                       (rt.dataRoot() / "Config" / "alertas.json").string());
        connect(alerts_.get(), &agent::AlertEngine::fired, this, [this](const nlohmann::json& ev) {
            for (const auto& n : ev.value("notify", nlohmann::json::array()))
                if (n == "ui") emit notify("⚠ " + QString::fromStdString(ev.value("text", "")), "warn");
        });
        alerts_->start(250);
        agent_ = std::make_unique<agent::AgentViewModel>(rt, *alerts_);
        connect(agent_.get(), &agent::AgentViewModel::notify, this, &StudioViewModel::notify);
        // Los roles automaticos: un disparo que un rol activo reclama va a un
        // agente de un tiro y NO al chat del asistente interactivo.
        roles_ = std::make_unique<agent::Roles>(rt.dataRoot() / "Config" / "roles.json");
        despachador_ = std::make_unique<agent::Despachador>(*roles_, agent_->motores());
        connect(despachador_.get(), &agent::Despachador::notify, this, &StudioViewModel::notify);
        agent_->setReclamado([this](const nlohmann::json& ev) { return despachador_->evento(ev); });
        // Barra de mensajes del asistente al operador (y respuestas a invitados).
        messages_ = std::make_unique<OperatorMessages>();
        if (rt.config().safeMode) {
            // El launcher arranco en modo seguro tras varias caidas: que el
            // operador sepa por que no ve sus escenas.
            messages_->post(
                {{"kind", "message"},
                 {"priority", "critical"},
                 {"title", "Modo seguro"},
                 {"text",
                  "La aplicación se cerró varias veces seguidas y arrancó en modo seguro: tus escenas no "
                  "se cargaron (no se borraron). Cerrá y volvé a abrir para intentar el arranque normal."}});
        }
        messages_->setChatSender([this](const QString& to, const QString& text) {
            conference_->sendChat(to, text);
            return QString();
        });
        messages_->setCredentialSink([this](const QString& platform, const QVariantMap& values) {
            return saveAccountCredential(platform, values);
        });
        messages_->setLateAnswerSink([this](const nlohmann::json& item) {
            const std::string kind = item.value("kind", "");
            const std::string st = item.value("status", "");
            const QString text = QString::fromStdString(item.value("text", ""));
            const QString answer = QString::fromStdString(item.value("answer", ""));
            // Sin el nombre (lo eligio el invitado): solo su id.
            const QString who = "el invitado " + QString::fromStdString(item.value("participantId", "?"));
            QString note;
            if (kind == "question")
                note = st == "answered" ? "[RESPUESTA DEL OPERADOR] a tu pregunta \"" + text + "\": " + answer
                                        : "[OPERADOR] descarto tu pregunta \"" + text + "\".";
            else if (kind == "reply")
                note = st == "sent" ? "[OPERADOR] le mando a " + who + ": \"" + answer + "\""
                                    : "[OPERADOR] ignoro el mensaje de " + who + ".";
            else if (kind == "credential") {
                const QString p = QString::fromStdString(item.value("platform", ""));
                note = st == "saved"
                           ? "[OPERADOR] guardo la credencial de " + p + ". Siguiente paso: " + p + "_login."
                           : "[OPERADOR] cerro el formulario de credenciales de " + p + " sin guardar.";
            }
            if (!note.isEmpty()) agent_->deliverNote(note);
        });
    }
    inputs_ = std::make_unique<InputsViewModel>(rt, *this);
#if defined(BA_HAVE_CLAP)
    // Racks de audio (LSP) de cada fuente: se montan los guardados.
    racks_ = std::make_unique<audio::RackManager>(rt.dataRoot());
    racks_->restore();
    audio_ = std::make_unique<audio::AudioViewModel>(*racks_);
#endif
    // Las etiquetas de video ("Invitado 2") pasan a ser el nombre real.
    connect(conference_.get(), &ConferenceViewModel::roomChanged, this,
            [this]() { frames_->setGuestNames(conference_->slotNames()); });
    connect(conference_.get(), &ConferenceViewModel::sourceAdded, this, [this]() {
        sources_->reload();
        mixer_->reload();
    });
    cpuInfo_ = os_cpu_usage_info_start();

    connect(sources_.get(), &SourcesModel::error, this,
            [this](const QString& m) { emit notify(m, "error"); });
    connect(settings_.get(), &SettingsModel::applied, this, [this](const QString& summary) {
        emit notify(summary, summary.contains("ERROR") ? "error" : "ok");
        emit canvasChanged();
        emit themeChanged();
        emit sceneChanged();
        frames_->setGuests(rt_.config().guests);
    });

    connect(&tick_, &QTimer::timeout, this, &StudioViewModel::onTick);
    tick_.start(100);
    connect(&frameTick_, &QTimer::timeout, frames_.get(), &FrameService::poll);
    frameTick_.start(16); // hasta 60 Hz; FrameService solo avisa si hay frame nuevo
}

StudioViewModel::~StudioViewModel() {
    if (cpuInfo_ != nullptr) {
        os_cpu_usage_info_destroy(static_cast<os_cpu_usage_info_t*>(cpuInfo_));
    }
}

void StudioViewModel::onTick() {
    rt_.tick();
    if (AppRuntime::stopRequested()) {
        emit quitRequested();
        return;
    }
    const EngineStatus st = rt_.engine().status();
    if (st.recording && !recording_) {
        recSince_ = std::chrono::steady_clock::now();
    }
    if (st.streaming && !streaming_) {
        streamSince_ = std::chrono::steady_clock::now();
        lastBytes_ = 0;
        lastBytesAt_ = streamSince_;
    }
    // Un corte pedido es un aviso corto; uno que nadie pidio (red, servidor,
    // disco lleno) queda fijo en la barra del operador hasta que lo cierre.
    const auto dropped = [this](const char* title, const std::string& text) {
        if (messages_) {
            messages_->post(
                {{"kind", "message"}, {"priority", "critical"}, {"title", title}, {"text", text}});
        } else {
            emit notify(QString::fromUtf8(title), "error");
        }
    };
    if (recording_ && !st.recording) {
        if (rt_.recordStopWanted()) {
            emit notify("Grabación detenida", "info");
        } else {
            rt_.stopRecording(); // ya no hay nada que retomar tras una caida
            dropped("Se cortó la grabación",
                    "La grabación se detuvo sola. Revisá el espacio libre en el disco y volvé a grabar.");
        }
    }
    if (streaming_ && !st.streaming) {
        if (rt_.streamStopWanted()) {
            emit notify("Transmisión detenida", "warn");
        } else {
            rt_.stopStreaming();
            dropped(
                "Se cayó la transmisión",
                "La emisión se cortó sola (internet o el servidor de destino). Revisá la conexión y volvé a "
                "salir al aire.");
        }
    }
    recording_ = st.recording;
    streaming_ = st.streaming;
    // Avisar UNA vez por problema, no en cada vuelta del reloj.
    if (st.reconnecting && !reconnecting_)
        emit notify("Se cortó la transmisión: reconectando… (" + qs(st.outputProblem) + ")", "error");
    else if (!st.reconnecting && reconnecting_ && st.streaming)
        emit notify("Transmisión recuperada", "ok");
    reconnecting_ = st.reconnecting;
    outputProblem_ = qs(st.outputProblem);
    fps_ = st.fps;
    encoder_ = qs(st.videoEncoder);
    dropped_ = st.droppedFrames;
    total_ = st.totalFrames;
    // Bitrate real de la transmision (promedio de ~1 s).
    const auto now = std::chrono::steady_clock::now();
    const double dt = std::chrono::duration<double>(now - lastBytesAt_).count();
    if (st.streaming && dt >= 1.0) {
        kbps_ = static_cast<int>((st.bytesSent - lastBytes_) * 8.0 / 1000.0 / dt);
        lastBytes_ = st.bytesSent;
        lastBytesAt_ = now;
    } else if (!st.streaming) {
        kbps_ = 0;
    }
    cpu_ = os_cpu_usage_info_query(static_cast<os_cpu_usage_info_t*>(cpuInfo_));
    gatewayAlive_ = rt_.gatewayAlive();
    float l = -100;
    float r = -100;
    rt_.engine().programPeaksDb(l, r);
    peakL_ = std::max<double>(l, peakL_ - 1.5);
    peakR_ = std::max<double>(r, peakR_ - 1.5);
    mixer_->refreshLevels();
    emit statusChanged();
}

QString StudioViewModel::recordTime() const { return recording_ ? hms(recSince_) : QString("00:00:00"); }
QString StudioViewModel::streamTime() const { return streaming_ ? hms(streamSince_) : QString("00:00:00"); }
QString StudioViewModel::currentScene() const { return qs(rt_.engine().currentScene()); }
QString StudioViewModel::transitionType() const { return qs(rt_.config().transition.type); }
double StudioViewModel::canvasAspect() const {
    const VideoConfig& v = rt_.config().video;
    return v.baseHeight > 0 ? double(v.baseWidth) / v.baseHeight : 16.0 / 9.0;
}
QString StudioViewModel::canvasLabel() const {
    const VideoConfig& v = rt_.config().video;
    return QString("%1x%2 · %3 fps").arg(v.outputWidth).arg(v.outputHeight).arg(v.fpsNum);
}
QString StudioViewModel::streamServer() const { return qs(rt_.config().streamCfg.server); }
bool StudioViewModel::streamKeyAvailable() const {
    const char* k = std::getenv(rt_.config().streamKeyEnv.c_str());
    return k != nullptr && *k != '\0';
}
bool StudioViewModel::streamKeySaved() const { return rt_.streamKeySaved(); }
QString StudioViewModel::room() const { return qs(const_cast<AppRuntime&>(rt_).gateway().room); }
QString StudioViewModel::joinUrl() const {
    const GatewaySettings& g = const_cast<AppRuntime&>(rt_).gateway();
    QString host = "127.0.0.1";
    if (g.bindAddress != "127.0.0.1") {
        // Red local: la primera IPv4 no loopback.
        for (const QHostAddress& a : QNetworkInterface::allAddresses()) {
            if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback()) {
                host = a.toString();
                break;
            }
        }
    }
    return QString("http://%1:%2").arg(host).arg(g.port);
}
int StudioViewModel::guests() const { return rt_.config().guests; }
QString StudioViewModel::theme() const { return qs(rt_.config().theme); }

QObject* StudioViewModel::scenes() const { return scenes_.get(); }
QObject* StudioViewModel::sources() const { return sources_.get(); }
QObject* StudioViewModel::mixer() const { return mixer_.get(); }
QObject* StudioViewModel::sourceTypes() const { return types_.get(); }
QObject* StudioViewModel::settings() const { return settings_.get(); }
QObject* StudioViewModel::dashboard() const { return dashboard_.get(); }
QObject* StudioViewModel::frames() const { return frames_.get(); }
QObject* StudioViewModel::properties() const { return props_.get(); }
QObject* StudioViewModel::youtube() const { return youtube_.get(); }
QObject* StudioViewModel::twitch() const { return twitch_.get(); }
QObject* StudioViewModel::facebook() const { return facebook_.get(); }
QObject* StudioViewModel::system() const { return system_.get(); }
QObject* StudioViewModel::agent() const { return agent_.get(); }
QObject* StudioViewModel::despachadorQml() const { return despachador_.get(); }
QObject* StudioViewModel::messages() const { return messages_.get(); }

QObject* StudioViewModel::audio() const {
#if defined(BA_HAVE_CLAP)
    return audio_.get();
#else
    return nullptr;
#endif
}

QVariantMap StudioViewModel::accountGuide(const QString& platform) const {
    const nlohmann::json g = accounts::guide(platform.toStdString(), facebook_->redirectUri().toStdString());
    return QJsonDocument::fromJson(QByteArray::fromStdString(g.dump())).object().toVariantMap();
}

QString StudioViewModel::saveAccountCredential(const QString& platform, const QVariantMap& v) {
    const std::string p = accounts::normalize(platform.toStdString());
    if (p == "youtube") {
        QString path = v.value("importFile").toString();
        if (v.value("fromDownloads").toBool()) {
            QString why;
            path = accounts::findGoogleClientJson(&why);
            if (path.isEmpty()) return why;
        }
        if (!path.isEmpty()) {
            const QUrl u(path);
            QFile f(u.isLocalFile() ? u.toLocalFile() : path);
            if (!f.open(QIODevice::ReadOnly)) return "No se pudo leer " + f.fileName() + ".";
            return youtube_->saveClient(QString::fromUtf8(f.readAll()));
        }
        return youtube_->saveClient(v.value("clientId").toString().trimmed(),
                                    v.value("clientSecret").toString().trimmed());
    }
    if (p == "twitch")
        return twitch_->saveClient(v.value("clientId").toString().trimmed(),
                                   v.value("clientSecret").toString().trimmed());
    if (p == "facebook")
        return facebook_->saveClient(v.value("appId").toString().trimmed(),
                                     v.value("appSecret").toString().trimmed(), facebook_->client().port());
    return "Plataforma desconocida: " + platform + ".";
}
QObject* StudioViewModel::inputs() const { return inputs_.get(); }
QObject* StudioViewModel::conference() const { return conference_.get(); }
QObject* StudioViewModel::evento() const { return evento_.get(); }
QObject* StudioViewModel::grafico() const { return grafico_.get(); }
QObject* StudioViewModel::rotulador() const { return rotulador_.get(); }
QObject* StudioViewModel::registro() const { return registro_.get(); }
QObject* StudioViewModel::extensiones() const { return extensiones_.get(); }
QObject* StudioViewModel::riel() const { return riel_.get(); }

void StudioViewModel::setSeccionConfig(const QString& s) {
    if (s == seccionConfig_) return;
    seccionConfig_ = s;
    emit seccionConfigChanged();
}

QJsonObject StudioViewModel::contextoDeVinculos() const {
    QJsonObject ctx;
    ctx["escena"] = currentScene();

    // La sala: quién está al aire y quiénes esperan, en el orden en que los
    // ve el operador. `invitado1`, `invitado2`... son ESE orden, que es lo que
    // uno tiene delante cuando dice «el invitado 2».
    const QJsonObject sala = conference_ != nullptr ? conference_->client().snapshot() : QJsonObject{};
    QHash<QString, QJsonObject> porId;
    for (const QJsonValue& v : sala.value("participants").toArray())
        porId.insert(v.toObject().value("id").toString(), v.toObject());

    const auto deParticipante = [](const QJsonObject& p) {
        return QJsonObject{{"nombre", p.value("name").toString()},
                           // `info` es lo que el invitado puso de sí mismo al
                           // entrar: es el cargo o la procedencia.
                           {"cargo", p.value("info").toString()},
                           {"contacto", p.value("contact").toString()}};
    };

    int n = 0;
    bool hayOrador = false;
    QJsonObject primeroEnCola;
    for (const QJsonValue& v : sala.value("order").toArray()) {
        const auto it = porId.constFind(v.toString());
        if (it == porId.constEnd()) continue;
        const QJsonObject p = *it;
        ctx["invitado" + QString::number(++n)] = deParticipante(p);
        const QString estado = p.value("speaker").toString();
        if (estado == "onair" && !hayOrador) {
            ctx["orador"] = deParticipante(p);
            hayOrador = true;
        } else if (estado == "queued" && primeroEnCola.isEmpty()) {
            primeroEnCola = deParticipante(p);
        }
    }
    if (!primeroEnCola.isEmpty()) ctx["siguiente"] = primeroEnCola;

    // El guion: qué bloque va y qué dice el cue. Si el cue que corre es de
    // orador, ese es el orador según el guion, que puede no ser el mismo que
    // el de la sala (alguien que habla desde el estudio, sin conectarse).
    if (evento_ != nullptr) {
        const QVariantMap actual = evento_->property("actual").toMap();
        const QVariantMap sig = evento_->property("siguiente").toMap();
        QJsonObject guion{{"bloque", actual.value("bloqueNombre").toString()},
                          {"cue", actual.value("texto").toString()},
                          {"tipo", actual.value("tipo").toString()},
                          {"proximo", sig.value("texto").toString()}};
        const QVariantMap datos = actual.value("datos").toMap();
        if (actual.value("tipo").toString() == "orador")
            guion["orador"] = datos.value("participante").toString();
        ctx["guion"] = guion;
        ctx["evento"] = QJsonObject{{"nombre", evento_->property("nombre").toString()}};
    }
    return ctx;
}

void StudioViewModel::toggleRecording() {
    if (recording_) {
        rt_.stopRecording();
    } else if (rt_.startRecording()) {
        emit notify("Grabando en " + qs(rt_.config().rec.directory), "ok");
    } else {
        emit notify("No se pudo grabar: " + qs(rt_.lastError()), "error");
    }
    onTick();
}

void StudioViewModel::startStreaming(const QString& server, const QString& key) {
    if (rt_.startStreaming(server.toStdString(), key.toStdString())) {
        emit notify("Al aire", "ok");
    } else {
        emit notify("No se pudo transmitir: " + qs(rt_.lastError()), "error");
    }
    onTick();
}

void StudioViewModel::stopStreaming() {
    rt_.stopStreaming();
    onTick();
}

void StudioViewModel::takeScene(const QString& name) {
    if (!rt_.setScene(name.toStdString())) {
        emit notify(qs(rt_.lastError()), "error");
        return;
    }
    scenes_->liveChanged();
    sources_->setScene(name); // el editor de fuentes sigue a la escena al aire
    emit sceneChanged();
    rt_.save();
}

void StudioViewModel::addScene(const QString& name) {
    if (!rt_.addScene(name.trimmed().toStdString())) {
        emit notify(qs(rt_.lastError()), "error");
        return;
    }
    scenes_->reload();
    rt_.save();
}

QString StudioViewModel::copyScene(const QString& from, const QString& to) {
    // Sin nombre, se propone uno libre: copiar tiene que ser un clic, no un
    // dialogo. Se renombra despues, que es cuando uno ya sabe como llamarla.
    QString destino = to;
    if (destino.isEmpty()) {
        const auto existe = [this](const QString& n) {
            for (const std::string& s : rt_.orderedSceneNames())
                if (qs(s) == n) return true;
            return false;
        };
        destino = from + " (copia)";
        for (int i = 2; existe(destino); ++i) destino = from + " (copia " + QString::number(i) + ")";
    }
    if (!rt_.copyScene(from.toStdString(), destino.toStdString())) {
        emit notify(qs(rt_.lastError()), "error");
        return {};
    }
    scenes_->reload();
    emit sceneChanged();
    rt_.save();
    const std::string aviso = rt_.lastError();
    emit notify(aviso.empty() ? "Escena copiada: " + destino
                              : "Escena copiada, pero " + qs(aviso),
                aviso.empty() ? "ok" : "warn");
    return destino;
}

void StudioViewModel::setSceneProtected(const QString& name, bool protegida) {
    if (!rt_.setSceneProtected(name.toStdString(), protegida)) {
        emit notify(qs(rt_.lastError()), "error");
        return;
    }
    scenes_->reload();
    rt_.save();
    emit notify(protegida ? "«" + name + "» protegida: no se puede borrar ni renombrar"
                          : "«" + name + "» ya no está protegida",
                "ok");
}

bool StudioViewModel::sceneProtected(const QString& name) const {
    return rt_.sceneProtected(name.toStdString());
}

void StudioViewModel::removeScene(const QString& name) {
    if (!rt_.removeScene(name.toStdString())) {
        emit notify(qs(rt_.lastError()), "error");
        return;
    }
    scenes_->reload();
    if (sources_->scene() == name) {
        sources_->setScene(currentScene());
    }
    emit sceneChanged();
    rt_.save();
}

void StudioViewModel::setSceneFolder(const QString& name, const QString& folder) {
    if (!rt_.setSceneFolder(name.toStdString(), folder.trimmed().toStdString())) {
        emit notify(qs(rt_.lastError()), "error");
        return;
    }
    scenes_->reload();
    emit sceneChanged();
    rt_.save();
}

QStringList StudioViewModel::sceneFolders() const {
    QStringList out;
    for (const auto& f : rt_.sceneFolders())
        out << qs(f);
    return out;
}

QString StudioViewModel::assistantIntake(const QString& id, const QString& name) {
    if (!intakeSink_) return "El servidor MCP no está disponible.";
    return intakeSink_(id, name);
}

void StudioViewModel::moveScene(const QString& name, const QString& before) {
    if (!rt_.moveScene(name.toStdString(), before.toStdString())) {
        emit notify(qs(rt_.lastError()), "error");
        return;
    }
    scenes_->reload();
    emit sceneChanged();
    rt_.save();
}

void StudioViewModel::renameScene(const QString& from, const QString& to) {
    if (!rt_.renameScene(from.toStdString(), to.trimmed().toStdString())) {
        emit notify(qs(rt_.lastError()), "error");
        return;
    }
    scenes_->reload();
    if (sources_->scene() == from) {
        sources_->setScene(to.trimmed());
    }
    emit sceneChanged();
    rt_.save();
}

void StudioViewModel::save() {
    if (rt_.save()) {
        emit notify("Guardado", "ok");
    } else {
        emit notify("No se pudo guardar: " + qs(rt_.lastError()), "error");
    }
}

void StudioViewModel::refreshModels() {
    scenes_->reload();
    scenes_->liveChanged();
    // Si la escena que mostraba el editor ya no existe, sigue a la del aire.
    const std::vector<std::string> names = rt_.engine().sceneNames();
    if (std::find(names.begin(), names.end(), sources_->scene().toStdString()) == names.end()) {
        sources_->setScene(currentScene());
    }
    sources_->reload();
    mixer_->reload();
    emit sceneChanged();
    emit statusChanged();
}

void StudioViewModel::setScenePreview(const QString& name) {
    // 960 px de ancho alcanza para el lienzo del editor y no castiga la GPU.
    rt_.engine().enableScenePreview(name.toStdString(), 960);
}

} // namespace ba::app
