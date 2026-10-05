// ============================================================================
// src/app/ui/StudioViewModel.cpp — ver StudioViewModel.hpp
// ============================================================================

#include "ui/StudioViewModel.hpp"
#include "platform/Paths.hpp"
#include <fstream>
#include <QClipboard>
#include <QDateTime>
#include <QElapsedTimer>

#include <QGuiApplication>
#include <QScreen>
#include <QWindow>

#if defined(BA_HAVE_CLAP)
#include "audio/AudioViewModel.hpp"
#include "audio/Matriz.hpp"
#include "audio/RackManager.hpp"
#endif

#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QPointer>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QStorageInfo>
#include <QUrl>

#include <util/platform.h>

#include <algorithm>
#include <cstdlib>

#include "AccountGuides.hpp"
#include "agent/AgentViewModel.hpp"
#include "agent/AlarmaSonora.hpp"
#include "agent/SubtitulosCC.hpp"
#include "agent/Vocabulario.hpp"
#include "control/Botonera.hpp"
#include "control/BusEventos.hpp"
#include "control/Dsk.hpp"
#include "control/Acciones.hpp"
#include "control/SatelliteServidor.hpp"
#include "qr/Qr.hpp"
#include "legislatura/Bancas.hpp"
#include "legislatura/SeguirMicrofono.hpp"
#include "legislatura/OrdenDelDia.hpp"
#include "legislatura/Quorum.hpp"
#include "legislatura/Cuerpo.hpp"
#include "legislatura/CuerpoViewModel.hpp"
#include "agent/AlertEngine.hpp"
#include "agent/Despachador.hpp"
#include "agent/FlujosTexto.hpp"
#include "agent/ModelosLocales.hpp"
#include "agent/Roles.hpp"
#include "agent/Subtitulos.hpp"
#include "agent/Traductor.hpp"
#include "agent/VocesRack.hpp"
#include "agent/Voz.hpp"
#include "agent/VozPrompter.hpp"
#include "AppRuntime.hpp"
#include "diag/DiarioDeArranque.hpp"
#include "diag/Registro.hpp"
#include "dispositivos/Dispositivos.hpp"
#include "dispositivos/Registro.hpp"
#include "graficos/Emisor.hpp"
#include "graficos/Vinculos.hpp"
#include "net/HttpsClient.hpp"
#include "plugins/Plugins.hpp"
#include "ptz/ControlCamaras.hpp"
#include "red/Red.hpp"
#include "ui/CarruselViewModel.hpp"
#include "ui/ChatOverlayRenderer.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/DashboardModel.hpp"
#include "ui/EventoViewModel.hpp"
#include "ui/ExtensionesModel.hpp"
#include "ui/FacebookViewModel.hpp"
#include "ui/FrameService.hpp"
#include "ui/GraficoViewModel.hpp"
#include "ui/InputsViewModel.hpp"
#include "ui/ListModels.hpp"
#include "ui/MediosViewModel.hpp"
#include "ui/OperatorMessages.hpp"
#include "ui/PantallasViewModel.hpp"
#include "ui/PersonasViewModel.hpp"
#include "ui/PrompterViewModel.hpp"
#include "ui/PropertiesModel.hpp"
#include "ui/PublicoViewModel.hpp"
#include "ui/QrViewModel.hpp"
#include "ui/RegistroViewModel.hpp"
#include "ui/RielConfig.hpp"
#include "ui/RotuladorViewModel.hpp"
#include "ui/Seguidor.hpp"
#include "ui/SettingsModel.hpp"
#include "ui/SystemViewModel.hpp"
#include "ui/TwitchViewModel.hpp"
#include "ui/YouTubeViewModel.hpp"
#include "video/SuperRes.hpp"
#include "video/VideoGrafos.hpp"
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
    // El arranque, medido: cada etapa que tarde más de 150 ms queda en el registro
    // (la interfaz tardaba ~12 s en aparecer y no se sabía en qué).
    QElapsedTimer relojArranque;
    relojArranque.start();
    qint64 ultimaMarca = 0;
    const auto marca = [&relojArranque, &ultimaMarca](const char* que) {
        const qint64 ahora = relojArranque.elapsed();
        if (ahora - ultimaMarca > 150)
            ba::diag::registro().info("arranque: " + std::string(que) + " tardó " +
                                      std::to_string(ahora - ultimaMarca) + " ms");
        ultimaMarca = ahora;
    };
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
    marca("scenes");
    sources_ = std::make_unique<SourcesModel>(rt);
    marca("sources");
    sources_->setScene(currentScene());
    mixer_ = std::make_unique<MixerModel>(rt);
    marca("mixer");
    types_ = std::make_unique<SourceTypesModel>(rt);
    marca("types");
    settings_ = std::make_unique<SettingsModel>(rt);
    marca("settings");
    {
        // La disposicion del dashboard sale de un archivo que se edita solo y
        // que tambien toca el asistente: si queda roto, no puede impedir que el
        // programa abra.
        ba::diag::Cargando c("dashboard");
        dashboard_ = std::make_unique<DashboardModel>(c.saltear() ? std::filesystem::path{}
                                                                  : rt.dataRoot() / "Config" / "ui.json");
        marca("dashboard");
    }
    frames_ = std::make_unique<FrameService>(rt.engine(), rt.config().guests);
    marca("frames");
    frames_->setLobbySlots(rt.gateway().lobbySlots);
    props_ = std::make_unique<PropertiesModel>(rt);
    marca("props");
    conference_ = std::make_unique<ConferenceViewModel>(rt);
    marca("conference");
    // El encargo al asistente vive aca (necesita el MCP): la sala solo avisa.
    conference_->setIntakeSink([this](const QString& id, const QString& name, bool yaAdentro) {
        return assistantIntake(id, name, yaAdentro);
    });
    connect(conference_.get(), &ConferenceViewModel::notify, this, &StudioViewModel::notify);
    // Un solo renderizador de gráficos para todos los que disparan uno: el
    // guion, el editor, el rotulador y el asistente.
    emisor_ = std::make_unique<graficos::Emisor>(rt.dataRoot());
    marca("emisor");
    // El guion del evento necesita la sala para los cues de orador.
    {
        // El guion abre solo el ultimo evento que se toco, que es un archivo
        // del disco y puede venir de cualquier lado.
        ba::diag::Cargando c("evento");
        evento_ = std::make_unique<EventoViewModel>(rt, conference_.get(), emisor_.get(), c.saltear());
        marca("evento");
    }
    connect(evento_.get(), &EventoViewModel::notify, this, &StudioViewModel::notify);
    grafico_ = std::make_unique<GraficoViewModel>(rt, *emisor_);
    marca("grafico");
    // El rotulador mira la escena que está al aire, así que la sigue.
    rotulador_ = std::make_unique<RotuladorViewModel>(rt, *emisor_, conference_.get());
    marca("rotulador");
    // El teleprompter: los discursos son los del evento (los mismos que lee el
    // cue) y el estado es del gateway, así que necesita a los dos.
    prompter_ = std::make_unique<PrompterViewModel>(evento_.get(), conference_.get());
    marca("prompter");
    connect(prompter_.get(), &PrompterViewModel::notify, this, &StudioViewModel::notify);
    // Pantallas externas: necesita la sala para saber con qué dirección la ve
    // un televisor de la red.
    pantallas_ = std::make_unique<PantallasViewModel>(rt, conference_.get());
    marca("pantallas");
    connect(pantallas_.get(), &PantallasViewModel::notify, this, &StudioViewModel::notify);
    // Códigos QR: la biblioteca es del aparato y del evento, y los enlaces
    // propios (sala, prompter, rótulos) se los pide al gateway.
    qr_ = std::make_unique<QrViewModel>(rt, evento_.get(), conference_.get());
    marca("qr");
    // Despues de `frames_`: mira los mismos videos que el resto de la interfaz.
    personas_ = std::make_unique<PersonasViewModel>(rt, *this);
    marca("personas");
    // Las preguntas del publico y la urna. Viven en B; esto las trae y las
    // modera. No consulta nada hasta que una vista se muestra (ver `activo`).
    publico_ = std::make_unique<PublicoViewModel>(conference_.get(), this);
    marca("publico");
    connect(publico_.get(), &PublicoViewModel::notify, this, &StudioViewModel::notify);
    medios_ = std::make_unique<MediosViewModel>(rt, this);
    marca("medios");
    connect(medios_.get(), &MediosViewModel::notify, this, &StudioViewModel::notify);
    carruseles_ = std::make_unique<CarruselViewModel>(rt, this);
    marca("carruseles");
    connect(carruseles_.get(), &CarruselViewModel::notify, this, &StudioViewModel::notify);
    // La tarjeta de control sigue a la escena: al cortar a otra escena pasa a
    // manejar el carrusel que esa escena tenga. Sin esto el operador tendria
    // que acordarse de elegirlo a mano en el peor momento para acordarse de
    // algo. `sceneChanged` avisa tanto del corte como del cambio de fuentes.
    connect(this, &StudioViewModel::sceneChanged, carruseles_.get(), &CarruselViewModel::revisarEscena);
    connect(this, &StudioViewModel::escenasCambiaron, carruseles_.get(), &CarruselViewModel::revisarEscena);
    carruseles_->revisarEscena();
    connect(qr_.get(), &QrViewModel::notify, this, &StudioViewModel::notify);
    connect(rotulador_.get(), &RotuladorViewModel::notify, this, &StudioViewModel::notify);
    connect(this, &StudioViewModel::sceneChanged, rotulador_.get(), &RotuladorViewModel::refrescar);

    // Los vínculos: «el nombre del zócalo sale del orador que está al aire».
    // El contexto se arma acá porque es el único lugar que ve todo junto: la
    // sala sabe quién habla, el guion sabe quién sigue, el motor sabe qué
    // escena está al aire.
    ba::diag::Cargando cargandoVinculos("vinculos");
    vinculos_ = std::make_unique<graficos::Vinculos>(
        cargandoVinculos.saltear() ? std::filesystem::path{} : rt.dataRoot() / "Config" / "vinculos.json",
        *emisor_);
    marca("vinculos");
    vinculos_->setContexto([this]() { return contextoDeVinculos(); });
    rotulador_->setVinculos(vinculos_.get());
    evento_->setVinculos(vinculos_.get());

    {
        // Mirar la carpeta es leer disco y parsear JSON ajeno: si eso se
        // llevara puesto el arranque, la proxima vez se saltea.
        ba::diag::Cargando c("extensiones");
        extensiones_ = std::make_unique<ExtensionesModel>(rt);
        marca("extensiones");
        if (c.saltear()) extensiones_.reset();
    }
    if (extensiones_) connect(extensiones_.get(), &ExtensionesModel::notify, this, &StudioViewModel::notify);

    // El riel se lee siempre, tambien en modo seguro: es COMO SE NAVEGA. Si
    // no se cargara, el operador quedaria sin poder llegar a Configuracion
    // justo cuando algo anda mal.
    riel_ = std::make_unique<RielConfig>(rt.dataRoot() / "Config" / "riel.json");
    marca("riel");

    registro_ = std::make_unique<RegistroViewModel>(rt);
    marca("registro");
    connect(registro_.get(), &RegistroViewModel::notify, this, &StudioViewModel::notify);
    connect(vinculos_.get(), &graficos::Vinculos::actuo, this,
            [this](const QString&, const QString& texto, bool error) {
                if (error) emit notify("Un rótulo automático no salió: " + texto, "error");
            });
    connect(grafico_.get(), &GraficoViewModel::notify, this, &StudioViewModel::notify);
    // HTTPS propio (plataformas): raices de certificados del bundle primero.
    net::HttpsClient::setDefaultCaSearch(
        {(ba::platform::bundleRoot() / "data" / "cacert.pem").string(),
         (rt.dataRoot() / "data" / "cacert.pem").string(),
         (QCoreApplication::applicationDirPath() + "/../../data/cacert.pem").toStdString()});
    youtube_ = std::make_unique<YouTubeViewModel>(rt);
    marca("youtube");
    connect(youtube_.get(), &YouTubeViewModel::notify, this, &StudioViewModel::notify);
    twitch_ = std::make_unique<TwitchViewModel>(rt);
    marca("twitch");
    connect(twitch_.get(), &TwitchViewModel::notify, this, &StudioViewModel::notify);
    facebook_ = std::make_unique<FacebookViewModel>(rt);
    marca("facebook");
    connect(facebook_.get(), &FacebookViewModel::notify, this, &StudioViewModel::notify);
    system_ = std::make_unique<SystemViewModel>();
    marca("system");
    connect(system_.get(), &SystemViewModel::quitForUpdate, this, [this]() {
        emit notify("Cerrando para actualizar: el sistema vuelve a abrir solo en unos segundos.", "info");
        QTimer::singleShot(800, this, [this]() { emit quitRequested(); });
    });
    chatOverlay_ = std::make_unique<ChatOverlayRenderer>(*youtube_, *conference_, *twitch_, *facebook_);
    marca("chatOverlay");
    // Disparadores (alertas) contra el estado real de la app, y la pestaña Asistente.
    {
        agent::AlertInputs in;
        in.audio = [this]() {
            std::vector<agent::AlertInputs::Audio> out;
            for (const std::string& n : rt_.engine().audioSources())
                out.push_back({n, rt_.engine().sourcePeakDb(n), rt_.engine().sourceMuted(n)});
            // La mezcla del programa (lo que sale): para el silencio y la saturación de fábrica.
            out.push_back({"Programa", std::max(peakL_, peakR_), false});
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
        // Las salidas de texto (voz a texto con nombre): las últimas frases.
        in.textos = [this]() {
            std::vector<nlohmann::json> out;
            if (!flujos_) return out;
            const std::uint64_t ultimo = flujos_->ultimoSeq();
            for (const auto& f : flujos_->desde(ultimo > 50 ? ultimo - 50 : 0))
                out.push_back({{"key", std::to_string(f.seq)},
                               {"flujo", f.flujo.toStdString()},
                               {"texto", f.texto.toStdString()},
                               {"origen", f.origen.toStdString()}});
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
            return agent::AlertInputs::Status{s.streaming, s.recording, s.droppedFrames, s.totalFrames,
                                              s.reconnecting, rt_.config().encoder.videoBitrateKbps};
        };
        in.diskFreeGb = [this]() {
            const QStorageInfo si(QString::fromStdWString((rt_.dataRoot() / "Recordings").wstring()));
            return si.isValid() ? static_cast<double>(si.bytesAvailable()) / 1e9 : -1.0;
        };
        // Señal de cada entrada de video (las de GStreamer: cuadros que llegan). Se
        // consulta como mucho una vez por segundo.
        in.senales = [this, cache = std::make_shared<std::pair<qint64, std::vector<std::pair<std::string, bool>>>>()]() {
            const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
            if (video_ && ahora - cache->first > 1000) {
                cache->first = ahora;
                cache->second.clear();
                for (const QString& e : video_->entradas()) {
                    const nlohmann::json st = video_->estadoEntrada(e.toStdString());
                    if (!st.contains("gstreamer")) continue; // las de libobs: sin dato de cuadros
                    const nlohmann::json g = st.value("gstreamer", nlohmann::json::object());
                    if (!g.is_object()) continue;
                    const bool activa = st.value("activa", true);
                    const bool con = !activa || (st.value("ancho", 0) > 0 && g.value("fps_recibidos", 0.0) > 0.5);
                    cache->second.emplace_back(e.toStdString(), con);
                }
            }
            return cache->second;
        };
        in.loudness = [this]() { return rt_.engine().loudness().json(); };
        alerts_ = std::make_unique<agent::AlertEngine>(std::move(in),
                                                       (rt.dataRoot() / "Config" / "alertas.json").string());
        marca("alerts");
        alarma_ = std::make_unique<agent::AlarmaSonora>(rt.dataRoot());
        cuerpo_ = std::make_unique<legislatura::CuerpoViewModel>(rt.dataRoot());
        vocabulario_ = std::make_unique<agent::Vocabulario>(rt.dataRoot() / "Config" / "vocabulario.json");
        bus_ = std::make_unique<control::BusEventos>();
        {
            std::ifstream f(rt.dataRoot() / "Config" / "escenas.json");
            const nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
            if (j.is_object()) corteDirectoEditor_ = j.value("corte_directo_editor", false);
        }
        connect(alerts_.get(), &agent::AlertEngine::fired, this, [this](const nlohmann::json& ev) {
            {
                const nlohmann::json& rid = ev["ruleId"];
                bus_->publicar("alarma/" + (rid.is_string() ? rid.get<std::string>() : rid.dump()),
                               {{"nombre", ev.value("ruleName", std::string())},
                                {"tipo", ev.value("type", std::string())},
                                {"texto", ev.value("llmText", std::string())},
                                {"reconocer", ev.value("reconocer", false)},
                                {"id", ev.value("id", 0ULL)}},
                               true);
            }
            for (const auto& n : ev.value("notify", nlohmann::json::array()))
                if (n == "ui") emit notify("⚠ " + QString::fromStdString(ev.value("text", "")), "warn");
            alarma_->sonar(QString::fromStdString(ev.value("sonido", std::string("ninguno"))));
        });
        connect(alerts_.get(), &agent::AlertEngine::alarmasCambio, this, [this]() {
            alarma_->criticaActiva(alarmaCritica());
            emit alarmasCambio();
        });
        alerts_->start(250);
        agent_ = std::make_unique<agent::AgentViewModel>(rt, *alerts_);
        marca("agent");
        connect(agent_.get(), &agent::AgentViewModel::notify, this, &StudioViewModel::notify);
        // Los roles automaticos: un disparo que un rol activo reclama va a un
        // agente de un tiro y NO al chat del asistente interactivo.
        roles_ = std::make_unique<agent::Roles>(rt.dataRoot() / "Config" / "roles.json");
        marca("roles");
        despachador_ = std::make_unique<agent::Despachador>(*roles_, agent_->motores());
        marca("despachador");
        connect(despachador_.get(), &agent::Despachador::notify, this, &StudioViewModel::notify);
        agent_->setReclamado([this](const nlohmann::json& ev) { return despachador_->evento(ev); });
        // El modelo local: sus archivos en Modelos/ (portable), el motor junto a ba-app.
        modelos_ =
            std::make_unique<agent::ModelosLocales>(rt.dataRoot(), QCoreApplication::applicationDirPath());
        connect(modelos_.get(), &agent::ModelosLocales::notify, this, &StudioViewModel::notify);
        QTimer::singleShot(0, modelos_.get(), [m = modelos_.get()]() { m->arrancarSiCorresponde(); });
        voz_ = std::make_unique<agent::Voz>(rt, *modelos_);
        marca("voz");
        modelos_->setProbadorTc([v = voz_.get()](const QString& frase, agent::ModelosLocales::Respuesta r) {
            v->probarTc(frase, std::move(r));
        });
        connect(voz_.get(), &agent::Voz::notify, this, &StudioViewModel::notify);
        vozPrompter_ = std::make_unique<agent::VozPrompter>(rt, *voz_, conference_.get());
        marca("vozPrompter");
        // El modelo local como motor de agentes y del asistente (proveedor «local»).
        if (agent_) {
            agent::Voz* v = voz_.get();
            agent_->setLocal([v](const QString& quien, const QString& texto, bool disparador) {
                v->atenderDesde(texto, quien, disparador);
            });
            v->setAsistenteGrande([a = agent_.get()]() { return a->asistenteGrande(); });
            v->setAsistenteLocal([a = agent_.get()]() { return a->asistenteLocal(); });
        }
        // El micrófono del operador se cambia en Configuración: Sala lo muestra.
        connect(voz_.get(), &agent::Voz::cambio, conference_.get(), &ConferenceViewModel::operatorChanged);
        // Barra de mensajes del asistente al operador (y respuestas a invitados).
        messages_ = std::make_unique<OperatorMessages>();
        marca("messages");
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
                if (p.startsWith("camaras:"))
                    note =
                        st == "saved"
                            ? "[OPERADOR] guardo usuario y clave de las camaras " + p.mid(8) +
                                  " (las que dejo sin clave, no). Siguiente paso: camara_ficha de cada una."
                            : "[OPERADOR] cerro el formulario de las camaras " + p.mid(8) + " sin guardar.";
                else if (p.startsWith("camara:"))
                    note = st == "saved"
                               ? "[OPERADOR] guardo usuario y clave de la camara " + p.mid(7) +
                                     ". Siguiente paso: camara_ficha {\"ip\": \"" + p.mid(7) + "\"}."
                               : "[OPERADOR] cerro el formulario de la camara " + p.mid(7) + " sin guardar.";
                else
                    note =
                        st == "saved"
                            ? "[OPERADOR] guardo la credencial de " + p + ". Siguiente paso: " + p + "_login."
                            : "[OPERADOR] cerro el formulario de credenciales de " + p + " sin guardar.";
            }
            if (!note.isEmpty()) agent_->deliverNote(note);
        });
    }
    // Las salidas de texto (antes que la Voz las use).
    flujos_ = std::make_unique<agent::FlujosTexto>(rt.dataRoot());
    marca("flujos");
    // Los cuadros «Subtítulos» de las escenas muestran una salida de texto.
    subtitulos_ = std::make_unique<agent::Subtitulos>(*flujos_);
    // Subtítulos ocultos (CC) en la transmisión: 608 o la API de YouTube.
    cc_ = std::make_unique<agent::SubtitulosCC>(*flujos_, rt.dataRoot(), [this](const QString& t, double s) {
        return rt_.engine().enviarCaption(t.toStdString(), s);
    });
    // «Traducción automática» rotulada mientras sale una traducción (pantalla o CC).
    {
        std::ifstream f(rt.dataRoot() / "Config" / "rotulo-traduccion.json");
        const nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
        if (j.is_object()) {
            rotuloTraduccion_ = j.value("activo", true);
            rotuloCapa_ = std::clamp(j.value("capa", 4), 1, 4);
        }
    }
    rotuloApagar_.setSingleShot(true);
    connect(&rotuloApagar_, &QTimer::timeout, this, [this]() {
        if (dsk_) dsk_->mostrar(rotuloCapa_, false, true);
    });
    connect(flujos_.get(), &agent::FlujosTexto::fraseConOrigen, this,
            [this](const QString& flujo, const QString&, const QString& origen) { rotularTraduccion(flujo, origen); });
    if (voz_) voz_->setFlujos(flujos_.get());
    // Las tarjetas «Texto a voz»: dicen lo que llega por una salida de texto.
    if (modelos_) {
        voces_ = std::make_unique<agent::VocesRack>(*modelos_);
        marca("voces");
        voces_->setFlujos(flujos_.get());
        voces_->setVozSistema([v = voz_.get()]() { return v != nullptr ? v->vozElegida() : QString(); });
        // La traducción en vivo: de una salida de texto a otra, en otro idioma.
        traductor_ = std::make_unique<agent::Traductor>(rt.dataRoot(), *modelos_, *flujos_);
        marca("traductor");
    }
    inputs_ = std::make_unique<InputsViewModel>(rt, *this);
    marca("inputs");
    // El PTZ de cada entrada (y Mejorar imagen): se entera enseguida de las
    // entradas nuevas y de lo que cambia en Configuración.
    camaras_ = std::make_unique<ptz::ControlCamaras>(rt, rt.dataRoot());
    // Las bancas presenciales de la fila de oradores (legislatura/Bancas).
    {
        legislatura::Bancas::Ganchos g;
        g.camaraAlAire = [this](const std::string& c) { return camaras_->alAire(QString::fromStdString(c)); };
        g.irAPosicion = [this](const std::string& c, const std::string& p) {
            const QVariantMap r = camaras_->irAPosicion(QString::fromStdString(c), QString::fromStdString(p), 1.0);
            return r.value("ok").toBool() ? std::string() : r.value("error").toString().toStdString();
        };
        g.escenaDe = [this](const std::string& camara) {
            // La primera escena que muestra esa cámara (que no sea la del aire si hay otra).
            std::string otra;
            for (const std::string& sc : rt_.engine().sceneNames()) {
                const auto ss = rt_.engine().sceneSources(sc);
                if (std::find(ss.begin(), ss.end(), camara) == ss.end()) continue;
                if (sc != rt_.engine().currentScene()) return sc;
                otra = sc;
            }
            return otra;
        };
        g.armar = [this](const std::string& e) { armarEscena(QString::fromStdString(e)); };
        g.cortar = [this](const std::string& e) { takeScene(QString::fromStdString(e)); };
        g.microfono = [this](const std::string& f, bool abierto) { rt_.engine().setSourceMuted(f, !abierto); };
        bancas_ = std::make_unique<legislatura::Bancas>(cuerpo_->cuerpo(), g);
        seguirMic_ = std::make_unique<legislatura::SeguirMicrofono>(rt.dataRoot(), *bus_, cuerpo_->cuerpo(), *bancas_);
        ordenDia_ = std::make_unique<legislatura::OrdenDelDia>(rt.dataRoot(), *bus_);
        // Los presentes con voto salen del padrón del evento abierto.
        quorum_ = std::make_unique<legislatura::Quorum>(rt.dataRoot(), [this]() {
            auto* ev = static_cast<EventoViewModel*>(evento());
            if (ev == nullptr) return 0;
            const QVariantMap r = ev->property("resumenPadron").toMap();
            return r.value("conVoto").toInt() > 0 ? r.value("votoPresente").toInt() : r.value("presentes").toInt();
        });
        if (auto* ev = static_cast<EventoViewModel*>(evento())) {
            ev->setAnexoActa([this]() {
                QString t = QString::fromStdString(quorum_->anexoActa() + ordenDia_->anexoActa(remotosPalabra()));
                // La transcripción de la sesión, con la huella de cada archivo (se puede verificar después).
                const QStringList archivos = flujos_ ? flujos_->archivosDeSesion() : QStringList{};
                if (!archivos.isEmpty()) {
                    t += "## Transcripción\n\n| Archivo | SHA-256 |\n|---|---|\n";
                    for (const QString& a : archivos) {
                        QFile f(a);
                        QString h = "—";
                        if (f.open(QIODevice::ReadOnly)) {
                            QCryptographicHash c(QCryptographicHash::Sha256);
                            c.addData(&f);
                            h = QString::fromLatin1(c.result().toHex());
                        }
                        t += "| " + a + " | " + h + " |\n";
                    }
                    t += "\n_La transcripción es automática: puede tener errores; la versión taquigráfica es la oficial._\n\n";
                }
                return t;
            });
        }
    }
    marca("camaras");
    connect(inputs_.get(), &InputsViewModel::changed, camaras_.get(), &ptz::ControlCamaras::sincronizar);
    // El grafo de tarjetas de video de cada entrada (después del PTZ).
    video_ = std::make_unique<video::VideoGrafos>(rt, rt.dataRoot());
    // Lo que la revisión previa del evento mira del aparato.
    evento_->setChequeosAparato([this]() {
        QVariantList out;
        const auto agregar = [&out](bool grave, const QString& t) { out << QVariantMap{{"grave", grave}, {"texto", t}}; };
        const QStorageInfo si(QString::fromStdWString((rt_.dataRoot() / "Recordings").wstring()));
        const double gb = si.isValid() ? static_cast<double>(si.bytesAvailable()) / 1e9 : -1;
        const int kbps = rt_.config().encoder.videoBitrateKbps;
        if (gb >= 0 && kbps > 0) {
            const double horas = gb / (kbps * 1.1 * 3600.0 / 8.0 / 1e6);
            if (horas < 1)
                agregar(true, QString("Disco: quedan %1 GB, menos de una hora de grabación").arg(gb, 0, 'f', 1));
            else if (horas < 3)
                agregar(false, QString("Disco: alcanza para ~%1 h de grabación").arg(horas, 0, 'f', 1));
        }
        if (!streamKeyAvailable()) agregar(false, "No hay destino de transmisión configurado (servidor y clave)");
        if (video_)
            for (const QString& e : video_->entradas()) {
                const nlohmann::json st = video_->estadoEntrada(e.toStdString());
                if (!st.contains("gstreamer") || !st.value("activa", true)) continue;
                if (st.value("ancho", 0) <= 0)
                    agregar(false, "La entrada «" + e + "» no tiene señal ahora");
            }
        return out;
    });
    // La superresolución de las entradas de red (motores, modelos, laboratorio).
    superres_ = std::make_unique<video::SuperRes>(rt.dataRoot(), video_.get());
    marca("video");
    connect(inputs_.get(), &InputsViewModel::changed, video_.get(), &video::VideoGrafos::sincronizar);
    // Los equipos de video de la red: la biblioteca de marcas y la base OUI
    // vienen con el programa (data/red).
    red_ = std::make_unique<red::Red>(ba::platform::bundleRoot() / "data" / "red",
                                      QString::fromStdWString((rt.dataRoot() / "Credenciales").wstring()),
                                      video_.get(), camaras_.get());
    marca("red");
    // Los plugins de Python: en los datos (Plugins/), con el Python del programa.
    plugins_ = std::make_unique<plugins::Plugins>(rt, video_.get(), camaras_.get(), messages_.get());
    // Plugins <-> bus de eventos: lo que publican va al bus (plugin/<id>/<canal>)
    // y los temas de la app les llegan a los que escuchan.
    plugins_->setAlPublicar([this](const std::string& t, const nlohmann::json& d) { bus_->publicar(t, d, true); });
    bus_->suscribir("#", [this](const std::string& t, const nlohmann::json& d) {
        if (plugins_ && t.rfind("plugin/", 0) != 0) plugins_->eventoApp(t, d);
    });
    // La botonera: el modelo, los permisos y la ejecución viven acá; el gateway
    // es solo el borde de red (TLS, tokens, WebSocket de las tablets).
    {
        control::Botonera::Ganchos g;
        g.escenas = [this]() { return rt_.engine().sceneNames(); };
        g.fuentesAudio = [this]() { return rt_.engine().audioSources(); };
        g.bancas = [this]() {
            std::vector<std::pair<std::string, std::string>> out;
            for (const auto& i : cuerpo_->cuerpo().integrantes())
                if (!i.value("banca", std::string()).empty())
                    out.emplace_back(i.value("banca", std::string()), i.value("nombre", std::string()));
            return out;
        };
        botonera_ = std::make_unique<control::Botonera>(rt.dataRoot(), *bus_, std::move(g));
        satellite_ = std::make_unique<control::SatelliteServidor>(rt.dataRoot(), *botonera_);
        dsk_ = std::make_unique<control::Dsk>(rt.dataRoot(), rt_.engine());
        acciones_ = std::make_unique<control::Acciones>(rt.dataRoot());
        {
            auto& g = acciones_->ganchos();
            g.escenas = [this]() { return rt_.engine().sceneNames(); };
            g.fuentesAudio = [this]() { return rt_.engine().audioSources(); };
            g.mudo = [this](const std::string& f) { return rt_.engine().sourceMuted(f); };
            g.dskArriba = [this](int capa) {
                for (const auto& c : dsk_->capas())
                    if (c.value("capa", 0) == capa) return c.value("visible", false);
                return false;
            };
        }
        dsk_->setAlCambiar([this](const nlohmann::json& c) {
            bus_->publicar("aire/dsk", c);
            publicarTally(); // lo que muestra una capa también está al aire
        });
        auto* cli = &static_cast<ConferenceViewModel*>(conference_.get())->client();
        const auto aQt = [](const nlohmann::json& j) {
            return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump())).object();
        };
        connect(cli, &ConferenceClient::botoneraEvento, this, [this, cli, aQt](const QJsonObject& o) {
            const nlohmann::json ev = nlohmann::json::parse(QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString(),
                                                            nullptr, false);
            if (!ev.is_object()) return;
            const std::string tipo = ev.value("tipo", std::string());
            if (tipo == "solicitud") {
                const std::string e = botonera_->solicitud(ev.value("solicitud", std::string()), ev.value("codigo", std::string()),
                                                           ev.value("nombre", std::string()), ev.value("ip", std::string()));
                if (!e.empty())
                    cli->post("/api/v1/botonera/resolver",
                              aQt({{"solicitud", ev.value("solicitud", std::string())}, {"estado", "invalido"}, {"error", e}}),
                              [](bool, const QJsonObject&) {});
            } else if (tipo == "entrada") {
                botonera_->entrada(ev.value("dispositivo", std::string()), ev.value("msg", nlohmann::json::object()));
            } else if (tipo == "conexion") {
                botonera_->marcarConexion(ev.value("dispositivo", std::string()), ev.value("conectado", false),
                                          ev.value("ip", std::string()));
            }
        });
        connect(botonera_.get(), &control::Botonera::mensaje, this,
                [this, cli, aQt](const QString& dev, const nlohmann::json& msg) {
                    const std::string origen = botonera_->origenDe(dev.toStdString());
                    if (origen == "tablet")
                        cli->post("/api/v1/botonera/enviar", aQt({{"dispositivo", dev.toStdString()}, {"msg", msg}}),
                                  [](bool, const QJsonObject&) {});
                    else if (origen == "plugin" && plugins_)
                        plugins_->superficieEstado(dev.toStdString(), msg);
                    else if (origen == "satellite" && satellite_)
                        satellite_->mensaje(dev.toStdString(), msg);
                });
        connect(botonera_.get(), &control::Botonera::credencialesCambio, this, [this]() { sincronizarBotonera(); });
        connect(cli, &ConferenceClient::onlineChanged, this, [this](bool on) {
            if (on) sincronizarBotonera();
        });
        connect(botonera_.get(), &control::Botonera::solicitudNueva, this,
                [this](const QString&, const QString& nombre, const QString& perfil, const QString& ip) {
                    emit notify("«" + nombre + "» (" + ip + ") pide emparejarse como botonera con el perfil «" + perfil +
                                    "»: aprobalo en la página Botonera.",
                                "warn");
                });
        // Los aparatos de hardware de los plugins (Stream Deck, MIDI…) son botoneras más.
        plugins_->setSuperficies(
            [this](const std::string& id, const std::string& nombre, const nlohmann::json& cap) {
                botonera_->registrarLocal(id, nombre, "plugin", cap);
            },
            [this](const std::string& id) { botonera_->quitarLocal(id); },
            [this](const std::string& id, const nlohmann::json& msg) { botonera_->entrada(id, msg); });
    }
    marca("plugins");
    // Seguir una cara con el PTZ (la ventana del PTZ y el asistente).
    seguidor_ = std::make_unique<Seguidor>(personas_.get(), camaras_.get());
    marca("seguidor");
    // Los equipos (red, esta máquina, USB): un catálogo con acciones declaradas.
    dispositivos_ = std::make_unique<dispositivos::Dispositivos>();
    marca("dispositivos");
    dispositivos::registrarTodo(*dispositivos_,
                                {red_.get(), plugins_.get(), video_.get(), camaras_.get(), messages_.get()});
