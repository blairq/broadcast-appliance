// ============================================================================
// src/app/ui/StudioViewModel.cpp — ver StudioViewModel.hpp
// ============================================================================

#include "ui/StudioViewModel.hpp"
#include "platform/Paths.hpp"
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
                                              s.reconnecting};
        };
        in.diskFreeGb = [this]() {
            const QStorageInfo si(QString::fromStdWString((rt_.dataRoot() / "Recordings").wstring()));
            return si.isValid() ? static_cast<double>(si.bytesAvailable()) / 1e9 : -1.0;
        };
        alerts_ = std::make_unique<agent::AlertEngine>(std::move(in),
                                                       (rt.dataRoot() / "Config" / "alertas.json").string());
        marca("alerts");
        connect(alerts_.get(), &agent::AlertEngine::fired, this, [this](const nlohmann::json& ev) {
            for (const auto& n : ev.value("notify", nlohmann::json::array()))
                if (n == "ui") emit notify("⚠ " + QString::fromStdString(ev.value("text", "")), "warn");
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
    marca("camaras");
    connect(inputs_.get(), &InputsViewModel::changed, camaras_.get(), &ptz::ControlCamaras::sincronizar);
    // El grafo de tarjetas de video de cada entrada (después del PTZ).
    video_ = std::make_unique<video::VideoGrafos>(rt, rt.dataRoot());
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
    p->publicarTally(aire, armada, fuentesDe(aire), fuentesDe(armada), streaming(), recording());
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

} // namespace ba::app
