// ============================================================================
// src/app/ui/StudioViewModel.cpp — ver StudioViewModel.hpp
// ============================================================================

#include "ui/StudioViewModel.hpp"
#include "sala/CupoSala.hpp"
#include "sala/ConfigSala.hpp"
#include "emision/InputSpecs.hpp"
#include "sala/VotacionSala.hpp"
#include "sala/PreguntasSala.hpp"
#include "sala/SeguridadSala.hpp"
#include "escenas/Conmutador.hpp"
#include "asamblea/Votaciones.hpp"
#include "asamblea/Presidencia.hpp"
#include "escenas/RespaldoEntradas.hpp"
#include "escenas/AjustesEscenas.hpp"
#include "escenas/ControlGrillas.hpp"
#include "arranque/EnergiaAire.hpp"
#include "arranque/ModoVivoConfig.hpp"
#include "ui/EnlacesViewModel.hpp"
#include "ui/RecursosViewModel.hpp"
#include "diag/ChequeosEquipo.hpp"
#include "platform/GstEntorno.hpp"
#include "Piezas.hpp" // motor::derivaDePlacas (los chequeos del equipo)
#include "emision/SalidasConfig.hpp"
#include "arranque/InterfazConfig.hpp"
#include "agent/RotuloTraduccionConfig.hpp"
#include "emision/RetornosInvitados.hpp"
#include "emision/SeguridadAire.hpp"
#include "ui/AlarmasViewModel.hpp"
#include "ui/AsambleaViewModel.hpp"
#include "ui/BitacoraEvento.hpp"
#include "audio/ObjetivoLoudness.hpp"
#include "ajustes/Ajustes.hpp"
#include "i18n/I18n.hpp"
#include "persist/ColaEscritura.hpp"
#include "platform/Paths.hpp"
#include <fstream>
#include <map>
#include <set>
#include <QClipboard>
#include <QDateTime>
#include <QQmlEngine>
#include <QTimer>
#include <QElapsedTimer>

#include <QGuiApplication>
#include <QScreen>
#include <QWindow>

#if defined(BA_HAVE_CLAP)
#include "audio/AudioViewModel.hpp"
#include "audio/Matriz.hpp"
#include "audio/Calibracion.hpp"
#include "audio/Mezclador.hpp"
#include "audio/RackManager.hpp"
#endif

#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QBuffer>
#include <QFileInfo>
#include <QPointer>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QStorageInfo>
#include <QUrl>

#include "comun/HoraMonotonica.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "net/AccountGuides.hpp"
#include "agent/AgentViewModel.hpp"
#include "agent/EstadoIA.hpp"
#include "ui/DocumentosViewModel.hpp"
#include "ui/PdfEscena.hpp"
#include "ui/ManualesViewModel.hpp"
#include "agent/AlarmaSonora.hpp"
#include "agent/SubtitulosCC.hpp"
#include "agent/Vocabulario.hpp"
#include "agent/Notificador.hpp"
#include "emision/AvisoFueraDeUi.hpp"
#include "control/Botonera.hpp"
#include "control/BotoneraGateway.hpp"
#include "control/ControlWeb.hpp"
#include "control/ControlWebGateway.hpp"
#include "control/TemasWeb.hpp"
#include "sala/Direcciones.hpp"
#include "sala/Pedido.hpp"
#include "asamblea/Debate.hpp"
#include "control/CarasBotonera.hpp"
#include "control/BusEventos.hpp"
#include "control/Dsk.hpp"
#include "control/Acciones.hpp"
#include "arranque/AppSettings.hpp"
#include "control/SatelliteServidor.hpp"
#include "qr/Qr.hpp"
#include "asamblea/Bancas.hpp"
#include "asamblea/ConferenciaDcn.hpp"
#include "asamblea/SesionRemota.hpp"
#include "asamblea/SeguirMicrofono.hpp"
#include "asamblea/OrdenDelDia.hpp"
#include "asamblea/InformesResultado.hpp"
#include "evento/APdf.hpp"
#include "asamblea/Quorum.hpp"
#include "asamblea/Cuerpo.hpp"
#include "asamblea/CuerpoViewModel.hpp"
#include "agent/AlertEngine.hpp"
#include "agent/Despachador.hpp"
#include "agent/FlujosTexto.hpp"
#include "agent/ServidorModeloLocal.hpp"
#include "agent/ModelosLocales.hpp"
#include "net/Descargas.hpp"

#include <QHash>
#include "agent/Roles.hpp"
#include "agent/Subtitulos.hpp"
#include "agent/Traductor.hpp"
#include "agent/VocesRack.hpp"
#include "agent/Voz.hpp"
#include "agent/VozPrompter.hpp"
#include "arranque/AppRuntime.hpp"
#include "fault/ModoSeguro.hpp"
#include "diag/DiarioDeArranque.hpp"
#include "diag/Registro.hpp"
#include "diag/VigiaBucle.hpp"
#include "graficos/GraficoSource.hpp"
#include "evento/Bitacora.hpp"
#include "evento/CierreEvento.hpp"
#include "evento/FotosConfig.hpp"
#include "mcp/McpTraza.hpp"
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
#include "ui/FuenteQrPintor.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/TemaEventoViewModel.hpp"
#include "ui/LineaViewModel.hpp"
#include "net/AccesoRemoto.hpp"
#include "ui/DashboardModel.hpp"
#include "ui/EventoViewModel.hpp"
#include "ui/HistoricoViewModel.hpp"
#include "video/BancoSuperRes.hpp"
#include "ui/ExtensionesModel.hpp"
#include "ui/FacebookViewModel.hpp"
#include "ui/FrameService.hpp"
#include "ui/GraficoViewModel.hpp"
#include "ui/InputsViewModel.hpp"
#include "ui/ListModels.hpp"
#include "ui/MediosViewModel.hpp"
#include "ui/MultiviewViewModel.hpp"
#include "ui/OperatorMessages.hpp"
#include "ui/PantallasViewModel.hpp"
#include "pantallas/Catalogo.hpp"
#include "pantallas/Monitores.hpp"
#include "pantallas/Ventanas.hpp"
#include "ui/AtencionViewModel.hpp"
#include "ui/TotemsViewModel.hpp"
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
#include "arranque/Configuraciones.hpp"
#include "ui/TwitchViewModel.hpp"
#include "ui/YouTubeViewModel.hpp"
#include "video/SuperRes.hpp"
#include "medios/Playlist.hpp"
#include "ui/PuestoCorreccion.hpp"
#include "video/Scopes.hpp"
#include "video/VideoGrafos.hpp"
#include "tiempos/TiemposModel.hpp"
#include "resolucion/ResolucionModel.hpp"
#include "video/Camaras.hpp"
#include "video/CatalogoGst.hpp"
#include "Motor.hpp"
#include "platform/Hilo.hpp"
#include <QCoreApplication>

namespace ba::app {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

/// Un hijo objeto, o {} si falta o no es un objeto (value() sobre null tira una excepción).
nlohmann::json hijoObjeto(const nlohmann::json& j, const char* k) {
    return j.is_object() && j.contains(k) && j[k].is_object() ? j[k] : nlohmann::json::object();
}

/// La salida que maneja esta interfaz, en Config/interfaz.json ("" = la por omisión).
constexpr const char* kClaveSalidaActiva = "salida_activa";

QString hms(std::chrono::steady_clock::time_point since) {
    const auto s =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - since).count();
    return QString("%1:%2:%3")
        .arg(s / 3600, 2, 10, QChar('0'))
        .arg((s / 60) % 60, 2, 10, QChar('0'))
        .arg(s % 60, 2, 10, QChar('0'));
}

} // namespace

namespace {
/// ¿Le llegan cuadros a la entrada? El motor publica cuánto hace del último dato
/// (`sin_datos_s`: -1 si todavía no llegó nada).
} // namespace