#if defined(BA_HAVE_CLAP)
    // Racks de audio (LSP) de cada fuente: se montan los guardados.
    racks_ = std::make_unique<audio::RackManager>(rt.dataRoot());
    marca("racks");
    racks_->restore();
    audio_ = std::make_unique<audio::AudioViewModel>(*racks_);
    marca("audio");
    if (voz_) voz_->setRacks(racks_.get()); // la voz sale de los nodos «Voz a texto»
    // Las salidas de texto: las de las tarjetas «Voz a texto» y las traducidas.
    flujos_->setConfig([r = racks_.get(), t = traductor_.get()](const QString& flujo) {
        nlohmann::json c = r->textoDe(flujo.toStdString());
        if (c.is_null() && t != nullptr) c = t->salidaDe(flujo.toStdString());
        return c;
    });
    flujos_->setLista([r = racks_.get(), t = traductor_.get()]() {
        nlohmann::json l = r->textos();
        if (t != nullptr)
            for (const auto& s : t->salidas())
                l.push_back(s);
        return l;
    });
    if (voces_) {
        voces_->setRacks(racks_.get());
        connect(racks_.get(), &audio::RackManager::changed, voces_.get(), &agent::VocesRack::cambio);
    }
    connect(racks_.get(), &audio::RackManager::changed, flujos_.get(), &agent::FlujosTexto::cambio);
    // Cada traducción es una tarjeta «Traducir» de un rack.
    if (traductor_) {
        auto* r = racks_.get();
        traductor_->setTarjetas(
            {[r]() { return r->traducciones(); },
             [r](const nlohmann::json& t, std::string* id) {
                 std::string e;
                 *id = r->addTraduccion(t, &e);
                 return e;
             },
             [r](const std::string& id, const nlohmann::json& c) { return r->setTraduccionOpciones(id, c); },
             [r](const std::string& id) { return r->quitarTraduccion(id); }});
        connect(racks_.get(), &audio::RackManager::changed, traductor_.get(), &agent::Traductor::sincronizar);
        connect(racks_.get(), &audio::RackManager::editado, traductor_.get(), &agent::Traductor::sincronizar);
    }
    matriz_ = std::make_unique<audio::Matriz>(rt.dataRoot());
    marca("matriz");
    // Una entrada de placa que trae la tarjeta «Entrada» de un rack también se abre.
    matriz_->setUsadasPorOtros([r = racks_.get()]() { return r->fuentesDeEntradas(); });
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
        camaras_->sincronizar(); // Mejorar imagen general: rige ya
    });

    connect(&tick_, &QTimer::timeout, this, &StudioViewModel::onTick);
    tick_.start(100);
    connect(&frameTick_, &QTimer::timeout, frames_.get(), &FrameService::poll);
    frameTick_.start(16); // hasta 60 Hz; FrameService solo avisa si hay frame nuevo
    // Tras una caída: la placa o el negro vuelven como estaban.
    QTimer::singleShot(1500, this, [this]() {
        restaurarPlaca();
        if (dsk_) dsk_->restaurar();
    });
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
    // Señal de las entradas (multiview, escena de respaldo): una vez por segundo.
    if (vigiaTicks_ % 10 == 5) revisarSenales();
    // El vigía de los módulos de audio: lo que saltó por lento o colgado.
    if (++vigiaTicks_ % 10 == 0 && racks_ != nullptr)
        for (const auto& m : racks_->tomarSaltados())
            emit notify("Audio: «" + qs(m) + "» se saltó por lento (pasa sin procesar). Rearmalo en Audio cuando esté bien.", "warn");
    // La interfaz volvió de una traba: cuánto duró (el aire no se cortó).
    if (const std::uint64_t ms = rt_.latido().tomarTrabaTerminada(); ms > 0)
        emit notify(QString("La interfaz estuvo trabada %1 s; el aire siguió").arg(ms / 1000.0, 0, 'f', 1), "warn");
    const EngineStatus st = rt_.engine().status();
    if (st.recording && !recording_) {
        recSince_ = std::chrono::steady_clock::now();
        recSinceMs_ = QDateTime::currentMSecsSinceEpoch();
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
    // Si cambio el aire o la grabacion, el tally lo tiene que saber: una
    // pantalla que dice «al aire» con la transmision cortada miente, y es la
    // clase de mentira por la que alguien se confia.
    const bool cambioElAire = recording_ != st.recording || streaming_ != st.streaming;
    recording_ = st.recording;
    streaming_ = st.streaming;
    if (cambioElAire) publicarTally();
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
    publicarEventos();
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
    return QString("%1x%2 · %3 fps").arg(v.outputWidth).arg(v.outputHeight).arg(qs(fpsLegible(v.fpsNum, v.fpsDen)));
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
QObject* StudioViewModel::modelos() const { return modelos_.get(); }
QObject* StudioViewModel::voz() const { return voz_.get(); }
QObject* StudioViewModel::vozPrompter() const { return vozPrompter_.get(); }
QObject* StudioViewModel::matriz() const {
#if defined(BA_HAVE_CLAP)
    return matriz_.get();
#else
    return nullptr;
#endif
}
QObject* StudioViewModel::camaras() const { return camaras_.get(); }
QObject* StudioViewModel::video() const { return video_.get(); }
QObject* StudioViewModel::red() const { return red_.get(); }
QObject* StudioViewModel::pluginsQml() const { return plugins_.get(); }
QObject* StudioViewModel::seguidor() const { return seguidor_.get(); }
QObject* StudioViewModel::dispositivosQml() const { return dispositivos_.get(); }
QObject* StudioViewModel::flujos() const { return flujos_.get(); }
QObject* StudioViewModel::voces() const { return voces_.get(); }
QObject* StudioViewModel::traductor() const { return traductor_.get(); }
QObject* StudioViewModel::superres() const { return superres_.get(); }
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
    // Una cámara de la red (camara:<ip>): usuario y clave, cifrados.
    if (platform.startsWith("camara:")) {
        if (!red_) return "El escáner de red no está disponible.";
        return red_->guardarCredenciales(platform.mid(7), v.value("usuario").toString(),
                                         v.value("clave").toString());
    }
    // Varias cámaras en un formulario: «usuario|<ip>» y «clave|<ip>». Las que
    // quedan sin clave se saltean (se pueden completar después).
    if (platform.startsWith("camaras:")) {
        if (!red_) return "El escáner de red no está disponible.";
        int guardadas = 0;
        for (const QString& ip : platform.mid(8).split(',', Qt::SkipEmptyParts)) {
            const QString clave = v.value("clave|" + ip).toString();
            if (clave.isEmpty()) continue;
            QString usuario = v.value("usuario|" + ip).toString().trimmed();
            if (usuario.isEmpty())
                usuario = QString::fromStdString(
                    red_->equipo(ip.toStdString()).value("usuario_de_fabrica", std::string("admin")));
            const QString e = red_->guardarCredenciales(ip, usuario, clave);
            if (!e.isEmpty()) return ip + ": " + e;
            ++guardadas;
        }
        return guardadas == 0 ? "No escribiste ninguna clave." : QString();
    }
    // Las claves de un plugin (plugin:<id>): un campo por cada una que declara.
    if (platform.startsWith("plugin:")) {
        if (!plugins_) return "Los plugins no están disponibles.";
        int guardadas = 0;
        for (auto it = v.begin(); it != v.end(); ++it) {
            const QString valor = it.value().toString();
            if (valor.isEmpty()) continue;
            const QString e = plugins_->guardarSecreto(platform.mid(7), it.key(), valor);
            if (!e.isEmpty()) return e;
            ++guardadas;
        }
        return guardadas == 0 ? "No escribiste ninguna clave." : QString();
    }
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
QObject* StudioViewModel::prompter() const { return prompter_.get(); }
QObject* StudioViewModel::pantallas() const { return pantallas_.get(); }
QObject* StudioViewModel::qr() const { return qr_.get(); }
QObject* StudioViewModel::personas() const { return personas_.get(); }
QObject* StudioViewModel::publico() const { return publico_.get(); }
QObject* StudioViewModel::medios() const { return medios_.get(); }
QObject* StudioViewModel::carruseles() const { return carruseles_.get(); }
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

    // Lo que sale en un zócalo tiene que estar VALIDADO: si el invitado está en
    // el Cuerpo (por nombre), salen el nombre y el cargo de ahí; si no, el
    // `info` que él escribió al entrar solo sale si el operador lo aprobó
    // (invitado_info_aprobar). Sin eso, el cargo va vacío.
    const auto deParticipante = [this](const QJsonObject& p) {
        const QString nombre = p.value("name").toString();
        const nlohmann::json enCuerpo = cuerpo_ ? cuerpo_->cuerpo().buscar(nombre.toStdString()) : nlohmann::json::object();
        QString cargo, bloque;
        bool validado = false;
        if (!enCuerpo.empty()) {
            cargo = QString::fromStdString(enCuerpo.value("cargo_oficial", std::string()));
            bloque = QString::fromStdString(enCuerpo.value("bloque", std::string()));
            validado = enCuerpo.contains("validado_por");
        } else if (const auto it = infoAprobada_.constFind(p.value("id").toString()); it != infoAprobada_.constEnd()) {
            cargo = *it;
            validado = true;
        }
        return QJsonObject{{"nombre", enCuerpo.empty() ? nombre : QString::fromStdString(enCuerpo.value("nombre", nombre.toStdString()))},
                           {"cargo", cargo},
                           {"bloque", bloque},
                           {"validado", validado},
                           {"sugerencia", p.value("info").toString()},
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
    // El punto del orden del día que se está tratando: {punto.texto}, {punto.n}…
    if (ordenDia_) {
        const nlohmann::json p = ordenDia_->actual();
        if (!p.empty())
            ctx["punto"] = QJsonObject{{"n", p.value("n", 0)},
                                       {"titulo", QString::fromStdString(p.value("titulo", std::string()))},
                                       {"expediente", QString::fromStdString(p.value("expediente", std::string()))},
                                       {"texto", QString("Punto %1: %2").arg(p.value("n", 0)).arg(QString::fromStdString(p.value("titulo", std::string())))}};
    }
    // Un orador presencial (banca) al aire manda sobre el de la sala.
    if (bancas_) {
        const nlohmann::json b = bancas_->oradorParaZocalo();
        if (!b.empty())
            ctx["orador"] = QJsonDocument::fromJson(QByteArray::fromStdString(b.dump())).object();
    }

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
    if (!recording_) {
        // Antes de grabar: cuánto alcanza el disco a este bitrate.
        const nlohmann::json d = rt_.discoGrabacion();
        const double horas = d.value("horas", -1.0);
        if (horas >= 0 && horas < 0.25) {
            emit pedirConfirmarGrabacion(horas);
            return;
        }
        if (horas >= 0 && horas < 2)
            emit notify(QString("Ojo: el disco alcanza para ~%1 h de grabación").arg(horas, 0, 'f', 1), "warn");
    }
    if (recording_) {
        rt_.stopRecording();
    } else if (rt_.startRecording()) {
        emit notify("Grabando en " + qs(rt_.config().rec.directory), "ok");
    } else {
        emit notify("No se pudo grabar: " + qs(rt_.lastError()), "error");
    }
    onTick();
}

void StudioViewModel::grabarIgual() {
    if (recording_) return;
    if (rt_.startRecording())
        emit notify("Grabando (con poco disco)", "warn");
    else
        emit notify("No se pudo grabar: " + qs(rt_.lastError()), "error");
    onTick();
}

QVariantMap StudioViewModel::loudnessUi() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(rt_.engine().loudness().json().dump())).object().toVariantMap();
}

