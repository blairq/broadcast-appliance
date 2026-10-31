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
#include "ui/AsistenteViewModel.hpp"
#include "ui/ControlViewModel.hpp"
#include "ui/CuentasViewModel.hpp"
#include "ui/EmisionViewModel.hpp"
#include "ui/SalidasViewModel.hpp"
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

#include "comun/Duracion.hpp"
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
#include "ui/ImagenViewModel.hpp"
#include "ui/GraficoViewModel.hpp"
#include "ui/InputsViewModel.hpp"
#include "ui/ListModels.hpp"
#include "ui/MediosViewModel.hpp"
#include "ui/MezclaViewModel.hpp"
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
    // Las salidas (ui/SalidasViewModel): el conmutador de escenas de cada una, el FTB y la
    // placa, y cuál maneja esta interfaz. Acá se refleja lo de la activa (escenas, fuentes,
    // mezcla); las pantallas de tally de la sala y el monitor «armada» siguen a la por omisión.
    salidas_ = std::make_unique<SalidasViewModel>(SalidasViewModel::Partes{
        .rt = rt,
        .recursos = *recursos_,
        .interfaz = *interfaz_,
        .airePorOmision = [this]() {
            return QVariantMap{{"transmitiendo", streaming_},
                               {"grabando", recording_},
                               {"reconectando", reconnecting_},
                               {"tiempoTransmision", streamTime()},
                               {"tiempoGrabacion", recordTime()}};
        }});
    connect(salidas_.get(), &SalidasViewModel::listaCambio, this, [this](const QJsonArray& alGateway) {
        frames_->setSalidas(alGateway); // las claves «program:<id>», sus nombres y sus colores
        // El gateway sabe qué salidas hay para que la sesión remota y el cast elijan (se la
        // vuelve a mandar sola si el gateway se reinicia).
        if (conference_) conference_->client().ponerSalidas(alGateway);
        if (control_) control_->controlWeb().salidasCambiaron(); // sus aparatos ven la lista nueva
    });
    connect(salidas_.get(), &SalidasViewModel::alAireActivaCambio, this, [this](const QString& escena) {
        scenes_->liveChanged();
        sources_->setScene(escena); // el editor de fuentes sigue a la escena al aire
        emit sceneChanged();
    });
    connect(salidas_.get(), &SalidasViewModel::armadaActivaCambio, this, [this](const QString& escena) {
        scenes_->setArmada(escena);
        emit sceneChanged();
    });
    connect(salidas_.get(), &SalidasViewModel::afvActivaCambio, this, [this]() {
        emit statusChanged();
        emit afvChanged();
    });
    connect(salidas_.get(), &SalidasViewModel::alAirePorOmisionCambio, this, &StudioViewModel::publicarTally);
    connect(salidas_.get(), &SalidasViewModel::armadaPorOmisionCambio, this, [this](const QString& escena) {
        frames_->setArmada(escena); // el monitor «armada» mira la de la por omisión
        publicarTally();
    });
    connect(salidas_.get(), &SalidasViewModel::activaElegida, this, &StudioViewModel::reflejarSalidaActiva);
    // Lo que sale de la activa cambia con las pastillas.
    connect(this, &StudioViewModel::statusChanged, salidas_.get(), &SalidasViewModel::aireCambio);
    salidas_->publicar();
    // La salida que manejaba esta interfaz al cerrarse (si sigue estando; si no, la por omisión).
    salidas_->retomarActiva();
    props_ = std::make_unique<PropertiesModel>(rt);
    marca("props");
    conference_ = std::make_unique<ConferenceViewModel>(rt);
    // El transporte A→B lo mide el gateway y viaja en la foto de la sala (los SLI, F4-01).
    recursos_->alTransporte([this]() {
        const QJsonValue t = conference_->client().snapshot().value("uso").toObject().value("transporte");
        const nlohmann::json j = nlohmann::json::parse(QJsonDocument(t.toArray()).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
        return j.is_array() ? j : nlohmann::json::array();
    });
    // Grabar, transmitir y lo que los rodea: destinos, señal limpia, ISO, disco, loudness,
    // retornos y la salud de cada destino (ui/EmisionViewModel).
    emision_ = std::make_unique<EmisionViewModel>(
        EmisionViewModel::Partes{.rt = rt, .sala = *conference_, .grabando = [this]() { return recording_; }});
    connect(emision_.get(), &EmisionViewModel::notify, this, &StudioViewModel::notify);
    connect(emision_.get(), &EmisionViewModel::refrescar, this, &StudioViewModel::onTick);
    connect(emision_.get(), &EmisionViewModel::estadoCambio, this, &StudioViewModel::statusChanged);
    // Los destinos de cada salida van en su fila.
    connect(emision_.get(), &EmisionViewModel::destinosChanged, salidas_.get(), &SalidasViewModel::publicar);
    salidas_->publicar(); // el gateway se entera de las salidas apenas hay con quién hablar
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
        return salidas_->conmutador().armar(escena.toStdString());
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
        return EventoViewModel::Sistema{&salidas_->conmutadores(),
                                        bus_.get(),
                                        control_ ? &control_->dsk() : nullptr,
                                        imagen_ ? &imagen_->camaras() : nullptr,
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
    // Las cuentas de las plataformas y el formulario de credenciales (ui/CuentasViewModel).
    cuentas_ = std::make_unique<CuentasViewModel>(CuentasViewModel::Partes{.rt = rt,
                                                                           .bus = [this]() { return bus_.get(); },
                                                                           .red = [this]() { return imagen_ ? &imagen_->red() : nullptr; },
                                                                           .plugins = [this]() { return plugins_.get(); }});
    marca("cuentas");
    connect(cuentas_.get(), &CuentasViewModel::notify, this, &StudioViewModel::notify);
    // «Dónde verlo en vivo» de la página web del evento: lo que saben las cuentas.
    evento_->setPlataformas([this]() { return cuentas_->paraLaWeb(); });
    // Las preguntas del público para la página del evento (solo las moderadas). Mientras el
    // evento tiene página, la cola se consulta (como si la página Público estuviera abierta).
    evento_->setPublico([this]() { return publico_->preguntasParaLaWeb(); }, [this]() { return personasConectadas(); });
    connect(publico_.get(), &PublicoViewModel::cambio, evento_.get(), &EventoViewModel::publicarWeb);
    // Lo que sabe el resto del sistema y es público, para la página del evento. Cada cosa
    // puede no existir (la asamblea se arma después, o no hay): la página no muestra lo que
    // no viene.
    evento_->setSala([this]() {
        nlohmann::json sala = nlohmann::json::object();
        sala["aire"] = {{"transmitiendo", streaming_}, {"grabando", recording_}};
        // Lo de la asamblea (oradores, orden del día, quórum, el cuerpo y el recinto).
        if (asamblea_) sala.update(asamblea_->paraLaWeb());
        if (nlohmann::json v = publico_->votacionParaLaWeb(); !v.is_null()) sala["votacion"] = std::move(v);
        return sala;
    });
    connect(evento_.get(), &EventoViewModel::webChanged, this, [this]() {
        const bool hay = evento_->web().value("hay").toBool();
        if (hay == publicoPorWeb_) return;
        publicoPorWeb_ = hay;
        publico_->setActivo(hay);
    });
    connect(cuentas_.get(), &CuentasViewModel::webCambio, evento_.get(), &EventoViewModel::publicarWeb);
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
    chatOverlay_ = std::make_unique<ChatOverlayRenderer>(cuentas_->youtube(), *conference_, cuentas_->twitch(),
                                                         cuentas_->facebook());
    marca("chatOverlay");
    // El corte de luz (F1-11): la energía, la alarma «en batería» y la política.
    energia_ = std::make_unique<EnergiaAire>(EnergiaAire::Acciones{[&rt]() { return rt.cerrarGrabacionesPorEnergia(); },
                                                                   [&rt]() { rt.retomarGrabacionesPorEnergia(); },
                                                                   [&rt]() { return rt.emergenciaEnergia(); }});
    energia_->vigilar();
    connect(energia_.get(), &EnergiaAire::aviso, this, &StudioViewModel::notify);
    // Disparadores (alertas) contra el estado real de la app, y la pestaña Asistente. El
    // vigía corre en el plano de control (F3-03) y lee lo que dicen sus fuentes
    // (ui/AlarmasViewModel): lo de la interfaz como foto, lo del aire directo.
    {
        alarmas_ = std::make_unique<AlarmasViewModel>(AlarmasViewModel::Fuentes{
            .rt = rt,
            .energia = *energia_,
            .sala = *conference_,
            .chat = *chatOverlay_,
            .recursos = [this]() { return recursos_.get(); },
            .flujos = [this]() { return asistente_ ? &asistente_->flujos() : nullptr; },
            .video = [this]() { return imagen_ ? &imagen_->video() : nullptr; }});
        connect(alarmas_.get(), &AlarmasViewModel::notify, this, &StudioViewModel::notify);
        marca("alerts");
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
        // El vivo a mano (GUI o MCP: AppRuntime::salirAlAireManual). El servidor sí
        // (es público); la clave nunca.
        rt_.alVivoManual([this](const std::string& servidor) {
            QMetaObject::invokeMethod(this, [this, servidor]() {
                if (bus_) bus_->publicar("salida/vivo", {{"plataforma", "manual"}, {"servidor", servidor}}, true);
            });
        });
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
        // Los asistentes, la voz, las salidas de texto y la barra de mensajes (ui/AsistenteViewModel).
        asistente_ = std::make_unique<AsistenteViewModel>(AsistenteViewModel::Partes{
            .rt = rt,
            .alarmas = *alarmas_,
            .sala = *conference_,
            .evento = evento_.get(),
            .enVivo = [this]() { return modoVivo(); },
            .guardarCredencial = [this](const QString& pl, const QVariantMap& v) { return cuentas_->saveAccountCredential(pl, v); },
            .dsk = [this]() { return control_ ? &control_->dsk() : nullptr; },
            .cuerpo = [this]() { return asamblea_ ? &asamblea_->cuerpo().cuerpo() : nullptr; }});
        connect(asistente_.get(), &AsistenteViewModel::notify, this, &StudioViewModel::notify);
        marca("asistente");
    }
    // La documentación del evento (docs/): cada cambio republica la web del evento.
    documentos_ = std::make_unique<DocumentosViewModel>([&rt]() { return rt.dataRoot(); });
    if (evento_) connect(documentos_.get(), &DocumentosViewModel::cambio, evento_.get(), &EventoViewModel::publicarWeb);
    connect(documentos_.get(), &DocumentosViewModel::notify, this, &StudioViewModel::notify);
    // Los PDF en las escenas: la lista para elegir es la documentación del evento.
    pdfEscena_ = std::make_unique<PdfEscena>(rt);
    connect(documentos_.get(), &DocumentosViewModel::cambio, pdfEscena_.get(), &PdfEscena::refrescarDocumentos);
    inputs_ = std::make_unique<InputsViewModel>(rt, *this);
    marca("inputs");
    // Las cámaras (PTZ y Mejorar imagen), los grafos de video, la superresolución, los
    // tiempos, la resolución, los equipos de la red y seguir caras (ui/ImagenViewModel).
    imagen_ = std::make_unique<ImagenViewModel>(ImagenViewModel::Partes{.rt = rt,
                                                                       .bus = *bus_,
                                                                       .sala = *conference_,
                                                                       .entradas = *inputs_,
                                                                       .recursos = *recursos_,
                                                                       .cuadros = *frames_,
                                                                       .personas = personas_.get(),
                                                                       .racks = [this]() { return mezcla_ ? &mezcla_->racks() : nullptr; }});
    connect(imagen_.get(), &ImagenViewModel::notify, this, &StudioViewModel::notify);
    marca("imagen");
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
            for (const QVariant& v : imagen_->camaras().camaras()) out << v.toMap().value("entrada").toString();
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
            for (const QString& e : imagen_->video().entradas()) out << QVariantMap{{"clave", "entrada:" + e}, {"titulo", e}};
            return out;
        };
        catalogo_ = std::make_unique<CatalogoVisible>(std::move(f));
        const auto refrescar = [this]() { catalogo_->refrescar(); };
        connect(this, &StudioViewModel::escenasCambiaron, catalogo_.get(), refrescar);
        connect(multivistas_.get(), &MultiviewViewModel::cambio, catalogo_.get(), refrescar);
        if (dashboard_) connect(dashboard_.get(), &DashboardModel::backgroundChanged, catalogo_.get(), refrescar);
        connect(&imagen_->camaras(), &ptz::ControlCamaras::cambio, catalogo_.get(), refrescar);
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
    // Las entradas de video también son cosas que se pueden mostrar (catálogo).
    connect(&imagen_->video(), &video::VideoGrafos::cambio, catalogo_.get(), [this]() { catalogo_->refrescar(); });
    catalogo_->refrescar();
    // El respaldo de las entradas al aire que pierden señal (modelo): acá solo se muestra.
    respaldo_ = std::make_unique<escenas::RespaldoEntradas>(rt, imagen_->video(), salidas_->conmutador(), this);
    connect(respaldo_.get(), &escenas::RespaldoEntradas::sinSenalCambio, this, &StudioViewModel::senalChanged);
    connect(respaldo_.get(), &escenas::RespaldoEntradas::aviso, this,
            [this](const QString& texto) { emit notify(texto, "error"); });
    // Lo que la revisión previa del evento mira del aparato.
    evento_->setChequeosAparato([this]() { return imagen_->chequeosDelAparato(streamKeyAvailable()); });
    // Los plugins de Python: en los datos (Plugins/), con el Python del programa.
    plugins_ = std::make_unique<plugins::Plugins>(rt, &imagen_->video(), &imagen_->camaras(), &asistente_->mensajes());
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
        .conmutador = salidas_->conmutador(),
        .camaras = imagen_->camaras(),
        .plugins = [this]() { return plugins_.get(); },
        .evento = evento_.get(),
        .archivosDeSesion = [this]() { return asistente_->flujos().archivosDeSesion(); }});
    connect(asamblea_.get(), &AsambleaViewModel::notify, this, &StudioViewModel::notify);
    bus_->suscribir("#", [this](const std::string& t, const nlohmann::json& d) {
        if (plugins_ && t.rfind("plugin/", 0) != 0) plugins_->eventoApp(t, d);
    });
    // Las superficies de control: la botonera, el Satellite, el control web, las capas DSK
    // y las acciones (ui/ControlViewModel).
    control_ = std::make_unique<ControlViewModel>(ControlViewModel::Partes{.rt = rt,
                                                                           .bus = *bus_,
                                                                           .sala = *conference_,
                                                                           .direcciones = *direcciones_,
                                                                           .asamblea = *asamblea_,
                                                                           .camaras = &imagen_->camaras(),
                                                                           .plugins = plugins_.get(),
                                                                           .pdfEscena = pdfEscena_.get(),
                                                                           .salidas = [this]() { return salidas_->paraSuperficies(); }});
    connect(control_.get(), &ControlViewModel::notify, this, &StudioViewModel::notify);
    connect(control_.get(), &ControlViewModel::mostrarQr, this, &StudioViewModel::mostrarQr);
    connect(control_.get(), &ControlViewModel::dskCambio, this, &StudioViewModel::publicarTally);
    control_->dsk().setSalida(salidas_->activaId());
    linea_ = std::make_unique<LineaViewModel>(*evento_, rt_.engine(), frames_.get(), &control_->acciones(), bus_.get());
    // La grilla de personas (fuentes ba_grilla): la gente de la sala y de la asamblea,
    // repartida en las grillas; el mosaico lo pinta el gateway (BA.Grilla.<k>).
    grillasPersonas_ = std::make_unique<escenas::ControlGrillas>(
        escenas::ControlGrillas::Partes{conference_->client(), rt_.engine(), asamblea_->sesionRemota(),
                                        &asamblea_->cuerpo().cuerpo()},
        this);
    marca("plugins");
    // Los equipos (red, esta máquina, USB): un catálogo con acciones declaradas, y
    // el registro de los dispositivos de audio y video con el nombre del operador
    // (de la MÁQUINA, no del evento: <datos>/Config/dispositivos.json).
    dispositivos_ = std::make_unique<dispositivos::Dispositivos>(ba::platform::dataRoot() / "Config" /
                                                                 "dispositivos.json");
    marca("dispositivos");
    dispositivos::registrarTodo(*dispositivos_, {&imagen_->red(), plugins_.get(), &imagen_->video(), &imagen_->camaras(),
                                                 &asistente_->mensajes(), &rt, inputs_.get()});
    // El audio: los racks de cada fuente, la pestaña Audio, la matriz y el Mezclador
    // (ui/MezclaViewModel).
    mezcla_ = std::make_unique<MezclaViewModel>(MezclaViewModel::Partes{
        .rt = rt, .tiras = *mixer_, .salida = salidas_->activaId(), .alAire = [this] { return alAire(); }});
    connect(mezcla_.get(), &MezclaViewModel::notify, this, &StudioViewModel::notify);
    marca("audio");
    // La voz, las salidas de texto, las tarjetas «Texto a voz» y las traducciones, con los racks.
    asistente_->conRacks(mezcla_->racks());
    // Las pistas que graba el programa (T29) quedan rotuladas en la Matriz.
    connect(this, &StudioViewModel::statusChanged, mezcla_.get(), &MezclaViewModel::revisarPistas);
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
        f.direcciones = [this]() -> const sala::Direcciones* { return direcciones_.get(); };
        f.webEvento = [this]() -> QVariantMap {
            if (!evento_) return {};
            return {{"hay", evento_->web().value("hay").toBool()}, {"evento", evento_->abierto()}};
        };
        // La API remota, el Satellite, el control web y los pedidos de cada aparato.
        control_->alimentarEnlaces(f);
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
        control_->avisarA(*e);
        if (evento_) {
            connect(evento_.get(), &EventoViewModel::webChanged, e, avisar);
            connect(evento_.get(), &EventoViewModel::guionChanged, e, avisar);
        }
    }
    // La atención a quien se conecta (Sala y Asamblea): con todo lo que usa ya armado.
    {
        atencion_ = std::make_unique<AtencionViewModel>(rt, *this, asistente_->voz(), asistente_->modelos());
        connect(atencion_.get(), &AtencionViewModel::notify, this, &StudioViewModel::notify);
        marca("atencion");
    }
    // El registro de personas por tótem (docs/funciones/REGISTRO-TOTEM.md): con la
    // atención (su voz), Personas, el evento y la sala ya armados. Existe aunque
    // no haya motor de voz: la configuración y el catálogo no dependen de él.
    totems_ = std::make_unique<TotemsViewModel>(rt, *this, &asistente_->voz(), &asistente_->modelos());
    connect(totems_.get(), &TotemsViewModel::notify, this, &StudioViewModel::notify);
    // El modelo que entiende en los tótems es el del rol «registro» (pestaña Agentes): si lo
    // cambian (motor, modelo, apagarlo), la página de tótems dice con qué entiende.
    totems_->setRoles(&asistente_->roles());
    connect(&asistente_->despachador(), &agent::Despachador::cambio, totems_.get(), &TotemsViewModel::cambio);
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
        control_->dsk().restaurar();
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
    rt_.engine().alCambiarAudioFuente(nullptr); // la matriz y las tiras se van con la vista; el motor sigue
    rt_.dejarDeObservar(observadorRt_); // el runtime vive más que la vista
    // El runtime y los flujos de texto viven más que el evento: que no le
    // pregunten la carpeta a un EventoViewModel que ya no está.
    rt_.setCarpetaDelEvento(nullptr);
    asistente_->flujos().setCarpetaDelEvento(nullptr);
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
    if (++vigiaTicks_ % 10 == 0) mezcla_->vigilarModulos();
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
    salidas_->alTic(st); // qué sale de cada una de las demás salidas y desde cuándo
    // Un corte pedido es un aviso corto; uno que nadie pidio (red, servidor,
    // disco lleno) queda fijo en la barra del operador hasta que lo cierre.
    const auto dropped = [this](const QString& title, const QString& text) {
        asistente_->mensajes().post(
            {{"kind", "message"}, {"priority", "critical"}, {"title", title.toStdString()}, {"text", text.toStdString()}});
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
    if (const std::string& activa = salidas_->activaId(); !activa.empty()) { // el vúmetro de la activa (las alarmas siguen mirando la por omisión)
        rt_.engine().programPeaksDb(l, r, activa);
        peakActivaL_ = std::max<double>(l, peakActivaL_ - 1.5);
        peakActivaR_ = std::max<double>(r, peakActivaR_ - 1.5);
    }
    mixer_->refreshLevels();
    emit nivelesChanged();
    // El resto, solo si cambió algo de lo que se ve (con tiempos de grabación o
    // transmisión corriendo, una vez por segundo).
    const QVariantMap aa = salidas_->aire();
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

QString StudioViewModel::recordTime() const { return recording_ ? hmsDesde(recSince_) : QString("00:00:00"); }
QString StudioViewModel::streamTime() const { return streaming_ ? hmsDesde(streamSince_) : QString("00:00:00"); }
QString StudioViewModel::currentScene() const {
    return qs(rt_.engine().currentScene(salidas_ ? salidas_->activaId() : std::string()));
}
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
QObject* StudioViewModel::youtube() const { return cuentas_->youtubeQml(); }
QObject* StudioViewModel::twitch() const { return cuentas_->twitchQml(); }
QObject* StudioViewModel::facebook() const { return cuentas_->facebookQml(); }
QObject* StudioViewModel::cuentasQml() const {
    QQmlEngine::setObjectOwnership(cuentas_.get(), QQmlEngine::CppOwnership);
    return cuentas_.get();
}
QObject* StudioViewModel::system() const { return system_.get(); }
QObject* StudioViewModel::agent() const { return asistente_->agenteQml(); }
QObject* StudioViewModel::manualesQml() const { return asistente_->manualesQml(); }
QObject* StudioViewModel::agenteAutomatizacion() const { return asistente_->agenteAutomatizacionQml(); }
QObject* StudioViewModel::modelos() const { return asistente_->modelosQml(); }
QObject* StudioViewModel::descargas() const { return &net::Descargas::la(); }
QObject* StudioViewModel::voz() const { return asistente_->vozQml(); }
QObject* StudioViewModel::vozPrompter() const { return asistente_->vozPrompterQml(); }
QObject* StudioViewModel::mezcladorQml() const { return mezcla_->mezcladorQml(); }
QObject* StudioViewModel::matriz() const { return mezcla_->matrizQml(); }
QObject* StudioViewModel::camaras() const { return imagen_->camarasQml(); }
QObject* StudioViewModel::tiempos() const { return imagen_->tiemposQml(); }
QObject* StudioViewModel::resolucion() const { return imagen_->resolucionQml(); }
QObject* StudioViewModel::video() const { return imagen_->videoQml(); }
QObject* StudioViewModel::red() const { return imagen_->redQml(); }
QObject* StudioViewModel::pluginsQml() const { return plugins_.get(); }
QObject* StudioViewModel::seguidor() const { return imagen_->seguidorQml(); }
QObject* StudioViewModel::dispositivosQml() const { return dispositivos_.get(); }
QObject* StudioViewModel::flujos() const { return asistente_->flujosQml(); }
QObject* StudioViewModel::voces() const { return asistente_->vocesQml(); }
QObject* StudioViewModel::traductor() const { return asistente_->traductorQml(); }
QObject* StudioViewModel::estadoIA() const { return asistente_->estadoIAQml(); }
QObject* StudioViewModel::documentosQml() const { return documentos_.get(); }
QObject* StudioViewModel::pdfEscenaQml() const { return pdfEscena_.get(); }
QObject* StudioViewModel::superres() const { return imagen_->superresQml(); }
QObject* StudioViewModel::bancoSrObj() const { return imagen_->bancoSrQml(); }
QObject* StudioViewModel::imagenQml() const {
    QQmlEngine::setObjectOwnership(imagen_.get(), QQmlEngine::CppOwnership);
    return imagen_.get();
}
QObject* StudioViewModel::despachadorQml() const { return asistente_->despachadorQml(); }
QObject* StudioViewModel::messages() const { return asistente_->mensajesQml(); }
QObject* StudioViewModel::asistenteQml() const {
    QQmlEngine::setObjectOwnership(asistente_.get(), QQmlEngine::CppOwnership);
    return asistente_.get();
}

QObject* StudioViewModel::audio() const { return mezcla_->audioQml(); }
QObject* StudioViewModel::mezclaQml() const {
    QQmlEngine::setObjectOwnership(mezcla_.get(), QQmlEngine::CppOwnership);
    return mezcla_.get();
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

void StudioViewModel::takeScene(const QString& name) {
    if (const std::string e = salidas_->conmutadorActivo().poner(name.toStdString()); !e.empty()) emit notify(qs(e), "error");
}

QString StudioViewModel::escenaArmada() const { return qs(salidas_->conmutadorActivo().armada()); }

void StudioViewModel::publicarTally() {
    auto* p = qobject_cast<PantallasViewModel*>(pantallas());
    if (p == nullptr) return;
    // Las pantallas de tally de la sala: las de la por omisión (no la activa de esta interfaz).
    const QString aire = qs(rt_.engine().currentScene());
    const QString armada = qs(salidas_->conmutador().armada());
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
    if (const std::string e = salidas_->conmutadorActivo().armar(name.toStdString()); !e.empty()) emit notify(qs(e), "aviso");
}

void StudioViewModel::take() { takeCon(QString(), -1); }

void StudioViewModel::takeCon(const QString& tipo, int ms) {
    escenas::Conmutador& c = salidas_->conmutadorActivo();
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
        control_->ajustesAplicados(); // el tema de las páginas web, si cambió
        imagen_->ajustesAplicados(); // Mejorar imagen general y el legalizador rigen ya
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
    if (hubo(C::Audio) || hubo(C::Fuentes)) mezcla_->revisarFuentes();
    if (hubo(C::Escenas) || hubo(C::Aire)) {
        scenes_->liveChanged();
        emit sceneChanged();
    }
    if (hubo(C::Salidas)) {
        emit statusChanged();
        emit emision_->destinosChanged();
    }
    if (hubo(C::Salidas) || hubo(C::Aire)) salidas_->publicar();
}

QObject* StudioViewModel::emisionQml() const {
    QQmlEngine::setObjectOwnership(emision_.get(), QQmlEngine::CppOwnership);
    return emision_.get();
}

QObject* StudioViewModel::salidasQml() const {
    QQmlEngine::setObjectOwnership(salidas_.get(), QQmlEngine::CppOwnership);
    return salidas_.get();
}

void StudioViewModel::reflejarSalidaActiva() {
    // La interfaz pasa a mostrar lo de la activa: escenas, fuentes, mezcla, tally.
    const std::string& activa = salidas_->activaId();
    peakActivaL_ = peakActivaR_ = -100;
    scenes_->setSalida(activa);
    scenes_->setArmada(escenaArmada());
    sources_->setScene(currentScene());
    mixer_->setSalida(activa);
    if (mezcla_) mezcla_->setSalida(activa);
    if (control_) control_->dsk().setSalida(activa);
    emit sceneChanged();
    emit afvChanged();
    emit statusChanged();
    emit nivelesChanged();
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

QObject* StudioViewModel::ccQml() const { return asistente_->ccQml(); }

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
    const QString armada = qs(salidas_->conmutador().armada());
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
        const QStringList aireQ = alAireEn(salidas_->activaId());
        QStringList armadaQ;
        for (const auto& f : fuentesDe(escenaArmada())) armadaQ << QString::fromStdString(f.get<std::string>());
        QVariantMap borde, enSalidas;
        for (const QString& f : armadaQ) borde[f] = QStringLiteral("#2ecc71");
        for (const QString& f : aireQ) borde[f] = QStringLiteral("#e53935");
        if (salidas_->varias())
            for (const auto& v : salidas_->filas()) {
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
    bus_->publicar("aire/tbar", {{"hacia", rt_.engine().manualHacia()}, {"posicion", salidas_->conmutador().tbarPosicion()}});
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
        for (const QJsonValue& v : salidas_->filas()) {
            const QJsonObject f = v.toObject();
            const std::string id = f.value("id").toString().toStdString();
            const std::string esc = f.value("alAire").toString().toStdString();
            const QString arm = f.value("armada").toString();
            nlohmann::json fa = fuentesDe(qs(esc));
            for (const std::string& d : rt_.engine().fuentesDsk(id))
                if (std::find(fa.begin(), fa.end(), d) == fa.end()) fa.push_back(d);
            const escenas::Conmutador* c = salidas_->conmutadores().de(id);
            const bool porOmision = f.value("porOmision").toBool();
            // El fader y el mudo de cada fuente en su mezcla (los de la por omisión ya van
            // en audio/nivel/<fuente>).
            nlohmann::json niveles = nlohmann::json::object();
            if (!porOmision)
                for (int i = 0; i < mixer_->rowCount(); ++i)
                    niveles[mixer_->data(mixer_->index(i), MixerModel::NameRole).toString().toStdString()] = mixer_->nivelEn(i, id);
            ss.push_back({{"id", id},
                          {"medios", mediosEn(fa)},
                          {"dsk", control_->dsk().capas(id)},
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
        emision_->muestrearSalud(*bus_);
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

QObject* StudioViewModel::botoneraQml() const { return control_->botoneraQml(); }
QObject* StudioViewModel::recursosQml() const {
    if (recursos_) QQmlEngine::setObjectOwnership(recursos_.get(), QQmlEngine::CppOwnership);
    return recursos_.get();
}
QObject* StudioViewModel::controlWebQml() const { return control_->controlWebQml(); }
QObject* StudioViewModel::controlQml() const {
    QQmlEngine::setObjectOwnership(control_.get(), QQmlEngine::CppOwnership);
    return control_.get();
}


QObject* StudioViewModel::satelliteQml() const { return control_->satelliteQml(); }
QObject* StudioViewModel::carasBotoneraQml() const { return control_->carasBotoneraQml(); }
QObject* StudioViewModel::dskQml() const { return control_->dskQml(); }
QObject* StudioViewModel::grillasQml() const { return grillasPersonas_.get(); }
QObject* StudioViewModel::accionesQml() const { return control_->accionesQml(); }
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

} // namespace ba::app

namespace ba::app {


QString StudioViewModel::cupoUi(int n) { return QString::fromStdString(cupoSala_->cambiar(n)); }

QObject* StudioViewModel::playlistQml() const { return playlist_.get(); }
QObject* StudioViewModel::puestoCorreccionQml() const { return asistente_->puestoCorreccionQml(); }

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
    escenas::Conmutador* c = salidas_->conmutadores().de(salida);
    if (c == nullptr) return tr("No hay una salida «%1».").arg(qs(salida));
    return qs(c->armar(kEscena));
}

} // namespace ba::app

namespace ba::app {

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
    const nlohmann::json e = salidas_->conmutador().transicionEntrada(escena.toStdString());
    if (e.empty()) return {};
    return {{"tipo", QString::fromStdString(e.value("tipo", std::string()))}, {"ms", e.value("ms", -1)}};
}

QString StudioViewModel::setTransicionEntrada(const QString& escena, const QString& tipo, int ms) {
    return qs(salidas_->conmutador().setTransicionEntrada(escena.toStdString(), tipo.toStdString(), ms));
}

bool StudioViewModel::afv(const QString& fuente) const { return salidas_->conmutadorActivo().afv(fuente.toStdString()); }

void StudioViewModel::setAfv(const QString& fuente, bool sigue) {
    salidas_->conmutadorActivo().setAfv(fuente.toStdString(), sigue);
}

QStringList StudioViewModel::afvFuentes() const {
    QStringList out;
    for (const std::string& f : salidas_->conmutadorActivo().afvFuentes()) out << qs(f);
    return out;
}

void StudioViewModel::tbar(double t, bool soltar) {
    escenas::Conmutador& c = salidas_->conmutadorActivo();
    if (const std::string e = c.tbar(t, soltar); !e.empty()) emit notify(qs(e), c.armada().empty() ? "aviso" : "error");
}

} // namespace ba::app

namespace ba::app {

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
    agent::AgentViewModel& agente = asistente_->agente();
    if (const QString s = agente.sesionParaReanudar(); !s.isEmpty())
        args << "--app-arg=--reanudar-asistente=" + s + "|" + agente.provider() + "|" + agente.model()
             << "--app-arg=--asistente-evento=" + configuraciones_->activaUi();
    return system_->launch(args, aviso);
}

} // namespace ba::app