StudioViewModel::StudioViewModel(AppRuntime& rt, QObject* parent) : QObject(parent), rt_(rt) {
    // Cada aviso del operador (los de abajo a la derecha) queda en el registro: si no,
    // solo los ve quien está mirando y no hay cómo saber cuáles se apilaron.
    connect(this, &StudioViewModel::notify, this, [](const QString& texto, const QString& nivel) {
        ba::diag::registro().linea(nivel == "error" ? ba::diag::Nivel::Aviso : ba::diag::Nivel::Info, "aviso-ui", 0,
                                   (nivel + ": " + texto).toStdString());
    });
    // Lo que usa la máquina: mide en su hilo desde el arranque (las alarmas y el MCP lo leen).
    recursos_ = std::make_unique<RecursosViewModel>(rt);
    // La cola única de descargas (modelos, agentes, …): antes que quien baja algo.
    // Su historial es de la máquina (como Modelos/ y Agentes/), no de la configuración.
    net::Descargas::iniciar(ba::platform::dataRoot());
    // Una vez: cada evento a su carpeta (antes de abrir el evento; ver PLAN-EVENTO-TODO).
    if (const nlohmann::json sep = Configuraciones::separarEventos(ba::platform::dataRoot()); !sep.empty()) {
        QStringList partes;
        for (const auto& x : sep)
            partes << (x.contains("error") ? tr("%1 (no se pudo: %2)")
                                                  .arg(QString::fromStdString(x.value("evento", std::string())),
                                                       QString::fromStdString(x.value("error", std::string())))
                                            : tr("%1 → «%2»")
                                                  .arg(QString::fromStdString(x.value("evento", std::string())),
                                                       QString::fromStdString(x.value("a", std::string()))));
        ba::diag::registro().linea(ba::diag::Nivel::Info, "eventos", 0,
                                   ba::i18n::llenar(ba::i18n::traducir("eventos", "cada evento a su carpeta: %1"),
                                                    {partes.join("; ").toStdString()}));
        QTimer::singleShot(4000, this, [this, partes]() {
            emit notify(tr("Cada evento tiene ahora su carpeta con todo lo suyo. Se separaron: %1 (abrilos con "
                           "«Abrir otro…» en la página del evento)")
                            .arg(partes.join("; ")),
                        "info");
        });
    }
    // La configuración chica que se guarda (modelo): se lee una vez, acá.
    modoVivoCfg_ = std::make_unique<config::ModoVivoConfig>(rt.dataRoot());
    rotulo_ = std::make_unique<agent::RotuloTraduccionConfig>(rt.dataRoot());
    ajustesEscenas_ = std::make_unique<escenas::AjustesEscenas>(rt.dataRoot());
    interfaz_ = std::make_unique<config::InterfazConfig>(rt.dataRoot());
    // El arranque, medido: cada etapa que tarde más de 150 ms queda en el registro
    // (la interfaz tardaba ~12 s en aparecer y no se sabía en qué).
    QElapsedTimer relojArranque;
    relojArranque.start();
    qint64 ultimaMarca = 0;
    const auto marca = [&relojArranque, &ultimaMarca](const char* que) {
        const qint64 ahora = relojArranque.elapsed();
        if (ahora - ultimaMarca > 150)
            ba::diag::registro().info(ba::i18n::llenar(ba::i18n::traducir("arranque", "arranque: %1 tardó %2 ms"),
                                                       {que, std::to_string(ahora - ultimaMarca)}));
        ultimaMarca = ahora;
    };
    // Lo que el motor no pudo hacer: sus órdenes van en cola y la interfaz no
    // espera el resultado, así que el error llega después, desde su hilo.
    rt_.engine().alFallarMotor([vm = QPointer<StudioViewModel>(this)](const std::string& texto) {
        QMetaObject::invokeMethod(
            qApp,
            [vm, t = QString::fromStdString(texto), ok = BroadcastEngine::esAvisoDeDiscoBien(texto)]() {
                if (vm) emit vm->notify(t, ok ? "ok" : "error");
            },
            Qt::QueuedConnection);
    });
    // Modo seguro (el circuit breaker del launcher, tras varias caidas
    // seguidas): NADA que venga del disco. Aquel apaga todo; el diario apaga al
    // culpable. Juntos cubren los dos casos: el que rompe una cosa y el que
    // rompe algo que no sabemos cual es.
    if (rt.config().safeMode) {
        ba::diag::diario().setModoSeguro(true);
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "arranque", 0,
                                   ba::i18n::traducir("arranque", "modo seguro: no se carga nada del disco en esta corrida"));
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
    // El FTB, la placa, los retornos y el objetivo de loudness son del modelo: acá se muestran.
    seguridadAire_ = std::make_unique<emision::SeguridadAire>(rt);
    seguridadAire_->alCambiar([this]() { emit placaChanged(); });
    retornos_ = std::make_unique<emision::RetornosInvitados>(rt);
    objetivoLoudness_ = std::make_unique<audio::ObjetivoLoudness>(rt.dataRoot(), rt.engine().loudness(),
                                                                  [&e = rt.engine()]() { e.aplicarLoudness(); });
    // El conmutador de escenas es del modelo (uno por salida); acá solo se refleja lo
    // que cambia. La interfaz muestra el de la salida activa (salidaActiva); las
    // pantallas de tally de la sala y el monitor «armada» siguen a la por omisión; el
    // de cada salida llega además a la lista de salidas.
    conmutadores_ = std::make_unique<escenas::Conmutadores>(rt, this);
    conmutador_ = &conmutadores_->porOmision();
    salidasModelo_ = std::make_unique<JsonListModel>(
        QStringList{"id", "nombre", "color", "porOmision", "alAire", "armada", "transmitiendo", "grabando", "archivo", "destinos", "programa",
                    "codificador"},
        this);
    const auto seguirSalida = [this](escenas::Conmutador* c) {
        connect(c, &escenas::Conmutador::alAireCambio, this, &StudioViewModel::publicarSalidas);
        connect(c, &escenas::Conmutador::armadaCambio, this, &StudioViewModel::publicarSalidas);
        connect(c, &escenas::Conmutador::alAireCambio, this, [this, c](const QString& escena) {
            if (c != &conmutadorActivo()) return;
            scenes_->liveChanged();
            sources_->setScene(escena); // el editor de fuentes sigue a la escena al aire
            emit sceneChanged();
        });
        connect(c, &escenas::Conmutador::armadaCambio, this, [this, c](const QString& escena) {
            if (c != &conmutadorActivo()) return;
            scenes_->setArmada(escena);
            emit sceneChanged();
        });
        connect(c, &escenas::Conmutador::afvCambio, this, [this, c]() {
            if (c != &conmutadorActivo()) return;
            emit statusChanged();
            emit afvChanged();
        });
    };
    for (escenas::Conmutador* c : conmutadores_->todos()) seguirSalida(c);
    connect(conmutadores_.get(), &escenas::Conmutadores::creado, this, seguirSalida);
    connect(conmutadores_.get(), &escenas::Conmutadores::cambio, this, &StudioViewModel::publicarSalidas);
    connect(conmutador_, &escenas::Conmutador::alAireCambio, this, &StudioViewModel::publicarTally);
    connect(conmutador_, &escenas::Conmutador::armadaCambio, this, [this](const QString& escena) {
        frames_->setArmada(escena); // el monitor «armada» mira la de la por omisión
        publicarTally();
    });
    // Los destinos de cada salida van en su fila.
    connect(this, &StudioViewModel::destinosChanged, this, &StudioViewModel::publicarSalidas);
    // La máquina se pone justa (o se alivia): cambia el aviso de crear otra salida.
    connect(recursos_.get(), &RecursosViewModel::cambio, this, [this]() {
        const QVariantMap n = nuevaSalida();
        if (n != nuevaSalidaAntes_) {
            nuevaSalidaAntes_ = n;
            emit nuevaSalidaChanged();
        }
    });
    publicarSalidas();
    // La salida que manejaba esta interfaz al cerrarse (si sigue estando; si no, la por omisión).
    if (const nlohmann::json v = interfaz_->valor(kClaveSalidaActiva); v.is_string() && !v.get<std::string>().empty())
        setSalidaActiva(qs(v.get<std::string>()));
    props_ = std::make_unique<PropertiesModel>(rt);
    marca("props");
    conference_ = std::make_unique<ConferenceViewModel>(rt);
    // El transporte A→B lo mide el gateway y viaja en la foto de la sala (los SLI, F4-01).
    recursos_->alTransporte([this]() {
        const QJsonValue t = conference_->client().snapshot().value("uso").toObject().value("transporte");
        const nlohmann::json j = nlohmann::json::parse(QJsonDocument(t.toArray()).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
        return j.is_array() ? j : nlohmann::json::array();
    });
    publicarSalidas(); // el gateway se entera de las salidas apenas hay con quién hablar
    marca("conference");
    direcciones_ = std::make_unique<sala::Direcciones>(conference_->client());
    // El cupo es del modelo; acá solo se muestra lo que pasó.
    cupoSala_ = std::make_unique<sala::CupoSala>(rt, conference_->client());
    cupoSala_->alAvisar([this](const std::string& texto, bool error) {
        emit notify(QString::fromStdString(texto), error ? "error" : "ok");
    });
    cupoSala_->alCambiar([this](int n) { frames_->setGuests(n); });
    configSala_ = std::make_unique<sala::ConfigSala>(conference_->client());
    seguridad_ = std::make_unique<sala::SeguridadSala>(rt.dataRoot(), conference_->client(), [this]() { return modoVivo(); });
    // El encargo al asistente vive aca (necesita el MCP): la sala solo avisa.
    conference_->setIntakeSink([this](const QString& id, const QString& name, bool yaAdentro) {
        return assistantIntake(id, name, yaAdentro);
    });
    connect(conference_.get(), &ConferenceViewModel::notify, this, &StudioViewModel::notify);
    accesoRemoto_ = std::make_unique<AccesoRemoto>(rt, conference_->client());
    accesoRemoto_->setDirecciones(direcciones_.get());
    accesoRemoto_->setEnVivo([this]() { return modoVivo(); });
    // «Configurar todo con la API key»: los cambios de Configuración → Pinggy van por un
    // modelo aparte (como el MCP), así no se pisa lo que el operador tenga a medio editar.
    accesoRemoto_->setAplicarPinggy([this](const QVariantMap& cambios) -> QString {
        SettingsModel m(rt_);
        m.revert();
        for (auto it = cambios.begin(); it != cambios.end(); ++it) {
            if (!m.value(it.key()).isValid()) return tr("esta versión no tiene %1").arg(it.key());
            m.setValue(it.key(), it.value());
        }
        const QString resumen = m.apply();
        if (m.fallo()) return resumen;
        return {}; // los formularios y las listas se enteran solos (avisos del runtime)
    });
    // Un solo renderizador de gráficos para todos los que disparan uno: el
    // guion, el editor, el rotulador y el asistente.
    emisor_ = std::make_unique<graficos::Emisor>(rt.dataRoot());
    connect(emisor_.get(), &graficos::Emisor::fallo, this, [this](const QString& m) { emit notify(m, "error"); });
    // El video en vivo dentro de un gráfico (la cámara en el zócalo) lo ponen las escenas.
    emisor_->setCapasDeVideo({[&rt](const std::string& g, const nlohmann::json& c) { return rt.engine().ponerVideoDeGrafico(g, c); },
                              [&rt](const std::string& g, std::int64_t ns) { rt.engine().empezarVideoDeGrafico(g, ns); },
                              [&rt](const std::string& g) { rt.engine().quitarVideoDeGrafico(g); }});
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
    // Un evento con página web y dirección propia: al ponerlo en curso, la dirección
    // pública pasa a ser su subdominio (con la API key de pinggy). Reinicia la sala:
    // al aire no se toca (se avisa).
    connect(evento_.get(), &EventoViewModel::pedirDireccion, this, [this](const QString& sub) {
        if (!accesoRemoto_ || !accesoRemoto_->apiKeyGuardada()) return;
        const QString actual = accesoRemoto_->estado().value("pinggy").toMap().value("urlPublica").toString();
        if (actual.contains("//" + sub + ".")) return; // ya es esa
        if (streaming_ || recording_) {
            emit notify(tr("El evento tiene su dirección (%1.a.pinggy.link) pero se está transmitiendo o grabando: "
                           "no se cambia ahora (reinicia la conferencia). Cambiala desde Guion → Página web cuando se pueda.")
                            .arg(sub),
                        "warning");
            return;
        }
        emit notify(tr("Pasando la dirección pública a %1.a.pinggy.link (la conferencia se reinicia)…").arg(sub), "info");
        accesoRemoto_->configurarConApiKey(sub);
    });
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
    // El tema del evento abierto: lo publica al gateway para todas las páginas web.
    temaEvento_ = std::make_unique<TemaEventoViewModel>(*evento_, *conference_);
    // Despues de `frames_`: mira los mismos videos que el resto de la interfaz.
    personas_ = std::make_unique<PersonasViewModel>(rt, *this);
    marca("personas");
    // Las preguntas del publico y la urna. Viven en B; esto las trae y las
    // modera. No consulta nada hasta que una vista se muestra (ver `activo`).
    publico_ = std::make_unique<PublicoViewModel>(conference_.get(), this);
    marca("publico");
    // La votación es del modelo (con el padrón del evento abierto); el público y el MCP la usan.
    votacion_ = std::make_unique<sala::VotacionSala>(conference_->client(), [this]() {
        return evento_ ? evento_->votantes() : std::nullopt;
    });
    publico_->setVotacion(votacion_.get());
    // Las preguntas, ídem; un mensaje del chat se busca en el chat que junta las plataformas.
    preguntas_ = std::make_unique<sala::PreguntasSala>(conference_->client(), [this](const std::string& clave)
                                                           -> std::optional<sala::PreguntasSala::MensajeChat> {
        if (!chatOverlay_) return std::nullopt;
        for (const auto& m : chatOverlay_->collect())
            if (m.key.toStdString() == clave)
                return sala::PreguntasSala::MensajeChat{m.text.toStdString(), m.author.toStdString(),
                                                        m.source == "room" ? std::string("sala") : m.source.toStdString()};
        return std::nullopt;
    });
    publico_->setPreguntas(preguntas_.get());
    connect(publico_.get(), &PublicoViewModel::notify, this, &StudioViewModel::notify);
    medios_ = std::make_unique<MediosViewModel>(rt, this);
    playlist_ = std::make_unique<medios::Playlist>(rt, this);
    playlist_->setArmar([this](const QString& escena) {
        scenes_->reload();
        emit escenasCambiaron();
        return conmutador_->armar(escena.toStdString());
    });
    connect(playlist_.get(), &medios::Playlist::notify, this, &StudioViewModel::notify);
    // Tras un corte de luz, la playlist sigue donde estaba (P0.10). Un rato
    // después: que las fuentes ya estén armadas.
    if (rt.corteAnterior())
        QTimer::singleShot(ba::ajustes::entero("arranque.retomarPlaylistMs", 2000), this, [this]() {
            const std::string r = playlist_->retomar();
            if (r.empty()) emit notify(tr("La playlist sigue donde estaba antes del corte"), "info");
            ba::diag::registro().linea(
                ba::diag::Nivel::Aviso, "arranque", 0,
                ba::i18n::llenar(ba::i18n::traducir("arranque", "playlist tras el corte: %1"),
                                 {r.empty() ? ba::i18n::traducir("arranque", "sigue donde estaba") : r}));
        });
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
    fuenteQr_ = std::make_unique<FuenteQrPintor>(*qr_);
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
    evento_->setCarruseles(carruseles_.get());
    // La línea de tiempo: el conmutador de cada salida y lo que mandan sus piezas (L6/L7).
    // Se pide cada vez: varios de estos se crean más abajo.
    evento_->setSistema([this]() {
        return EventoViewModel::Sistema{conmutadores_.get(),
                                        bus_.get(),
                                        dsk_.get(),
                                        camaras_.get(),
                                        asamblea_ ? &asamblea_->ordenDelDia() : nullptr,
                                        asamblea_ ? asamblea_->votaciones() : nullptr,
                                        playlist_.get()};
    });

    {
        // Mirar la carpeta es leer disco y parsear JSON ajeno: si eso se
        // llevara puesto el arranque, la proxima vez se saltea.
        ba::diag::Cargando c("extensiones");
        extensiones_ = std::make_unique<ExtensionesModel>(rt);
        extensiones_->setEnVivo([this]() { return modoVivo(); });
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
                if (error) emit notify(tr("Un rótulo automático no salió: %1").arg(texto), "error");
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
    // «Dónde verlo en vivo» de la página web del evento: lo que saben las cuentas.
    evento_->setPlataformas([this]() {
        nlohmann::json yt{{"liveId", youtube_->liveBroadcastId().toStdString()},
                          {"canalId", youtube_->account().value("id").toString().toStdString()},
                          {"handle", youtube_->account().value("customUrl").toString().toStdString()}};
        nlohmann::json tw{{"login", twitch_->user().value("login").toString().toStdString()},
                          {"enVivo", twitch_->streamingHere() || !twitch_->stream().isEmpty()}};
        nlohmann::json fb{{"liveId", facebook_->liveId().toStdString()}, {"pageId", facebook_->pageId().toStdString()}};
        return nlohmann::json{{"youtube", yt}, {"twitch", tw}, {"facebook", fb}};
    });
    // Las preguntas del público para la página del evento: SOLO las moderadas (la que se
    // responde, las elegidas, las respondidas); nunca las nuevas sin revisar ni las
    // descartadas. Mientras el evento tiene página, la cola se consulta (como si la
    // página Público estuviera abierta).
    evento_->setPublico(
        [this]() {
            nlohmann::json alAire = nlohmann::json::array(), elegidas = nlohmann::json::array(),
                           respondidas = nlohmann::json::array();
            for (const QVariant& v : publico_->preguntas()) {
                const QVariantMap p = v.toMap();
                const QString est = p.value("estado").toString();
                nlohmann::json q{{"texto", p.value("texto").toString().toStdString()},
                                 {"nombre", p.value("nombre").toString().toStdString()},
                                 {"votos", p.value("votos").toInt()},
                                 {"estado", est.toStdString()}};
                if (est == "al_aire") alAire.push_back(q);
                else if (est == "elegida") elegidas.push_back(q);
                else if (est == "respondida") respondidas.push_back(q);
            }
            nlohmann::json out = alAire;
            for (auto& q : elegidas) out.push_back(q);
            for (auto& q : respondidas) out.push_back(q);
            if (out.size() > 40) out.erase(out.begin() + 40, out.end());
            return out;
        },
        [this]() {
            return personasConectadas();
        });
    connect(publico_.get(), &PublicoViewModel::cambio, evento_.get(), &EventoViewModel::publicarWeb);
    // Lo que sabe el resto del sistema y es público, para la página del evento. Cada cosa
    // puede no existir (la asamblea se arma después, o no hay): la página no muestra lo que
    // no viene.
    evento_->setSala([this]() {
        nlohmann::json sala = nlohmann::json::object();
        sala["aire"] = {{"transmitiendo", streaming_}, {"grabando", recording_}};
        const auto publicoDe = [](const nlohmann::json& i) {
            nlohmann::json p{{"nombre", i.value("nombre", std::string())}};
            for (const char* k : {"bloque", "cargo"})
                if (i.contains(k) && i[k].is_string() && !i[k].get<std::string>().empty()) p[k] = i[k];
            return p;
        };
        // Lo de la asamblea (oradores, orden del día, quórum, el cuerpo y el recinto).
        if (asamblea_) sala.update(asamblea_->paraLaWeb());
        {
            // La votación: la pregunta y si está abierta; el resultado solo cuando ya se puede
            // mostrar (cerrada, o el operador eligió mostrarlo mientras está abierta).
            const QVariantMap v = publico_->votacion();
            if (v.value("hubo").toBool()) {
                const bool abierta = v.value("abierta").toBool();
                nlohmann::json vo{{"pregunta", v.value("pregunta").toString().toStdString()},
                                  {"abierta", abierta},
                                  {"modo", v.value("modo").toString().toStdString()},
                                  {"votaron", v.value("votaron").toInt()},
                                  {"podian", v.value("podian").toInt()}};
                if (!abierta || v.value("enVivo").toBool()) {
                    nlohmann::json res = nlohmann::json::array();
                    for (const QVariant& r : v.value("resultado").toList())
                        res.push_back({{"opcion", r.toMap().value("opcion").toString().toStdString()},
                                       {"votos", r.toMap().value("votos").toInt()}});
                    vo["resultado"] = res;
                }
                sala["votacion"] = vo;
            }
        }
        return sala;
    });
    connect(evento_.get(), &EventoViewModel::webChanged, this, [this]() {
        const bool hay = evento_->web().value("hay").toBool();
        if (hay == publicoPorWeb_) return;
        publicoPorWeb_ = hay;
        publico_->setActivo(hay);
    });
    connect(youtube_.get(), &YouTubeViewModel::stateChanged, evento_.get(), &EventoViewModel::publicarWeb);
    connect(twitch_.get(), &TwitchViewModel::stateChanged, evento_.get(), &EventoViewModel::publicarWeb);
    connect(twitch_.get(), &TwitchViewModel::channelChanged, evento_.get(), &EventoViewModel::publicarWeb);
    connect(facebook_.get(), &FacebookViewModel::stateChanged, evento_.get(), &EventoViewModel::publicarWeb);
    system_ = std::make_unique<SystemViewModel>();
    // Los chequeos del equipo en la tarjeta de preparación (F4-07): con lo que ya miden
    // las salidas (las sesiones de codificador), el motor (las placas) y el reloj.
    system_->alEquipo([this]() {
        diag::DatosEquipo d;
        d.codificaciones = rt_.engine().salidasAhora().value("codificaciones", nlohmann::json::array());
        d.limiteSesiones = ba::ajustes::entero("equipo.limiteSesiones", 0);
        d.placas = ba::motor::derivaDePlacas();
        std::tie(d.relojTipo, d.relojError) = rt_.engine().relojEstado();
        const std::filesystem::path ayudante = ba::platform::gstRaiz() / "libexec" / "gstreamer-1.0" /
#if defined(_WIN32)
                                               "gst-ptp-helper.exe";
#else
                                               "gst-ptp-helper";
#endif
        std::error_code ec;
        d.ptpAyudante = std::filesystem::exists(ayudante, ec);
        d.ptpPermisos = diag::permisosDeAyudantePtp(ayudante);
        QVariantList out;
        for (const auto& it : diag::chequeosDeEquipo(d))
            out.push_back(QVariantMap{{"id", QString::fromStdString(it.id)},
                                      {"estado", QString::fromStdString(it.estado)},
                                      {"titulo", QString::fromStdString(it.titulo)},
                                      {"detalle", QString::fromStdString(it.detalle)},
                                      {"como", QString::fromStdString(it.como)}});
        return out;
    });
    // Cambiar de configuración = anotarla y reiniciar por el launcher (como una actualización).
    configuraciones_ = std::make_unique<Configuraciones>([this]() {
        const QString evento = configuraciones_->activaUi();
        const QString aviso = eventoNuevo_ ? tr("Evento «%1» creado: reiniciando para abrirlo").arg(evento)
                                           : tr("Abriendo el evento «%1»: la app se reinicia").arg(evento);
        eventoNuevo_ = false;
        return reiniciarPrograma(aviso);
    });
    // Terminó un evento nuevo hecho aparte (una copia puede tardar): un solo aviso.
    connect(configuraciones_.get(), &Configuraciones::nuevoEventoListo, this,
            [this](const QString& nombre, const QString& error, bool abriendo) {
                eventoNuevo_ = abriendo; // el reinicio sale enseguida (Configuraciones::nuevoEventoAparte)
                if (!error.isEmpty()) emit notify(tr("No se pudo crear «%1»: %2").arg(nombre, error), "error");
                else if (!abriendo) emit notify(tr("Evento «%1» listo: abrilo con «Abrir otro…»").arg(nombre), "info");
            });
    // Lo que lleva más de un mes en la papelera de eventos se va (con grabaciones, son GB).
    if (const int n = configuraciones_->vaciarViejos(Configuraciones::kDiasPapelera); n > 0)
        qInfo("%s", ba::i18n::llenar(ba::i18n::traducir("eventos", "[eventos] papelera: %1 con más de %2 días, borrados"),
                                     {std::to_string(n), std::to_string(Configuraciones::kDiasPapelera)})
                        .c_str());
    marca("system");
    // Antes de cerrarse, el aviso con una cuenta regresiva (Main.qml lo muestra encima de todo):
    // que se cierre de golpe parecía una caída. El launcher ya está esperando que cierre.
    connect(system_.get(), &SystemViewModel::quitForUpdate, this, [this](const QString& aviso) {
        emit notify(aviso, "info");
        reinicio_ = {{"aviso", aviso}, {"segundos", 3}};
        emit reinicioChanged();
        auto* cuenta = new QTimer(this);
        cuenta->setInterval(1000);
        connect(cuenta, &QTimer::timeout, this, [this, cuenta]() {
            const int quedan = reinicio_.value("segundos").toInt() - 1;
            reinicio_["segundos"] = std::max(0, quedan);
            emit reinicioChanged();
            if (quedan > 0) return;
            cuenta->stop();
            emit quitRequested();
        });
        cuenta->start();
    });
    chatOverlay_ = std::make_unique<ChatOverlayRenderer>(*youtube_, *conference_, *twitch_, *facebook_);
    marca("chatOverlay");
    // El corte de luz (F1-11): la energía, la alarma «en batería» y la política.
    energia_ = std::make_unique<EnergiaAire>(EnergiaAire::Acciones{[&rt]() { return rt.cerrarGrabacionesPorEnergia(); },
                                                                   [&rt]() { rt.retomarGrabacionesPorEnergia(); },
                                                                   [&rt]() { return rt.emergenciaEnergia(); }});
    energia_->vigilar();
    connect(energia_.get(), &EnergiaAire::aviso, this, &StudioViewModel::notify);
    // Disparadores (alertas) contra el estado real de la app, y la pestaña Asistente. El
    // vigía corre en el plano de control (F3-03): `in` es lo que se lee en ESTE hilo y se
    // le pasa como foto (agent/EntradasAlarmas); `directas`, lo seguro entre hilos, que
    // sigue con la interfaz trabada (todo lo que sale al aire).
    {
        agent::AlertInputs in;
        in.energia = [this]() { return energia_->estado(); };
        in.audio = [this]() {
            std::vector<agent::AlertInputs::Audio> out;
            for (const std::string& n : rt_.engine().audioSources())
                out.push_back({n, rt_.engine().sourcePeakDb(n), rt_.engine().sourceMuted(n)});
            return out; // la mezcla del programa va directa (abajo)
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

        // El disco de todo lo que se graba (por volumen; la copia en el mismo disco). Mira
        // el espacio libre de cada carpeta: una vez cada 5 s alcanza.
        in.disco = [this, cache = std::make_shared<std::pair<qint64, nlohmann::json>>()]() {
            const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
            if (cache->first == 0 || ahora - cache->first >= 5000) *cache = {ahora, rt_.discoGrabacion()};
            return cache->second;
        };
        // Señal de cada entrada de video (las de GStreamer: cuadros que llegan). Se
        // consulta como mucho una vez por segundo.
        in.senales = [this, cache = std::make_shared<std::pair<qint64, std::vector<std::pair<std::string, bool>>>>(),
                      // Histéresis: la que quedó sin señal vuelve a «con señal» recién
                      // después de 10 s seguidos con cuadros. Una cámara que se corta y
                      // vuelve cada pocos segundos disparaba la alarma otra vez a cada
                      // rato (y la franja roja reaparecía apenas se la reconocía).
                      vuelta = std::make_shared<std::map<std::string, std::pair<bool, qint64>>>(),
                      desde = QDateTime::currentMSecsSinceEpoch()]() {
            const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
            // Recién arrancada, las entradas todavía se están conectando (una cámara de red
            // tarda unos segundos): no hay alarmas de «sin señal» los primeros 15 s.
            if (ahora - desde < 15000) return std::vector<std::pair<std::string, bool>>{};
            if (video_ && ahora - cache->first > 1000) {
                cache->first = ahora;
                cache->second.clear();
                for (const QString& e : video_->entradas()) {
                    const nlohmann::json st = video_->estadoEntrada(e.toStdString());
                    if (!st.contains("gstreamer")) continue; // sin el dato de GStreamer: sin cuadros que mirar
                    const nlohmann::json g = st.value("gstreamer", nlohmann::json::object());
                    if (!g.is_object()) continue;
                    // Las de los invitados y el remoto: sin nadie conectado no hay cuadros, y
                    // no es una falla (para eso está «invitado caído»).
                    const nlohmann::json aj = st.value("ajustes", nlohmann::json::object());
                    if (aj.is_object() && (aj.value("video_invitado", false) ||
                                           aj.value("pipeline", std::string()).rfind("bavideoanillo", 0) == 0))
                        continue;
                    const bool activa = st.value("activa", true);
                    // Una que todavía no entregó nada (una cámara de red conectándose): hasta
                    // los 45 s del arranque no es «sin señal»; la que tenía y la perdió, sí.
                    const bool nuncaLlego = g.value("sin_datos_s", 0.0) < 0.0;
                    bool con = !activa || video::VideoGrafos::tieneSenal(st) || (nuncaLlego && ahora - desde < 45000);
                    // La primera vez que se la ve no «vuelve» de nada: arranca dada por buena
                    // (si no, a los 15 s del arranque todas pasaban 10 s «sin señal»).
                    auto& [reportada, conDesde] =
                        vuelta->try_emplace(e.toStdString(), std::pair<bool, qint64>{true, 0}).first->second;
                    if (!con) {
                        reportada = false;
                        conDesde = 0;
                    } else if (!reportada && activa) {
                        if (conDesde == 0) conDesde = ahora;
                        if (ahora - conDesde < 10000) con = false; // todavía no se la da por vuelta
                        else reportada = true;
                    } else {
                        reportada = true;
                    }
                    cache->second.emplace_back(e.toStdString(), con);
                }
            }
            return cache->second;
        };
        in.superres = [this]() { return video_ ? video_->programador().value("entradas", nlohmann::json::object()) : nlohmann::json::object(); };
        // DIRECTAS: el motor, la foto de las salidas, la persistencia y los recursos.
        agent::AlertInputs directas;
        BroadcastEngine& motor = rt_.engine();
        // La mezcla del programa (lo que sale): para el silencio y la saturación de fábrica.
        directas.audio = [&motor]() {
            return std::vector<agent::AlertInputs::Audio>{{"Programa", motor.picoProgramaVigiaDb(), false}};
        };
        // De la foto de las salidas que publica el hilo de salidas del motor (no de status(),
        // que recorre las salidas de la interfaz).
        directas.status = [&rt]() {
            const nlohmann::json f = rt.engine().fotoSalidas();
            const nlohmann::json t = f.value("principal", nlohmann::json::object());
            agent::AlertInputs::Status a;
            a.streaming = t.value("andando", false);
            a.droppedFrames = t.value("perdidos", 0);
            a.totalFrames = t.value("cuadros", 0);
            a.reconnecting = t.value("reconectando", false);
            a.recording = f.value("grabacion", nlohmann::json::object()).value("activa", false);
            a.grabacionPedida = !rt.recordStopWanted();
            const nlohmann::json salidas = f.value("salidas", nlohmann::json::object()); // con nombre: UB si no
            for (const auto& [id, x] : salidas.items())
                if (x.value("grabacion", nlohmann::json::object()).value("caida", false)) a.grabacionesCaidas.push_back(id);
            return a;
        };
        directas.recursos = [this]() { return recursos_ ? recursos_->muestreador().niveles() : nlohmann::json::object(); };
        directas.persistencia = [&rt]() { return rt.avisosPersistencia(); };
        directas.programaVideo = [&motor]() {
            const auto v = motor.programaVideo();
            return nlohmann::json{{"valido", v.valido}, {"luma", v.luma}, {"quieto_s", v.quietoSeg}};
        };
        directas.loudness = [&motor]() { return motor.loudnessPrograma().json(); };
        // El área de las alarmas (su ViewModel, ligado al vigía): ui/AlarmasViewModel.
        alarmas_ = std::make_unique<AlarmasViewModel>(std::move(in), std::move(directas), rt.dataRoot());
        connect(alarmas_.get(), &AlarmasViewModel::notify, this, &StudioViewModel::notify);
        marca("alerts");
        vocabulario_ = std::make_unique<agent::Vocabulario>(rt.dataRoot() / "Config" / "vocabulario.json");
        bus_ = std::make_unique<control::BusEventos>();
        // Cada disparo, al bus (lo ve el control web).
        alarmas_->setPublicar([this](const std::string& tema, const nlohmann::json& d) { bus_->publicar(tema, d, true); });
        bus_->setMediciones(control::BusEventos::medicionesDeFabrica());
        // La bitácora del evento en curso y el histórico (ui/BitacoraEvento).
        if (evento_) {
            bitacoraEvento_ = std::make_unique<BitacoraEvento>(rt_, *evento_, *bus_);
            connect(bitacoraEvento_.get(), &BitacoraEvento::notify, this, &StudioViewModel::notify);
        }
        // Con un evento en curso, lo que se graba va a su carpeta (H4).
        // (Y las grabaciones que esperaban, tras un corte, vuelven ahí: no a la de fábrica.)
        if (evento_) rt_.setCarpetaDelEvento([this]() { return evento_->carpetaEvento(); });
        else rt_.retomarGrabaciones();
        // El cue en que va el guion, al bus («evento/cue»): lo muestra la botonera entre
        // sus botones «◀ Cue» y «Cue ▶», y cualquier superficie que lo quiera.
        if (evento_) {
            const auto publicarCue = [this]() {
                if (!bus_ || !evento_) return;
                const QVariantMap a = evento_->actual(), s = evento_->siguiente(), r = evento_->reloj();
                const auto titulo = [](const QVariantMap& c) {
                    const QString t = c.value("texto").toString();
                    return (t.isEmpty() ? c.value("tipo").toString() : t).toStdString();
                };
                bus_->publicar("evento/cue", {{"estado", evento_->estado().toStdString()},
                                             {"texto", a.isEmpty() ? std::string() : titulo(a)},
                                             {"siguiente", s.isEmpty() ? std::string() : titulo(s)},
                                             {"numero", r.value("cueNro").toInt()},
                                             {"total", r.value("cuesTotal").toInt()},
                                             {"hold", evento_->hold()}},
                               true);
            };
            connect(evento_.get(), &EventoViewModel::correChanged, this, publicarCue);
            publicarCue();
        }
        // Cada vivo con su URL pública (sin la clave), al bus y al histórico.
        const auto alVivo = [this](const QVariantMap& m) {
            bus_->publicar("salida/vivo",
                           nlohmann::json::parse(QJsonDocument(QJsonObject::fromVariantMap(m)).toJson().toStdString(),
                                                 nullptr, false),
                           true);
        };
        connect(youtube_.get(), &YouTubeViewModel::vivo, this, alVivo);
        // El vivo a mano (GUI o MCP: AppRuntime::salirAlAireManual). El servidor sí
        // (es público); la clave nunca.
        rt_.alVivoManual([this](const std::string& servidor) {
            QMetaObject::invokeMethod(this, [this, servidor]() {
                if (bus_) bus_->publicar("salida/vivo", {{"plataforma", "manual"}, {"servidor", servidor}}, true);
            });
        });
        connect(twitch_.get(), &TwitchViewModel::vivo, this, alVivo);
        connect(facebook_.get(), &FacebookViewModel::vivo, this, alVivo);
        notificador_ = std::make_unique<agent::Notificador>(rt.dataRoot());
        // La interfaz trabada con aire: la detecta el latido en su hilo y la lleva
        // AvisoFueraDeUi en el suyo (esta vista, la que se traba, no interviene).
        avisoFueraDeUi_ = std::make_unique<AvisoFueraDeUi>(rt.dataRoot());
        avisoFueraDeUi_->escuchar(rt.latido());
        // Y los notificadores de los disparadores, desde el hilo del vigía.
        alarmas_->setAvisoFueraDeUi(avisoFueraDeUi_.get());
        settings_->setEnVivo([this]() { return modoVivo(); });
        settings_->setCupoEnCaliente([this](int n) { return cupoSala_->cambiar(n); });
        settings_->setConfigSala(configSala_.get());
        // ¿Volvemos de una caída? El supervisor dejó escrita la causa.
        {
            const auto archivo = ba::platform::dataRoot() / "Runtime" / "ultima-caida.json";
            std::ifstream f(archivo);
            nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
            const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
            if (j.is_object() && !j.value("vista", false) && ahora - j.value("hora_ms", 0LL) < ba::ajustes::entero64("launcher.cartelCaidaMin", 10) * 60 * 1000) {
                caidaMs_ = j.value("hora_ms", 0LL);
                caida_ = QVariantMap{{"causa", QString::fromStdString(j.value("causa", std::string()))},
                                     {"componente", QString::fromStdString(j.value("componente", std::string()))},
                                     {"hace_s", static_cast<int>((ahora - caidaMs_) / 1000)}};
                // ¿Había algo al aire (en alguna salida)? Si no, no hay nada que esperar.
                bool habia = false;
                for (const auto& [id, a] : rt_.aireAnterior().salidas)
                    habia = habia || a.transmitiendo || a.grabando || !a.destinos.empty();
                caida_["habia_aire"] = habia;
                j["vista"] = true; // el cartel sale una sola vez
                f.close();
                ba::persist::cola().escribir(archivo, j.dump(1, ' ', false, nlohmann::json::error_handler_t::replace));
            }
        }
        if (!configRecuperada().empty())
            QTimer::singleShot(3000, this, [this]() {
                emit notify(tr("La configuración «%1» estaba dañada: se cargó la versión anterior (la dañada quedó "
                               "como .corrupto).")
                                .arg(QString::fromStdString(configRecuperada())),
                            "warn");
            });
        agent_ = std::make_unique<agent::AgentViewModel>(rt, alarmas_->motor(), "evento");
        // El de automatización: el que recibe las alertas (siempre levantado).
        agenteAuto_ = std::make_unique<agent::AgentViewModel>(rt, alarmas_->motor(), "automatizacion");
        marca("agent");
        connect(agent_.get(), &agent::AgentViewModel::notify, this, &StudioViewModel::notify);
        connect(agenteAuto_.get(), &agent::AgentViewModel::notify, this, &StudioViewModel::notify);
        // Reiniciada con una conversación para retomar (--reanudar-asistente=<id>|<proveedor>|<modelo>,
        // ver el reinicio de Configuraciones): el asistente arranca con ella y se muestra.
        for (const QString& a : QCoreApplication::arguments())
            if (a.startsWith("--reanudar-asistente=")) {
                const QStringList p = a.mid(21).split('|');
                QString evento;
                for (const QString& b : QCoreApplication::arguments())
                    if (b.startsWith("--asistente-evento=")) evento = b.mid(19);
                const QString seguir =
                    evento.isEmpty() ? QString()
                                     : QCoreApplication::translate(
                                           "asistente",
                                           "La app se reinició y ya está abierto el evento «%1». Si estabas en medio "
                                           "de algo, seguí desde donde quedaste; si no, esperá lo que te pida el operador.")
                                           .arg(evento);
                agent_->reanudarAlArrancar(p.value(0), p.value(1), p.mid(2).join('|'), seguir);
            }
        // Las reglas las cambia el motor (interfaz, MCP, plugins): la lista del
        // asistente se entera por él, sin que cada llamador se acuerde de avisar.
        connect(&alarmas_->motor(), &agent::AlertEngine::reglasCambio, agent_.get(), &agent::AgentViewModel::alertsChanged);
        connect(&alarmas_->motor(), &agent::AlertEngine::reglasCambio, agenteAuto_.get(), &agent::AgentViewModel::alertsChanged);
        // Los roles automaticos: un disparo que un rol activo reclama va a un
        // agente de un tiro y NO al chat del asistente interactivo.
        roles_ = std::make_unique<agent::Roles>(rt.dataRoot() / "Config" / "roles.json");
        marca("roles");
        despachador_ = std::make_unique<agent::Despachador>(*roles_, agent_->motores());
        agent_->setCorridasEnCurso([d = despachador_.get()]() { return d->enCurso(); });
        agenteAuto_->setCorridasEnCurso([d = despachador_.get()]() { return d->enCurso(); });
        marca("despachador");
        // Los manuales de usuario (Configuración → Sistema → Manuales): los arma el asistente de evento.
        manuales_ = std::make_unique<ManualesViewModel>(ManualesViewModel::Partes{
            .enVivo = [this]() { return modoVivo(); },
            .encargar = [this](const QString& t) { if (agent_) agent_->encargar(t); },
            .faltaAsistente = [this]() { return agent_ ? agent_->faltaParaEncargo() : QString("El asistente no está disponible."); },
            .paginas = []() { return RielConfig::idsDePaginas(); }});
        connect(despachador_.get(), &agent::Despachador::notify, this, &StudioViewModel::notify);
        // Las alertas las recibe el de automatización: él se las ofrece primero a los roles
        // (una sola vez por alerta; el del evento ya no las mira).
        agenteAuto_->setReclamado([this](const nlohmann::json& ev) { return despachador_->evento(ev); });
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
            // El chat del modelo local (pi + llama-server): qué .gguf y cómo bajar el que falta.
            for (agent::AgentViewModel* a : {agent_.get(), agenteAuto_.get()})
                a->setModeloChat(
                    [m = modelos_.get()](const QString& pedido, QString* falta, QString* repo) {
                        return m->ggufParaChat(pedido, falta, repo);
                    },
                    [m = modelos_.get()](const QString& repo) { m->descargar(repo); });
            // El llama-server carga la visión del modelo (--mmproj) si está bajada: la usan
            // el chat y el tótem (el dorso de un documento). Quién va con qué, ModelosLocales.
            agent::ServidorModeloLocal::el().setMmprojPara(
                [m = modelos_.get()](const QString& gguf) { return m->mmprojPara(gguf); });
            agenteAuto_->setLocal([v](const QString& quien, const QString& texto, bool disparador) {
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
        if (ba::fault::delProceso().activo()) {
            // El launcher arrancó en modo seguro tras varias caídas (F1-10): que el
            // operador sepa qué se apagó (nivel 1-2: las escenas están) o por qué
            // no ve sus escenas (nivel 3).
            messages_->post({{"kind", "message"},
                             {"priority", "critical"},
                             {"title", tr("Modo seguro").toStdString()},
                             {"text", ba::fault::delProceso().aviso()}});
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
            const QString who = QCoreApplication::translate("asistente", "el invitado %1")
                                    .arg(QString::fromStdString(item.value("participantId", "?")));
            // Las marcas «[OPERADOR]» y «[RESPUESTA DEL OPERADOR]» no se traducen: la ayuda
            // del MCP y las descripciones de las herramientas le dicen al asistente que las busque.
            const QString op = QStringLiteral("[OPERADOR] ");
            QString note;
            if (kind == "question")
                note = st == "answered" ? QStringLiteral("[RESPUESTA DEL OPERADOR] ") +
                                              QCoreApplication::translate("asistente", "a tu pregunta \"%1\": %2")
                                                  .arg(text, answer)
                                        : op + QCoreApplication::translate("asistente", "descarto tu pregunta \"%1\".")
                                                   .arg(text);
            else if (kind == "reply")
                note = st == "sent" ? op + QCoreApplication::translate("asistente", "le mando a %1: \"%2\"").arg(who, answer)
                                    : op + QCoreApplication::translate("asistente", "ignoro el mensaje de %1.").arg(who);
            else if (kind == "credential") {
                const QString p = QString::fromStdString(item.value("platform", ""));
                if (p.startsWith("camaras:"))
                    note = op + (st == "saved"
                                     ? QCoreApplication::translate(
                                           "asistente",
                                           "guardo usuario y clave de las camaras %1 (las que dejo sin clave, no). "
                                           "Siguiente paso: camara_ficha de cada una.")
                                           .arg(p.mid(8))
                                     : QCoreApplication::translate(
                                           "asistente", "cerro el formulario de las camaras %1 sin guardar.")
                                           .arg(p.mid(8)));
                else if (p.startsWith("camara:"))
                    note = op + (st == "saved"
                                     ? QCoreApplication::translate(
                                           "asistente",
                                           "guardo usuario y clave de la camara %1. Siguiente paso: camara_ficha {\"ip\": \"%1\"}.")
                                           .arg(p.mid(7))
                                     : QCoreApplication::translate(
                                           "asistente", "cerro el formulario de la camara %1 sin guardar.")
                                           .arg(p.mid(7)));
                else
                    note = op + (st == "saved"
                                     ? QCoreApplication::translate(
                                           "asistente", "guardo la credencial de %1. Siguiente paso: %1_login.")
                                           .arg(p)
                                     : QCoreApplication::translate(
                                           "asistente", "cerro el formulario de credenciales de %1 sin guardar.")
                                           .arg(p));
            }
            // Al asistente que lo pidió (el de automatización atiende el lobby y los avisos).
            if (!note.isEmpty())
                (item.value("asistente", "") == "automatizacion" ? agenteAuto_ : agent_)->deliverNote(note);
        });
    }
    // Las salidas de texto (antes que la Voz las use).
    flujos_ = std::make_unique<agent::FlujosTexto>(rt.dataRoot());
    if (evento_) flujos_->setCarpetaDelEvento([this]() { return evento_->carpetaEvento(); });
    marca("flujos");
    // El puesto web del corrector de subtítulos (/correccion/, T44).
    puestoCorreccion_ = std::make_unique<PuestoCorreccion>(static_cast<ConferenceViewModel*>(conference_.get())->client(),
                                                           *flujos_, this);
    // Los cuadros «Subtítulos» de las escenas muestran una salida de texto.
    subtitulos_ = std::make_unique<agent::Subtitulos>(*flujos_);
    // Subtítulos ocultos (CC) en la transmisión: 608 o la API de YouTube.
    cc_ = std::make_unique<agent::SubtitulosCC>(*flujos_, rt.dataRoot(), [this](const QString& t, double s) {
        return rt_.engine().enviarCaption(t.toStdString(), s);
    });
    // «Traducción automática» rotulada mientras sale una traducción (pantalla o CC).
    rotuloApagar_.setSingleShot(true);
    connect(&rotuloApagar_, &QTimer::timeout, this, [this]() {
        if (dsk_) dsk_->mostrar(rotulo_->capa(), false, true);
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
    // La documentación del evento (docs/): cada cambio republica la web del evento.
    documentos_ = std::make_unique<DocumentosViewModel>([&rt]() { return rt.dataRoot(); });
    if (evento_) connect(documentos_.get(), &DocumentosViewModel::cambio, evento_.get(), &EventoViewModel::publicarWeb);
    connect(documentos_.get(), &DocumentosViewModel::notify, this, &StudioViewModel::notify);
    // Los PDF en las escenas: la lista para elegir es la documentación del evento.
    pdfEscena_ = std::make_unique<PdfEscena>(rt);
    connect(documentos_.get(), &DocumentosViewModel::cambio, pdfEscena_.get(), &PdfEscena::refrescarDocumentos);
    // Lo que no anda de la IA, junto (la pastilla «IA» y ia_estado).
    estadoIA_ = std::make_unique<agent::EstadoIA>(agent_.get(), modelos_.get(), voz_.get(), vozPrompter_.get(),
                                                  traductor_.get());
    inputs_ = std::make_unique<InputsViewModel>(rt, *this);
    marca("inputs");
    // El PTZ de cada entrada (y Mejorar imagen): se entera enseguida de las
    // entradas nuevas y de lo que cambia en Configuración.
    camaras_ = std::make_unique<ptz::ControlCamaras>(rt, rt.dataRoot());
    // El catálogo de cosas que se pueden mostrar y los monitores de la máquina
    // (Salidas → Pantallas). Después de las cámaras, las multivistas, el tablero
    // y las pantallas de la red: el catálogo los lee a todos.
    {
        // Las multivistas antes que el catálogo (las lista) y las ventanas (las migran).
        multivistas_ = std::make_unique<MultiviewViewModel>(rt.dataRoot() / "Config");
        CatalogoVisible::Fuentes f;
        f.escenas = [this]() {
            QStringList out;
            for (const std::string& n : rt_.orderedSceneNames()) out << QString::fromStdString(n);
            return out;
        };
        f.multiviews = [this]() { return multivistas_->lista(); };
        f.fondo = [this]() { return dashboard_ ? dashboard_->background() : QStringList{}; };
        f.fondoDistribucion = [this]() { return dashboard_ ? dashboard_->backgroundLayout() : QString("grid"); };
        f.camarasPtz = [this]() {
            QStringList out;
            if (camaras_)
                for (const QVariant& v : camaras_->camaras()) out << v.toMap().value("entrada").toString();
            return out;
        };
        f.paginas = []() { return RielConfig::todasLasPaginas(); };
        f.contenidosRed = [this]() { return pantallas_ ? pantallas_->contenidos() : QVariantList{}; };
        // Los tótems de registro: cada uno a una TV, que muestra el QR para registrarse.
        f.totems = [this]() { return totems_ ? totems_->paraElegir() : QVariantList{}; };
        // Las fuentes de video sueltas (invitados, sus pantallas, las entradas):
        // lo que antes solo se podía sacar a una ventana de video por su clave.
        f.videos = [this]() {
            QVariantList out;
            if (frames_) {
                for (const QString& k : frames_->keys()) out << QVariantMap{{"clave", k}, {"titulo", frames_->label(k)}};
                // Las cámaras de los tótems de registro: para ver en otro monitor quién se
                // está registrando (negro mientras ese canal no conversa con nadie).
                for (int k = 1; k <= FrameService::kRegistroSlots; ++k) {
                    const QString clave = QString("registro:%1").arg(k);
                    out << QVariantMap{{"clave", clave}, {"titulo", frames_->label(clave)}};
                }
            }
            if (video_)
                for (const QString& e : video_->entradas()) out << QVariantMap{{"clave", "entrada:" + e}, {"titulo", e}};
            return out;
        };
        catalogo_ = std::make_unique<CatalogoVisible>(std::move(f));
        const auto refrescar = [this]() { catalogo_->refrescar(); };
        connect(this, &StudioViewModel::escenasCambiaron, catalogo_.get(), refrescar);
        connect(multivistas_.get(), &MultiviewViewModel::cambio, catalogo_.get(), refrescar);
        if (dashboard_) connect(dashboard_.get(), &DashboardModel::backgroundChanged, catalogo_.get(), refrescar);
        connect(camaras_.get(), &ptz::ControlCamaras::cambio, catalogo_.get(), refrescar);
        connect(frames_.get(), &FrameService::keysChanged, catalogo_.get(), refrescar);
        monitores_ = std::make_unique<MonitoresModel>(rt.dataRoot() / "Config" / "monitores.json", catalogo_.get());
        monitores_->conectarAlSistema();
        marca("monitores");
        // Las ventanas sueltas (pantallas/Ventanas.hpp): las mismas cosas del
        // catálogo y los mismos monitores. La primera vez trae las ventanas de
        // video que había en ui.json.
        {
            ba::diag::Cargando c("ventanas");
            ventanas_ = std::make_unique<VentanasModel>(
                c.saltear() ? std::filesystem::path{} : rt.dataRoot() / "Config" / "ventanas.json", catalogo_.get(),
                monitores_.get());
            if (!c.saltear()) ventanas_->migrarDesde(rt.dataRoot() / "Config" / "ui.json", multivistas_->actual());
        }
        multivistas_->setVentanas(ventanas_.get(), monitores_.get());
        connect(monitores_.get(), &MonitoresModel::cambio, this, &StudioViewModel::monitoresChanged);
        marca("ventanas");
    }
    // Los tiempos: solo fotos que ya se publican (el motor, las salidas, los grafos, los racks).
    tiempos_ = std::make_unique<tiempos::TiemposModel>([this]() {
        tiempos::Fotos f;
        if (motor::Motor* m = rt_.engine().motor())
            if (const auto foto = m->instantanea()) f.motor = *foto;
        f.salidas = rt_.engine().salidasAhora();
        if (video_)
            for (const QVariant& v : video_->estudio().value("nodos").toList()) {
                const QVariantMap n = v.toMap();
                const QString t = n.value("etiqueta").toString();
                f.titulos[n.value("id").toString().toStdString()] = (t.isEmpty() ? n.value("titulo").toString() : t).toStdString();
            }
        if (racks_)
            for (const std::string& s : racks_->fuentesConRack())
                if (audio::AudioRack* r = racks_->rack(s, false)) f.racks[s] = r->describe();
        // Los lugares de la sala sin nadie: su entrada recibe la placa del gateway, pero no es nadie.
        if (conference_) {
            BroadcastEngine& e = rt_.engine();
            for (const std::string& n : e.sourceNames()) {
                const nlohmann::json a = nlohmann::json::parse(e.sourceSettingsJson(n), nullptr, false);
                if (!a.is_object() || !a.contains("slot")) continue;
                const std::string canal = a.value("channel", a.value("anillo", std::string("guest")));
                if (canal != "guest" && canal != "share") continue; // el remoto con la palabra: la sesión remota
                const int slot = a.value("slot", 0);
                if (slot > 0 && conference_->participanteEnSlot(slot).isEmpty())
                    f.enEspera[n] =
                        tr("el lugar %1 de la conferencia está vacío (llega la placa)").arg(slot).toStdString();
            }
        }
        return f;
    }, rt.dataRoot() / "Config" / "tiempos-analisis.json");
    // El plan de resolución (solo lectura): lo que publican el motor, las escenas y los dispositivos.
    {
        resolucion::ResolucionModel::Fuentes rf;
        rf.motor = [this]() -> nlohmann::json {
            if (motor::Motor* m = rt_.engine().motor())
                if (const auto foto = m->instantanea()) return *foto;
            return nlohmann::json::object();
        };
        rf.escenas = [this]() {
            std::vector<std::pair<std::string, std::vector<std::pair<std::string, resolucion::Tam>>>> out;
            BroadcastEngine& e = rt_.engine();
            int lw = 0, lh = 0;
            if (!lienzoActual(lw, lh)) lw = e.videoConfig().baseWidth, lh = e.videoConfig().baseHeight;
            for (const std::string& esc : e.sceneNames()) {
                std::vector<std::pair<std::string, resolucion::Tam>> items;
                for (const auto& [fuente, r] : e.itemsVisibles(esc))
                    items.push_back({fuente, {static_cast<int>(std::lround(r.w * lw)), static_cast<int>(std::lround(r.h * lh))}});
                out.push_back({esc, std::move(items)});
            }
            return out;
        };
        rf.camaraDe = [this](const std::string& entrada) {
            const nlohmann::json a = nlohmann::json::parse(rt_.engine().sourceSettingsJson(entrada), nullptr, false);
            return a.is_object() ? a.value("camara", std::string()) : std::string();
        };
        rf.modosDe = [](const std::string& camara) { return video::modosDeCamara(camara); };
        rf.interior = [this](const std::string& elemento) {
            return video_ ? video_->gst().interior(elemento) : nlohmann::json::object();
        };
        rf.grafo = [this]() {
            std::vector<resolucion::ResolucionModel::NodoGrafo> out;
            if (video_)
                for (const auto& n : video_->grafoVideo().nodos()) out.push_back({n.id, n.tipo, n.etiqueta, n.grupo});
            return out;
        };
        rf.lienzo = []() {
            int w = 0, h = 0;
            if (!lienzoActual(w, h)) w = 1920, h = 1080;
            return resolucion::Tam{w, h};
        };
        rf.fps = []() { return fpsActual(); };
        resolucion_ = std::make_unique<resolucion::ResolucionModel>(std::move(rf));
    }
    // El análisis por elemento (las sondas del motor): lo llama el modelo en su hilo.
    tiempos_->setSondas({[this](const std::string& e) -> std::string {
                             motor::Motor* m = rt_.engine().motor();
                             return m != nullptr ? m->analizarEmpezar(e) : tr("el motor no está andando").toStdString();
                         },
                         [this](const std::string& e) -> nlohmann::json {
                             motor::Motor* m = rt_.engine().motor();
                             return m != nullptr ? m->analizarTerminar(e)
                                : nlohmann::json{{"error", tr("el motor no está andando").toStdString()}};
                         }});
    marca("camaras");
    connect(inputs_.get(), &InputsViewModel::changed, camaras_.get(), &ptz::ControlCamaras::sincronizar);
    // El grafo de tarjetas de video de cada entrada (después del PTZ).
    video_ = std::make_unique<video::VideoGrafos>(rt, rt.dataRoot());
    video_->setLegalizar(rt.config().legalizar);
    // «Priorizar programa» por calor (F4-05): el primer paso es la superresolución, que
    // pasa a barato (sin turnos activos) y vuelve a lo que tenía al aliviarse. Los
    // demás pasos (modelos, vistas sin mirar, resolución de invitados) todavía no
    // tienen quien los atienda: quedan en `sistema_recursos` como «sin_gancho».
    recursos_->alDegradar([this, antes = std::make_shared<nlohmann::json>()](const std::string& paso, bool apagar) {
        if (paso != "superres" || !video_) return false;
        if (apagar) {
            *antes = video_->programador().value("max_activas", nlohmann::json(3));
            video_->configurarProgramador({{"max_activas", 0}});
        } else {
            video_->configurarProgramador({{"max_activas", antes->is_number() ? *antes : nlohmann::json(3)}});
        }
        return true;
    });
    // Las entradas de video también son cosas que se pueden mostrar (catálogo).
    connect(video_.get(), &video::VideoGrafos::cambio, catalogo_.get(), [this]() { catalogo_->refrescar(); });
    catalogo_->refrescar();
    // El respaldo de las entradas al aire que pierden señal (modelo): acá solo se muestra.
    respaldo_ = std::make_unique<escenas::RespaldoEntradas>(rt, *video_, *conmutador_, this);
    connect(respaldo_.get(), &escenas::RespaldoEntradas::sinSenalCambio, this, &StudioViewModel::senalChanged);
    connect(respaldo_.get(), &escenas::RespaldoEntradas::aviso, this,
            [this](const QString& texto) { emit notify(texto, "error"); });
    // La superresolución que no llega: aviso al operador y evento en el bus (T40).
    connect(video_.get(), &video::VideoGrafos::superresTarde, this,
            [this](const QString& e, double ms, double cuadroMs, bool bypass) {
                emit notify((bypass ? tr("La superresolución de «%1» no llega (%2 ms por cuadro de %3): pasada a barato")
                                    : tr("La superresolución de «%1» no llega (%2 ms por cuadro de %3)"))
                                .arg(e).arg(ms, 0, 'f', 1).arg(cuadroMs, 0, 'f', 1),
                            "warn");
                if (bus_) bus_->publicar("superres/tarde", {{"entrada", e.toStdString()}, {"ms", ms}, {"cuadro_ms", cuadroMs}, {"bypass", bypass}});
            });
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
                agregar(true, tr("Disco: quedan %1 GB, menos de una hora de grabación").arg(gb, 0, 'f', 1));
            else if (horas < 3)
                agregar(false, tr("Disco: alcanza para ~%1 h de grabación").arg(horas, 0, 'f', 1));
        }
        if (!streamKeyAvailable()) agregar(false, tr("No hay destino de transmisión configurado (servidor y clave)"));
        if (video_)
            for (const QString& e : video_->entradas()) {
                const nlohmann::json st = video_->estadoEntrada(e.toStdString());
                if (!st.contains("gstreamer") || !st.value("activa", true)) continue;
                // Una entrada solo de audio (un tono, un micrófono por pipeline) no tiene
                // imagen nunca: no es «sin señal».
                const nlohmann::json aj = nlohmann::json::parse(rt_.engine().sourceSettingsJson(e.toStdString()), nullptr, false);
                if (aj.is_object() && aj.value("pipeline", std::string()).empty() && aj.value("uri", std::string()).empty() &&
                    aj.value("camara", std::string()).empty() && !aj.value("pipeline_audio", std::string()).empty())
                    continue;
                if (st.value("ancho", 0) <= 0)
                    agregar(false, tr("La entrada «%1» no tiene señal ahora").arg(e));
            }
        return out;
    });
    // La superresolución de las entradas de red (motores, modelos, laboratorio).
    superres_ = std::make_unique<video::SuperRes>(rt.dataRoot(), video_.get());
    {
        video::BancoSuperRes::Entorno en;
        en.alAire = [this]() { return rt_.engine().haySalida(); };
        en.fps = [this]() {
            const auto& v = rt_.engine().videoConfig();
            return std::make_pair(v.fpsNum, v.fpsDen);
        };
        en.tamanosCamaras = [this]() { return video_->tamanosDeEntradas(); };
        bancoSr_ = std::make_unique<video::BancoSuperRes>(rt.dataRoot(), *superres_, en);
        video_->setEstimadorSr([this](const std::string& p, int w, int h) { return bancoSr_->estimarMs(p, w, h); });
    }
    marca("video");
    connect(inputs_.get(), &InputsViewModel::changed, video_.get(), &video::VideoGrafos::sincronizar);
    // Los equipos de video de la red: la biblioteca de marcas y la base OUI
    // vienen con el programa (data/red).
    red_ = std::make_unique<red::Red>(ba::platform::bundleRoot() / "data" / "red",
                                      QString::fromStdWString((ba::platform::credentialsDir()).wstring()),
                                      video_.get(), camaras_.get());
    marca("red");
    // Los plugins de Python: en los datos (Plugins/), con el Python del programa.
    plugins_ = std::make_unique<plugins::Plugins>(rt, video_.get(), camaras_.get(), messages_.get());
    // Plugins <-> bus de eventos: lo que publican va al bus (plugin/<id>/<canal>)
    // y los temas de la app les llegan a los que escuchan.
    plugins_->setAlPublicar([this](const std::string& t, const nlohmann::json& d) { bus_->publicar(t, d, true); });
    // En vivo (T48) solo arrancan los plugins aprobados para el aire (T60).
    plugins_->setEnVivo([this]() { return modoVivo(); });
    // Gráficos animados de plugins (@plugin.grafico): el plugin los declara, quedan en
    // el catálogo de gráficos y el emisor se los pide cuando hay que dibujarlos.
    if (emisor_) {
        plugins_->setAlDeclararGrafico([this](const std::string& id, const nlohmann::json& d) {
            emisor_->registrarDePlugin(QString::fromStdString(id),
                                       QJsonDocument::fromJson(QByteArray::fromStdString(d.dump())).object());
        });
        emisor_->setProveedorPlugin([this](const QString& id, const QJsonObject& args,
                                           std::function<void(QJsonObject, QString)> listo) {
            const auto a = nlohmann::json::parse(QJsonDocument(args).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
            // Dibujar todos los cuadros en Python puede llevar su rato: un minuto de tope.
            plugins_->pedir(id.toStdString(), "grafico", a, [listo](const nlohmann::json& r) {
                if (!r.value("ok", false))
                    return listo({}, QString::fromStdString(r.value("error", tr("no contestó").toStdString())));
                const auto res = r.value("resultado", nlohmann::json::object());
                listo(QJsonDocument::fromJson(QByteArray::fromStdString(res.dump())).object(), {});
            }, 60000);
        });
    }
    // La asamblea: el cuerpo, la fila de oradores, el debate, la central, la banca virtual,
    // el quórum, el orden del día, las votaciones y la presidencia (ui/AsambleaViewModel).
    asamblea_ = std::make_unique<AsambleaViewModel>(AsambleaViewModel::Partes{
        .rt = rt,
        .bus = *bus_,
        .sala = *conference_,
        .direcciones = *direcciones_,
        .conmutador = *conmutador_,
        .camaras = *camaras_,
        .plugins = [this]() { return plugins_.get(); },
        .evento = evento_.get(),
        .archivosDeSesion = [this]() { return flujos_ ? flujos_->archivosDeSesion() : QStringList{}; }});
    connect(asamblea_.get(), &AsambleaViewModel::notify, this, &StudioViewModel::notify);
    bus_->suscribir("#", [this](const std::string& t, const nlohmann::json& d) {
        if (plugins_ && t.rfind("plugin/", 0) != 0) plugins_->eventoApp(t, d);
    });
    // Las salidas para las superficies de control (botonera, atajos, control web):
    // [{id, nombre, color, por_omision}], las que andan, la por omisión primera.
    const auto listaSalidas = [this]() {
        nlohmann::json out = nlohmann::json::array();
        for (const QJsonValue& v : salidasModelo_->items()) {
            const QJsonObject f = v.toObject();
            out.push_back({{"id", f.value("id").toString().toStdString()},
                           {"nombre", f.value("nombre").toString().toStdString()},
                           {"color", f.value("color").toString().toStdString()},
                           {"por_omision", f.value("porOmision").toBool()}});
        }
        return out;
    };
    // La botonera: el modelo, los permisos y la ejecución viven acá; el gateway
    // es solo el borde de red (TLS, tokens, WebSocket de las tablets).
    {
        control::Botonera::Ganchos g;
        g.escenas = [this]() { return rt_.engine().sceneNames(); };
        g.salidas = listaSalidas;
        g.fuentesAudio = [this]() { return rt_.engine().audioSources(); };
        g.pdfs = [this]() {
            std::vector<std::pair<std::string, std::string>> out;
            if (pdfEscena_)
                for (const QVariant& v : pdfEscena_->documentos())
                    out.emplace_back(v.toMap().value("id").toString().toStdString(), v.toMap().value("titulo").toString().toStdString());
            return out;
        };
        g.bancas = [this]() {
            std::vector<std::pair<std::string, std::string>> out;
            for (const auto& i : asamblea_->cuerpo().cuerpo().integrantes())
                if (!i.value("banca", std::string()).empty())
                    out.emplace_back(i.value("banca", std::string()), i.value("nombre", std::string()));
            return out;
        };
        g.recinto = [this]() { return asamblea_->cuerpo().cuerpo().recinto(); };
        // La cara de un botón: la foto del integrante (subida o capturada con la
        // cámara de su banca) o la foto de una posición PTZ, en 96 px.
        g.imagen = [this](const std::string& ref) -> std::string {
            QString ruta;
            const QString r = QString::fromStdString(ref);
            if (r.startsWith("medio:")) {
                ruta = QString::fromStdString(carasBotonera_->rutaPoster(ref.substr(6)));
            } else if (r.startsWith("cuerpo:")) {
                ruta = asamblea_->cuerpo().rutaFoto(r.mid(7));
            } else if (r.startsWith("ptz:") && camaras_) {
                const QString resto = r.mid(4);
                const int barra = resto.lastIndexOf('/');
                if (barra > 0)
                    for (const QVariant& v : camaras_->posiciones(resto.left(barra))) {
                        const QVariantMap m = v.toMap();
                        if (m.value("id").toString() == resto.mid(barra + 1) ||
                            m.value("nombre").toString().compare(resto.mid(barra + 1), Qt::CaseInsensitive) == 0)
                            ruta = m.value("miniatura_ruta").toString();
                    }
            }
            if (ruta.isEmpty()) return {};
            const QFileInfo fi(ruta);
            const QString clave = ruta + "|" + QString::number(fi.lastModified().toMSecsSinceEpoch());
            auto it = imagenesBotonera_.find(clave);
            if (it != imagenesBotonera_.end()) return it->second;
            QImage img(ruta);
            if (img.isNull()) return {};
            const int lado = std::min(img.width(), img.height());
            img = img.copy((img.width() - lado) / 2, (img.height() - lado) / 2, lado, lado)
                      .scaled(96, 96, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            QByteArray jpg;
            QBuffer buf(&jpg);
            buf.open(QIODevice::WriteOnly);
            img.convertToFormat(QImage::Format_RGB888).save(&buf, "JPG", 80);
            const std::string url = "data:image/jpeg;base64," + jpg.toBase64().toStdString();
            if (imagenesBotonera_.size() > 400) imagenesBotonera_.clear();
            imagenesBotonera_[clave] = url;
            return url;
        };
        g.cara = [this](const std::string& ref) -> nlohmann::json {
            if (ref.rfind("medio:", 0) != 0) return nullptr;
            return carasBotonera_->cara(ref.substr(6));
        };
        g.medioDeCara = [this](const std::string& id) -> nlohmann::json {
            const QVariantMap i = carasBotonera_->info(qs(id));
            return {{"medio", i.value("medio").toString().toStdString()}, {"video", i.value("video").toBool()}};
        };
        g.presidentes = [this]() {
            std::set<std::string> out;
            for (const auto& u : asamblea_->dcn().estado().value("unidades", nlohmann::json::array()))
                if (u.value("tipo", std::string()) == "presidente" && !u.value("banca", std::string()).empty())
                    out.insert(u.value("banca", std::string()));
            return out;
        };
        carasBotonera_ = std::make_unique<control::CarasBotonera>(rt, rt.dataRoot());
        botonera_ = std::make_unique<control::Botonera>(rt.dataRoot(), *bus_, std::move(g));
        satellite_ = std::make_unique<control::SatelliteServidor>(rt.dataRoot(), *botonera_);
        dsk_ = std::make_unique<control::Dsk>(rt.dataRoot(), rt_.engine());
        dsk_->setSalida(salidaActiva_);
        acciones_ = std::make_unique<control::Acciones>(rt.dataRoot());
        {
            auto& g = acciones_->ganchos();
            g.escenas = [this]() { return rt_.engine().sceneNames(); };
            g.fuentesAudio = [this]() { return rt_.engine().audioSources(); };
            g.mudo = [this](const std::string& f, const std::string& salida) { return rt_.engine().sourceMuted(f, salida); };
            g.salidas = listaSalidas;
            g.dskArriba = [this](int capa, const std::string& salida) { return dsk_->arriba(capa, salida); };
        }
        linea_ = std::make_unique<LineaViewModel>(*evento_, rt_.engine(), frames_.get(), acciones_.get(), bus_.get());
        dsk_->setAlCambiar([this](const nlohmann::json& c) {
            bus_->publicar("aire/dsk", c);
            publicarTally(); // lo que muestra una capa también está al aire
        });
        auto* cli = &static_cast<ConferenceViewModel*>(conference_.get())->client();
        // La botonera con el gateway (tablets: eventos, credenciales, emparejar): su modelo.
        botoneraGw_ = std::make_unique<control::BotoneraGateway>(*botonera_, *cli);
        connect(botoneraGw_.get(), &control::BotoneraGateway::aviso, this,
                [this](const QString& texto, const QString& nivel) { emit notify(texto, nivel); });
        // Los aparatos de plugins y del satélite (las tablets van por BotoneraGateway).
        connect(botonera_.get(), &control::Botonera::mensaje, this, [this](const QString& dev, const nlohmann::json& msg) {
            const std::string origen = botonera_->origenDe(dev.toStdString());
            if (origen == "plugin" && plugins_)
                plugins_->superficieEstado(dev.toStdString(), msg);
            else if (origen == "satellite" && satellite_)
                satellite_->mensaje(dev.toStdString(), msg);
        });
        // El control web (dashboard /control/): sus aparatos y el puente con el gateway (como la botonera).
        controlWeb_ = std::make_unique<control::ControlWeb>(rt.dataRoot());
        controlWeb_->ganchos().salidas = listaSalidas;
        controlWebGw_ = std::make_unique<control::ControlWebGateway>(*controlWeb_, *cli);
        // El tema de las páginas web: cada vez que la sala (re)arranca, se lo vuelve a pasar.
        connect(conference_.get(), &ConferenceViewModel::onlineChanged, this, [this]() {
            if (static_cast<ConferenceViewModel*>(conference_.get())->online()) mandarTemaWeb();
        });
        connect(controlWebGw_.get(), &control::ControlWebGateway::aviso, this,
                [this](const QString& texto, const QString& nivel) { emit notify(texto, nivel); });
        connect(controlWeb_.get(), &control::ControlWeb::solicitudNueva, this,
                [this](const QString&, const QString& nombre, const QString& ip) {
                    emit notify(tr("«%1» (%2) pide entrar al control web: aprobalo en Servicios → Control web.").arg(nombre, ip),
                                "warn");
                });
        connect(botonera_.get(), &control::Botonera::solicitudNueva, this,
                [this](const QString&, const QString& nombre, const QString& perfil, const QString& ip) {
                    emit notify(tr("«%1» (%2) pide emparejarse como botonera con el perfil «%3»: aprobalo en la página "
                                   "Botonera.")
                                    .arg(nombre, ip, perfil),
                                "warn");
                });
        // Los aparatos de hardware de los plugins (Stream Deck, MIDI…) son botoneras más.
        plugins_->setSuperficies(
            [this](const std::string& id, const std::string& nombre, const nlohmann::json& cap) {
                botonera_->registrarLocal(id, nombre, "plugin", cap);
            },
            [this](const std::string& id) { botonera_->quitarLocal(id); },
            [this](const std::string& id, const nlohmann::json& msg) { botonera_->entrada(id, msg); });
        // La grilla de personas (fuentes ba_grilla): la gente de la sala y de la asamblea,
        // repartida en las grillas; el mosaico lo pinta el gateway (BA.Grilla.<k>).
        grillasPersonas_ = std::make_unique<escenas::ControlGrillas>(
            escenas::ControlGrillas::Partes{*cli, rt_.engine(), asamblea_->sesionRemota(), &asamblea_->cuerpo().cuerpo()},
            this);
    }
    marca("plugins");
    // Seguir una cara con el PTZ (la ventana del PTZ y el asistente).
    seguidor_ = std::make_unique<Seguidor>(personas_.get(), camaras_.get());
    // Ir a una posición a pedido (enfocar, dar la palabra, un preset) corta el
    // seguimiento de caras de esa cámara: si no, se pelean.
    camaras_->setDejarDeSeguir([this](const QString& e) {
        if (seguidor_) seguidor_->dejar(e);
    });
    seguidor_->setGrafos(video_.get());
    marca("seguidor");
    // Los equipos (red, esta máquina, USB): un catálogo con acciones declaradas, y
    // el registro de los dispositivos de audio y video con el nombre del operador
    // (de la MÁQUINA, no del evento: <datos>/Config/dispositivos.json).
    dispositivos_ = std::make_unique<dispositivos::Dispositivos>(ba::platform::dataRoot() / "Config" /
                                                                 "dispositivos.json");
    marca("dispositivos");
    dispositivos::registrarTodo(*dispositivos_, {red_.get(), plugins_.get(), video_.get(), camaras_.get(),
                                                 messages_.get(), &rt, inputs_.get()});
#if defined(BA_HAVE_CLAP)
    // Racks de audio (LSP) de cada fuente: se montan los guardados.
    racks_ = std::make_unique<audio::RackManager>(rt.dataRoot());
    marca("racks");
    racks_->restore();
    audio_ = std::make_unique<audio::AudioViewModel>(*racks_);
    // Una entrada de audio nueva (o una que se va), la agregue quien la agregue (la
    // interfaz, el asistente, una receta): la lista del modelo de Audio se rehace sola.
    // El mezclador de las tarjetas la nota en su vuelta de 100 ms (reload = modelReset).
    connect(mixer_.get(), &QAbstractItemModel::modelReset, audio_.get(), [this]() { audio_->refresh(); });
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
    // El ruteo (escucha, pistas) es del motor: cuando cambia, por el camino que sea,
    // la matriz avisa a sus vistas y el PFL de la tarjeta y del Mezclador se entera.
    // El audio de cada fuente es del motor: cuando cambia, por el camino que sea, la
    // matriz lo anota y guarda (y se lo pone a una fuente que nace), y el mezclador
    // refresca esa tira. La matriz solo se redibuja si cambió el ruteo.
    rt.engine().alCambiarAudioFuente([m = matriz_.get(), mx = mixer_.get()](const std::string& f, const char* que) {
        m->avisarFuente(f, que);
        if (std::strcmp(que, "alta") != 0) mx->avisarCambio(f);
    });
    connect(matriz_.get(), &audio::Matriz::cambio, this, &StudioViewModel::pflChanged);
    // El Mezclador (la ventana propia): niveles de entradas y salidas, y su estado.
    mezclador_ = std::make_unique<audio::Mezclador>(rt, matriz_.get());
    mezclador_->setSalida(salidaActiva_);
    mezclador_->setRacks(racks_.get());
    // Un barrido de calibración nunca con la emisión o la grabación en curso.
    mezclador_->calibracion()->setAlAire([this] { return alAire(); });
    // Una entrada de placa se abre si la trae la tarjeta «Entrada» de un rack, o si su
    // propio rack tiene algo (un filtro, voz a texto): sin salida en la matriz igual
    // tiene que llegarles el audio.
    matriz_->setUsadasPorOtros([r = racks_.get()]() {
        std::set<std::string> s = r->fuentesDeEntradas();
        for (auto& f : r->fuentesConRack()) s.insert(f);
        return s;
    });
    // Las pistas que graba el programa (T29) quedan rotuladas en la Matriz.
    connect(this, &StudioViewModel::statusChanged, this, [this]() {
        const int mask = rt_.engine().pistasGrabacionMask();
        for (int i = 1; i < audio::Matriz::kPistas; ++i)
            matriz_->setUsoPista(i, (mask & (1 << i)) ? tr("grabación del programa").toStdString() : std::string());
    });
#endif
    // Las etiquetas de video ("Invitado 2") pasan a ser el nombre real.
    connect(conference_.get(), &ConferenceViewModel::roomChanged, this,
            [this]() { frames_->setGuestNames(conference_->slotNames()); });
    connect(conference_.get(), &ConferenceViewModel::sourceAdded, this, [this]() {
        sources_->reload();
        mixer_->reload();
    });

    connect(sources_.get(), &SourcesModel::error, this,
            [this](const QString& m) { emit notify(m, "error"); });
    connect(settings_.get(), &SettingsModel::avisoSala, this, [this](const QString& texto) { emit notify(texto, "error"); });
    connect(settings_.get(), &SettingsModel::salaReiniciada, this, [this](const QString& e) {
        emit notify(e.isEmpty() ? tr("Conferencia reiniciada con la configuración nueva.")
                                : tr("La conferencia no se reinició (%1): apretá «Reiniciar la conferencia» en Configuración "
                                     "o en la página Conferencia.")
                                      .arg(e),
                    e.isEmpty() ? "ok" : "error");
    });
    // El resumen de lo que aplicó el formulario de la GUI; lo que rige ya (lienzo,
    // tema, invitados, cámaras…) lo hace el aviso «Ajustes», aplique quien aplique.
    connect(settings_.get(), &SettingsModel::applied, this, [this](const QString& summary) {
        emit notify(summary, summary.contains("ERROR") ? "error" : "ok");
    });

    // Lo que cambia en el runtime (escenas, fuentes, salidas), lo pida quien lo
    // pida (la interfaz, el MCP, un plugin, el guion), se refleja acá solo. Los
    // avisos se juntan y se aplican en una pasada por vuelta del bucle: arrastrar
    // una capa o crear escenas en tanda no recarga las listas en cada paso.
    observadorRt_ = rt_.observar([this](AppRuntime::Cambio c) {
        const unsigned bit = 1u << static_cast<unsigned>(c);
        if (avisosPendientes_.fetch_or(bit) == 0)
            QMetaObject::invokeMethod(this, &StudioViewModel::refrescarAvisos, Qt::QueuedConnection);
    });
    // Todos los enlaces del aparato (página Enlaces y MCP enlaces_*): cómo preguntar y
    // prender cada servicio, sin que el modelo conozca sus clases.
    {
        EnlacesViewModel::Fuentes f;
        f.enlaces = [this]() { return &conference_->enlaces(); };
        f.salaEnLinea = [this]() { return conference_->online(); };
        f.salaCerrada = [this]() { return conference_->locked(); };
        f.cerrarSala = [this](bool on) { conference_->setLocked(on); };
        f.tunel = [this]() { return accesoRemoto_ ? accesoRemoto_->conexion() : QVariantMap(); };
        f.activarTunel = [this](bool on) -> QString {
            if (!accesoRemoto_) return tr("No disponible.");
            if (!on) return accesoRemoto_->desconectar();
            const QVariantList formas = accesoRemoto_->formasDeConectar();
            if (formas.isEmpty())
                return tr("No hay ninguna forma de conectar guardada: configurala en Configuración → Acceso remoto.");
            return accesoRemoto_->conectar(formas.first().toMap().value("id").toString());
        };
        f.apiRemota = [this]() { return apiRemotaUi(); };
        f.activarApiRemota = [this](bool on) { setApiRemotaUi(on); };
        f.satellite = [this]() -> QVariantMap {
            if (!satellite_) return {};
            return {{"activo", satellite_->activo()}, {"puerto", satellite_->puerto()}, {"error", satellite_->error()}};
        };
        f.activarSatellite = [this](bool on) -> QString {
            return satellite_ ? satellite_->configurar(on, satellite_->puerto()) : tr("No disponible.");
        };
        f.direcciones = [this]() -> const sala::Direcciones* { return direcciones_.get(); };
        f.webEvento = [this]() -> QVariantMap {
            if (!evento_) return {};
            return {{"hay", evento_->web().value("hay").toBool()}, {"evento", evento_->abierto()}};
        };
        f.controlWeb = [this]() -> QVariantMap {
            return {{"conectados", controlWeb_->conectados()},
                    {"aparatos", controlWeb_->aparatosUi().size()},
                    {"tunel", controlWeb_->tunel()}};
        };
        f.emparejarControlWeb = [this]() { emparejarControlWebUi(); };
        // Los pedidos de aprobación por aparato: se aprueban en la misma fila de Servicios.
        // Lo hace la persona que está en la consola (el asistente va por sus herramientas).
        {
            EnlacesViewModel::Aprobaciones a;
            a.pendientes = [this]() {
                QVariantList out;
                for (const QVariant& v : botonera_->solicitudesUi()) {
                    QVariantMap m = v.toMap();
                    m["perfil"] = m.value("perfilId");
                    m["detalle"] = tr("pide emparejarse");
                    out << m;
                }
                return out;
            };
            a.resolver = [this](const QString& sol, bool aprobar, const QString& perfil) -> QString {
                const std::string e = botoneraGw_->resolver(sol.toStdString(), aprobar, perfil.toStdString());
                if (e.empty()) emit notify(aprobar ? tr("Tablet emparejada.") : tr("Emparejamiento rechazado."), aprobar ? "ok" : "info");
                return qs(e);
            };
            a.perfilModo = "lista";
            a.perfiles = [this]() {
                QVariantList out;
                for (const QVariant& v : botonera_->perfilesUi()) {
                    const QVariantMap p = v.toMap();
                    out << QVariantMap{{"id", p.value("id")}, {"nombre", p.value("nombre")}};
                }
                return out;
            };
            f.aprobaciones["botonera"] = std::move(a);
        }
        {
            EnlacesViewModel::Aprobaciones a;
            a.pendientes = [this]() {
                QVariantList out;
                for (const QVariant& v : controlWeb_->solicitudesUi()) {
                    QVariantMap m = v.toMap();
                    m["detalle"] = tr("pide entrar al control web");
                    out << m;
                }
                return out;
            };
            a.resolver = [this](const QString& sol, bool aprobar, const QString& perfil) -> QString {
                const QString e = controlWeb_->aprobarUi(sol, aprobar, perfil);
                if (e.isEmpty()) emit notify(aprobar ? tr("Aparato emparejado.") : tr("Pedido rechazado."), aprobar ? "ok" : "info");
                return e;
            };
            a.perfilModo = "texto";
            a.perfilAyuda = tr("todo, o: escenas, audio");
            f.aprobaciones["control"] = std::move(a);
        }
        if (satellite_) {
            EnlacesViewModel::Aprobaciones a;
            a.pendientes = [this]() {
                QVariantList out;
                for (const QVariant& v : satellite_->solicitudesUi()) {
                    QVariantMap m = v.toMap();
                    m["detalle"] = tr("quiere conectar un Satellite");
                    out << m;
                }
                return out;
            };
            a.resolver = [this](const QString& ip, bool aprobar, const QString&) -> QString {
                if (!satellite_->pendientes().contains(ip)) return tr("Ese pedido ya no está.");
                if (aprobar)
                    satellite_->permitir(ip, true);
                else
                    satellite_->rechazar(ip);
                return {};
            };
            a.textoAprobar = tr("Permitir");
            a.unoEspera = tr("Una IP pide conectar: permitila acá abajo");
            a.variosEsperan = tr("%1 IP piden conectar: permitilas acá abajo");
            f.aprobaciones["satellite"] = std::move(a);
        }
        {
            EnlacesViewModel::Aprobaciones a;
            a.pendientes = [this]() {
                QVariantList out;
                const auto* l = qobject_cast<const JsonListModel*>(conference_->waiting());
                if (l == nullptr) return out;
                for (const QJsonValue& v : l->items()) {
                    const QJsonObject p = v.toObject();
                    out << QVariantMap{{"id", p.value("uid").toString()},
                                       {"nombre", p.value("name").toString()},
                                       {"ip", QString()},
                                       {"ms", p.value("joinedMs").toDouble()},
                                       {"detalle", p.value("info").toString().isEmpty()
                                                       ? tr("espera en el lobby")
                                                       : tr("espera en el lobby · %1").arg(p.value("info").toString())}};
                }
                return out;
            };
            a.resolver = [this](const QString& uid, bool aprobar, const QString&) -> QString {
                if (aprobar)
                    conference_->admit(uid);
                else
                    conference_->reject(uid);
                return {};
            };
            a.textoAprobar = tr("Admitir");
            a.unoEspera = tr("Una persona espera en el lobby: admitila acá abajo");
            a.variosEsperan = tr("%1 personas esperan en el lobby: admitilas acá abajo");
            f.aprobaciones["invitados"] = std::move(a);
        }
        enlaces_ = std::make_unique<EnlacesViewModel>(std::move(f));
        auto* e = enlaces_.get();
        const auto avisar = [e]() { e->avisarCambio(); };
        connect(conference_.get(), &ConferenceViewModel::onlineChanged, e, avisar);
        // Cambiaron las direcciones (la red, el túnel, una puerta): los enlaces se vuelven a pedir.
        connect(direcciones_.get(), &sala::Direcciones::cambio, e, &EnlacesViewModel::refrescar);
        connect(conference_.get(), &ConferenceViewModel::roomChanged, e, avisar);
        if (accesoRemoto_) connect(accesoRemoto_.get(), &AccesoRemoto::estadoChanged, e, avisar);
        if (satellite_) connect(satellite_.get(), &control::SatelliteServidor::cambio, e, avisar);
        connect(controlWeb_.get(), &control::ControlWeb::aparatosCambio, e, avisar);
        connect(controlWeb_.get(), &control::ControlWeb::solicitudesCambio, e, avisar);
        connect(controlWeb_.get(), &control::ControlWeb::conexionesCambio, e, avisar);
        connect(botonera_.get(), &control::Botonera::solicitudesCambio, e, avisar);
        connect(botonera_.get(), &control::Botonera::cambio, e, avisar); // los nombres de los perfiles
        if (evento_) {
            connect(evento_.get(), &EventoViewModel::webChanged, e, avisar);
            connect(evento_.get(), &EventoViewModel::guionChanged, e, avisar);
        }
    }
    // La atención a quien se conecta (Sala y Asamblea): con todo lo que usa ya armado.
    if (voz_ && modelos_ && despachador_) {
        atencion_ = std::make_unique<AtencionViewModel>(rt, *this, *voz_, *modelos_);
        connect(atencion_.get(), &AtencionViewModel::notify, this, &StudioViewModel::notify);
        marca("atencion");
    }
    // El registro de personas por tótem (docs/funciones/REGISTRO-TOTEM.md): con la
    // atención (su voz), Personas, el evento y la sala ya armados. Existe aunque
    // no haya motor de voz: la configuración y el catálogo no dependen de él.
    totems_ = std::make_unique<TotemsViewModel>(rt, *this, voz_.get(), modelos_.get());
    connect(totems_.get(), &TotemsViewModel::notify, this, &StudioViewModel::notify);
    // El modelo que entiende en los tótems es el del rol «registro» (pestaña Agentes): si lo
    // cambian (motor, modelo, apagarlo), la página de tótems dice con qué entiende.
    if (roles_) totems_->setRoles(roles_.get());
    if (despachador_) connect(despachador_.get(), &agent::Despachador::cambio, totems_.get(), &TotemsViewModel::cambio);
    if (catalogo_) connect(totems_.get(), &TotemsViewModel::cambio, catalogo_.get(), [this]() { catalogo_->refrescar(); });
    if (qr_) qr_->setTotems([this]() { return totems_ ? totems_->paraElegir() : QVariantList{}; });
    if (catalogo_) catalogo_->refrescar();
    marca("totems");
    connect(&tick_, &QTimer::timeout, this, &StudioViewModel::onTick);
    tick_.start(100);
    connect(&frameTick_, &QTimer::timeout, frames_.get(), &FrameService::poll);
    frameTick_.start(16); // hasta 60 Hz; FrameService solo avisa si hay frame nuevo
    // El negro y la placa ya volvieron con el runtime (AppRuntime::restaurarAire, F3-04):
    // acá solo se avisa, con la interfaz ya cargada (lo encolado corre con el bucle andando).
    if (rt_.placaRestaurada())
        QMetaObject::invokeMethod(
            this, [this]() { emit notify(tr("Se restauró lo que estaba al aire antes del corte (placa o negro)"), "warn"); },
            Qt::QueuedConnection);
    // Lo que arranca al abrir el programa: las capas fijas (DSK) y los gráficos con `alAbrir`.
    QTimer::singleShot(ba::ajustes::entero("arranque.restaurarPlacaMs", 1500), this, [this]() {
        if (dsk_) dsk_->restaurar();
        // Los gráficos que arrancan solos al abrir el evento (ajuste `alAbrir` de la
        // fuente): el logo que flamea en bucle, la placa de espera. Sin esto, una
        // plantilla con su bandera la mostraba recién cuando alguien la disparaba.
        if (emisor_)
            for (const auto& g : graficos::instancias()) {
                if (!g.alAbrir || g.reproduciendo) continue;
                const QJsonObject v = QJsonDocument::fromJson(QByteArray::fromStdString(g.valores.dump())).object();
                const QString e = emisor_->mostrar(QString::fromStdString(g.grafico), v, g.id, 0, g.bucle);
                if (!e.isEmpty())
                    ba::diag::registro().linea(ba::diag::Nivel::Aviso, "graficos", 0,
                                           ba::i18n::llenar(ba::i18n::traducir("graficos", "«%1» no arrancó al abrir: %2"),
                                                            {g.nombre, e.toStdString()}));
            }
    });
}

StudioViewModel::~StudioViewModel() {
    rt_.engine().alFallarMotor(nullptr);
    rt_.engine().alCambiarAudioFuente(nullptr); // la matriz y el mezclador se van con la vista; el motor sigue
    rt_.dejarDeObservar(observadorRt_); // el runtime vive más que la vista
    // El runtime y los flujos de texto viven más que el evento: que no le
    // pregunten la carpeta a un EventoViewModel que ya no está.
    rt_.setCarpetaDelEvento(nullptr);
    if (video_) video_->setEstimadorSr(nullptr);
    if (flujos_) flujos_->setCarpetaDelEvento(nullptr);
    // La bitácora antes que el bus y el evento de los que cuelga (anota «la app se cierra»).
    bitacoraEvento_.reset();
}

QVariant StudioViewModel::uiValor(const QString& clave, const QVariant& porDefecto) const {
    // El archivo es del modelo (config::InterfazConfig); acá solo se convierte a QVariant.
    const nlohmann::json v = interfaz_->valor(clave.toStdString());
    if (v.is_number_integer()) return QVariant::fromValue(v.get<long long>());
    if (v.is_number()) return v.get<double>();
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_string()) return QString::fromStdString(v.get<std::string>());
    return porDefecto;
}

void StudioViewModel::uiGuardar(const QString& clave, const QVariant& valor) {
    nlohmann::json v;
    switch (valor.typeId()) {
        case QMetaType::Bool: v = valor.toBool(); break;
        case QMetaType::Int:
        case QMetaType::LongLong: v = valor.toLongLong(); break;
        case QMetaType::Double: v = valor.toDouble(); break;
        default: v = valor.toString().toStdString(); break;
    }
    interfaz_->guardar(clave.toStdString(), v);
}

void StudioViewModel::onTick() {
    asamblea_->tic();
    // (El reloj del runtime ya no es este: lo da el plano de control, arranque/PlanoControl.
    // Acá solo lo que muestra la vista.)
    // El retorno a los invitados: sin nadie que lo mire hace 10 s, la vista que
    // baja el programa entero en cada cuadro descansa (ver retornoEnUso). Quién
    // mira lo dice el GATEWAY (cuántas sesiones reciben el programa: invitados,
    // televisores, la consola de la presidencia…); lo de abajo queda por si la
    // sala todavía no lo informó. Con una lista de casos acá se escapaba el que
    // faltaba: la consola de la presidencia se quedaba «esperando el programa».
    {
        auto* conf = static_cast<ConferenceViewModel*>(conference());
        const int miran = conf != nullptr ? conf->client().miranPrograma() : -1;
        const bool alguien = miran > 0 ||
                             (conf != nullptr && static_cast<JsonListModel*>(conf->inside())->count() > 0) ||
                             asamblea_->sesionRemota()->conectados() > 0 ||
                             (pantallas_ != nullptr && pantallas_->muestraPrograma());
        const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
        if (alguien) retornoVistoMs_ = ahora;
        rt_.engine().retornoEnUso(alguien || ahora - retornoVistoMs_ < 10000);
        // Las demás salidas: su retorno (BA.Salida.<id>) solo mientras el gateway dice
        // que alguien lo mira, con los mismos 10 s de gracia.
        const QJsonObject miranSalidas = conf != nullptr ? conf->client().miranSalidas() : QJsonObject();
        for (const std::string& id : rt_.engine().salidaIds()) {
            if (id == BroadcastEngine::kSalidaPorOmision) continue;
            const QString q = qs(id);
            if (miranSalidas.value(q).toInt() > 0) salidaVistaMs_[q] = ahora;
            rt_.engine().retornoEnUso(ahora - salidaVistaMs_.value(q, ahora - 10000) < 10000, id);
        }
    }
    // Lo de la interfaz que mira el vigía de las alarmas (en el plano de control), como
    // foto: cada 200 ms (él evalúa cada 250).
    if (alarmas_ && vigiaTicks_ % 2 == 0) alarmas_->refrescarFoto();
    // Señal de las entradas (multiview, escena de respaldo): una vez por segundo.
    if (vigiaTicks_ % 10 == 5) revisarSenales();
    // El vigía de los módulos de audio: lo que saltó por lento o colgado.
    if (++vigiaTicks_ % 10 == 0 && racks_ != nullptr)
        for (const auto& m : racks_->tomarSaltados())
            emit notify(tr("Audio: «%1» se saltó por lento (pasa sin procesar). Rearmalo en Audio cuando esté bien.").arg(qs(m)),
                        "warn");
    // La interfaz volvió de una traba: cuánto duró (el aire no se cortó).
    if (const std::uint64_t ms = rt_.latido().tomarTrabaTerminada(); ms > 0)
        emit notify(tr("La interfaz estuvo trabada %1 s; el aire siguió").arg(ms / 1000.0, 0, 'f', 1), "warn");
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
    {
        // Las demás salidas: qué sale de cada una y desde cuándo (los relojes de
        // aireActiva, que no se reinician al cambiar de activa).
        std::map<std::string, AireDeSalida> ahora;
        const auto t = std::chrono::steady_clock::now();
        for (const auto& e : st.salidas) {
            if (e.porOmision) continue;
            const auto antes = aireSalidas_.find(e.id);
            AireDeSalida a = antes != aireSalidas_.end() ? antes->second : AireDeSalida{};
            const bool transmite = e.streaming || e.destinos > 0;
            if (transmite && !a.transmite) a.transmiteDesde = t;
            if (e.recording && !a.graba) a.grabaDesde = t;
            a.transmite = transmite;
            a.graba = e.recording;
            a.reconecta = e.reconnecting;
            ahora[e.id] = a;
        }
        aireSalidas_ = std::move(ahora);
    }
    // Un corte pedido es un aviso corto; uno que nadie pidio (red, servidor,
    // disco lleno) queda fijo en la barra del operador hasta que lo cierre.
    const auto dropped = [this](const QString& title, const QString& text) {
        if (messages_) {
            messages_->post({{"kind", "message"},
                             {"priority", "critical"},
                             {"title", title.toStdString()},
                             {"text", text.toStdString()}});
        } else {
            emit notify(title, "error");
        }
    };
    if (recording_ && !st.recording) {
        if (rt_.recordStopWanted()) {
            emit notify(tr("Grabación detenida"), "info");
        } else if (st.recordingDown) {
            // Se cortó y el motor la rearma sola en otra parte (F1-05): NO se para, que eso
            // la mataría. Queda la alarma record_down hasta que vuelva.
            dropped(tr("Se cortó la grabación"),
                    tr("La grabación se cortó y vuelve sola en otra parte del archivo. Si no vuelve, revisá el disco."));
        } else {
            rt_.stopRecording(); // nadie la está rearmando: no queda nada que retomar
            dropped(tr("Se cortó la grabación"),
                    tr("La grabación se detuvo sola. Revisá el espacio libre en el disco y volvé a grabar."));
        }
    }
    if (streaming_ && !st.streaming) {
        if (rt_.streamStopWanted()) {
            emit notify(tr("Transmisión detenida"), "warn");
        } else {
            rt_.stopStreaming();
            dropped(
                tr("Se cayó la transmisión"),
                tr("La emisión se cortó sola (internet o el servidor de destino). Revisá la conexión y volvé a "
                   "salir al aire."));
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
        emit notify(tr("Se cortó la transmisión: reconectando… (%1)").arg(qs(st.outputProblem)), "error");
    else if (!st.reconnecting && reconnecting_ && st.streaming)
        emit notify(tr("Transmisión recuperada"), "ok");
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
    cpu_ = cpuUso_.medir();
    gatewayAlive_ = rt_.gatewayAlive();
    float l = -100;
    float r = -100;
    rt_.engine().programPeaksDb(l, r);
    peakL_ = std::max<double>(l, peakL_ - 1.5);
    peakR_ = std::max<double>(r, peakR_ - 1.5);
    if (!salidaActiva_.empty()) { // el vúmetro de la activa (las alarmas siguen mirando la por omisión)
        rt_.engine().programPeaksDb(l, r, salidaActiva_);
        peakActivaL_ = std::max<double>(l, peakActivaL_ - 1.5);
        peakActivaR_ = std::max<double>(r, peakActivaR_ - 1.5);
    }
    mixer_->refreshLevels();
    emit nivelesChanged();
    // El resto, solo si cambió algo de lo que se ve (con tiempos de grabación o
    // transmisión corriendo, una vez por segundo).
    const QVariantMap aa = aireActiva();
    QStringList firmaActiva;
    for (auto it = aa.begin(); it != aa.end(); ++it) firmaActiva << it.value().toString();
    const QString firma = QStringList{firmaActiva.join(','),QString::number(recording_), QString::number(streaming_), QString::number(alAire()),
                                      QString::number(transmitiendo()), QString::number(modoVivo()), QString::number(reconnecting_),
                                      outputProblem_, recordTime(), streamTime(), QString::number(fps_, 'f', 1), encoder_,
                                      QString::number(dropped_), QString::number(total_), QString::number(kbps_),
                                      QString::number(cpu_, 'f', 0), QString::number(gatewayAlive_), streamServer(),
                                      QString::number(streamKeyAvailable()), QString::number(streamKeySaved()), room(),
                                      QString::number(guests()), QString::number(personasConectadas())}
                              .join('|');
    if (firma != firmaEstado_) {
        firmaEstado_ = firma;
        emit statusChanged();
    }
    publicarEventos();
}

QString StudioViewModel::recordTime() const { return recording_ ? hms(recSince_) : QString("00:00:00"); }
QString StudioViewModel::streamTime() const { return streaming_ ? hms(streamSince_) : QString("00:00:00"); }
QString StudioViewModel::currentScene() const { return qs(rt_.engine().currentScene(salidaActiva_)); }
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
QObject* StudioViewModel::direccionesQml() const { return direcciones_.get(); }
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
QObject* StudioViewModel::manualesQml() const { return manuales_.get(); }
QObject* StudioViewModel::agenteAutomatizacion() const { return agenteAuto_.get(); }
QObject* StudioViewModel::modelos() const { return modelos_.get(); }
QObject* StudioViewModel::descargas() const { return &net::Descargas::la(); }
QObject* StudioViewModel::voz() const { return voz_.get(); }
QObject* StudioViewModel::vozPrompter() const { return vozPrompter_.get(); }
QObject* StudioViewModel::mezcladorQml() const {
#if defined(BA_HAVE_CLAP)
    return mezclador_.get();
#else
    return nullptr;
#endif
}
QObject* StudioViewModel::matriz() const {
#if defined(BA_HAVE_CLAP)
    return matriz_.get();
#else
    return nullptr;
#endif
}
QObject* StudioViewModel::camaras() const { return camaras_.get(); }
QObject* StudioViewModel::tiempos() const { return tiempos_.get(); }
QObject* StudioViewModel::resolucion() const { return resolucion_.get(); }
QObject* StudioViewModel::video() const { return video_.get(); }
QObject* StudioViewModel::red() const { return red_.get(); }
QObject* StudioViewModel::pluginsQml() const { return plugins_.get(); }
QObject* StudioViewModel::seguidor() const { return seguidor_.get(); }
QObject* StudioViewModel::dispositivosQml() const { return dispositivos_.get(); }
QObject* StudioViewModel::flujos() const { return flujos_.get(); }
QObject* StudioViewModel::voces() const { return voces_.get(); }
QObject* StudioViewModel::traductor() const { return traductor_.get(); }
QObject* StudioViewModel::estadoIA() const { return estadoIA_.get(); }
QObject* StudioViewModel::documentosQml() const { return documentos_.get(); }
QObject* StudioViewModel::pdfEscenaQml() const { return pdfEscena_.get(); }
QObject* StudioViewModel::superres() const { return superres_.get(); }
QObject* StudioViewModel::bancoSrObj() const { return bancoSr_.get(); }
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
    return QJsonDocument::fromJson(QByteArray::fromStdString(g.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().toVariantMap();
}

QString StudioViewModel::saveAccountCredential(const QString& platform, const QVariantMap& v) {
    // Una cámara de la red (camara:<ip>): usuario y clave, cifrados.
    if (platform.startsWith("camara:")) {
        if (!red_) return tr("El escáner de red no está disponible.");
        return red_->guardarCredenciales(platform.mid(7), v.value("usuario").toString(),
                                         v.value("clave").toString());
    }
    // Varias cámaras en un formulario: «usuario|<ip>» y «clave|<ip>». Las que
    // quedan sin clave se saltean (se pueden completar después).
    if (platform.startsWith("camaras:")) {
        if (!red_) return tr("El escáner de red no está disponible.");
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
        return guardadas == 0 ? tr("No escribiste ninguna clave.") : QString();
    }
    // Las claves de un plugin (plugin:<id>): un campo por cada una que declara.
    if (platform.startsWith("plugin:")) {
        if (!plugins_) return tr("Los plugins no están disponibles.");
        int guardadas = 0;
        for (auto it = v.begin(); it != v.end(); ++it) {
            const QString valor = it.value().toString();
            if (valor.isEmpty()) continue;
            const QString e = plugins_->guardarSecreto(platform.mid(7), it.key(), valor);
            if (!e.isEmpty()) return e;
            ++guardadas;
        }
        return guardadas == 0 ? tr("No escribiste ninguna clave.") : QString();
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
            if (!f.open(QIODevice::ReadOnly)) return tr("No se pudo leer %1.").arg(f.fileName());
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
    return tr("Plataforma desconocida: %1.").arg(platform);
}
QObject* StudioViewModel::inputs() const { return inputs_.get(); }
QObject* StudioViewModel::conference() const { return conference_.get(); }
QObject* StudioViewModel::evento() const { return evento_.get(); }
HistoricoViewModel* StudioViewModel::historico() const { return bitacoraEvento_ ? bitacoraEvento_->historico() : nullptr; }
QObject* StudioViewModel::historicoObj() const { return historico(); }
QObject* StudioViewModel::grafico() const { return grafico_.get(); }
QObject* StudioViewModel::rotulador() const { return rotulador_.get(); }
QObject* StudioViewModel::prompter() const { return prompter_.get(); }
QObject* StudioViewModel::pantallas() const { return pantallas_.get(); }
QObject* StudioViewModel::catalogoQml() const { return catalogo_.get(); }
QString StudioViewModel::identificarPantallas(const QString& cual) {
    if (cual.trimmed().isEmpty()) {
        emit identificarPedido({});
        return {};
    }
    const QString id = monitores_ ? monitores_->resolver(cual) : QString();
    if (id.isEmpty()) return tr("No hay un monitor «%1»: decí su número, su nombre o su id.").arg(cual);
    const QVariantMap m = monitores_->monitor(id);
    if (!m.value("conectado").toBool()) return tr("El monitor «%1» no está conectado.").arg(cual);
    emit identificarPedido({id});
    return {};
}

QObject* StudioViewModel::pantallasConectadasQml() const { return monitores_.get(); }
QObject* StudioViewModel::ventanasQml() const { return ventanas_.get(); }
QObject* StudioViewModel::qr() const { return qr_.get(); }
QObject* StudioViewModel::temaEvento() const { return temaEvento_.get(); }
QObject* StudioViewModel::lineaQml() const { return linea_.get(); }
QObject* StudioViewModel::enlacesQml() const { return enlaces_.get(); }

QObject* StudioViewModel::personas() const { return personas_.get(); }
QObject* StudioViewModel::atencion() const { return atencion_.get(); }
QObject* StudioViewModel::totems() const { return totems_.get(); }
QObject* StudioViewModel::publico() const { return publico_.get(); }
QObject* StudioViewModel::medios() const { return medios_.get(); }
QObject* StudioViewModel::carruseles() const { return carruseles_.get(); }
QObject* StudioViewModel::registro() const { return registro_.get(); }
QObject* StudioViewModel::extensiones() const { return extensiones_.get(); }
QObject* StudioViewModel::riel() const { return riel_.get(); }

void StudioViewModel::setSeccionConfig(const QString& pedida) {
    // Los nombres de antes de reordenar Configuración (2026-10-11), por si el
    // asistente o una guía los usan todavía.
    static const QHash<QString, QString> kAntes{{"Escena", "Video"},
                                                {"Gateway", "Red de la conferencia"},
                                                {"Red", "Red de la conferencia"},
                                                {"Sala", "Conferencia"},
                                                {"Red de la sala", "Red de la conferencia"},
                                                {"Acceso directo", "Cloudflare"},
                                                {"Acceso remoto", "Cloudflare"},
                                                {"cloudflare", "Cloudflare"},
                                                {"WireGuard", "Túnel propio (WireGuard)"},
                                                {"wireguard", "Túnel propio (WireGuard)"},
                                                {"Túnel propio", "Túnel propio (WireGuard)"},
                                                {"Sistema", "Sistema y datos"},
                                                {"Barra de navegación", "Apariencia"},
                                                {"Barra de navegacion", "Apariencia"},
                                                {"Audio y voz", "Voz del sistema"},
                                                {"Voz del operador", "Voz del sistema"},
                                                {"pinggy", "Pinggy"}};
    const QString s = kAntes.value(pedida, pedida);
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
        const nlohmann::json enCuerpo = asamblea_ ? asamblea_->cuerpo().cuerpo().buscar(nombre.toStdString()) : nlohmann::json::object();
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
    ctx["hora"] = QTime::currentTime().toString("HH:mm");
    // Campos vivos (T43): cambian cada segundo; en un vínculo «vivo» el gráfico se
    // actualiza sin volver a entrar.
    ctx["hora_s"] = QTime::currentTime().toString("HH:mm:ss");
    // El punto en tratamiento, el orador presencial (manda sobre el de la sala), el
    // quórum y la última votación oficial: los de la asamblea.
    if (asamblea_) asamblea_->completarVinculos(ctx);
    return ctx;
}

void StudioViewModel::toggleRecording() {
    if (!recording_) {
        // Antes de grabar: cuánto alcanza el disco a este bitrate.
        const nlohmann::json d = rt_.discoGrabacion();
        const double horas = d.value("horas", -1.0);
        if (horas >= 0 && horas < AppRuntime::kDiscoMinimoH) {
            emit pedirConfirmarGrabacion(horas);
            return;
        }
        if (horas >= 0 && horas < 2)
            emit notify(tr("Ojo: el disco alcanza para ~%1 h de grabación").arg(horas, 0, 'f', 1), "warn");
    }
    if (recording_) {
        rt_.stopRecording();
    } else if (rt_.startRecording()) {
        emit notify(tr("Grabando en %1").arg(qs(rt_.carpetaGrabacion())), "ok");
    } else {
        emit notify(tr("No se pudo grabar: %1").arg(qs(rt_.lastError())), "error");
    }
    onTick();
}

void StudioViewModel::grabarIgual() {
    if (recording_) return;
    if (rt_.startRecording())
        emit notify(tr("Grabando (con poco disco)"), "warn");
    else
        emit notify(tr("No se pudo grabar: %1").arg(qs(rt_.lastError())), "error");
    onTick();
}

QVariantMap StudioViewModel::loudnessUi() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(rt_.engine().loudness().json().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().toVariantMap();
}

void StudioViewModel::resetLoudnessUi() { rt_.engine().loudness().reiniciar(); }

QString StudioViewModel::objetivoLoudnessUi(const QVariantMap& o) {
    const auto j = nlohmann::json::parse(QJsonDocument(QJsonObject::fromVariantMap(o)).toJson().toStdString(), nullptr, false);
    return qs(objetivoLoudness_->poner(j));
}

QString StudioViewModel::retornoInvitadoUi(int slot) const { return qs(retornos_->modo(slot)); }

QString StudioViewModel::setRetornoInvitadoUi(int slot, const QString& modo) {
    return qs(retornos_->poner(slot, modo.toStdString()));
}

QVariantMap StudioViewModel::discoUi() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(rt_.discoGrabacion().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().toVariantMap();
}

QVariantMap StudioViewModel::remuxUi() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(rt_.engine().remuxEstado().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().toVariantMap();
}

void StudioViewModel::startStreaming(const QString& server, const QString& key) {
    if (rt_.salirAlAireManual(server.toStdString(), key.toStdString())) {
        // Nunca un "Al aire" verde si no cambió nada.
        if (rt_.engine().ultimoInicio() == BroadcastEngine::Inicio::YaIgual)
            emit notify(tr("Ya estabas transmitiendo a ese destino: no cambió nada"), "info");
        else
            emit notify(tr("Al aire"), "ok");
    } else if (rt_.engine().ultimoInicio() == BroadcastEngine::Inicio::YaOtroDestino) {
        emit pedirCambioDestino(qs(rt_.engine().destinoActivo()), server, key);
    } else {
        emit notify(tr("No se pudo transmitir: %1").arg(qs(rt_.lastError())), "error");
    }
    onTick();
}

void StudioViewModel::cambiarDestinoStream(const QString& server, const QString& key) {
    if (rt_.salirAlAireManual(server.toStdString(), key.toStdString(), true))
        emit notify(tr("Transmitiendo al destino nuevo"), "ok");
    else
        emit notify(tr("No se pudo cambiar de destino: %1").arg(qs(rt_.lastError())), "error");
    onTick();
}

QVariantList StudioViewModel::destinosUi() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(rt_.destinos().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).array().toVariantList();
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
    if (const std::string e = conmutadorActivo().poner(name.toStdString()); !e.empty()) emit notify(qs(e), "error");
}

QString StudioViewModel::escenaArmada() const { return qs(conmutadorActivo().armada()); }

void StudioViewModel::publicarTally() {
    auto* p = qobject_cast<PantallasViewModel*>(pantallas());
    if (p == nullptr) return;
    // Las pantallas de tally de la sala: las de la por omisión (no la activa de esta interfaz).
    const QString aire = qs(rt_.engine().currentScene());
    const QString armada = qs(conmutador_->armada());
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
    if (const std::string e = conmutadorActivo().armar(name.toStdString()); !e.empty()) emit notify(qs(e), "aviso");
}

void StudioViewModel::take() { takeCon(QString(), -1); }

void StudioViewModel::takeCon(const QString& tipo, int ms) {
    escenas::Conmutador& c = conmutadorActivo();
    const bool habiaArmada = !c.armada().empty();
    if (const std::string e = c.take(tipo.toStdString(), ms); !e.empty())
        emit notify(qs(e), habiaArmada ? "error" : "aviso");
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
        destino = tr("%1 (copia)").arg(from);
        for (int i = 2; existe(destino); ++i)
            destino = tr("%1 (copia %2)").arg(from).arg(i);
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
    emit notify(aviso.empty() ? tr("Escena copiada: %1").arg(destino) : tr("Escena copiada, pero %1").arg(qs(aviso)),
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
    emit notify(protegida ? tr("«%1» protegida: no se puede borrar ni renombrar").arg(name)
                          : tr("«%1» ya no está protegida").arg(name),
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
    if (!intakeSink_) return tr("El servidor MCP no está disponible.");
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
        emit notify(tr("Guardado"), "ok");
    } else {
        emit notify(tr("No se pudo guardar: %1").arg(qs(rt_.lastError())), "error");
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

void StudioViewModel::refrescarAvisos() {
    const unsigned b = avisosPendientes_.exchange(0);
    const auto hubo = [b](AppRuntime::Cambio c) { return (b & (1u << static_cast<unsigned>(c))) != 0; };
    using C = AppRuntime::Cambio;
    if (hubo(C::Ajustes)) {
        // Se aplicó la configuración (la GUI o settings_set del MCP): lo que rige ya.
        emit canvasChanged();
        emit themeChanged();
        emit sceneChanged();
        frames_->setGuests(rt_.config().guests);
        if (rt_.config().temaWeb != temaWebMandado_) {
            mandarTemaWeb();
            emit temaWebChanged();
        }
        camaras_->sincronizar(); // Mejorar imagen general: rige ya
        video_->setLegalizar(rt_.config().legalizar);
    }
    // Las listas se recargan SOLO si quedaron distintas: si el cambio lo hizo la
    // vista, ya las recargó ella, y otra recarga le borraría la selección.
    if (hubo(C::Escenas)) {
        const std::vector<std::string> names = rt_.orderedSceneNames();
        bool distinta = scenes_->rowCount() != static_cast<int>(names.size());
        for (int i = 0; !distinta && i < scenes_->rowCount(); ++i)
            distinta = scenes_->data(scenes_->index(i), ScenesModel::NameRole).toString() !=
                       qs(names[static_cast<std::size_t>(i)]);
        if (distinta)
            scenes_->reload();
        else if (scenes_->rowCount() > 0) // carpeta o protección: las filas las leen en vivo
            emit scenes_->dataChanged(scenes_->index(0), scenes_->index(scenes_->rowCount() - 1),
                                      {ScenesModel::FolderRole, ScenesModel::ProtegidaRole});
        emit escenasCambiaron(); // el árbol por carpetas se rearma (barato)
        // Si la escena que mostraba el editor ya no existe, sigue a la del aire.
        if (std::find(names.begin(), names.end(), sources_->scene().toStdString()) == names.end())
            sources_->setScene(currentScene());
    }
    if (hubo(C::Escenas) || hubo(C::Fuentes)) {
        // El editor muestra de arriba hacia abajo: la última del motor es la fila 0.
        const std::vector<std::string> fuentes = rt_.engine().sceneSources(sources_->scene().toStdString());
        bool distinta = sources_->rowCount() != static_cast<int>(fuentes.size());
        for (int i = 0; !distinta && i < sources_->rowCount(); ++i)
            distinta = sources_->data(sources_->index(i), SourcesModel::NameRole).toString() !=
                       qs(fuentes[fuentes.size() - 1 - static_cast<std::size_t>(i)]);
        if (distinta) sources_->reload();
    }
    // Una capa o un volumen: las filas leen el estado en vivo, alcanza con decir
    // que cambiaron (sin recargar: no se corta un arrastre a medias).
    if (hubo(C::Capa) && sources_->rowCount() > 0)
        emit sources_->dataChanged(sources_->index(0), sources_->index(sources_->rowCount() - 1));
    if (hubo(C::Audio) && mixer_->rowCount() > 0)
        emit mixer_->dataChanged(mixer_->index(0), mixer_->index(mixer_->rowCount() - 1),
                                 {MixerModel::VolumeDbRole, MixerModel::MutedRole});
    // Una entrada de audio nueva (audio_input_add del MCP, un plugin): la lista de
    // fuentes de la página Audio la muestra sin tener que volver a entrar.
    if ((hubo(C::Audio) || hubo(C::Fuentes)) && audio_) audio_->revisarFuentes();
    if (hubo(C::Escenas) || hubo(C::Aire)) {
        scenes_->liveChanged();
        emit sceneChanged();
    }
    if (hubo(C::Salidas)) {
        emit statusChanged();
        emit destinosChanged();
    }
    if (hubo(C::Salidas) || hubo(C::Aire)) publicarSalidas();
}

QObject* StudioViewModel::salidas() const { return salidasModelo_.get(); }

void StudioViewModel::publicarSalidas() {
    const nlohmann::json todo = rt_.salidas();
    const nlohmann::json lista = todo.value("salidas", nlohmann::json::array());
    const nlohmann::json destinos = rt_.destinos();
    const EngineStatus st = rt_.engine().status();
    QJsonArray filas, alGateway;
    for (const auto& s : lista) {
        const std::string id = s.value("id", std::string());
        if (!rt_.engine().existeSalida(id)) continue;
        const escenas::Conmutador* c = conmutadores_->de(id);
        QJsonArray suyos; // los destinos que salen con el programa de esta
        for (const auto& d : destinos)
            if (salidas::salidaDeDestino(d) == id)
                suyos.append(QJsonObject{{"id", qs(d.value("id", std::string()))},
                                         {"nombre", qs(d.value("nombre", std::string()))},
                                         {"activo", d.value("activo", false)},
                                         {"reconectando", d.value("reconectando", false)},
                                         {"respaldo", d.value("respaldo", false)}});
        QJsonObject f{{"id", qs(id)},
                      {"nombre", qs(s.value("nombre", id))},
                      {"color", qs(s.value("color", std::string()))},
                      {"porOmision", s.value("por_omision", false)},
                      {"alAire", qs(rt_.engine().currentScene(id))},
                      {"armada", c != nullptr ? qs(c->armada()) : QString()},
                      {"archivo", qs(rt_.engine().archivoGrabacion(id))},
                      {"destinos", suyos},
                      // La clave de FrameService de su programa (la por omisión, la de siempre).
                      {"programa", s.value("por_omision", false) ? QStringLiteral("program") : "program:" + qs(id)},
                      {"codificador", qs(s.value("codificador", std::string()))}};
        for (const auto& e : st.salidas)
            if (e.id == id) {
                f["transmitiendo"] = e.streaming || e.destinos > 0;
                f["grabando"] = e.recording;
            }
        alGateway.append(QJsonObject{{"id", f["id"]}, {"nombre", f["nombre"]}, {"color", f["color"]}, {"porOmision", f["porOmision"]}});
        filas.append(f);
    }
    const int tope = todo.value("tope", 0);
    const bool cambio = filas != salidasModelo_->items() || tope != topeSalidas_;
    topeSalidas_ = tope;
    if (filas != salidasModelo_->items()) salidasModelo_->setItems(filas);
    frames_->setSalidas(alGateway); // las claves «program:<id>», sus nombres y sus colores
    // La activa se borró: la interfaz vuelve a la por omisión.
    if (!salidaActiva_.empty() && !rt_.engine().existeSalida(salidaActiva_)) {
        salidaActiva_.clear();
        reflejarSalidaActiva();
    } else if (cambio) {
        emit salidaActivaChanged();
    }
    if (const QVariantMap n = nuevaSalida(); n != nuevaSalidaAntes_) {
        nuevaSalidaAntes_ = n;
        emit nuevaSalidaChanged();
    }
    // El gateway sabe qué salidas hay para que la sesión remota y el cast elijan (se la
    // vuelve a mandar sola si el gateway se reinicia).
    if (conference_) conference_->client().ponerSalidas(alGateway);
    if (controlWeb_) controlWeb_->salidasCambiaron(); // sus aparatos ven la lista nueva
}

escenas::Conmutador& StudioViewModel::conmutadorActivo() const {
    escenas::Conmutador* c = salidaActiva_.empty() ? nullptr : conmutadores_->de(salidaActiva_);
    return c != nullptr ? *c : *conmutador_;
}

QString StudioViewModel::salidaActiva() const {
    return qs(salidaActiva_.empty() ? std::string(BroadcastEngine::kSalidaPorOmision) : salidaActiva_);
}

void StudioViewModel::setSalidaActiva(const QString& id) {
    // La por omisión se anota "" (todo lo que pregunta «¿es otra?» es un empty()).
    std::string nueva = rt_.engine().idDeSalida(id.toStdString());
    if (nueva.empty()) return; // no existe
    if (nueva == BroadcastEngine::kSalidaPorOmision) nueva.clear();
    if (nueva == salidaActiva_) return;
    salidaActiva_ = nueva;
    peakActivaL_ = peakActivaR_ = -100;
    reflejarSalidaActiva();
}

void StudioViewModel::reflejarSalidaActiva() {
    scenes_->setSalida(salidaActiva_);
    scenes_->setArmada(escenaArmada());
    sources_->setScene(currentScene());
    mixer_->setSalida(salidaActiva_);
    if (mezclador_) mezclador_->setSalida(salidaActiva_);
    if (dsk_) dsk_->setSalida(salidaActiva_);
    // Vuelve con ella al arrancar (Config/interfaz.json: es de esta interfaz, no del aire).
    interfaz_->guardar(kClaveSalidaActiva, salidaActiva_);
    emit salidaActivaChanged();
    emit placaChanged();
    emit sceneChanged();
    emit afvChanged();
    emit statusChanged();
    emit nivelesChanged();
}

QVariantMap StudioViewModel::salidaActivaDatos() const {
    const QString id = salidaActiva();
    for (const auto& v : salidasModelo_->items())
        if (v.toObject().value("id").toString() == id) return v.toObject().toVariantMap();
    return {};
}

bool StudioViewModel::variasSalidas() const { return salidasModelo_->count() > 1; }

QString StudioViewModel::claveProgramaActiva() const {
    return salidaActiva_.empty() ? QStringLiteral("program") : "program:" + qs(salidaActiva_);
}

QString StudioViewModel::claveArmadaActiva() const {
    return salidaActiva_.empty() ? QStringLiteral("armada") : "armada:" + qs(salidaActiva_);
}

QVariantMap StudioViewModel::nuevaSalida() const {
    // El tope lo pone el operador; que la máquina dé abasto lo dicen los recursos (la
    // tarjeta Sistema, sistema_recursos): justa, se puede igual pero se avisa.
    if (salidasModelo_->count() >= topeSalidas_)
        return {{"puede", false}, {"aviso", tr("Ya hay %n salida, que es el tope.", "", topeSalidas_)}};
    const QString estado = recursos_ ? recursos_->estado() : QString();
    if (estado == "critico")
        return {{"puede", true},
                {"aviso", tr("La máquina está al límite (%1): otra salida es otro programa, otro preview y otra mezcla.")
                              .arg(recursos_->limita())}};
    if (estado == "aviso")
        return {{"puede", true}, {"aviso", tr("La máquina está justa (%1): mirá la tarjeta Sistema antes de sumar otra.").arg(recursos_->limita())}};
    return {{"puede", true}, {"aviso", QString()}};
}

QString StudioViewModel::crearSalidaUi(const QString& nombre, const QString& color) {
    // El id sale del nombre (letras y números, sin acentos: va en los nombres de los
    // canales y los archivos) y no se repite; sin nada útil, «salida-<n>».
    QString base;
    for (const QChar ch : nombre.normalized(QString::NormalizationForm_D).toLower()) {
        if (ch.isLetterOrNumber() && ch.unicode() < 128) base += ch;
        else if ((ch.isSpace() || ch == '-' || ch == '_') && !base.isEmpty() && !base.endsWith('-')) base += '-';
    }
    while (base.endsWith('-')) base.chop(1);
    if (base.isEmpty()) base = QStringLiteral("salida");
    QString id = base;
    for (int n = 2; rt_.engine().existeSalida(id.toStdString()); ++n) id = QString("%1-%2").arg(base).arg(n);
    const std::string e = rt_.crearSalida(id.toStdString(), nombre.trimmed().toStdString(), color.toStdString());
    if (!e.empty()) return qs(e);
    publicarSalidas();
    setSalidaActiva(id);
    return {};
}

QString StudioViewModel::setTopeSalidasUi(int tope) {
    const std::string e = rt_.setTopeSalidas(tope);
    publicarSalidas();
    return qs(e);
}

QString StudioViewModel::grabarSalida(const QString& id, bool grabar) {
    const std::string s = rt_.engine().idDeSalida(id.toStdString());
    if (s.empty()) return tr("No hay una salida «%1».").arg(id);
    if (!grabar) {
        rt_.stopRecording(s);
        return {};
    }
    return rt_.startRecording(s) ? QString() : qs(rt_.lastError());
}

QString StudioViewModel::crearSalida(const QString& id, const QString& nombre, const QString& color) {
    return qs(rt_.crearSalida(id.toStdString(), nombre.toStdString(), color.toStdString()));
}

QString StudioViewModel::editarSalida(const QString& id, const QString& nombre, const QString& color) {
    nlohmann::json cambios = nlohmann::json::object();
    if (!nombre.isEmpty()) cambios["nombre"] = nombre.toStdString();
    if (!color.isEmpty()) cambios["color"] = color.toStdString();
    return qs(rt_.editarSalida(id.toStdString(), cambios));
}

QString StudioViewModel::borrarSalida(const QString& id, const QString& susDestinos) {
    return qs(rt_.borrarSalida(id.toStdString(), susDestinos.toStdString()));
}

QString StudioViewModel::setCodificadorSalida(const QString& id, const QString& codificador) {
    return qs(rt_.editarSalida(id.toStdString(), {{"codificador", codificador.toStdString()}}));
}

void StudioViewModel::setScenePreview(const QString& name) {
    // 960 px de ancho alcanza para el lienzo del editor y no castiga la GPU.
    rt_.engine().enableScenePreview(name.toStdString(), 960);
}

QVariantMap StudioViewModel::latidoUi() const {
    // Las trabas largas (LatidoAire, desde otro hilo) y las demoras del bucle (VigiaBucle).
    nlohmann::json j = rt_.latido().estado();
    j["bucle_ui"] = ba::diag::VigiaBucle::interfaz().informe();
    QJsonObject o = QJsonDocument::fromJson(QByteArray::fromStdString(j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object();
    o["frames_poll"] = frames_->medidas(); // lo que tarda FrameService::poll en este hilo (F3-11)
    return o.toVariantMap();
}

QString StudioViewModel::latidoPoliticaUi(const QVariantMap& cambios) {
    const auto j = nlohmann::json::parse(QJsonDocument(QJsonObject::fromVariantMap(cambios)).toJson().toStdString(),
                                         nullptr, false);
    return QString::fromStdString(rt_.latido().ponerPolitica(j));
}

QObject* StudioViewModel::alarmasQml() const {
    QQmlEngine::setObjectOwnership(alarmas_.get(), QQmlEngine::CppOwnership);
    return alarmas_.get();
}

agent::AlertEngine& StudioViewModel::alerts() { return alarmas_->motor(); }

bool StudioViewModel::focoEnTexto() const {
    QObject* f = QGuiApplication::focusObject();
    return f != nullptr && (f->inherits("QQuickTextInput") || f->inherits("QQuickTextEdit"));
}

bool StudioViewModel::corteDirectoEditor() const { return ajustesEscenas_->corteDirectoEditor(); }

void StudioViewModel::setCorteDirectoEditor(bool si) {
    if (ajustesEscenas_->setCorteDirectoEditor(si)) emit escenasAjustesChanged();
}

QString StudioViewModel::ftbUi(bool activar, int ms, const QString& audio) {
    // placaChanged llega por alCambiar
    return qs(seguridadAire_->ftb(activar, ms, audio.toStdString(), salidaActiva_));
}

QString StudioViewModel::placaUi(bool activar, const QString& texto, const QString& imagen, int ms) {
    return qs(seguridadAire_->placa(activar, texto.toStdString(), imagen.toStdString(), ms, salidaActiva_));
}

QVariantMap StudioViewModel::placaActiva() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(seguridadAire_->estado(salidaActiva_).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().toVariantMap();
}

QVariantMap StudioViewModel::aireActiva() const {
    const bool alAireActiva = rt_.engine().haySalidaEn(salidaActiva_);
    if (salidaActiva_.empty())
        return {{"transmitiendo", streaming_}, {"grabando", recording_}, {"reconectando", reconnecting_},
                {"alAire", alAireActiva}, {"tiempoTransmision", streamTime()}, {"tiempoGrabacion", recordTime()}};
    const auto it = aireSalidas_.find(salidaActiva_);
    const AireDeSalida a = it != aireSalidas_.end() ? it->second : AireDeSalida{};
    return {{"transmitiendo", a.transmite},
            {"grabando", a.graba},
            {"reconectando", a.reconecta},
            {"alAire", alAireActiva},
            {"tiempoTransmision", a.transmite ? hms(a.transmiteDesde) : QStringLiteral("00:00:00")},
            {"tiempoGrabacion", a.graba ? hms(a.grabaDesde) : QStringLiteral("00:00:00")}};
}

QStringList StudioViewModel::textosPlaca() const {
    // Salen al aire: en el idioma del evento (se arman cada vez que se piden, así que un
    // cambio de idioma se ve en la lista siguiente; una placa ya al aire queda como salió).
    const auto ev = [](const std::string& s) { return QString::fromStdString(s); };
    QStringList l{ev(ba::i18n::traducirEvento("StudioViewModel", "Volvemos enseguida")),
                  ev(ba::i18n::traducirEvento("StudioViewModel", "Cuarto intermedio")),
                  ev(ba::i18n::traducirEvento("StudioViewModel", "Minuto de silencio")),
                  ev(ba::i18n::traducirEvento("StudioViewModel", "Sesión suspendida")),
                  ev(ba::i18n::traducirEvento("StudioViewModel", "Dificultades técnicas"))};
    const QString usado = QString::fromStdString(rt_.engine().estadoPlaca().value("placa_texto", std::string()));
    if (!usado.isEmpty() && !l.contains(usado)) l.prepend(usado);
    return l;
}

QObject* StudioViewModel::cuerpoQml() const { return asamblea_->cuerpoQml(); }

QString StudioViewModel::aprobarInfoInvitado(const QString& participante, const QString& texto) {
    const QJsonObject sala = conference_ != nullptr ? conference_->client().snapshot() : QJsonObject{};
    for (const QJsonValue& v : sala.value("participants").toArray()) {
        const QJsonObject p = v.toObject();
        if (p.value("id").toString() == participante || p.value("name").toString() == participante) {
            infoAprobada_[p.value("id").toString()] = texto.isEmpty() ? p.value("info").toString() : texto;
            return {};
        }
    }
    return tr("no hay un invitado «%1» en la conferencia").arg(participante);
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
    QPointer<StudioViewModel> yo(this);
    return qs(preguntas_->sumarDesdeChat(key.toStdString(), [yo](const std::string& err, const nlohmann::json&) {
        if (yo)
            emit yo->notify(err.empty() ? tr("Sumada a la cola de preguntas")
                                        : tr("No se pudo sumar la pregunta: %1").arg(QString::fromStdString(err)),
                            err.empty() ? "ok" : "error");
    }));
}

QObject* StudioViewModel::bancasQml() const { return asamblea_->bancasQml(); }
QObject* StudioViewModel::debateQml() const { return asamblea_->debateQml(); }
QObject* StudioViewModel::configuracionesQml() const { return configuraciones_.get(); }
QObject* StudioViewModel::dcnQml() const { return asamblea_->dcnQml(); }
QObject* StudioViewModel::sesionRemotaQml() const { return asamblea_->sesionRemotaQml(); }
QObject* StudioViewModel::votacionesQml() const { return asamblea_->votacionesQml(); }
QObject* StudioViewModel::asambleaQml() const {
    QQmlEngine::setObjectOwnership(asamblea_.get(), QQmlEngine::CppOwnership);
    return asamblea_.get();
}

QVariantMap StudioViewModel::respaldoEntrada(const QString& entrada) const {
    const nlohmann::json r = respaldo_->respaldo(entrada.toStdString());
    return QJsonDocument::fromJson(QByteArray::fromStdString(r.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().toVariantMap();
}

QString StudioViewModel::setRespaldoEntrada(const QString& entrada, const QVariantMap& r) {
    return qs(respaldo_->setRespaldo(entrada.toStdString(), r.value("escena").toString().toStdString(),
                                     r.value("al_perder_aire", "avisar").toString().toStdString(),
                                     r.value("ms", 1000).toInt()));
}

void StudioViewModel::revisarSenales() {
    if (respaldo_) respaldo_->revisar();
}

QStringList StudioViewModel::sinSenal() const { return respaldo_ ? respaldo_->sinSenal() : QStringList(); }

QObject* StudioViewModel::ccQml() const { return cc_.get(); }

} // namespace ba::app

namespace ba::app {

namespace {
QVariant vocabQv(const nlohmann::json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(nlohmann::json{{"v", j}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
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
    const int n = vocabulario_->cargarCuerpo(asamblea_->cuerpo().cuerpo());
    emit notify(tr("Nombres del cuerpo: %1").arg(n), "info");
    return n;
}

} // namespace ba::app

namespace ba::app {

QObject* StudioViewModel::busQml() const { return bus_.get(); }

void StudioViewModel::publicarEventos() {
    if (!bus_) return;
    // Volviendo de una caída: cuánto tardó el aire en volver.
    if (!caida_.isEmpty() && !caida_.contains("aire_vuelta_s") && (streaming_ || recording_) && caidaMs_ > 0) {
        caida_["aire_vuelta_s"] = (QDateTime::currentMSecsSinceEpoch() - caidaMs_) / 1000.0;
        emit caidaChanged();
        // Volvió el aire: el aviso dura 30 s más y se cierra solo (en el modelo, no en la
        // vista: el MCP deja de informarla aunque la ventana no la muestre).
        const qint64 marca = caidaMs_;
        QTimer::singleShot(30000, this, [this, marca]() {
            if (caidaMs_ == marca && caida_.contains("aire_vuelta_s")) cerrarCartelCaida();
        });
    }
    // T47: al arrancar una sesión en vivo (el guion corre), la sala cambia de PIN
    // si cambió el día (el PIN es uno por día: el mismo día, el mismo PIN).
    {
        const bool ev = evento_ != nullptr && evento_->property("enVivo").toBool();
        if (ev && !eventoEnVivo_ && seguridad_)
            seguridad_->rotarPinSiCambioElDia([this](const std::string& error, bool cambio, const std::string& pin) {
                if (error.empty() && cambio) emit notify(tr("Día nuevo: la conferencia tiene PIN nuevo (%1)").arg(qs(pin)), "info");
            });
        eventoEnVivo_ = ev;
    }
    // Al terminar el vivo, lo desbloqueado vuelve a quedar protegido para la próxima.
    if (const bool vivo = modoVivo(); vivo != estabaEnVivo_) {
        estabaEnVivo_ = vivo;
        if (!vivo) desbloqueadas_.clear();
        bus_->publicar("aire/modo_vivo", {{"activo", vivo}});
    }
    ++busTicks_;
    // Lo del BusEventos es de la por omisión (los temas por salida son de la etapa 4).
    const QString aire = qs(rt_.engine().currentScene());
    const QString armada = qs(conmutador_->armada());
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
    {
        // Para el multiview: el tally de la salida activa (rojo al aire, verde armada) y, con
        // varias salidas, en cuáles está al aire cada fuente (las demás con su color).
        const auto alAireEn = [this](const std::string& salida) {
            QStringList out;
            const std::string escena = rt_.engine().currentScene(salida);
            if (!escena.empty())
                for (const std::string& f : rt_.engine().sceneSources(escena)) out << qs(f);
            for (const std::string& f : rt_.engine().fuentesDsk(salida))
                if (!out.contains(qs(f))) out << qs(f);
            return out;
        };
        const QStringList aireQ = alAireEn(salidaActiva_);
        QStringList armadaQ;
        for (const auto& f : fuentesDe(escenaArmada())) armadaQ << QString::fromStdString(f.get<std::string>());
        QVariantMap borde, enSalidas;
        for (const QString& f : armadaQ) borde[f] = QStringLiteral("#2ecc71");
        for (const QString& f : aireQ) borde[f] = QStringLiteral("#e53935");
        if (salidasModelo_->count() > 1)
            for (const auto& v : salidasModelo_->items()) {
                const QJsonObject o = v.toObject();
                const QVariantMap marca{{"nombre", o.value("nombre").toString()}, {"color", o.value("color").toString()}};
                for (const QString& f : alAireEn(o.value("id").toString().toStdString())) {
                    enSalidas[f] = enSalidas.value(f).toList() << marca;
                    if (!borde.contains(f)) borde[f] = marca.value("color");
                }
            }
        const QVariantMap t{{"aire", aireQ}, {"armada", armadaQ}, {"borde", borde}, {"salidas", enSalidas}};
        if (t != tally_) {
            tally_ = t;
            emit tallyChanged();
        }
    }
    // Lo que se reproduce al aire en una salida (con las fuentes que muestra) y cuánto le
    // queda, al segundo: [{fuente, restante_s, duracion_s}].
    const std::vector<BroadcastEngine::MediaInfo> sonando = rt_.engine().mediaSources();
    const auto mediosEn = [&sonando](const nlohmann::json& fuentes) {
        nlohmann::json out = nlohmann::json::array();
        for (const auto& m : sonando) {
            if (m.state != "playing" || m.durationMs <= 0) continue;
            if (std::find(fuentes.begin(), fuentes.end(), m.name) == fuentes.end()) continue;
            out.push_back({{"fuente", m.name},
                           {"restante_s", static_cast<int>((m.durationMs - m.timeMs + 999) / 1000)},
                           {"duracion_s", static_cast<int>(m.durationMs / 1000)}});
        }
        return out;
    };
    {
        const nlohmann::json mediosJ = mediosEn(fuentesAire);
        QVariantList medios;
        for (const auto& m : mediosJ)
            medios.append(QVariantMap{{"fuente", qs(m.value("fuente", std::string()))},
                                      {"restante_s", m.value("restante_s", 0)},
                                      {"duracion_s", m.value("duracion_s", 0)}});
        if (medios != mediosAlAire_) {
            mediosAlAire_ = medios;
            emit mediosChanged();
            bus_->publicar("aire/medios", mediosJ);
        }
    }
    bus_->publicar("aire/tbar", {{"hacia", rt_.engine().manualHacia()}, {"posicion", conmutador_->tbarPosicion()}});
    bus_->publicar("aire/tally", {{"programa", aire.toStdString()},
                                  {"preview", armada.toStdString()},
                                  {"fuentes_programa", fuentesAire},
                                  {"fuentes_preview", fuentesDe(armada)},
                                  {"transmitiendo", streaming_},
                                  {"grabando", recording_}});
    {
        // Lo mismo de cada salida (aire/<id>/…, salida/<id>/…): las superficies que
        // manejan otra salida leen los suyos (PLAN-SALIDAS, etapa 4).
        nlohmann::json ss = nlohmann::json::array();
        for (const QJsonValue& v : salidasModelo_->items()) {
            const QJsonObject f = v.toObject();
            const std::string id = f.value("id").toString().toStdString();
            const std::string esc = f.value("alAire").toString().toStdString();
            const QString arm = f.value("armada").toString();
            nlohmann::json fa = fuentesDe(qs(esc));
            for (const std::string& d : rt_.engine().fuentesDsk(id))
                if (std::find(fa.begin(), fa.end(), d) == fa.end()) fa.push_back(d);
            const escenas::Conmutador* c = conmutadores_->de(id);
            const bool porOmision = f.value("porOmision").toBool();
            // El fader y el mudo de cada fuente en su mezcla (los de la por omisión ya van
            // en audio/nivel/<fuente>).
            nlohmann::json niveles = nlohmann::json::object();
            if (!porOmision)
                for (int i = 0; i < mixer_->rowCount(); ++i)
                    niveles[mixer_->data(mixer_->index(i), MixerModel::NameRole).toString().toStdString()] = mixer_->nivelEn(i, id);
            ss.push_back({{"id", id},
                          {"medios", mediosEn(fa)},
                          {"dsk", dsk_ ? dsk_->capas(id) : nlohmann::json::array()},
                          {"tbar", {{"hacia", rt_.engine().manualHacia(id)}, {"posicion", c != nullptr ? c->tbarPosicion() : 0.0}}},
                          {"niveles", niveles},
                          {"nombre", f.value("nombre").toString().toStdString()},
                          {"color", f.value("color").toString().toStdString()},
                          {"por_omision", porOmision},
                          {"escena", esc},
                          {"armada", arm.toStdString()},
                          {"fuentes_programa", fa},
                          {"fuentes_preview", fuentesDe(arm)},
                          {"transmitiendo", f.value("transmitiendo").toBool()},
                          {"grabando", f.value("grabando").toBool()}});
        }
        bus_->publicarSalidas(ss);
    }
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
    // Niveles: solo si se movieron 1 dB o más (o cambió el volumen o el mudo). El fader y
    // el mudo de la por omisión (el mezclador muestra el de la activa: no es lo mismo).
    for (int i = 0; i < mixer_->rowCount(); ++i)
        bus_->publicar("audio/nivel/" + mixer_->data(mixer_->index(i), MixerModel::NameRole).toString().toStdString(),
                       mixer_->nivelEn(i, {}));
    if (segundo) {
        muestrearSalud();
        bus_->publicar("audio/loudness", rt_.engine().loudness().json());
        nlohmann::json sin = nlohmann::json::array();
        for (const QString& e : sinSenal()) sin.push_back(e.toStdString());
        bus_->publicar("entrada/senal", {{"sin_senal", sin}});
        if (asamblea_) bus_->publicar("oradores/al_aire", asamblea_->bancas().oradorParaZocalo());
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
QObject* StudioViewModel::recursosQml() const {
    if (recursos_) QQmlEngine::setObjectOwnership(recursos_.get(), QQmlEngine::CppOwnership);
    return recursos_.get();
}
QObject* StudioViewModel::controlWebQml() const { return controlWeb_.get(); }

QString StudioViewModel::temaWeb() const { return qs(rt_.config().temaWeb); }

QVariantList StudioViewModel::temasWeb() const {
    QVariantList out;
    for (const auto& t : control::temasWeb())
        out << QVariantMap{{"id", t.id},       {"nombre", QCoreApplication::translate("control-web", t.nombre)},
                           {"descripcion", QCoreApplication::translate("control-web", t.descripcion)},
                           {"fondo", t.fondo}, {"superficie", t.superficie}, {"texto", t.texto},
                           {"acento", t.acento}};
    return out;
}

void StudioViewModel::setTemaWeb(const QString& id) {
    const std::string t = id.toStdString();
    if (!control::temaWebValido(t) || t == rt_.config().temaWeb) return;
    rt_.config().temaWeb = t;
    rt_.save();
    rt_.ajustesAplicados(); // Configuración se entera (y acá, refrescarAvisos lo manda)
}

void StudioViewModel::mandarTemaWeb() {
    const std::string t = rt_.config().temaWeb;
    temaWebMandado_ = t;
    auto& cli = static_cast<ConferenceViewModel*>(conference_.get())->client();
    QPointer<StudioViewModel> yo(this);
    sala::pedir(cli, "POST", "/api/v1/tema", {{"tema", t}}, [yo](const std::string& err, const nlohmann::json&) {
        if (yo && !err.empty()) yo->temaWebMandado_.clear(); // se reintenta cuando la sala vuelva
    });
}

void StudioViewModel::emparejarControlWebUi() {
    QPointer<StudioViewModel> yo(this);
    controlWebGw_->emparejar(nullptr, "", [yo](const nlohmann::json& r) {
        if (!yo) return;
        if (!r.contains("url")) {
            emit yo->notify(QString::fromStdString(r.value("error", tr("No se pudo emparejar.").toStdString())), "error");
            return;
        }
        const std::string aviso = r.value("aviso", std::string());
        emit yo->mostrarQr(QString::fromStdString(r.value("url", std::string())), tr("Control web (/control/)"),
                           tr("Abrilo en el navegador del aparato (de la red local; el código vale unos "
                              "minutos y sirve una vez). Después aprobalo acá, en Servicios → Control web.") +
                               (aviso.empty() ? QString() : "\n" + QString::fromStdString(aviso)),
                           "verde");
    });
}
QObject* StudioViewModel::satelliteQml() const { return satellite_.get(); }
QObject* StudioViewModel::carasBotoneraQml() const { return carasBotonera_.get(); }
QObject* StudioViewModel::dskQml() const { return dsk_.get(); }
QObject* StudioViewModel::grillasQml() const { return grillasPersonas_.get(); }
QObject* StudioViewModel::accionesQml() const { return acciones_.get(); }
double StudioViewModel::picoDe(const QString& fuente) const { return mixer_ ? mixer_->picoDe(fuente.toStdString()) : -100.0; }
QObject* StudioViewModel::notificadorQml() const { return notificador_.get(); }
QObject* StudioViewModel::accesoRemotoQml() const { return accesoRemoto_.get(); }
QObject* StudioViewModel::seguirMicrofonoQml() const { return asamblea_->seguirMicrofonoQml(); }
QObject* StudioViewModel::ordenDelDiaQml() const { return asamblea_->ordenDelDiaQml(); }
QObject* StudioViewModel::informesQml() const { return asamblea_->informesQml(); }
QObject* StudioViewModel::quorumQml() const { return asamblea_->quorumQml(); }

QString StudioViewModel::capitulosPuntosUi() const {
    return qs(asamblea_->ordenDelDia().capitulosTexto(recordStartMs()));
}

QStringList StudioViewModel::nombresParaDsk() const {
    QStringList out;
    for (const std::string& n : rt_.engine().sceneNames()) out << qs(n);
    for (const std::string& n : rt_.engine().sourceNames())
        if (!out.contains(qs(n))) out << qs(n);
    return out;
}

void StudioViewModel::emparejarTabletUi(const QString& perfil, const QString& nombre, int venceDias) {
    QPointer<StudioViewModel> yo(this);
    botoneraGw_->emparejar(perfil.toStdString(), nombre.toStdString(), venceDias, [yo](const nlohmann::json& r) {
        if (!yo) return;
        const QJsonDocument d = QJsonDocument::fromJson(
            QByteArray::fromStdString(r.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)));
        emit yo->emparejamientoListo(d.object().toVariantMap());
    });
}

void StudioViewModel::aprobarTabletUi(const QString& solicitud, bool aprobar, const QString& perfil) {
    const std::string e = botoneraGw_->resolver(solicitud.toStdString(), aprobar, perfil.toStdString());
    if (!e.empty()) {
        emit notify(qs(e), "error");
        return;
    }
    emit notify(aprobar ? tr("Tablet emparejada.") : tr("Emparejamiento rechazado."), aprobar ? "ok" : "info");
}

} // namespace ba::app

namespace ba::app {

QVariantMap StudioViewModel::apurosDiscoUi() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(rt_.engine().discoEstado().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().toVariantMap();
}

void StudioViewModel::priorizarProgramaUi(bool si) { rt_.engine().priorizarPrograma(si); }

QString StudioViewModel::modoDiscoUi(const QString& modo) { return qs(rt_.engine().setModoDisco(modo.toStdString())); }

QVariantList StudioViewModel::isosUi() const {
    QVariantList out;
    // El tamaño, de lo que contó el motor: preguntarle al disco desde la
    // interfaz (cada segundo) la trababa con el disco trabado.
    for (const auto& g : rt_.engine().sourceRecordings())
        out.append(QVariantMap{{"fuente", qs(g.source)},
                               {"archivo", qs(g.path)},
                               {"mb", static_cast<double>(g.bytes) / 1e6},
                               {"audioPropio", g.audioPropio},
                               {"activa", g.activa},
                               {"pausada", g.pausada}});
    return out;
}

void StudioViewModel::isoInvitadoUi(const QString& id) {
    const QString f = fuenteDeInvitado(id);
    if (f.isEmpty()) return emit notify(tr("Ese invitado no tiene una fuente en el programa todavía."), "warn");
    const QString e = isoUi(f, true);
    emit notify(e.isEmpty() ? tr("Grabando «%1» aparte").arg(f) : e, e.isEmpty() ? "ok" : "error");
}

QString StudioViewModel::isoUi(const QString& fuente, bool grabar) {
    if (!grabar) {
        rt_.stopSourceRecording(fuente.toStdString());
        emit statusChanged();
        return {};
    }
    const std::string e = rt_.grabarAparte(fuente.toStdString()); // las reglas son del modelo
    if (!e.empty()) return qs(e);
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

QVariantMap StudioViewModel::limpiaUi() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(rt_.engine().cleanFeedEstado().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().toVariantMap();
}

QString StudioViewModel::limpiaActivarUi(bool activar, const QString& destino) {
    if (!activar) {
        rt_.detenerSenalLimpia();
        return {};
    }
    return qs(rt_.iniciarSenalLimpia(destino.toStdString()));
}

QString StudioViewModel::urlApiRemota() const {
    // La dirección la decide la sala (sala/Direcciones): la puerta HTTP en la red local.
    return qs(direcciones_->origen("companion"));
}

QVariantMap StudioViewModel::apiRemotaUi() const {
    QVariantList accesos;
    for (const auto& a : botonera_->accesosHttp())
        accesos.append(QVariantMap{{"id", qs(a.value("id", std::string()))},
                                   {"nombre", qs(a.value("nombre", std::string()))},
                                   {"perfil", qs(a.value("perfil", std::string()))}});
    QVariantMap out{{"activo", botonera_->apiRemotaActiva()}, {"accesos", accesos}, {"url", urlApiRemota()}};
    if (urlApiRemota().isEmpty()) out["aviso"] = qs(direcciones_->motivo("companion"));
    return out;
}

void StudioViewModel::setApiRemotaUi(bool activo) { botonera_->setApiRemotaActiva(activo); }

QVariantMap StudioViewModel::crearAccesoHttpUi(const QString& perfil, const QString& nombre) {
    const nlohmann::json r = botonera_->crearAccesoHttp(perfil.toStdString(), nombre.toStdString());
    if (r.contains("error")) return {{"error", qs(r.value("error", std::string()))}};
    return {{"dispositivo", qs(r.value("dispositivo", std::string()))},
            {"token", qs(r.value("token", std::string()))},
            {"url_ejemplo", urlApiRemota() + qs(r.value("ejemplo", std::string()))}};
}

QString StudioViewModel::balistica() const { return qs(rt_.config().balistica); }

void StudioViewModel::setBalistica(const QString& b) {
    const std::string v = b.toStdString();
    if ((v != "pico" && v != "ppm" && v != "vu") || v == rt_.config().balistica) return;
    rt_.config().balistica = v;
    rt_.save();
    emit balisticaChanged();
}

QString StudioViewModel::pfl() const { return qs(rt_.engine().pfl()); }

QString StudioViewModel::pflUi(const QString& fuente) {
    // Tocar la que está en PFL la apaga. Uno solo a la vez y sin tocar la escucha fija
    // (Matriz::ponerPfl); el aviso llega por el motor (pflChanged).
    return matriz_->ponerPfl(pfl() == fuente ? QString() : fuente);
}

int StudioViewModel::retardoUi(const QString& fuente) const { return rt_.engine().sourceSyncOffset(fuente.toStdString()); }

QString StudioViewModel::setRetardoUi(const QString& fuente, int ms) {
    if (!rt_.engine().setSourceSyncOffset(fuente.toStdString(), std::clamp(ms, -5000, 5000)))
        return qs(rt_.engine().lastError());
    rt_.save();
    return {};
}

QString StudioViewModel::medirSincro(int segundos, std::function<void(const nlohmann::json&)> listo) {
    if (rt_.engine().midiendoSincro()) return tr("ya se está midiendo");
    if (!rt_.engine().empezarMedicionSincro()) return qs(rt_.engine().lastError());
    QTimer::singleShot(std::clamp(segundos, 4, 30) * 1000, this, [this, listo = std::move(listo)]() {
        listo(rt_.engine().terminarMedicionSincro());
    });
    return {};
}

QString StudioViewModel::medirSincroUi(int segundos) {
    return medirSincro(segundos, [this](const nlohmann::json& r) {
        emit medicionSincroLista(QJsonDocument::fromJson(QByteArray::fromStdString(r.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().toVariantMap());
    });
}

QString StudioViewModel::cupoUi(int n) { return QString::fromStdString(cupoSala_->cambiar(n)); }

QObject* StudioViewModel::playlistQml() const { return playlist_.get(); }
QObject* StudioViewModel::puestoCorreccionQml() const { return puestoCorreccion_.get(); }

QVariantMap StudioViewModel::grabacionUi() const {
    nlohmann::json j = rt_.engine().grabacionEstado();
    j["replay"] = rt_.engine().replayEstado();
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().toVariantMap();
}

QVariantMap StudioViewModel::scopeUi(const QString& fuente, const QString& tipo, int ancho, int alto) const {
    video::TipoScope t = video::TipoScope::Onda;
    if (!video::tipoScopeDeTexto(tipo, t)) return {{"error", tr("tipo desconocido")}};
    QImage img;
    std::uint64_t serie = 0;
    // El programa: el cuadro que ya se rinde para los monitores (no cuesta otro render).
    if (fuente.isEmpty() && frames_->latest("program", img, serie) && !img.isNull()) {
    } else {
        // Otra fuente: se pide aparte (capturarAparte) y se usa el último cuadro
        // que llegó; el panel pregunta cada 200 ms, así que el próximo ya lo tiene.
        // La interfaz no espera el cuadro.
        if (!scopeCapturando_) {
            scopeCapturando_ = true;
            QPointer<const StudioViewModel> yo(this);
            rt_.engine().capturarAparte(
                fuente.toStdString(), 640,
                [yo, fuente](const std::string& e, std::vector<std::uint8_t> rgba, int w, int h) {
                    QImage llego;
                    if (e.empty() && w > 0 && h > 0) llego = QImage(rgba.data(), w, h, w * 4, QImage::Format_RGBA8888).copy();
                    QMetaObject::invokeMethod(
                        qApp,
                        [yo, fuente, llego, e]() {
                            if (!yo) return;
                            auto* self = const_cast<StudioViewModel*>(yo.data());
                            self->scopeCapturando_ = false;
                            self->scopeFuente_ = fuente;
                            self->scopeCuadro_ = llego;
                            self->scopeError_ = QString::fromStdString(e);
                        },
                        Qt::QueuedConnection);
                });
        }
        if (scopeFuente_ != fuente || scopeCuadro_.isNull())
            return scopeError_.isEmpty() || scopeFuente_ != fuente ? QVariantMap{{"esperando", true}}
                                                                   : QVariantMap{{"error", scopeError_}};
        img = scopeCuadro_;
    }
    const video::Legalidad l = video::medirLegalidad(img);
    const QImage s = video::dibujarScope(img, t, std::clamp(ancho, 200, 1600), std::clamp(alto, 120, 1200));
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    s.save(&buf, "PNG");
    return {{"imagen", "data:image/png;base64," + QString::fromLatin1(png.toBase64())},
            {"legalidad", QVariantMap{{"negros_aplastados_pct", std::round(l.lumaBajaPct * 10) / 10},
                                      {"blancos_quemados_pct", std::round(l.lumaAltaPct * 10) / 10},
                                      {"fuera_de_gama_pct", std::round(l.cromaPct * 10) / 10}}},
            {"legalizador", qs(rt_.config().legalizar)}};
}

QString StudioViewModel::legalizarUi(const QString& modo) {
    const std::string m = modo.toStdString();
    if (m != "no" && m != "pal" && m != "ntsc" && m != "rango") return tr("modo desconocido");
    rt_.config().legalizar = m;
    rt_.save();
    video_->setLegalizar(m);
    return {};
}

void StudioViewModel::esperarReplay(const std::string& antes, int ms, std::function<void(const QString&)> listo,
                                    const std::string& salida) {
    const std::string ahora = rt_.engine().ultimoReplay(salida);
    if (!ahora.empty() && ahora != antes) return listo(qs(ahora));
    if (ms <= 0) return listo(QString());
    QTimer::singleShot(200, this, [this, antes, ms, listo = std::move(listo), salida]() mutable {
        esperarReplay(antes, ms - 200, std::move(listo), salida);
    });
}

QString StudioViewModel::repetirUi() {
    const std::string antes = rt_.engine().ultimoReplay();
    if (!rt_.engine().guardarReplay()) return qs(rt_.engine().lastError());
    esperarReplay(antes, 8000, [this](const QString& f) {
        if (f.isEmpty()) return emit notify(tr("La repetición no llegó a escribirse."), "error");
        const QString e = armarRepeticion(f);
        emit notify(e.isEmpty() ? tr("Repetición armada: dale TAKE.") : e, e.isEmpty() ? "ok" : "error");
    });
    return {};
}

QString StudioViewModel::armarRepeticion(const QString& archivo, const std::string& salida) {
    static const std::string kEscena = "Repetición", kFuente = "Repetición (clip)";
    BroadcastEngine& e = rt_.engine();
    const auto escenas = e.sceneNames();
    if (std::find(escenas.begin(), escenas.end(), kEscena) == escenas.end() && !rt_.addScene(kEscena))
        return qs(rt_.lastError());
    const std::string ajustes =
        nlohmann::json{{"local_file", archivo.toStdString()}, {"looping", false}}
            .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    const auto fuentes = e.sceneSources(kEscena);
    if (std::find(fuentes.begin(), fuentes.end(), kFuente) == fuentes.end()) {
        SourceSpec spec;
        spec.id = inputs::kMedioType;
        spec.name = kFuente;
        spec.settingsJson = ajustes;
        if (!rt_.addSource(kEscena, spec)) return qs(rt_.lastError());
    } else if (!rt_.updateSourceSettings(kFuente, ajustes)) {
        return qs(rt_.lastError());
    }
    scenes_->reload();
    emit escenasCambiaron();
    rt_.save();
    if (salida.empty()) {
        armarEscena(qs(kEscena));
        return {};
    }
    escenas::Conmutador* c = conmutadores_->de(salida);
    if (c == nullptr) return tr("No hay una salida «%1».").arg(qs(salida));
    return qs(c->armar(kEscena));
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
    if (!rotulo_->activo() || !dsk_ || !origen.startsWith("Traducción")) return;
    const int capa = rotulo_->capa();
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
        if (c.value("capa", 0) == capa) {
            const auto cont = c.value("contenido", nlohmann::json::array());
            armada = cont.size() == 1 && cont[0] == nombre;
        }
    if (!armada)
        dsk_->definir(capa, {{"nombre", "Traducción automática"},
                                    {"contenido", nlohmann::json::array({nombre})},
                                    {"recuadro", {{"posicion", "arriba_izquierda"}, {"tamano", 0.22}, {"margen", 0.03}}},
                                    {"fijo", false},
                                    {"transicion_ms", 300}});
    for (const auto& c : dsk_->capas())
        if (c.value("capa", 0) == capa && !c.value("visible", false)) dsk_->mostrar(capa, true, false);
    rotuloApagar_.start(ba::ajustes::sAMs("traductor.rotuloApagarS", 15)); // se va después de la última frase traducida
}

bool StudioViewModel::rotuloTraduccionActivo() const { return rotulo_->activo(); }

void StudioViewModel::setRotuloTraduccion(bool activo) {
    rotulo_->setActivo(activo);
    if (!activo && dsk_) dsk_->mostrar(rotulo_->capa(), false, true);
}

nlohmann::json StudioViewModel::rotuloTraduccionEstado() const {
    return {{"activo", rotulo_->activo()}, {"capa", rotulo_->capa()}, {"arriba", rotuloApagar_.isActive()}};
}

} // namespace ba::app

namespace ba::app {

bool StudioViewModel::alAire() const { return rt_.engine().haySalida(); }
bool StudioViewModel::transmitiendo() const { return rt_.engine().haySalida(false); }

bool StudioViewModel::modoVivo() const {
    // En vivo = sale algo de verdad (una transmisión; la grabación, si el ajuste
    // la cuenta). Correr el guion del evento sin transmitir no es estar al aire.
    if (!modoVivoCfg_->activo()) return false;
    return rt_.engine().haySalida(modoVivoCfg_->incluirGrabando());
}

bool StudioViewModel::paginaBloqueada(const QString& id) const {
    return modoVivo() && modoVivoCfg_->paginas().contains(id) && !desbloqueadas_.contains(id);
}

void StudioViewModel::desbloquearPagina(const QString& id, const QString& motivo) {
    desbloqueadas_.insert(id);
    ba::diag::registro().info(ba::i18n::llenar(ba::i18n::traducir("modo-vivo", "modo en vivo: se desbloqueó «%1» — motivo: %2"),
                                               {id.toStdString(), motivo.toStdString()}));
    emit statusChanged();
}

nlohmann::json StudioViewModel::modoVivoEstado() const {
    nlohmann::json p = nlohmann::json::array();
    for (const QString& x : modoVivoCfg_->paginas()) p.push_back(x.toStdString());
    nlohmann::json d = nlohmann::json::array();
    for (const QString& x : desbloqueadas_) d.push_back(x.toStdString());
    return {{"en_vivo", modoVivo()}, {"activo", modoVivoCfg_->activo()}, {"incluir_grabando", modoVivoCfg_->incluirGrabando()},
            {"paginas_protegidas", p}, {"desbloqueadas", d},
            {"que_protege", ba::i18n::traducir("asistente", "las páginas de la lista piden escribir CONFIRMAR con un motivo; los ajustes que reinician el gateway o el programa no se aplican")}};
}

std::string StudioViewModel::setModoVivoAjustes(const nlohmann::json& a) {
    if (modoVivo()) return tr("no se cambia en vivo").toStdString();
    const std::string e = modoVivoCfg_->cambiar(a);
    emit statusChanged();
    return e;
}

int StudioViewModel::personasConectadas() const {
    // Los invitados de la sala (lobby, programa, fila de oradores) y los integrantes
    // conectados desde el navegador (sesión remota; se crea más tarde, puede no estar).
    return (conference_ ? conference_->peopleConnected() : 0) +
           (asamblea_ ? asamblea_->sesionRemota()->conectados() : 0);
}

QVariantMap StudioViewModel::impactoDeCerrar() const {
    const int n = personasConectadas();
    return {{"transmitiendo", streaming_}, {"grabando", recording_}, {"personas", n},
            {"hay", streaming_ || recording_ || n > 0}};
}

namespace {
/// «a», «a y b», «a, b y c».
QString enumerar(QStringList l) {
    if (l.isEmpty()) return {};
    const QString ultimo = l.takeLast();
    return l.isEmpty() ? ultimo
                       : QCoreApplication::translate("estudio", "%1 y %2")
                             .arg(l.join(QCoreApplication::translate("estudio", ", ")), ultimo);
}
} // namespace

QString StudioViewModel::avisoDeCerrar(const QString& accion) const {
    const int n = personasConectadas();
    if (!streaming_ && !recording_ && n == 0) return {};
    QStringList corta;
    if (accion.isEmpty()) {
        // Cerrar la ventana: «Estás AL AIRE y hay 2 personas conectadas. Si cerrás, …».
        QString que = streaming_ && recording_ ? tr("Estás AL AIRE y grabando")
                      : streaming_             ? tr("Estás AL AIRE")
                      : recording_             ? tr("Estás grabando")
                                               : QString();
        if (n > 0)
            que = que.isEmpty()                 ? tr("Hay %n persona conectada", "", n)
                  : streaming_ && recording_ ? tr("%1, y hay %n persona conectada", "", n).arg(que)
                                             : tr("%1 y hay %n persona conectada", "", n).arg(que);
        if (streaming_) corta << tr("se corta la emisión");
        if (recording_) corta << tr("se detiene la grabación");
        if (n > 0) corta << (n == 1 ? tr("esa persona queda afuera") : tr("esas personas quedan afuera"));
        return tr("%1. Si cerrás, %2.").arg(que, enumerar(corta));
    }
    // Actualizar, volver atrás…: «Estás AL AIRE: actualizar cierra la aplicación y …».
    // `accion` es una clave ("actualizar", "volver"); por compatibilidad también el texto
    // que mandaba la página («volver a la versión anterior»). Otra cosa va tal cual.
    const QString que = streaming_ ? tr("Estás AL AIRE") : recording_ ? tr("Estás GRABANDO") : tr("Hay gente conectada");
    if (streaming_) corta << tr("corta la emisión");
    if (recording_) corta << tr("corta la grabación");
    if (n > 0) corta << (corta.isEmpty() ? tr("la desconecta") : tr("desconecta a quienes están conectados"));
    if (accion == "actualizar") return tr("%1: actualizar cierra la aplicación y %2.").arg(que, enumerar(corta));
    if (accion == "volver" || accion == "volver a la versión anterior")
        return tr("%1: volver a la versión anterior cierra la aplicación y %2.").arg(que, enumerar(corta));
    return tr("%1: %2 cierra la aplicación y %3.").arg(que, accion, enumerar(corta));
}

} // namespace ba::app

namespace ba::app {

std::filesystem::path StudioViewModel::datosRaiz() const { return rt_.dataRoot(); }

nlohmann::json StudioViewModel::caidas(int n) const {
    std::ifstream f(ba::platform::dataRoot() / "Runtime" / "caidas.jsonl");
    std::vector<nlohmann::json> todas;
    std::string l;
    while (std::getline(f, l)) {
        const nlohmann::json j = nlohmann::json::parse(l, nullptr, false);
        if (j.is_object()) todas.push_back(j);
    }
    nlohmann::json out = nlohmann::json::array();
    for (int i = static_cast<int>(todas.size()) - 1; i >= 0 && static_cast<int>(out.size()) < n; --i) out.push_back(todas[static_cast<std::size_t>(i)]);
    return out;
}

} // namespace ba::app

namespace ba::app {

QVariantMap StudioViewModel::transicionEntrada(const QString& escena) const {
    const nlohmann::json e = conmutador_->transicionEntrada(escena.toStdString());
    if (e.empty()) return {};
    return {{"tipo", QString::fromStdString(e.value("tipo", std::string()))}, {"ms", e.value("ms", -1)}};
}

QString StudioViewModel::setTransicionEntrada(const QString& escena, const QString& tipo, int ms) {
    return qs(conmutador_->setTransicionEntrada(escena.toStdString(), tipo.toStdString(), ms));
}

bool StudioViewModel::afv(const QString& fuente) const { return conmutadorActivo().afv(fuente.toStdString()); }

void StudioViewModel::setAfv(const QString& fuente, bool sigue) { conmutadorActivo().setAfv(fuente.toStdString(), sigue); }

QStringList StudioViewModel::afvFuentes() const {
    QStringList out;
    for (const std::string& f : conmutadorActivo().afvFuentes()) out << qs(f);
    return out;
}

void StudioViewModel::tbar(double t, bool soltar) {
    escenas::Conmutador& c = conmutadorActivo();
    if (const std::string e = c.tbar(t, soltar); !e.empty()) emit notify(qs(e), c.armada().empty() ? "aviso" : "error");
}

} // namespace ba::app

namespace ba::app {

void StudioViewModel::muestrearSalud() {
    const nlohmann::json h = rt_.engine().saludSalidas();
    const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
    const std::uint64_t saltados = h.value("saltados_encoder", 0ULL);
    const int saltadosNuevos = static_cast<int>(saltados >= saltadosAntes_ ? saltados - saltadosAntes_ : 0);
    saltadosAntes_ = saltados;
    nlohmann::json actual = nlohmann::json::array();
    const auto uno = [&](const std::string& id, const nlohmann::json& o) {
        Salud& s = salud_[id];
        const bool activo = o.value("activo", false);
        const std::uint64_t bytes = o.value("bytes", 0ULL);
        const int perdidos = o.value("perdidos", 0);
        const int kbps = activo && bytes >= s.bytesAntes ? static_cast<int>((bytes - s.bytesAntes) * 8 / 1000) : 0;
        const int perdidosNuevos = perdidos >= s.perdidosAntes ? perdidos - s.perdidosAntes : 0;
        s.bytesAntes = bytes;
        s.perdidosAntes = perdidos;
        const bool rec = o.value("reconectando", false);
        if (rec && !s.reconectando) {
            ++s.reconexiones;
            s.reconectandoDesde = ahora;
        }
        if (!rec && s.reconectando && s.reconectandoDesde > 0) s.reconexionMs = ahora - s.reconectandoDesde;
        s.reconectando = rec;
        const nlohmann::json punto{{"ms", ahora}, {"kbps", kbps}, {"perdidos_red", perdidosNuevos},
                                   {"perdidos_enc", saltadosNuevos}, {"congestion", o.value("congestion", 0.0)}};
        if (activo) {
            s.serie.push_back(punto);
            while (s.serie.size() > 600) s.serie.pop_front();
        }
        nlohmann::json hist = nlohmann::json::array();
        const std::size_t desde = s.serie.size() > 60 ? s.serie.size() - 60 : 0;
        for (std::size_t i = desde; i < s.serie.size(); ++i) hist.push_back(s.serie[i].value("kbps", 0));
        actual.push_back({{"id", id},
                          {"activo", activo},
                          {"kbps", kbps},
                          {"perdidos_red", perdidos},
                          {"perdidos_enc", saltados},
                          {"congestion", std::round(o.value("congestion", 0.0) * 100) / 100},
                          {"reconectando", rec},
                          {"reconexiones", s.reconexiones},
                          {"ultima_reconexion_ms", s.reconexionMs},
                          {"kbps_historial", hist}});
    };
    uno("principal", h.value("principal", nlohmann::json::object()));
    const nlohmann::json extras = h.value("extras", nlohmann::json::object()); // .items() de un temporal queda colgado
    for (const auto& [id, o] : extras.items()) uno(id, o);
    if (actual != saludActual_) {
        saludActual_ = actual;
        emit saludChanged();
    }
    bus_->publicar("salida/salud", saludActual_);
}

QVariantList StudioViewModel::saludSalidasUi() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(nlohmann::json{{"v", saludActual_}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
        .object()
        .value("v")
        .toArray()
        .toVariantList();
}

nlohmann::json StudioViewModel::saludSerie(const std::string& destino, int minutos) const {
    auto it = salud_.find(destino);
    if (it == salud_.end()) return nlohmann::json::array();
    const std::size_t n = static_cast<std::size_t>(std::clamp(minutos, 1, 10) * 60);
    nlohmann::json out = nlohmann::json::array();
    const auto& serie = it->second.serie;
    for (std::size_t i = serie.size() > n ? serie.size() - n : 0; i < serie.size(); ++i) out.push_back(serie[i]);
    return out;
}

} // namespace ba::app

namespace ba::app {

QVariantList StudioViewModel::monitores() const {
    // Los de Salidas → Pantallas: orden físico e ids estables (una sola lista).
    return monitores_ ? monitores_->conectadosEnOrden() : QVariantList{};
}

QObject* StudioViewModel::multivistasQml() const {
    QQmlEngine::setObjectOwnership(multivistas_.get(), QQmlEngine::CppOwnership);
    return multivistas_.get();
}

QString StudioViewModel::reiniciarPrograma(const QString& aviso) {
    // Los modos de esta corrida (--test / --debug) siguen después del reinicio.
    QStringList args{"--reiniciar"};
    for (const QString& a : QCoreApplication::arguments())
        if (a == "--test" || a == "--debug") args << a;
    // La conversación del asistente sigue después del reinicio (la retoma al abrir y le
    // avisa en qué evento quedó, para que siga con lo que estaba haciendo).
    if (agent_)
        if (const QString s = agent_->sesionParaReanudar(); !s.isEmpty())
            args << "--app-arg=--reanudar-asistente=" + s + "|" + agent_->provider() + "|" + agent_->model()
                 << "--app-arg=--asistente-evento=" + configuraciones_->activaUi();
    return system_->launch(args, aviso);
}

} // namespace ba::app