void StudioViewModel::resetLoudnessUi() { rt_.engine().loudness().reiniciar(); }

QString StudioViewModel::objetivoLoudnessUi(const QVariantMap& o) {
    const auto j = nlohmann::json::parse(QJsonDocument(QJsonObject::fromVariantMap(o)).toJson().toStdString(), nullptr, false);
    const std::string e = rt_.engine().loudness().ponerObjetivo(j);
    if (e.empty()) {
        const auto ob = rt_.engine().loudness().objetivo();
        std::ofstream f(rt_.dataRoot() / "Config" / "loudness.json");
        f << nlohmann::json{{"perfil", ob.perfil}, {"lufs", ob.lufs}, {"tp", ob.tp}}.dump(1);
    }
    return qs(e);
}

QString StudioViewModel::retornoInvitadoUi(int slot) const { return qs(rt_.engine().retornoInvitado(slot)); }

QString StudioViewModel::setRetornoInvitadoUi(int slot, const QString& modo) {
    if (modo != "mix_minus" && modo != "programa") return "retorno: mix_minus o programa";
    if (slot < 1 || slot > rt_.config().guests) return "no hay un invitado " + QString::number(slot);
    rt_.engine().setRetornoInvitado(slot, modo.toStdString());
    nlohmann::json j = nlohmann::json::object();
    {
        std::ifstream f(rt_.dataRoot() / "Config" / "retornos.json");
        const nlohmann::json x = nlohmann::json::parse(f, nullptr, false);
        if (x.is_object()) j = x;
    }
    j[std::to_string(slot)] = modo.toStdString();
    std::ofstream o(rt_.dataRoot() / "Config" / "retornos.json");
    o << j.dump(1);
    return {};
}

QVariantMap StudioViewModel::discoUi() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(rt_.discoGrabacion().dump())).object().toVariantMap();
}

QVariantMap StudioViewModel::remuxUi() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(rt_.engine().remuxEstado().dump())).object().toVariantMap();
}

void StudioViewModel::startStreaming(const QString& server, const QString& key) {
    if (rt_.startStreaming(server.toStdString(), key.toStdString())) {
        // Nunca un "Al aire" verde si no cambió nada.
        if (rt_.engine().ultimoInicio() == BroadcastEngine::Inicio::YaIgual)
            emit notify("Ya estabas transmitiendo a ese destino: no cambió nada", "info");
        else
            emit notify("Al aire", "ok");
    } else if (rt_.engine().ultimoInicio() == BroadcastEngine::Inicio::YaOtroDestino) {
        emit pedirCambioDestino(qs(rt_.engine().destinoActivo()), server, key);
    } else {
        emit notify("No se pudo transmitir: " + qs(rt_.lastError()), "error");
    }
    onTick();
}

void StudioViewModel::cambiarDestinoStream(const QString& server, const QString& key) {
    if (rt_.startStreaming(server.toStdString(), key.toStdString(), true))
        emit notify("Transmitiendo al destino nuevo", "ok");
    else
        emit notify("No se pudo cambiar de destino: " + qs(rt_.lastError()), "error");
    onTick();
}

QVariantList StudioViewModel::destinosUi() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(rt_.destinos().dump())).array().toVariantList();
}

QString StudioViewModel::guardarDestinoUi(const QVariantMap& d) {
    const auto j = nlohmann::json::parse(QJsonDocument(QJsonObject::fromVariantMap(d)).toJson().toStdString(), nullptr, false);
    const std::string e = rt_.guardarDestino(j);
    if (e.empty()) emit destinosChanged();
    return qs(e);
}

QString StudioViewModel::borrarDestinoUi(const QString& id) {
    const std::string e = rt_.borrarDestino(id.toStdString());
    if (e.empty()) emit destinosChanged();
    return qs(e);
}

QString StudioViewModel::iniciarDestinoUi(const QString& id) {
    const std::string e = rt_.iniciarDestino(id.toStdString());
    emit destinosChanged();
    return qs(e);
}

void StudioViewModel::detenerDestinoUi(const QString& id) {
    rt_.detenerDestino(id.toStdString());
    emit destinosChanged();
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
    publicarTally();
    rt_.save();
}

QString StudioViewModel::escenaArmada() const { return scenes_->armada(); }

void StudioViewModel::publicarTally() {
    auto* p = qobject_cast<PantallasViewModel*>(pantallas());
    if (p == nullptr) return;
    const QString aire = qs(rt_.engine().currentScene());
    const QString armada = scenes_->armada();
    const auto fuentesDe = [this](const QString& escena) {
        QStringList out;
        if (escena.isEmpty()) return out;
        for (const std::string& f : rt_.engine().sceneSources(escena.toStdString()))
            out << qs(f);
        return out;
    };
    QStringList enAire = fuentesDe(aire);
    for (const std::string& f : rt_.engine().fuentesDsk())
        if (!enAire.contains(qs(f))) enAire << qs(f);
    p->publicarTally(aire, armada, enAire, fuentesDe(armada), streaming(), recording());
}

void StudioViewModel::armarEscena(const QString& name) {
    if (name == qs(rt_.engine().currentScene())) {
        // Armar lo que ya esta al aire deja el TAKE sin sentido (cortaria a lo
        // mismo). Se avisa en vez de aceptarlo callado.
        emit notify("«" + name + "» ya está al aire: armá otra para el próximo take.", "aviso");
        return;
    }
    scenes_->setArmada(name);
    frames_->setArmada(name); // el monitor de armada mira esta
    emit sceneChanged();
    publicarTally();
}

void StudioViewModel::take() {
    const QString armada = scenes_->armada();
    if (armada.isEmpty()) {
        emit notify("No hay ninguna escena armada: elegí una en el bus de preview.", "aviso");
        return;
    }
    const QString saliendo = qs(rt_.engine().currentScene());
    if (!rt_.setScene(armada.toStdString())) {
        emit notify(qs(rt_.lastError()), "error");
        return;
    }
    // El plano que sale del aire queda armado: en una mesa, despues de un take
    // el preview tiene lo anterior y volver es un solo golpe.
    scenes_->setArmada(saliendo);
    frames_->setArmada(saliendo);
    scenes_->liveChanged();
    sources_->setScene(armada);
    emit sceneChanged();
    publicarTally();
    rt_.save();
}

QStringList StudioViewModel::ventanasAbiertas() const {
    QStringList out;
    for (QWindow* w : QGuiApplication::topLevelWindows()) {
        if (w == nullptr || !w->isVisible() || !w->objectName().startsWith("ba:")) continue;
        if (w->screen() != nullptr && !out.contains(w->screen()->name())) out << w->screen()->name();
    }
    return out;
}

bool StudioViewModel::puedeUbicarVentanas() {
    return !QGuiApplication::platformName().contains("wayland", Qt::CaseInsensitive);
}

QVariantList StudioViewModel::escenasPorCarpeta() const {
    // MISMO ORDEN QUE LA PESTAÑA ESCENAS: primero las sueltas, después las
    // carpetas. Agrupando en el orden crudo del modelo, las sueltas quedaban
    // EN EL MEDIO de dos carpetas y sin título propio, o sea que parecían
    // parte de la de arriba. Dos vistas de lo mismo con dos ordenes distintos
    // obligan a leer en vez de reconocer.
    //
    // Adentro de cada grupo se respeta el orden que puso el operador:
    // ordenarlas alfabéticamente sería desordenarlas para el que las acomodó.
    QVariantList sueltas;
    QVariantList carpetas;    // [{carpeta, escenas}]
    QMap<QString, int> donde; // carpeta -> posicion en `carpetas`
    for (const std::string& n : rt_.orderedSceneNames()) {
        const QString carpeta = qs(rt_.sceneFolder(n));
        if (carpeta.isEmpty()) {
            sueltas.append(qs(n));
            continue;
        }
        if (!donde.contains(carpeta)) {
            donde[carpeta] = static_cast<int>(carpetas.size());
            carpetas.append(QVariantMap{{"carpeta", carpeta}, {"escenas", QVariantList{}}});
        }
        QVariantMap g = carpetas[donde[carpeta]].toMap();
        QVariantList e = g.value("escenas").toList();
        e.append(qs(n));
        g["escenas"] = e;
        carpetas[donde[carpeta]] = g;
    }
    QVariantList out;
    if (!sueltas.isEmpty()) out.append(QVariantMap{{"carpeta", QString()}, {"escenas", sueltas}});
    out.append(carpetas);
    return out;
}

void StudioViewModel::addScene(const QString& name) {
    if (!rt_.addScene(name.trimmed().toStdString())) {
        emit notify(qs(rt_.lastError()), "error");
        return;
    }
    scenes_->reload();
    emit escenasCambiaron();
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
        for (int i = 2; existe(destino); ++i)
            destino = from + " (copia " + QString::number(i) + ")";
    }
    if (!rt_.copyScene(from.toStdString(), destino.toStdString())) {
        emit notify(qs(rt_.lastError()), "error");
        return {};
    }
    scenes_->reload();
    emit escenasCambiaron();
    emit sceneChanged();
    rt_.save();
    const std::string aviso = rt_.lastError();
    emit notify(aviso.empty() ? "Escena copiada: " + destino : "Escena copiada, pero " + qs(aviso),
                aviso.empty() ? "ok" : "warn");
    return destino;
}

void StudioViewModel::setSceneProtected(const QString& name, bool protegida) {
    if (!rt_.setSceneProtected(name.toStdString(), protegida)) {
        emit notify(qs(rt_.lastError()), "error");
        return;
    }
    scenes_->reload();
    emit escenasCambiaron();
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
    emit escenasCambiaron();
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
    emit escenasCambiaron();
    emit sceneChanged();
    rt_.save();
}

QStringList StudioViewModel::sceneFolders() const {
    QStringList out;
    for (const auto& f : rt_.sceneFolders())
        out << qs(f);
    return out;
}

void StudioViewModel::copiar(const QString& texto) {
    if (QClipboard* c = QGuiApplication::clipboard()) {
        c->setText(texto);
        emit notify("Copiado", "ok");
    }
}

QString StudioViewModel::assistantIntake(const QString& id, const QString& name, bool yaAdentro) {
    if (!intakeSink_) return "El servidor MCP no está disponible.";
    return intakeSink_(id, name, yaAdentro);
}

void StudioViewModel::moveScene(const QString& name, const QString& before) {
    if (!rt_.moveScene(name.toStdString(), before.toStdString())) {
        emit notify(qs(rt_.lastError()), "error");
        return;
    }
    scenes_->reload();
    emit escenasCambiaron();
    emit sceneChanged();
    rt_.save();
}

void StudioViewModel::renameScene(const QString& from, const QString& to) {
    if (!rt_.renameScene(from.toStdString(), to.trimmed().toStdString())) {
        emit notify(qs(rt_.lastError()), "error");
        return;
    }
    scenes_->reload();
    emit escenasCambiaron();
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
    emit escenasCambiaron();
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

QVariantMap StudioViewModel::latidoUi() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(rt_.latido().estado().dump())).object().toVariantMap();
}

QString StudioViewModel::latidoPoliticaUi(const QVariantMap& cambios) {
    const auto j = nlohmann::json::parse(QJsonDocument(QJsonObject::fromVariantMap(cambios)).toJson().toStdString(),
                                         nullptr, false);
    return QString::fromStdString(rt_.latido().ponerPolitica(j));
}

QVariantList StudioViewModel::alarmasActivasUi() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(alerts_->activas().dump())).array().toVariantList();
}

int StudioViewModel::reconocerAlarmaUi(qint64 id) { return alerts_->reconocer(static_cast<std::uint64_t>(id)); }

bool StudioViewModel::alarmaCritica() const {
    for (const auto& e : alerts_->activas())
        if (e.value("sonido", std::string()) == "critico") return true;
    return false;
}

QObject* StudioViewModel::alarmaSonora() const { return alarma_.get(); }

bool StudioViewModel::focoEnTexto() const {
    QObject* f = QGuiApplication::focusObject();
    return f != nullptr && (f->inherits("QQuickTextInput") || f->inherits("QQuickTextEdit"));
}

void StudioViewModel::setCorteDirectoEditor(bool si) {
    if (si == corteDirectoEditor_) return;
    corteDirectoEditor_ = si;
    std::ofstream o(rt_.dataRoot() / "Config" / "escenas.json");
    o << nlohmann::json{{"corte_directo_editor", si}}.dump(1);
    emit escenasAjustesChanged();
}

namespace {
void guardarPlaca(const std::filesystem::path& datos, const nlohmann::json& estado) {
    std::error_code ec;
    std::filesystem::create_directories(datos / "Runtime", ec);
    std::ofstream o(datos / "Runtime" / "placa.json");
    o << estado.dump(1);
}
} // namespace

QString StudioViewModel::ftbUi(bool activar, int ms, const QString& audio) {
    if (!rt_.engine().ftb(activar, ms, audio != "mantener")) return qs(rt_.engine().lastError());
    guardarPlaca(rt_.dataRoot(), rt_.engine().estadoPlaca());
    emit placaChanged();
    return {};
}

QString StudioViewModel::placaUi(bool activar, const QString& texto, const QString& imagen, int ms) {
    if (!rt_.engine().placa(activar, texto.toStdString(), imagen.toStdString(), ms)) return qs(rt_.engine().lastError());
    guardarPlaca(rt_.dataRoot(), rt_.engine().estadoPlaca());
    emit placaChanged();
    return {};
}

QVariantMap StudioViewModel::estadoPlacaUi() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(rt_.engine().estadoPlaca().dump())).object().toVariantMap();
}

QStringList StudioViewModel::textosPlaca() const {
    QStringList l{"Volvemos enseguida", "Cuarto intermedio", "Minuto de silencio", "Sesión suspendida",
                  "Dificultades técnicas"};
    const QString usado = QString::fromStdString(rt_.engine().estadoPlaca().value("placa_texto", std::string()));
    if (!usado.isEmpty() && !l.contains(usado)) l.prepend(usado);
    return l;
}

void StudioViewModel::restaurarPlaca() {
    std::ifstream f(rt_.dataRoot() / "Runtime" / "placa.json");
    const nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    if (!j.is_object()) return;
    if (j.value("placa", false))
        rt_.engine().placa(true, j.value("placa_texto", std::string()), j.value("placa_imagen", std::string()), 0);
    if (j.value("ftb", false)) rt_.engine().ftb(true, 0, j.value("ftb_audio", false));
    if (j.value("placa", false) || j.value("ftb", false))
        emit notify("Se restauró lo que estaba al aire antes del corte (placa o negro)", "warn");
}

QObject* StudioViewModel::cuerpoQml() const { return cuerpo_.get(); }

QString StudioViewModel::aprobarInfoInvitado(const QString& participante, const QString& texto) {
    const QJsonObject sala = conference_ != nullptr ? conference_->client().snapshot() : QJsonObject{};
    for (const QJsonValue& v : sala.value("participants").toArray()) {
        const QJsonObject p = v.toObject();
        if (p.value("id").toString() == participante || p.value("name").toString() == participante) {
            infoAprobada_[p.value("id").toString()] = texto.isEmpty() ? p.value("info").toString() : texto;
            return {};
        }
    }
    return "no hay un invitado «" + participante + "» en la sala";
}

QVariantList StudioViewModel::chatRecientesUi() const {
    QVariantList out;
    if (!chatOverlay_) return out;
    const QStringList dest = chatOverlay_->destacados();
    for (const auto& m : chatOverlay_->collect())
        out << QVariantMap{{"key", m.key}, {"source", m.source}, {"author", m.author}, {"text", m.text},
                           {"destacado", dest.contains(m.key)}};
    return out;
}

bool StudioViewModel::destacarChatUi(const QString& key) { return chatOverlay_ && chatOverlay_->destacar(key); }

void StudioViewModel::quitarDestacadoChatUi(const QString& key) {
    if (chatOverlay_) chatOverlay_->quitarDestacado(key);
}

QString StudioViewModel::preguntaDesdeChatUi(const QString& key) {
    if (!chatOverlay_ || !conference_) return "el chat no está disponible";
    for (const auto& m : chatOverlay_->collect()) {
        if (m.key != key) continue;
        const QString deDonde = m.source == "room" ? "sala" : m.source;
        static_cast<ConferenceViewModel*>(conference_.get())
            ->client()
            .post("/api/v1/preguntas",
                  QJsonObject{{"accion", "preguntar"}, {"texto", m.text}, {"nombre", m.author}, {"deDonde", deDonde}},
                  [this](bool ok, const QJsonObject&) {
                      emit notify(ok ? "Sumada a la cola de preguntas" : "No se pudo sumar la pregunta", ok ? "ok" : "error");
                  });
        return {};
    }
    return "ese mensaje ya no está en el chat";
}

QObject* StudioViewModel::bancasQml() const { return bancas_.get(); }

namespace {
nlohmann::json leerRespaldos(const std::filesystem::path& datos) {
    std::ifstream f(datos / "Config" / "respaldo-entradas.json");
    const nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    return j.is_object() ? j : nlohmann::json::object();
}
} // namespace

QVariantMap StudioViewModel::respaldoEntrada(const QString& entrada) const {
    const nlohmann::json r = leerRespaldos(rt_.dataRoot()).value(entrada.toStdString(), nlohmann::json::object());
    return QJsonDocument::fromJson(QByteArray::fromStdString(r.dump())).object().toVariantMap();
}

QString StudioViewModel::setRespaldoEntrada(const QString& entrada, const QVariantMap& r) {
    const QString modo = r.value("al_perder_aire", "avisar").toString();
    if (modo != "avisar" && modo != "armar" && modo != "cortar") return "al_perder_aire: avisar, armar o cortar";
    nlohmann::json todos = leerRespaldos(rt_.dataRoot());
    todos[entrada.toStdString()] = {{"escena", r.value("escena").toString().toStdString()},
                                    {"al_perder_aire", modo.toStdString()},
                                    {"ms", std::clamp(r.value("ms", 1000).toInt(), 200, 60000)}};
    std::ofstream o(rt_.dataRoot() / "Config" / "respaldo-entradas.json");
    o << todos.dump(1);
    return {};
}

void StudioViewModel::revisarSenales() {
    if (!video_) return;
    QStringList sin;
    for (const QString& e : video_->entradas()) {
        const nlohmann::json st = video_->estadoEntrada(e.toStdString());
        if (!st.contains("gstreamer") || !st.value("activa", true)) continue;
        const nlohmann::json g = st.value("gstreamer", nlohmann::json::object());
        if (st.value("ancho", 0) <= 0 || !g.is_object() || g.value("fps_recibidos", 0.0) < 0.5) sin << e;
    }
    const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
    for (const QString& e : sin)
        if (!sinSenalDesde_.contains(e)) sinSenalDesde_[e] = ahora;
    for (auto it = sinSenalDesde_.begin(); it != sinSenalDesde_.end();)
        if (!sin.contains(it.key())) {
            respaldoHecho_.remove(it.key());
            it = sinSenalDesde_.erase(it);
        } else {
            ++it;
        }
    if (sin != sinSenal_) {
        sinSenal_ = sin;
        emit senalChanged();
    }
    // Una entrada AL AIRE sin señal: lo que se configuró (avisar, armar o cortar al respaldo).
    const nlohmann::json resp = leerRespaldos(rt_.dataRoot());
    auto alAire = rt_.engine().sceneSources(rt_.engine().currentScene());
    for (const std::string& f : rt_.engine().fuentesDsk()) alAire.push_back(f); // el recuadro de LSA también
    for (const QString& e : sin) {
        if (respaldoHecho_.contains(e)) continue;
        if (std::find(alAire.begin(), alAire.end(), e.toStdString()) == alAire.end()) continue;
        const nlohmann::json r = resp.value(e.toStdString(), nlohmann::json::object());
        if (ahora - sinSenalDesde_.value(e) < r.value("ms", 1000)) continue;
        respaldoHecho_.insert(e);
        const QString escena = QString::fromStdString(r.value("escena", std::string()));
        const std::string modo = r.value("al_perder_aire", std::string("avisar"));
        if (modo == "cortar" && !escena.isEmpty()) {
            takeScene(escena);
            emit notify("«" + e + "» se quedó sin señal al aire: se cortó a «" + escena + "»", "error");
        } else if (modo == "armar" && !escena.isEmpty()) {
            armarEscena(escena);
            emit notify("«" + e + "» se quedó sin señal al aire: «" + escena + "» quedó armada (TAKE)", "error");
        } else {
            emit notify("«" + e + "» se quedó sin señal AL AIRE", "error");
        }
    }
}

QObject* StudioViewModel::ccQml() const { return cc_.get(); }

} // namespace ba::app

namespace ba::app {

namespace {
QVariant vocabQv(const nlohmann::json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(nlohmann::json{{"v", j}}.dump()))
        .object()
        .value("v")
        .toVariant();
}
} // namespace

QVariantMap StudioViewModel::vocabularioUi() const {
    QVariantMap m = vocabQv(vocabulario_->todo()).toMap();
    m["reemplazos"] = vocabQv(vocabulario_->ultimosReemplazos());
    return m;
}

QString StudioViewModel::vocabularioAgregarUi(const QString& termino, const QString& variantes, const QString& grupo) {
    std::vector<std::string> v;
    for (const QString& x : variantes.split(',', Qt::SkipEmptyParts))
        if (!x.trimmed().isEmpty()) v.push_back(x.trimmed().toStdString());
    return QString::fromStdString(vocabulario_->agregar(termino.trimmed().toStdString(), v, grupo.toStdString()));
}

bool StudioViewModel::vocabularioQuitarUi(const QString& termino) { return vocabulario_->quitar(termino.toStdString()); }

void StudioViewModel::vocabularioGrupoUi(const QString& grupo, bool activo) {
    vocabulario_->activarGrupo(grupo.toStdString(), activo);
}

QVariantMap StudioViewModel::vocabularioProbarUi(const QString& texto) {
    nlohmann::json r = nlohmann::json::array();
    const std::string t = vocabulario_->corregir(texto.toStdString(), &r);
    return {{"texto", QString::fromStdString(t)}, {"reemplazos", vocabQv(r)}};
}

int StudioViewModel::vocabularioDelCuerpoUi() {
    std::vector<std::string> nombres;
    for (const auto& i : cuerpo_->cuerpo().integrantes()) {
        const std::string n = i.value("nombre", std::string());
        if (n.empty()) continue;
        nombres.push_back(n);
        // El apellido solo también (se dice «el diputado Pérez»).
        if (const auto sp = n.rfind(' '); sp != std::string::npos && n.size() - sp > 4) nombres.push_back(n.substr(sp + 1));
    }
    std::sort(nombres.begin(), nombres.end());
    nombres.erase(std::unique(nombres.begin(), nombres.end()), nombres.end());
    vocabulario_->ponerGrupo("cuerpo", nombres);
    return static_cast<int>(nombres.size());
}

} // namespace ba::app

namespace ba::app {

QObject* StudioViewModel::busQml() const { return bus_.get(); }

void StudioViewModel::publicarEventos() {
    if (!bus_) return;
    ++busTicks_;
    const QString aire = qs(rt_.engine().currentScene());
    const QString armada = scenes_->armada();
    const auto fuentesDe = [this](const QString& escena) {
        nlohmann::json out = nlohmann::json::array();
        if (!escena.isEmpty())
            for (const std::string& f : rt_.engine().sceneSources(escena.toStdString())) out.push_back(f);
        return out;
    };
    bus_->publicar("aire/escena", {{"escena", aire.toStdString()}});
    bus_->publicar("aire/armada", {{"escena", armada.toStdString()}});
    nlohmann::json fuentesAire = fuentesDe(aire);
    for (const std::string& f : rt_.engine().fuentesDsk())
        if (std::find(fuentesAire.begin(), fuentesAire.end(), f) == fuentesAire.end()) fuentesAire.push_back(f);
    bus_->publicar("aire/tally", {{"programa", aire.toStdString()},
                                  {"preview", armada.toStdString()},
                                  {"fuentes_programa", fuentesAire},
                                  {"fuentes_preview", fuentesDe(armada)},
                                  {"transmitiendo", streaming_},
                                  {"grabando", recording_}});
    // Los tiempos cambian cada segundo: se publican una vez por segundo.
    const bool segundo = busTicks_ % 10 == 0;
    nlohmann::json stream{{"activo", streaming_},
                          {"reconectando", reconnecting_},
                          {"problema", outputProblem_.toStdString()}};
    nlohmann::json grab{{"activo", recording_}};
    if (segundo || !streaming_) {
        stream["kbps"] = kbps_;
        stream["tiempo"] = streamTime().toStdString();
        stream["descartados"] = dropped_;
    } else if (const auto u = bus_->ultimo("salida/stream"); u.is_object()) {
        for (const char* k : {"kbps", "tiempo", "descartados"})
            if (u["valor"].contains(k)) stream[k] = u["valor"][k];
    }
    if (segundo || !recording_) grab["tiempo"] = recordTime().toStdString();
    else if (const auto u = bus_->ultimo("salida/grabacion"); u.is_object() && u["valor"].contains("tiempo"))
        grab["tiempo"] = u["valor"]["tiempo"];
    bus_->publicar("salida/stream", stream);
    bus_->publicar("salida/grabacion", grab);
    // Niveles: solo si se movieron 1 dB o más (o cambió el volumen o el mudo).
    for (int i = 0; i < mixer_->rowCount(); ++i) {
        const QModelIndex ix = mixer_->index(i);
        const std::string nombre = mixer_->data(ix, MixerModel::NameRole).toString().toStdString();
        const double pico = std::round(mixer_->data(ix, MixerModel::PeakDbRole).toDouble());
        const double vol = std::round(mixer_->data(ix, MixerModel::VolumeDbRole).toDouble() * 10) / 10;
        bus_->publicar("audio/nivel/" + nombre,
                       {{"pico_db", std::max(-60.0, pico)},
                        {"volumen_db", vol},
                        {"mudo", mixer_->data(ix, MixerModel::MutedRole).toBool()}});
    }
    if (segundo) {
        bus_->publicar("audio/loudness", rt_.engine().loudness().json());
        nlohmann::json sin = nlohmann::json::array();
        for (const QString& e : sinSenal_) sin.push_back(e.toStdString());
        bus_->publicar("entrada/senal", {{"sin_senal", sin}});
        if (bancas_) bus_->publicar("oradores/al_aire", bancas_->oradorParaZocalo());
        if (conference_) {
            nlohmann::json gente = nlohmann::json::array();
            const QJsonObject snap = static_cast<ConferenceViewModel*>(conference_.get())->client().snapshot();
            for (const QJsonValue& v : snap.value("participants").toArray()) {
                const QJsonObject o = v.toObject();
                gente.push_back({{"id", o.value("id").toString().toStdString()},
                                 {"nombre", o.value("name").toString().toStdString()},
                                 {"estado", o.value("state").toString().toStdString()},
                                 {"mic", o.value("mic").toBool()},
                                 {"cam", o.value("cam").toBool()},
                                 {"mano", o.value("hand").toBool()}});
            }
            bus_->publicar("sala/participantes", gente);
        }
    }
}

} // namespace ba::app

namespace ba::app {

QObject* StudioViewModel::botoneraQml() const { return botonera_.get(); }
QObject* StudioViewModel::satelliteQml() const { return satellite_.get(); }
QObject* StudioViewModel::dskQml() const { return dsk_.get(); }
QObject* StudioViewModel::accionesQml() const { return acciones_.get(); }
QObject* StudioViewModel::seguirMicrofonoQml() const { return seguirMic_.get(); }
QObject* StudioViewModel::ordenDelDiaQml() const { return ordenDia_.get(); }
QObject* StudioViewModel::quorumQml() const { return quorum_.get(); }

nlohmann::json StudioViewModel::remotosPalabra() const {
    nlohmann::json out = nlohmann::json::array();
    if (!conference_) return out;
    const QJsonObject snap = static_cast<ConferenceViewModel*>(conference_.get())->client().snapshot();
    for (const QJsonValue& v : snap.value("participants").toArray()) {
        const QJsonObject p = v.toObject();
        const qint64 ms = static_cast<qint64>(p.value("usedMs").toDouble());
        if (ms > 0) out.push_back({{"nombre", p.value("name").toString().toStdString()}, {"segundos", ms / 1000}});
    }
    return out;
}

QString StudioViewModel::capitulosPuntosUi() const {
    if (!recording_ || !ordenDia_) return {};
    QString t;
    for (const auto& c : ordenDia_->capitulos(recSinceMs_))
        t += QString::fromStdString(c.value("marca", std::string()) + " " + c.value("titulo", std::string())) + "\n";
    return t;
}

QStringList StudioViewModel::nombresParaDsk() const {
    QStringList out;
    for (const std::string& n : rt_.engine().sceneNames()) out << qs(n);
    for (const std::string& n : rt_.engine().sourceNames())
        if (!out.contains(qs(n))) out << qs(n);
    return out;
}

void StudioViewModel::sincronizarBotonera() {
    if (!conference_ || !botonera_) return;
    auto& cli = static_cast<ConferenceViewModel*>(conference_.get())->client();
    if (!cli.online()) return;
    const nlohmann::json body{{"dispositivos", botonera_->credencialesParaGateway()}};
    cli.post("/api/v1/botonera/credenciales", QJsonDocument::fromJson(QByteArray::fromStdString(body.dump())).object(),
             [](bool, const QJsonObject&) {});
}

void StudioViewModel::emparejarTablet(const QString& perfil, const QString& nombre, int venceDias,
                                      std::function<void(const QVariantMap&)> listo) {
    const nlohmann::json c = botonera_->nuevoCodigo(perfil.toStdString(), nombre.toStdString(), venceDias);
    if (c.contains("error")) {
        listo({{"error", QString::fromStdString(c.value("error", std::string()))}});
        return;
    }
    auto& cli = static_cast<ConferenceViewModel*>(conference_.get())->client();
    if (!cli.online()) {
        listo({{"error", "El gateway no está andando: la tablet no tendría a quién conectarse."}});
        return;
    }
    const QString codigo = QString::fromStdString(c.value("codigo", std::string()));
    const qint64 vence = c.value("vence_ms", 0LL);
    cli.get("/api/v1/network", [codigo, vence, listo](bool ok, const QJsonObject& r) {
        if (!ok) {
            listo({{"error", "No pude preguntarle al gateway la dirección de la red local."}});
            return;
        }
        // Una URL de la red local (no la del túnel), HTTPS si está.
        QStringList lan;
        for (const QJsonValue& v : r.value("lan").toArray()) lan << v.toString();
        QString base;
        for (const QJsonValue& v : r.value("urls").toArray()) {
            const QString u = v.toString();
            const bool deLan = std::any_of(lan.begin(), lan.end(), [&](const QString& ip) { return u.contains("//" + ip + ":") || u.contains("//" + ip + "/"); });
            if (!deLan) continue;
            if (base.isEmpty() || (u.startsWith("https") && !base.startsWith("https"))) base = u;
        }
        QVariantMap out{{"codigo", codigo}, {"vence_ms", vence}};
        const QJsonObject https = r.value("https").toObject();
        if (https.value("enabled").toBool()) out["huella"] = https.value("fingerprint").toString();
        if (r.value("bindAddress").toString() == "127.0.0.1")
            out["aviso"] = "El gateway escucha solo en esta máquina (127.0.0.1): una tablet no llega. Cambiá la "
                           "dirección de la sala a la red local en Configuración → Red (cualquiera en la red podría "
                           "ver las páginas públicas; la botonera igual pide su token).";
        // El gateway escucha solo en esta máquina: la URL igual se arma con la IP
        // de la red (anda en cuanto se abra el gateway a la red; el aviso lo dice).
        if (base.isEmpty() && !lan.isEmpty()) {
            if (https.value("enabled").toBool()) {
                base = "https://" + lan.first() + ":" + QString::number(https.value("port").toInt(8789)) + "/";
            } else {
                for (const QJsonValue& v : r.value("urls").toArray()) {
                    const QUrl u(v.toString());
                    if (u.scheme() == "http" && u.port() > 0) {
                        base = "http://" + lan.first() + ":" + QString::number(u.port()) + "/";
                        break;
                    }
                }
            }
        }
        if (base.isEmpty()) {
            out["error"] = "No encontré una dirección de la red local para la tablet (¿está conectada la red?).";
            listo(out);
            return;
        }
        if (!base.endsWith('/')) base += '/';
        const QString url = base + "botonera/#p=" + codigo;
        out["url"] = url;
        ba::qr::Estilo e;
        e.correccion = ba::qr::Correccion::Media;
        const std::string svg = ba::qr::svg(url.toStdString(), e);
        out["qr"] = "data:image/svg+xml;base64," + QString::fromLatin1(QByteArray::fromStdString(svg).toBase64());
        listo(out);
    });
}

void StudioViewModel::emparejarTabletUi(const QString& perfil, const QString& nombre, int venceDias) {
    QPointer<StudioViewModel> yo(this);
    emparejarTablet(perfil, nombre, venceDias, [yo](const QVariantMap& r) {
        if (yo) emit yo->emparejamientoListo(r);
    });
}

void StudioViewModel::aprobarTabletUi(const QString& solicitud, bool aprobar) {
    const nlohmann::json r = botonera_->resolverSolicitud(solicitud.toStdString(), aprobar);
    if (r.contains("error")) {
        emit notify(QString::fromStdString(r.value("error", std::string())), "error");
        return;
    }
    nlohmann::json body{{"solicitud", solicitud.toStdString()}, {"estado", aprobar ? "aprobada" : "rechazada"}};
    if (aprobar) {
        body["dispositivo"] = r.value("dispositivo", std::string());
        body["token"] = r.value("token", std::string());
    }
    auto& cli = static_cast<ConferenceViewModel*>(conference_.get())->client();
    // Primero las credenciales (para que el token ya valga) y después el token a la tablet.
    sincronizarBotonera();
    cli.post("/api/v1/botonera/resolver", QJsonDocument::fromJson(QByteArray::fromStdString(body.dump())).object(),
             [](bool, const QJsonObject&) {});
    emit notify(aprobar ? "Tablet emparejada." : "Emparejamiento rechazado.", aprobar ? "ok" : "info");
}

} // namespace ba::app

namespace ba::app {

QVariantList StudioViewModel::isosUi() const {
    QVariantList out;
    for (const auto& g : rt_.engine().sourceRecordings()) {
        std::error_code ec;
        const auto bytes = std::filesystem::file_size(g.path, ec);
        out.append(QVariantMap{{"fuente", qs(g.source)},
                               {"archivo", qs(g.path)},
                               {"mb", ec ? 0.0 : static_cast<double>(bytes) / 1e6},
                               {"audioPropio", g.audioPropio},
                               {"activa", g.activa}});
    }
    return out;
}

QString StudioViewModel::isoUi(const QString& fuente, bool grabar) {
    if (!grabar) {
        rt_.stopSourceRecording(fuente.toStdString());
        emit statusChanged();
        return {};
    }
    const nlohmann::json d = rt_.discoGrabacion();
    if (d.value("horas", -1.0) >= 0 && d.value("horas", -1.0) < 0.5)
        return "Queda disco para menos de media hora: liberá espacio antes de sumar grabaciones aparte.";
    if (static_cast<int>(rt_.engine().sourceRecordings().size()) >= BroadcastEngine::kMaxIndividuales)
        return QString("Ya se graban %1 aparte (el tope): pará alguna.").arg(BroadcastEngine::kMaxIndividuales);
    if (!rt_.startSourceRecording(fuente.toStdString())) return qs(rt_.lastError());
    emit statusChanged();
    return {};
}

QString StudioViewModel::fuenteDeInvitado(const QString& participante) const {
    if (!conference_) return {};
    const QJsonObject snap = static_cast<ConferenceViewModel*>(conference_.get())->client().snapshot();
    for (const QJsonValue& v : snap.value("participants").toArray()) {
        const QJsonObject p = v.toObject();
        if (p.value("id").toString() != participante && p.value("name").toString() != participante) continue;
        const int slot = p.value("slot").toInt();
        if (slot <= 0) return {};
        const QString f = "Invitado " + QString::number(slot);
        for (const std::string& n : rt_.engine().sourceNames())
            if (qs(n) == f) return f;
        return {};
    }
    return {};
}

QString StudioViewModel::isoTodasUi(bool grabar) {
    if (!grabar) {
        rt_.stopAllSourceRecordings();
        emit statusChanged();
        return {};
    }
    QStringList errores;
    for (const std::string& f : rt_.engine().sceneSources(rt_.engine().currentScene())) {
        bool ya = false;
        for (const auto& g : rt_.engine().sourceRecordings()) ya = ya || g.source == f;
        if (ya) continue;
        const QString e = isoUi(qs(f), true);
        if (!e.isEmpty()) errores << qs(f) + ": " + e;
    }
    return errores.join("; ");
}

} // namespace ba::app

namespace ba::app {

void StudioViewModel::rotularTraduccion(const QString& flujo, const QString& origen) {
    if (!rotuloTraduccion_ || !dsk_ || !origen.startsWith("Traducción")) return;
    // ¿Se está mostrando? En un cuadro «Subtítulos» o en los subtítulos ocultos.
    const bool enPantalla = subtitulos_ && subtitulos_->cuadrosDe(flujo) > 0;
    const QVariantMap cc = cc_ ? cc_->estado() : QVariantMap{};
    const bool enCc = cc.value("activo").toBool() && cc.value("flujo").toString() == flujo;
    if (!enPantalla && !enCc) return;
    const std::string nombre = "Traducción automática (rótulo)";
    if (!rt_.engine().textoGlobal(nombre, "Traducción automática")) return;
    // La capa se arma una vez con el rótulo arriba a la izquierda.
    const nlohmann::json capas = dsk_->capas();
    bool armada = false;
    for (const auto& c : capas)
        if (c.value("capa", 0) == rotuloCapa_) {
            const auto cont = c.value("contenido", nlohmann::json::array());
            armada = cont.size() == 1 && cont[0] == nombre;
        }
    if (!armada)
        dsk_->definir(rotuloCapa_, {{"nombre", "Traducción automática"},
                                    {"contenido", nlohmann::json::array({nombre})},
                                    {"recuadro", {{"posicion", "arriba_izquierda"}, {"tamano", 0.22}, {"margen", 0.03}}},
                                    {"fijo", false},
                                    {"transicion_ms", 300}});
    for (const auto& c : dsk_->capas())
        if (c.value("capa", 0) == rotuloCapa_ && !c.value("visible", false)) dsk_->mostrar(rotuloCapa_, true, false);
    rotuloApagar_.start(15000); // se va 15 s después de la última frase traducida
}

void StudioViewModel::setRotuloTraduccion(bool activo) {
    rotuloTraduccion_ = activo;
    std::ofstream o(rt_.dataRoot() / "Config" / "rotulo-traduccion.json");
    o << nlohmann::json{{"activo", activo}, {"capa", rotuloCapa_}}.dump(1);
    if (!activo && dsk_) dsk_->mostrar(rotuloCapa_, false, true);
}

nlohmann::json StudioViewModel::rotuloTraduccionEstado() const {
    return {{"activo", rotuloTraduccion_}, {"capa", rotuloCapa_}, {"arriba", rotuloApagar_.isActive()}};
}

} // namespace ba::app
