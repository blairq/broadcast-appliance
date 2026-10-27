// ============================================================================
// src/app/ui/StudioViewModel.cpp — ver StudioViewModel.hpp
// ============================================================================

#include "ui/StudioViewModel.hpp"
#include "sala/CupoSala.hpp"
#include "sala/VotacionSala.hpp"
#include "sala/PreguntasSala.hpp"
#include "sala/SeguridadSala.hpp"
#include "escenas/Conmutador.hpp"
#include "asamblea/Votaciones.hpp"
#include "asamblea/Presidencia.hpp"
#include "escenas/RespaldoEntradas.hpp"
#include "escenas/AjustesEscenas.hpp"
#include "escenas/ControlGrillas.hpp"
#include "arranque/ModoVivoConfig.hpp"
#include "ui/EnlacesViewModel.hpp"
#include "arranque/InterfazConfig.hpp"
#include "agent/RotuloTraduccionConfig.hpp"
#include "emision/RetornosInvitados.hpp"
#include "emision/SeguridadAire.hpp"
#include "audio/ObjetivoLoudness.hpp"
#include "ajustes/Ajustes.hpp"
#include "i18n/I18n.hpp"
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
#include "agent/AlarmaSonora.hpp"
#include "agent/SubtitulosCC.hpp"
#include "agent/Vocabulario.hpp"
#include "agent/Notificador.hpp"
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
#include "diag/DiarioDeArranque.hpp"
#include "diag/Registro.hpp"
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
#include <QCoreApplication>

namespace ba::app {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

/// Un hijo objeto, o {} si falta o no es un objeto (value() sobre null tira una excepción).
nlohmann::json hijoObjeto(const nlohmann::json& j, const char* k) {
    return j.is_object() && j.contains(k) && j[k].is_object() ? j[k] : nlohmann::json::object();
}

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
            [vm, t = QString::fromStdString(texto)]() {
                if (vm) emit vm->notify(t, t.startsWith("El disco anda bien") ? "ok" : "error");
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
    objetivoLoudness_ = std::make_unique<audio::ObjetivoLoudness>(rt.dataRoot(), rt.engine().loudness());
    // El conmutador de escenas es del modelo; acá solo se refleja lo que cambia.
    conmutador_ = std::make_unique<escenas::Conmutador>(rt, this);
    connect(conmutador_.get(), &escenas::Conmutador::alAireCambio, this, [this](const QString& escena) {
        scenes_->liveChanged();
        sources_->setScene(escena); // el editor de fuentes sigue a la escena al aire
        emit sceneChanged();
        publicarTally();
    });
    connect(conmutador_.get(), &escenas::Conmutador::armadaCambio, this, [this](const QString& escena) {
        scenes_->setArmada(escena);
        frames_->setArmada(escena); // el monitor de armada mira esta
        emit sceneChanged();
        publicarTally();
    });
    connect(conmutador_.get(), &escenas::Conmutador::afvCambio, this, &StudioViewModel::statusChanged);
    connect(conmutador_.get(), &escenas::Conmutador::afvCambio, this, &StudioViewModel::afvChanged);
    props_ = std::make_unique<PropertiesModel>(rt);
    marca("props");
    conference_ = std::make_unique<ConferenceViewModel>(rt);
    marca("conference");
    direcciones_ = std::make_unique<sala::Direcciones>(conference_->client());
    // El cupo es del modelo; acá solo se muestra lo que pasó.
    cupoSala_ = std::make_unique<sala::CupoSala>(rt, conference_->client());
    cupoSala_->alAvisar([this](const std::string& texto, bool error) {
        emit notify(QString::fromStdString(texto), error ? "error" : "ok");
    });
    cupoSala_->alCambiar([this](int n) { frames_->setGuests(n); });
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
        if (bancas_) {
            const nlohmann::json b = bancas_->estadoJson();
            nlohmann::json fila = nlohmann::json::array();
            for (const auto& i : b.value("cola", nlohmann::json::array())) fila.push_back(publicoDe(i));
            const nlohmann::json alAire = b.value("al_aire", nlohmann::json());
            if (alAire.is_object() || !fila.empty()) {
                nlohmann::json o{{"fila", fila}};
                if (alAire.is_object()) {
                    o["habla"] = publicoDe(alAire);
                    o["asignadoSeg"] = b.value("asignado_s", 0);
                    // Cuándo se le termina el tiempo (hora absoluta): la página cuenta sola.
                    o["hastaMs"] = b.value("desde_ms", 0LL) + b.value("asignado_s", 0LL) * 1000;
                }
                sala["oradores"] = o;
            }
        }
        if (ordenDia_) {
            nlohmann::json puntos = nlohmann::json::array();
            for (const auto& p : ordenDia_->puntos())
                puntos.push_back({{"n", p.value("n", 0)}, {"titulo", p.value("titulo", std::string())},
                                  {"expediente", p.value("expediente", std::string())},
                                  {"estado", p.value("estado", std::string())},
                                  {"resultado", p.value("resultado", std::string())}});
            if (!puntos.empty()) sala["ordenDelDia"] = puntos;
        }
        if (quorum_) {
            const nlohmann::json q = quorum_->estado();
            if (q.value("configurado", false))
                sala["quorum"] = {{"configurado", true}, {"presentes", q.value("presentes", 0)},
                                  {"necesarios", q.value("necesarios", 0)}, {"hay", q.value("hay_quorum", false)}};
        }
        if (cuerpo_) {
            // La asamblea: el cuerpo, quién está presente (conectado desde el navegador y
            // habilitado, o presente en su banca según la central), quién habla, el quórum,
            // el punto en tratamiento y los resultados oficiales. La página solo lo muestra si
            // el evento es una asamblea.
            std::set<std::string> remotos;
            if (sesionRemota_) for (const auto& b : sesionRemota_->presentesBancas()) remotos.insert(b);
            nlohmann::json porBanca = nlohmann::json::object();
            if (dcn_) porBanca = hijoObjeto(hijoObjeto(dcn_->estado(), "asistencia"), "por_banca");
            const std::string hablaId = bancas_ ? hijoObjeto(bancas_->estadoJson(), "al_aire").value("id", std::string()) : std::string();
            nlohmann::json integrantes = nlohmann::json::array();
            nlohmann::json porId = nlohmann::json::object(); // id → lo público del integrante (para el recinto)
            std::map<std::string, std::pair<int, int>> bloques; // bloque → {presentes, total}
            int presentes = 0;
            for (const auto& i : cuerpo_->cuerpo().integrantes()) {
                const std::string banca = i.value("banca", std::string());
                const bool remoto = !banca.empty() && remotos.count(banca);
                const bool enSala = !banca.empty() && porBanca.is_object() && porBanca.value(banca, false);
                nlohmann::json p{{"nombre", i.value("nombre", std::string())}, {"banca", banca},
                                 {"presente", remoto || enSala}, {"lugar", remoto ? "remoto" : enSala ? "sala" : ""},
                                 {"habla", !hablaId.empty() && i.value("id", std::string()) == hablaId}};
                for (const char* k : {"bloque", "cargo"})
                    if (i.contains(k) && i[k].is_string() && !i[k].get<std::string>().empty()) p[k] = i[k];
                if (remoto || enSala) ++presentes;
                auto& bl = bloques[i.value("bloque", std::string())];
                ++bl.second;
                if (remoto || enSala) ++bl.first;
                if (i.contains("id") && i["id"].is_string()) porId[i["id"].get<std::string>()] = p;
                integrantes.push_back(p);
            }
            nlohmann::json asam{{"cuerpo", cuerpo_->cuerpo().nombre()}, {"integrantes", integrantes},
                                {"presentes", presentes}, {"total", static_cast<int>(integrantes.size())}};
            // Los bloques (para ver de un vistazo cuántos de cada uno están).
            nlohmann::json bjs = nlohmann::json::array();
            for (const auto& [nombre, n] : bloques)
                if (!nombre.empty()) bjs.push_back({{"bloque", nombre}, {"presentes", n.first}, {"total", n.second}});
            asam["bloques"] = bjs;
            // El recinto: dónde está sentado cada uno (fila 0 la de adelante, la más cerca de
            // la presidencia; columna 0 la de la izquierda vista desde la cabina) y cómo se
            // dibuja (grilla, hemiciclo o lista). Solo lo público: sin presets ni micrófonos.
            {
                const nlohmann::json r = cuerpo_->cuerpo().recinto(true);
                nlohmann::json lugares = nlohmann::json::array();
                for (const auto& l : r.value("bancas", nlohmann::json::array())) {
                    nlohmann::json x{{"banca", l.value("banca", std::string())}, {"fila", l.value("fila", 0)},
                                     {"columna", l.value("columna", 0)}};
                    const std::string id = hijoObjeto(l, "integrante").value("id", std::string());
                    if (!id.empty() && porId.contains(id)) {
                        const auto& p = porId[id];
                        for (const char* k : {"nombre", "bloque", "cargo", "presente", "lugar", "habla"})
                            if (p.contains(k)) x[k] = p[k];
                    }
                    lugares.push_back(x);
                }
                if (!lugares.empty())
                    asam["recinto"] = {{"distribucion", r.value("distribucion", std::string("grilla"))},
                                       {"filas", r.value("filas", 0)}, {"columnas", r.value("columnas", 0)},
                                       {"bancas", lugares}};
            }
            if (quorum_) {
                const nlohmann::json q = quorum_->estado();
                if (q.value("configurado", false))
                    asam["quorum"] = {{"presentes", q.value("presentes", 0)}, {"necesarios", q.value("necesarios", 0)},
                                      {"hay", q.value("hay_quorum", false)}, {"miembros", q.value("miembros", 0)}};
                nlohmann::json vots = nlohmann::json::array();
                for (const auto& o : quorum_->oficiales())
                    vots.push_back({{"asunto", o.value("asunto", std::string())},
                                    {"afirmativos", o.value("afirmativos", 0)}, {"negativos", o.value("negativos", 0)},
                                    {"abstenciones", o.value("abstenciones", 0)}, {"aprobado", o.value("aprobado", false)}});
                asam["votaciones"] = vots;
            }
            if (ordenDia_) {
                const nlohmann::json a = ordenDia_->actual();
                if (a.is_object() && a.value("n", 0) > 0)
                    asam["punto"] = {{"n", a.value("n", 0)}, {"titulo", a.value("titulo", std::string())},
                                     {"expediente", a.value("expediente", std::string())}};
            }
            sala["asamblea"] = asam;
        }
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
                                              s.reconnecting, rt_.config().encoder.videoBitrateKbps,
                                              !rt_.recordStopWanted()};
        };
        in.programaVideo = [this]() {
            const auto v = rt_.engine().programaVideo();
            return nlohmann::json{{"valido", v.valido}, {"luma", v.luma}, {"quieto_s", v.quietoSeg}};
        };
        in.diskFreeGb = [this]() {
            const QStorageInfo si(QString::fromStdWString((rt_.dataRoot() / "Recordings").wstring()));
            return si.isValid() ? static_cast<double>(si.bytesAvailable()) / 1e9 : -1.0;
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
                    if (!st.contains("gstreamer")) continue; // las de libobs: sin dato de cuadros
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
        in.loudness = [this]() { return rt_.engine().loudness().json(); };
        in.superres = [this]() { return video_ ? video_->programador().value("entradas", nlohmann::json::object()) : nlohmann::json::object(); };
        alerts_ = std::make_unique<agent::AlertEngine>(std::move(in),
                                                       (rt.dataRoot() / "Config" / "alertas.json").string());
        marca("alerts");
        alarma_ = std::make_unique<agent::AlarmaSonora>(rt.dataRoot());
        cuerpo_ = std::make_unique<asamblea::CuerpoViewModel>(rt.dataRoot());
        vocabulario_ = std::make_unique<agent::Vocabulario>(rt.dataRoot() / "Config" / "vocabulario.json");
        bus_ = std::make_unique<control::BusEventos>();
        bus_->setMediciones(control::BusEventos::medicionesDeFabrica());
        armarBitacora();
        // Con un evento en curso, lo que se graba va a su carpeta (H4).
        if (evento_) rt_.setCarpetaDelEvento([this]() { return evento_->carpetaEvento(); });
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
        settings_->setEnVivo([this]() { return modoVivo(); });
        settings_->setCupoEnCaliente([this](int n) { return cupoSala_->cambiar(n); });
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
                // ¿Había algo al aire? Si no, no hay nada que esperar.
                std::ifstream aa(ba::platform::stateDir() / "al-aire.json");
                const nlohmann::json alAire = nlohmann::json::parse(aa, nullptr, false);
                caida_["habia_aire"] = alAire.is_object() && (alAire.value("streaming", false) || alAire.value("recording", false));
                j["vista"] = true; // el cartel sale una sola vez
                f.close();
                std::ofstream o(archivo);
                o << j.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
            }
        }
        if (!configRecuperada().empty())
            QTimer::singleShot(3000, this, [this]() {
                emit notify(tr("La configuración «%1» estaba dañada: se cargó la versión anterior (la dañada quedó "
                               "como .corrupto).")
                                .arg(QString::fromStdString(configRecuperada())),
                            "warn");
            });
        connect(alerts_.get(), &agent::AlertEngine::fired, this, [this](const nlohmann::json& ev) {
            // Avisos fuera del programa (Telegram, mail, webhook) para los que miran desde lejos.
            for (const auto& n : ev.value("notificar", nlohmann::json::array()))
                if (n.is_string())
                    notificador_->enviar(n.get<std::string>(),
                                         "⚠ " + ev.value("ruleName", ba::i18n::traducir("alertas", "Disparador")),
                                         ev.value("llmText", std::string()), [this, n](bool ok, const std::string& d) {
                                             if (!ok)
                                                 emit notify(tr("El aviso por «%1» no salió: %2")
                                                                 .arg(QString::fromStdString(n.get<std::string>()),
                                                                      QString::fromStdString(d)),
                                                             "warn");
                                         });
            {
                const nlohmann::json& rid = ev["ruleId"];
                bus_->publicar("alarma/" + (rid.is_string() ? rid.get<std::string>() : rid.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)),
                               {{"nombre", ev.value("ruleName", std::string())},
                                {"tipo", ev.value("type", std::string())},
                                {"texto", ev.value("llmText", std::string())},
                                {"reconocer", ev.value("reconocer", false)},
                                {"id", ev.value("id", 0ULL)}},
                               true);
            }
            for (const auto& n : ev.value("notify", nlohmann::json::array()))
                if (n == "ui") emit notify("⚠ " + QString::fromStdString(ev.value("text", "")), "warn");
            // Una alarma silenciada sigue en la lista (con su color), pero no suena.
            if (!ev.value("silenciada", false))
                alarma_->sonar(QString::fromStdString(ev.value("sonido", std::string("ninguno"))));
        });
        connect(alerts_.get(), &agent::AlertEngine::alarmasCambio, this, [this]() {
            alarma_->criticaActiva(alarmaCritica());
            emit alarmasCambio();
        });
        alerts_->start(250);
        agent_ = std::make_unique<agent::AgentViewModel>(rt, *alerts_, "evento");
        // El de automatización: el que recibe las alertas (siempre levantado).
        agenteAuto_ = std::make_unique<agent::AgentViewModel>(rt, *alerts_, "automatizacion");
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
                                     : "La app se reinició y ya está abierto el evento «" + evento +
                                           "». Si estabas en medio de algo, seguí desde donde quedaste; si no, esperá "
                                           "lo que te pida el operador.";
                agent_->reanudarAlArrancar(p.value(0), p.value(1), p.mid(2).join('|'), seguir);
            }
        // Las reglas las cambia el motor (interfaz, MCP, plugins): la lista del
        // asistente se entera por él, sin que cada llamador se acuerde de avisar.
        connect(alerts_.get(), &agent::AlertEngine::reglasCambio, agent_.get(), &agent::AgentViewModel::alertsChanged);
        connect(alerts_.get(), &agent::AlertEngine::reglasCambio, agenteAuto_.get(), &agent::AgentViewModel::alertsChanged);
        // Los roles automaticos: un disparo que un rol activo reclama va a un
        // agente de un tiro y NO al chat del asistente interactivo.
        roles_ = std::make_unique<agent::Roles>(rt.dataRoot() / "Config" / "roles.json");
        marca("roles");
        despachador_ = std::make_unique<agent::Despachador>(*roles_, agent_->motores());
        agent_->setCorridasEnCurso([d = despachador_.get()]() { return d->enCurso(); });
        agenteAuto_->setCorridasEnCurso([d = despachador_.get()]() { return d->enCurso(); });
        marca("despachador");
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
        if (rt.config().safeMode) {
            // El launcher arranco en modo seguro tras varias caidas: que el
            // operador sepa por que no ve sus escenas.
            messages_->post(
                {{"kind", "message"},
                 {"priority", "critical"},
                 {"title", tr("Modo seguro").toStdString()},
                 {"text",
                  tr("La aplicación se cerró varias veces seguidas y arrancó en modo seguro: tus escenas no "
                     "se cargaron (no se borraron). Cerrá y volvé a abrir para intentar el arranque normal.")
                      .toStdString()}});
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
        CatalogoVisible::Fuentes f;
        f.escenas = [this]() {
            QStringList out;
            for (const std::string& n : rt_.orderedSceneNames()) out << QString::fromStdString(n);
            return out;
        };
        f.multiviews = [this]() { return multiviews(); };
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
        connect(this, &StudioViewModel::multiviewChanged, catalogo_.get(), refrescar);
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
            if (!c.saltear()) ventanas_->migrarDesde(rt.dataRoot() / "Config" / "ui.json", multiviewActual());
        }
        connect(ventanas_.get(), &VentanasModel::cambio, this, &StudioViewModel::multiviewVentanasChanged);
        connect(ventanas_.get(), &QAbstractItemModel::dataChanged, this, &StudioViewModel::multiviewVentanasChanged);
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
    // Las bancas presenciales de la fila de oradores (asamblea/Bancas).
    {
        asamblea::Bancas::Ganchos g;
        // La regla del interlock es la de ControlCamaras (respeta su ajuste), no solo «está al aire».
        g.camaraAlAire = [this](const std::string& c) {
            return !camaras_->chequearMovimiento(QString::fromStdString(c)).isEmpty();
        };
        g.irAPosicion = [this](const std::string& c, const std::string& p) {
            const QVariantMap r = camaras_->irAPosicion(QString::fromStdString(c), QString::fromStdString(p), -1);
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
        g.armar = [this](const std::string& e) { conmutador_->armar(e); };
        g.cortar = [this](const std::string& e) { conmutador_->poner(e); };
        g.microfono = [this](const std::string& f, bool abierto) { rt_.engine().setSourceMuted(f, !abierto); };
        g.microfonoBanca = [this](const std::string& banca, bool abierto, bool terminar) {
            // Por la central solo si está conectada y la banca tiene su unidad; si
            // no, sigue valiendo la fuente de audio de la banca (la de arriba).
            if (!dcn_ || !dcn_->conectada() || !dcn_->tieneUnidad(banca)) return false;
            QPointer<StudioViewModel> yo(this);
            dcn_->control("microfono", {{"banca", banca}, {"abierto", abierto}, {"quitar", terminar}, {"quien", "fila de oradores"}},
                          [yo, banca](bool ok, const nlohmann::json&, const std::string& e) {
                              if (!ok && yo)
                    emit yo->notify(tr("Central DCN, banca %1: %2").arg(QString::fromStdString(banca), QString::fromStdString(e)),
                                    "error");
                          });
            return true;
        };
        // La fila única: los que hablan desde el navegador (sesión remota) también.
        g.esRemoto = [this](const std::string& id) {
            if (!sesionRemota_) return false;
            // El que tiene la palabra remota sigue siendo remoto aunque se le haya
            // cortado la red: si no, terminar no le soltaba los medios.
            if (sesionRemota_->tienePalabra(id)) return true;
            const auto v = sesionRemota_->presentesIds();
            return std::find(v.begin(), v.end(), id) != v.end();
        };
        g.palabraRemota = [this](const std::string& id, bool palabra, int segundos) {
            if (sesionRemota_) sesionRemota_->darPalabra(id, palabra, segundos);
        };
        g.filaCambio = [this](const std::vector<std::string>& cola, const std::string& alAire) {
            if (sesionRemota_) sesionRemota_->fila(cola, alAire);
        };
        // La fuente «Banca con la palabra»: si la toma es digital y hay escenas con la
        // fuente, ella la recorta de la toma entera (no se mueve nada).
        g.laEncuadraLaFuente = [this](const std::string& c, const std::string& p) {
            if (!rt_.engine().hayFuenteBanca()) return false;
            const QVariantMap d = camaras_->datosPosicion(QString::fromStdString(c), QString::fromStdString(p));
            return d.value("existe").toBool() && d.value("digital").toBool() && !d.value("motorizada").toBool();
        };
        g.escenaDeRanura = [this](int n) { return rt_.engine().escenaDeRanura(n); };
        g.posicionesConNombre = [this](const std::string& nombre) {
            std::vector<std::pair<std::string, std::string>> out;
            for (const QVariant& v : camaras_->buscarPosicion(QString::fromStdString(nombre))) {
                const QVariantMap m = v.toMap();
                out.emplace_back(m.value("entrada").toString().toStdString(), m.value("nombre").toString().toStdString());
            }
            return out;
        };
        bancas_ = std::make_unique<asamblea::Bancas>(cuerpo_->cuerpo(), g);
        // El debate entre dos: reusa lo de la fila (micrófonos, cámaras, escenas, remotos).
        {
            asamblea::Debate::Ganchos gd;
            gd.esRemoto = g.esRemoto;
            gd.palabraRemota = g.palabraRemota;
            gd.canalRemoto = [this](const std::string& id) {
                if (!sesionRemota_) return 0;
                const auto v = sesionRemota_->conPalabraTodos();
                const auto it = std::find(v.begin(), v.end(), id);
                return it == v.end() ? 0 : static_cast<int>(it - v.begin()) + 1;
            };
            gd.microfono = g.microfono;
            gd.microfonoBanca = g.microfonoBanca;
            // Cada lado lista: lo que la fuente de banca recorta no se mueve (dos bancas de la
            // misma cámara digital se ven a la vez); una motorizada, sí.
            gd.apuntar = [this](const std::string& id) { return bancas_->prepararToma(QString::fromStdString(id)).toStdString(); };
            gd.armar = g.armar;
            gd.cortar = g.cortar;
            gd.terminarOrador = [this]() { bancas_->terminar(); };
            gd.central = [this](bool debate) {
                if (!dcn_ || !dcn_->conectada()) return;
                QPointer<StudioViewModel> yo(this);
                if (debate) {
                    // Lo que tenía, para devolvérselo; después, operador con dos abiertos.
                    dcn_->control("ajustes", nlohmann::json::object(), [yo](bool ok, const nlohmann::json& r, const std::string& e) {
                        if (!yo) return;
                        if (!ok) {
                            emit yo->notify(tr("Central (debate): %1").arg(QString::fromStdString(e)), "error");
                            return;
                        }
                        yo->centralAntesDelDebate_ = {{"modo", r.value("modo", std::string("operador"))},
                                                      {"max_abiertos", r.value("max_abiertos", 4)}};
                        yo->dcn_->control("ajustes", {{"modo", "operador"}, {"max_abiertos", 2}},
                                          [yo](bool ok2, const nlohmann::json&, const std::string& e2) {
                                              if (yo && !ok2)
                                                  emit yo->notify(tr("Central (debate): %1").arg(QString::fromStdString(e2)), "error");
                                          });
                    });
                } else if (centralAntesDelDebate_.is_object() && !centralAntesDelDebate_.empty()) {
                    dcn_->control("ajustes", centralAntesDelDebate_, [yo](bool ok, const nlohmann::json&, const std::string& e) {
                        if (yo && !ok) emit yo->notify(tr("Central (fin del debate): %1").arg(QString::fromStdString(e)), "error");
                    });
                    centralAntesDelDebate_ = nlohmann::json();
                }
            };
            gd.avisarRemoto = [this](const std::string& id, const nlohmann::json& m) {
                if (sesionRemota_) sesionRemota_->mandar(id, m);
            };
            gd.avisoRemoto = [this](const std::string& id, const std::string& a) {
                if (sesionRemota_) sesionRemota_->avisoTiempo(id, a);
            };
            gd.avisoCentral = [this](const std::string& banca, const std::string& a) {
                if (dcn_ && dcn_->conectada() && dcn_->tieneUnidad(banca))
                    dcn_->control("tiempo", {{"banca", banca}, {"aviso", a}}, [](bool, const nlohmann::json&, const std::string&) {});
            };
            debate_ = std::make_unique<asamblea::Debate>(cuerpo_->cuerpo(), rt_.dataRoot(), gd);
            connect(debate_.get(), &asamblea::Debate::aviso, this, [this](const QString& t, const QString& n) { emit notify(t, n); });
        }
        // La foto de cada integrante, capturada con la cámara de su banca.
        {
            asamblea::CuerpoViewModel::Ganchos gc;
            gc.apuntar = [this](const QString& banca) { return bancas_->apuntar(banca); };
            gc.capturar = [this](const QString& camara, std::function<void(QImage)> listo) {
                // Sin esperar el cuadro acá (capturarAparte): vuelve a este hilo cuando llega.
                rt_.engine().capturarAparte(
                    camara.toStdString(), 960,
                    [listo = std::move(listo)](const std::string& e, std::vector<std::uint8_t> rgba, int w, int h) mutable {
                        QImage img;
                        if (e.empty() && w > 0 && h > 0) img = QImage(rgba.data(), w, h, w * 4, QImage::Format_RGBA8888).copy();
                        QMetaObject::invokeMethod(qApp, [listo = std::move(listo), img]() { listo(img); }, Qt::QueuedConnection);
                    });
            };
            cuerpo_->setGanchos(std::move(gc));
        }
        seguirMic_ = std::make_unique<asamblea::SeguirMicrofono>(rt.dataRoot(), *bus_, cuerpo_->cuerpo(), *bancas_);
        ordenDia_ = std::make_unique<asamblea::OrdenDelDia>(rt.dataRoot(), *bus_);
        // La banca virtual (sesión remota, en la sala o afuera): docs/funciones/SESION-REMOTA.md.
        sesionRemota_ = std::make_unique<asamblea::SesionRemota>(cuerpo_->cuerpo(), rt.dataRoot());
        // Los orígenes de los links de integrantes y presidencia: los de sala/Direcciones (la
        // red local para los de la sala; el túnel, aparte).
        {
            const auto origenes = [this]() {
                sesionRemota_->setOrigenes(direcciones_->origen("sesion"), direcciones_->origen("sesion", true),
                                           direcciones_->motivo("sesion"));
            };
            connect(direcciones_.get(), &sala::Direcciones::cambio, sesionRemota_.get(), origenes);
            origenes();
        }
        // Los presentes con voto salen del padrón del evento abierto (o de la central),
        // más los habilitados por la banca virtual, sin contar a nadie dos veces.
        quorum_ = std::make_unique<asamblea::Quorum>(rt.dataRoot(), [this]() {
            const std::vector<std::string> virtuales = sesionRemota_ ? sesionRemota_->presentesBancas() : std::vector<std::string>{};
            // Si se eligió, los presentes los dice la asistencia de la central de conferencia.
            if (dcn_ && dcn_->presentesDeLaCentral())
                if (const int p = dcn_->presentesCentral(); p >= 0) {
                    // Los que la central ya cuenta presentes en su banca no se suman.
                    const nlohmann::json porBanca =
                        hijoObjeto(hijoObjeto(dcn_->estado(), "asistencia"), "por_banca");
                    int extra = 0;
                    for (const auto& b : virtuales)
                        if (!porBanca.value(b, false)) ++extra;
                    return p + extra;
                }
            auto* ev = static_cast<EventoViewModel*>(evento());
            const int virt = sesionRemota_ ? sesionRemota_->presentes() : 0;
            if (ev == nullptr) return virt;
            const QVariantMap r = ev->property("resumenPadron").toMap();
            return (r.value("conVoto").toInt() > 0 ? r.value("votoPresente").toInt() : r.value("presentes").toInt()) + virt;
        });
        if (auto* ev = static_cast<EventoViewModel*>(evento())) {
            ev->setAnexoActa([this]() {
                QString t = QString::fromStdString(quorum_->anexoActa() + ordenDia_->anexoActa(remotosPalabra()));
                if (sesionRemota_) {
                    const std::string sr = sesionRemota_->anexoActa();
                    if (!sr.empty()) t += tr("## Banca virtual") + QString::fromStdString("\n\n```\n" + sr + "```\n\n");
                }
                // La transcripción de la sesión, con la huella de cada archivo (se puede verificar después).
                const QStringList archivos = flujos_ ? flujos_->archivosDeSesion() : QStringList{};
                if (!archivos.isEmpty()) {
                    t += tr("## Transcripción") + "\n\n| " + tr("Archivo") + " | SHA-256 |\n|---|---|\n";
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
                    t += "\n_" + tr("La transcripción es automática: puede tener errores; la versión taquigráfica es la oficial.") +
                         "_\n\n";
                }
                return t;
            });
        }
    }
    marca("camaras");
    connect(inputs_.get(), &InputsViewModel::changed, camaras_.get(), &ptz::ControlCamaras::sincronizar);
    // El grafo de tarjetas de video de cada entrada (después del PTZ).
    video_ = std::make_unique<video::VideoGrafos>(rt, rt.dataRoot());
    video_->setLegalizar(rt.config().legalizar);
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
    // La central de conferencia (plugin bosch-dcn): su estado sale del bus.
    dcn_ = std::make_unique<asamblea::ConferenciaDcn>([this]() { return plugins_.get(); }, *bus_, rt.dataRoot());
    // Las votaciones (central, celulares, Quórum) son del modelo.
    if (sesionRemota_ && quorum_)
        votaciones_ = std::make_unique<asamblea::Votaciones>(*sesionRemota_, *dcn_, *quorum_, *bus_, this);
    bus_->suscribir("#", [this](const std::string& t, const nlohmann::json& d) {
        if (plugins_ && t.rfind("plugin/", 0) != 0) plugins_->eventoApp(t, d);
    });
    // La botonera: el modelo, los permisos y la ejecución viven acá; el gateway
    // es solo el borde de red (TLS, tokens, WebSocket de las tablets).
    {
        control::Botonera::Ganchos g;
        g.escenas = [this]() { return rt_.engine().sceneNames(); };
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
            for (const auto& i : cuerpo_->cuerpo().integrantes())
                if (!i.value("banca", std::string()).empty())
                    out.emplace_back(i.value("banca", std::string()), i.value("nombre", std::string()));
            return out;
        };
        g.recinto = [this]() { return cuerpo_->cuerpo().recinto(); };
        // La cara de un botón: la foto del integrante (subida o capturada con la
        // cámara de su banca) o la foto de una posición PTZ, en 96 px.
        g.imagen = [this](const std::string& ref) -> std::string {
            QString ruta;
            const QString r = QString::fromStdString(ref);
            if (r.startsWith("medio:")) {
                ruta = QString::fromStdString(carasBotonera_->rutaPoster(ref.substr(6)));
            } else if (r.startsWith("cuerpo:")) {
                ruta = cuerpo_->rutaFoto(r.mid(7));
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
            if (dcn_)
                for (const auto& u : dcn_->estado().value("unidades", nlohmann::json::array()))
                    if (u.value("tipo", std::string()) == "presidente" && !u.value("banca", std::string()).empty())
                        out.insert(u.value("banca", std::string()));
            return out;
        };
        carasBotonera_ = std::make_unique<control::CarasBotonera>(rt, rt.dataRoot());
        botonera_ = std::make_unique<control::Botonera>(rt.dataRoot(), *bus_, std::move(g));
        // El Cuerpo cambió (nombres, fotos, unidades de la central): al bus, así se
        // redibuja la botonera y el plugin de la central relee su mapa.
        connect(cuerpo_.get(), &asamblea::CuerpoViewModel::cambio, this, [this]() {
            bus_->publicar("cuerpo/cambio", {{"ms", QDateTime::currentMSecsSinceEpoch()}}, true);
        });
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
            escenas::ControlGrillas::Partes{*cli, rt_.engine(), sesionRemota_.get(), cuerpo_ ? &cuerpo_->cuerpo() : nullptr},
            this);
        // La banca virtual: el gateway autentica y avisa; decide asamblea::Presidencia (con
        // las consolas de la presidencia y la prioridad del presidente). Acá solo se muestra.
        if (sesionRemota_) {
            connect(sesionRemota_.get(), &asamblea::SesionRemota::aviso, this,
                    [this](const QString& t, const QString& nivel) { emit notify(t, nivel); });
            connect(sesionRemota_.get(), &asamblea::SesionRemota::cambio, this, [this]() {
                if (quorum_) emit quorum_->cambio();
            });
            asamblea::Presidencia::Partes pp{.sesion = *sesionRemota_,
                                             .bancas = *bancas_,
                                             .debate = debate_.get(),
                                             .dcn = *dcn_,
                                             .votaciones = votaciones_.get(),
                                             .cuerpo = *cuerpo_,
                                             .quorum = quorum_.get(),
                                             .ordenDia = ordenDia_.get(),
                                             .sala = *cli,
                                             .bus = *bus_,
                                             .motor = rt_.engine(),
                                             .microfonoOperador = [this]() { return !conference_->operatorAudio().isEmpty(); }};
            presidencia_ = std::make_unique<asamblea::Presidencia>(std::move(pp), this);
            connect(presidencia_.get(), &asamblea::Presidencia::aviso, this,
                    [this](const QString& t, const QString& nivel) { emit notify(t, nivel); });
            connect(presidencia_.get(), &asamblea::Presidencia::presidenciaHablaCambio, this,
                    &StudioViewModel::presidenciaHablaChanged);
        }
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
    // La fuente «Banca con la palabra»: lo que muestra cada ranura sigue a la fila y al debate.
    if (bancas_) {
        ranurasTarde_.setSingleShot(true);
        ranurasTarde_.setInterval(1500);
        connect(&ranurasTarde_, &QTimer::timeout, this, [this]() { actualizarRanurasBanca(true); });
        const auto ranuras = [this]() {
            actualizarRanurasBanca();
            ranurasTarde_.start();
        };
        connect(bancas_.get(), &asamblea::Bancas::cambio, this, ranuras);
        if (debate_) connect(debate_.get(), &asamblea::Debate::cambio, this, ranuras);
        if (cuerpo_) connect(cuerpo_.get(), &asamblea::CuerpoViewModel::cambio, this, ranuras);
        if (camaras_) connect(camaras_.get(), &ptz::ControlCamaras::cambio, this, ranuras);
        QTimer::singleShot(0, this, ranuras);
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
    // Tras una caída: la placa o el negro vuelven como estaban.
    QTimer::singleShot(ba::ajustes::entero("arranque.restaurarPlacaMs", 1500), this, [this]() {
        restaurarPlaca();
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
    // Un sello a medias no se escribe: se corta y se vuelve a sellar otro día.
    cancelarSellos_ = true;
    for (auto& s : selladores_)
        if (s.hilo.joinable()) s.hilo.join();
    if (bitacoraOyente_ != 0) ba::diag::registro().quitarOyente(bitacoraOyente_);
    if (bitacoraBus_ != 0 && bus_) bus_->desuscribir(bitacoraBus_);
    if (bitacora_) {
        revisarConfig({}, "");
        bitacora_->anotar("evento", "app", ba::i18n::traducir("historico", "la app se cierra"));
        bitacora_->vaciar();
    }
}

void StudioViewModel::revisarConfig(const std::string& carpeta, const std::string& momento) {
    if (!fotos_ || !evento_) return;
    const auto dir = evento_->carpetaEvento(carpeta);
    if (dir.empty()) return;
    const auto ahoraMs = QDateTime::currentMSecsSinceEpoch();
    nlohmann::json f;
    try {
        f = fotos_->foto(ahoraMs);
    } catch (const std::exception&) {
        return;
    }
    const auto cfg = dir / "config";
    nlohmann::json antes = fotoUltima_;
    if (antes.is_null()) antes = evento::FotosConfig::cargar(cfg / "ultima.json");
    if (!antes.is_null() && antes.value("archivos", nlohmann::json::object()) == f.value("archivos", nlohmann::json::object())) {
        fotoUltima_ = f;
        std::error_code ec;
        if (momento == "inicio" && !std::filesystem::exists(cfg / "inicio.json", ec))
            evento::FotosConfig::guardar(cfg / "inicio.json", f);
        if (momento == "cierre") evento::FotosConfig::guardar(cfg / "cierre.json", f);
        return;
    }
    if (!antes.is_null() && bitacora_) {
        // Si es de otro evento (el que se cierra), la línea va a SU bitácora:
        // la bitácora ya puede estar apuntando al nuevo. Se escribe directo.
        const bool otro = !carpeta.empty();
        for (const auto& c : fotos_->comparar(antes, f)) {
            const std::string texto =
                antes.value("t", 0LL) != 0 && momento == "inicio"
                    ? ba::i18n::llenar(ba::i18n::traducir("historico", "%1 %2 (con la app cerrada)"),
                                       {c.value("archivo", std::string()), c.value("que", std::string())})
                    : c.value("archivo", std::string()) + " " + c.value("que", std::string());
            if (otro) {
                evento::Bitacora b;
                b.setArchivo(dir / "bitacora.jsonl");
                b.anotar("config", c.value("archivo", std::string()), texto, c, ahoraMs);
                b.vaciar();
            } else {
                bitacora_->anotar("config", c.value("archivo", std::string()), texto, c, ahoraMs);
            }
        }
    }
    fotoUltima_ = f;
    evento::FotosConfig::guardar(cfg / "ultima.json", f);
    std::error_code ec;
    if (momento == "inicio" && !std::filesystem::exists(cfg / "inicio.json", ec))
        evento::FotosConfig::guardar(cfg / "inicio.json", f);
    if (momento == "cierre") evento::FotosConfig::guardar(cfg / "cierre.json", f);
}

namespace {
nlohmann::json leerLista(const std::filesystem::path& p) {
    std::ifstream f(p);
    nlohmann::json j = f ? nlohmann::json::parse(f, nullptr, false) : nlohmann::json();
    return j.is_array() ? j : nlohmann::json::array();
}
void guardarLista(const std::filesystem::path& p, const nlohmann::json& j) {
    const auto tmp = std::filesystem::path(p.string() + ".tmp");
    {
        std::ofstream o(tmp);
        o << j.dump(1, '\t', false, nlohmann::json::error_handler_t::replace);
    }
    std::error_code ec;
    std::filesystem::rename(tmp, p, ec);
}
} // namespace

void StudioViewModel::indexarSalida(const std::string& tema, const nlohmann::json& valor) {
    if (!evento_) return;
    const auto dir = evento_->carpetaEvento();
    if (dir.empty()) return;
    const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
    if (tema == "salida/grabacion") {
        const auto p = dir / "grabaciones.json";
        nlohmann::json l = leerLista(p);
        if (valor.value("activo", false)) {
            nlohmann::json g{{"archivo", rt_.engine().archivoGrabacion()}, {"desde_ms", ahora}};
            if (!rt_.engine().archivoCopiaGrabacion().empty()) g["copia"] = rt_.engine().archivoCopiaGrabacion();
            l.push_back(g);
        } else {
            for (auto it = l.rbegin(); it != l.rend(); ++it) {
                if (it->contains("hasta_ms")) continue;
                (*it)["hasta_ms"] = ahora;
                // Si se pasó a MP4 al terminar, el archivo es otro.
                std::filesystem::path a = it->value("archivo", std::string());
                const std::string final = rt_.engine().archivoGrabacion();
                if (!final.empty()) a = final;
                (*it)["archivo"] = a.string();
                std::error_code ec;
                const auto bytes = std::filesystem::file_size(a, ec);
                if (!ec) (*it)["bytes"] = bytes;
                break;
            }
        }
        guardarLista(p, l);
        return;
    }
    const auto p = dir / "transmisiones.json";
    nlohmann::json l = leerLista(p);
    auto abierta = l.end();
    for (auto it = l.begin(); it != l.end(); ++it)
        if (!it->contains("hasta_ms")) abierta = it;
    if (tema == "salida/vivo") {
        if (abierta == l.end()) {
            l.push_back({{"desde_ms", ahora}, {"caidas", 0}});
            abierta = l.end() - 1;
        }
        for (auto it = valor.begin(); it != valor.end(); ++it) (*abierta)[it.key()] = it.value();
    } else if (valor.value("activo", false)) {
        if (abierta == l.end()) {
            l.push_back({{"desde_ms", ahora}, {"caidas", 0}});
        } else if (valor.value("reconectando", false)) {
            (*abierta)["caidas"] = abierta->value("caidas", 0) + 1;
        }
    } else if (abierta != l.end()) {
        (*abierta)["hasta_ms"] = ahora;
        if (!valor.value("problema", std::string()).empty()) (*abierta)["problema"] = valor["problema"];
    }
    guardarLista(p, l);
}

void StudioViewModel::cerrarEnHistorico(const std::string& carpeta) {
    if (!evento_) return;
    const auto dir = evento_->carpetaEvento(carpeta);
    if (dir.empty()) return;
    const evento::DatosDelEvento d = evento_->datosEvento(carpeta);
    // Hasta cuándo: si ya terminó (el cierre que quedó pendiente de un día que
    // cambió con la app cerrada), su fin; si no, ahora.
    const qint64 ahora = d.terminadoMs != 0 && d.estado == "terminado" ? d.terminadoMs : QDateTime::currentMSecsSinceEpoch();
    // Sin inicio conocido, el día del evento (lo que haya en los logs de hoy).
    std::int64_t desde = d.iniciadoMs;
    if (desde == 0) desde = QDateTime(QDate::currentDate(), QTime(0, 0)).toMSecsSinceEpoch();
    const int asamblea = evento::CierreEvento::copiarAsamblea(rt_.dataRoot() / "Config", dir);
    const nlohmann::json registro =
        evento::CierreEvento::recortarRegistros(ba::platform::dataRoot() / "Logs", desde, ahora, dir / "registro");
    {
        evento::Bitacora b;
        b.setArchivo(dir / "bitacora.jsonl");
        b.anotar("evento", "historico", "cerrado", {{"asamblea", asamblea}, {"registro", registro}}, ahora);
        b.vaciar();
    }
    // Ya terminados los hilos viejos, se limpian.
    for (auto it = selladores_.begin(); it != selladores_.end();) {
        if (it->listo->load()) {
            it->hilo.join();
            it = selladores_.erase(it);
        } else {
            ++it;
        }
    }
    auto listo = std::make_shared<std::atomic<bool>>(false);
    selladores_.push_back({std::thread([this, dir, ahora, listo]() {
        const nlohmann::json s = evento::CierreEvento::sellar(dir, ahora, &cancelarSellos_);
        listo->store(true);
        if (!s.is_null())
            ba::diag::registro().linea(ba::diag::Nivel::Info, "historico", 0,
                                       ba::i18n::llenar(ba::i18n::traducir("historico", "sellado %1 (%2 archivos)"),
                                                        {dir.filename().string(),
                                                         std::to_string(s.value("archivos", nlohmann::json::object()).size())}));
    }), listo});
}

// LA BITÁCORA DEL EVENTO EN CURSO (H2 de docs/planes/PLAN-EVENTOS-HISTORICO.md): se
// cuelga de lo que ya pasa por dos lugares —el registro general (MCP, botonera,
// guion, avisos) y el bus de eventos (aire, salidas, oradores, asamblea)— sin
// tocar cada módulo.
void StudioViewModel::armarBitacora() {
    if (!evento_ || !bus_) return;
    bitacora_ = std::make_unique<evento::Bitacora>();
    bitacora_->setLimpiador([](const nlohmann::json& j) { return mcp::Traza::limpio(j); });
    const nlohmann::json aj = evento_->ajustesHistorico();
    const nlohmann::json reg = aj.value("registrar", nlohmann::json::object());
    for (const std::string& c : evento::Bitacora::categorias())
        bitacora_->setRegistrar(c, !reg.is_object() || reg.value(c, true));
    // Lo que se repite muchas veces por segundo no es un hecho: es una medición.
    busExcluir_ = std::make_shared<std::vector<std::string>>(control::BusEventos::medicionesDeFabrica());
    if (aj.contains("bus_excluir") && aj["bus_excluir"].is_array()) {
        busExcluir_->clear();
        for (const auto& p : aj["bus_excluir"])
            if (p.is_string()) busExcluir_->push_back(p.get<std::string>());
    }
    auto excluir = busExcluir_;
    // La página Histórico y el MCP cambian qué se registra, en vivo.
    historico_ = std::make_unique<HistoricoViewModel>(*evento_);
    historico_->setCarpetasGrabacion([this]() { return std::vector<std::filesystem::path>{rt_.config().rec.directory}; });
    historico_->setAplicarAjustes([this](const nlohmann::json& a) {
        const nlohmann::json reg = a.value("registrar", nlohmann::json::object());
        for (const auto& [k, v] : reg.items())
            bitacora_->setRegistrar(k, v.get<bool>());
        std::vector<std::string> l;
        for (const auto& p : a.value("bus_excluir", nlohmann::json::array()))
            if (p.is_string()) l.push_back(p.get<std::string>());
        *busExcluir_ = l; // solo lo toca el hilo de la interfaz, igual que el oyente del bus
    });
    bitacora_->setArchivo(evento_->archivoBitacora());
    bitacora_->anotar("evento", "app", ba::i18n::traducir("historico", "la app arranca"),
                      {{"evento", evento_->enCurso().value("carpeta").toString().toStdString()}});
    // La configuración: lo que cambió con la app cerrada se ve al arrancar
    // (contra la última foto del evento), y después, cada 10 s.
    fotos_ = std::make_unique<evento::FotosConfig>(rt_.dataRoot() / "Config", evento_->raizEventos() / ".objetos");
    revisarConfig({}, "inicio");
    // LO QUE QUEDÓ SIN CERRAR: si el día cambió con la app cerrada, la jornada de
    // ayer se terminó al arrancar (antes de que hubiera bitácora) y no recibió su
    // cierre. Todo evento terminado en la última semana, con bitácora y sin
    // sellos, lo recibe ahora (su foto de cierre es la última que tuvo).
    {
        const qint64 semana = QDateTime::currentMSecsSinceEpoch() - 7LL * 24 * 3600 * 1000;
        const std::string enCursoAhora = evento_->enCurso().value("carpeta").toString().toStdString();
        for (const QVariant& v : evento_->eventos()) {
            const std::string c = v.toMap().value("carpeta").toString().toStdString();
            if (c.empty() || c == enCursoAhora) continue;
            const auto d = evento_->datosEvento(c);
            const auto dir = evento_->carpetaEvento(c);
            std::error_code ec;
            if (d.estado != "terminado" || d.terminadoMs < semana || dir.empty() ||
                !std::filesystem::exists(dir / "bitacora.jsonl", ec) || std::filesystem::exists(dir / "sellos.json", ec))
                continue;
            if (!std::filesystem::exists(dir / "config" / "cierre.json", ec))
                std::filesystem::copy_file(dir / "config" / "ultima.json", dir / "config" / "cierre.json", ec);
            ba::diag::registro().linea(ba::diag::Nivel::Info, "historico", 0,
                                       ba::i18n::llenar(ba::i18n::traducir("historico", "cierre pendiente de «%1»"), {c}));
            cerrarEnHistorico(c);
        }
    }
    // CADA HORA (y al minuto de arrancar): el disco de Eventos/ y la retención.
    {
        auto* tDisco = new QTimer(this);
        const auto revisar = [this]() {
            if (!historico_) return;
            const nlohmann::json d = historico_->disco();
            if (d.value("lleno", false)) {
                const QString t = tr("El disco de los eventos está al %1 % (quedan %2 GB)")
                                      .arg(d.value("usado_pct", 0))
                                      .arg(d.value("libre_gb", 0.0));
                ba::diag::registro().linea(ba::diag::Nivel::Aviso, "historico", 0, t.toStdString());
                emit notify(t, "warn");
            }
            const nlohmann::json r = historico_->retencion(false);
            if (!r.value("eventos", nlohmann::json::array()).empty())
                ba::diag::registro().linea(ba::diag::Nivel::Info, "historico", 0,
                                           ba::i18n::llenar(ba::i18n::traducir("historico",
                                                                               "retención: grabaciones borradas de %1 eventos (%2 MB)"),
                                                            {std::to_string(r["eventos"].size()),
                                                             std::to_string(r.value("bytes", 0ULL) / 1000000)}));
        };
        QTimer::singleShot(ba::ajustes::sAMs("historico.primeraRevisionS", 60), this, revisar);
        connect(tDisco, &QTimer::timeout, this, revisar);
        tDisco->start(std::max(60000, ba::ajustes::entero("historico.revisarCadaMin", 60) * 60 * 1000));
    }
    auto* tFotos = new QTimer(this);
    tFotos->setInterval(10000);
    connect(tFotos, &QTimer::timeout, this, [this]() { revisarConfig({}, ""); });
    tFotos->start();

    auto* b = bitacora_.get();
    // Del registro: cualquier hilo. Lo mismo repetido en 10 s se anota una vez
    // (un aviso que se dispara por cuadro no llena el disco).
    struct Repetidos {
        std::mutex mu;
        std::map<std::string, std::int64_t> visto;
    };
    auto rep = std::make_shared<Repetidos>();
    bitacoraOyente_ = ba::diag::registro().agregarOyente([b, rep](const ba::diag::Registrado& r) {
        std::string tipo;
        if (r.componente == "mcp")
            tipo = "mcp";
        else if (r.componente == "botonera")
            tipo = "botonera";
        else if (r.componente == "guion")
            tipo = "guion";
        else if (r.nivel >= ba::diag::Nivel::Aviso)
            tipo = "aviso";
        else
            return;
        if (r.nivel < ba::diag::Nivel::Info) return;
        // Qt avisa que se perdió un refresco cada vez que la pantalla se
        // bloquea o se duerme: no es un hecho del evento.
        if (r.componente == "qt" && r.texto.find("display link") != std::string::npos) return;
        {
            std::lock_guard<std::mutex> g(rep->mu);
            const std::string clave = r.componente + "|" + r.texto;
            auto it = rep->visto.find(clave);
            if (it != rep->visto.end() && r.ms - it->second < 10000) return;
            rep->visto[clave] = r.ms;
            if (rep->visto.size() > 2000) rep->visto.clear();
        }
        nlohmann::json d = nlohmann::json::object();
        if (r.nivel >= ba::diag::Nivel::Aviso) d["nivel"] = ba::diag::nombreDeNivel(r.nivel);
        if (r.codigo != 0) d["codigo"] = r.codigo;
        b->anotar(tipo, r.componente, r.texto, d, r.ms);
    });
    // Las salidas se publican cada segundo (cambia el tiempo y los kbps): de
    // esas solo importa cuando cambian estos campos.
    auto importa = std::make_shared<std::map<std::string, nlohmann::json>>();
    bitacoraBus_ = bus_->suscribir("#", [this, b, excluir, importa](const std::string& tema, const nlohmann::json& valor) {
        for (const std::string& p : *excluir)
            if (control::BusEventos::coincide(p, tema)) return;
        if (tema == "salida/stream" || tema == "salida/grabacion") {
            nlohmann::json clave = nlohmann::json::object();
            for (const char* k : {"activo", "reconectando", "problema"})
                if (valor.is_object() && valor.contains(k)) clave[k] = valor[k];
            auto& antes = (*importa)[tema];
            if (antes == clave) return;
            antes = clave;
            nlohmann::json v = valor;
            if (tema == "salida/grabacion" && v.value("activo", false)) v["archivo"] = rt_.engine().archivoGrabacion();
            b->anotar("bus", tema, {}, v);
            indexarSalida(tema, valor);
            return;
        }
        if (tema == "salida/vivo") indexarSalida(tema, valor);
        b->anotar("bus", tema, {}, valor);
    });
    // Cambia el evento en curso: lo que sigue va a su carpeta.
    auto anterior = std::make_shared<std::string>(evento_->enCurso().value("carpeta").toString().toStdString());
    connect(evento_.get(), &EventoViewModel::enCursoChanged, this, [this, anterior]() {
        const std::string ahora = evento_->enCurso().value("carpeta").toString().toStdString();
        if (ahora == *anterior) return;
        if (!anterior->empty()) {
            revisarConfig(*anterior, "cierre");
            bitacora_->anotar("evento", "historico", ba::i18n::traducir("historico", "deja de estar en curso"),
                              {{"siguiente", ahora}});
            bitacora_->vaciar();
            cerrarEnHistorico(*anterior);
        }
        bitacora_->setArchivo(evento_->archivoBitacora());
        if (!ahora.empty()) {
            bitacora_->anotar("evento", "historico", ba::i18n::traducir("historico", "en curso"), {{"anterior", *anterior}});
            fotoUltima_ = nullptr; // el evento nuevo compara contra su propia última foto
            revisarConfig({}, "inicio");
        }
        *anterior = ahora;
    });
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

void StudioViewModel::tiemposCentral() {
    // El tiempo del que tiene la palabra (presencial o remoto), una vez por segundo:
    // a un minuto y al terminar, aviso al operador, en el celular del remoto y en la
    // unidad de su banca si está la central (Bosch DCN), que además lleva el reloj.
    if (++dcnTic_ % 10 != 0 || !bancas_) return;
    const nlohmann::json j = bancas_->estadoJson();
    const nlohmann::json al = j.value("al_aire", nlohmann::json());
    if (!al.is_object()) {
        dcnOrador_.clear();
        return;
    }
    const std::string id = al.value("id", std::string()), banca = al.value("banca", std::string());
    const std::string nombre = al.value("nombre", std::string());
    const int asignado = j.value("asignado_s", 0), usado = static_cast<int>(j.value("usado_s", 0LL));
    if (asignado <= 0) return;
    const bool remoto = sesionRemota_ && sesionRemota_->tienePalabra(id);
    const bool central = !remoto && dcn_ && dcn_->conectada() && !banca.empty() && dcn_->tieneUnidad(banca);
    const std::string clave = id + "@" + std::to_string(j.value("desde_ms", 0LL));
    const auto ignorar = [](bool, const nlohmann::json&, const std::string&) {};
    if (clave != dcnOrador_) {
        dcnOrador_ = clave;
        dcnAvisos_ = 0;
        if (central) dcn_->control("tiempo", {{"limite_min", (asignado + 59) / 60}, {"reloj", true}}, ignorar);
    }
    const int restante = asignado - usado;
    const QString quien = banca.empty() ? QString::fromStdString(nombre)
                                        : tr("%1 (banca %2)").arg(QString::fromStdString(nombre), QString::fromStdString(banca));
    if (restante <= 60 && restante > 0 && !(dcnAvisos_ & 1)) {
        dcnAvisos_ |= 1;
        emit notify(tr("%1: queda 1 minuto").arg(quien), "warn");
        if (remoto) sesionRemota_->avisoTiempo(id, "ultimo_minuto");
        if (central) dcn_->control("tiempo", {{"banca", banca}, {"aviso", "ultimo_minuto"}}, ignorar);
        if (bus_) bus_->publicar("oradores/tiempo", {{"id", id}, {"banca", banca}, {"aviso", "ultimo_minuto"}});
    }
    if (restante <= 0 && !(dcnAvisos_ & 2)) {
        dcnAvisos_ |= 3;
        emit notify(tr("%1: se terminó su tiempo").arg(quien), "error");
        if (remoto) sesionRemota_->avisoTiempo(id, "terminado");
        if (central) dcn_->control("tiempo", {{"banca", banca}, {"aviso", "terminado"}}, ignorar);
        if (bus_) bus_->publicar("oradores/tiempo", {{"id", id}, {"banca", banca}, {"aviso", "terminado"}});
    }
}

void StudioViewModel::onTick() {
    tiemposCentral();
    rt_.tick();
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
                             (sesionRemota_ && sesionRemota_->conectados() > 0) ||
                             (pantallas_ != nullptr && pantallas_->muestraPrograma());
        const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
        if (alguien) retornoVistoMs_ = ahora;
        rt_.engine().retornoEnUso(alguien || ahora - retornoVistoMs_ < 10000);
    }
    if (AppRuntime::stopRequested()) {
        emit quitRequested();
        return;
    }
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
        } else {
            rt_.stopRecording(); // ya no hay nada que retomar tras una caida
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
    mixer_->refreshLevels();
    emit nivelesChanged();
    // El resto, solo si cambió algo de lo que se ve (con tiempos de grabación o
    // transmisión corriendo, una vez por segundo).
    const QString firma = QStringList{QString::number(recording_), QString::number(streaming_), QString::number(alAire()),
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
QObject* StudioViewModel::historicoObj() const { return historico_.get(); }
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
QObject* StudioViewModel::enlacesQml() const { return enlaces_.get(); }

void StudioViewModel::actualizarRanurasBanca(bool forzar) {
    if (!bancas_ || !cuerpo_) return;
    // Quién va en cada ranura: en un debate, A y B; si no, quien tiene la palabra. Al
    // que habla desde el navegador no se le pone cámara (tiene la suya: el remoto).
    std::vector<std::string> ids;
    if (debate_ && debate_->activo()) {
        // Los de la sala, en orden (ranura 1 y 2): con uno remoto, el de la sala va en
        // la 1 (la escena «sala y remoto» es banca 1 + el remoto).
        if (!debate_->aEsRemoto()) ids.push_back(debate_->idA());
        if (!debate_->bEsRemoto()) ids.push_back(debate_->idB());
    } else {
        const std::string id = bancas_->alAireId();
        bool remoto = false;
        if (sesionRemota_ && !id.empty()) {
            const std::vector<std::string> conPalabra = sesionRemota_->conPalabraTodos();
            remoto = std::find(conPalabra.begin(), conPalabra.end(), id) != conPalabra.end();
        }
        ids.push_back(remoto ? std::string() : id);
    }
    if (ids.size() > static_cast<std::size_t>(BroadcastEngine::kRanurasBanca)) ids.resize(BroadcastEngine::kRanurasBanca);
    std::vector<BroadcastEngine::RanuraBanca> ranuras;
    for (const std::string& id : ids) {
        BroadcastEngine::RanuraBanca r;
        const nlohmann::json i = id.empty() ? nlohmann::json::object() : cuerpo_->cuerpo().buscar(id);
        const nlohmann::json t = i.value("preset_ptz", nlohmann::json::object()); // la toma de su banca
        const QString camara = QString::fromStdString(t.value("camara", std::string()));
        if (!camara.isEmpty()) {
            r.entrada = camara.toStdString();
            const QVariantMap d = camaras_->datosPosicion(camara, QString::fromStdString(t.value("posicion", std::string())));
            // Digital (sin motor): la ventana de la posición, recortada de la toma entera.
            // Motorizada: la cámara entera (la cabeza ya fue a la posición al dar la palabra).
            if (d.value("existe").toBool() && d.value("digital").toBool() && !d.value("motorizada").toBool()) {
                const double z = std::max(1.0, d.value("zoom").toDouble());
                const double x = d.value("x").toDouble(), y = d.value("y").toDouble(), m = 0.5 / z;
                r.entera = d.value("recorteDigital").toBool();
                r.recorte[0] = static_cast<float>(std::max(0.0, x - m));
                r.recorte[1] = static_cast<float>(std::max(0.0, y - m));
                r.recorte[2] = static_cast<float>(std::max(0.0, 1.0 - (x + m)));
                r.recorte[3] = static_cast<float>(std::max(0.0, 1.0 - (y + m)));
            }
        }
        ranuras.push_back(r);
    }
    rt_.engine().setRanurasBanca(std::move(ranuras), forzar);
}
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
                                       {"texto", tr("Punto %1: %2").arg(p.value("n", 0)).arg(QString::fromStdString(p.value("titulo", std::string())))}};
    }
    // Un orador presencial (banca) al aire manda sobre el de la sala.
    if (bancas_) {
        const nlohmann::json b = bancas_->oradorParaZocalo();
        if (!b.empty())
            ctx["orador"] = QJsonDocument::fromJson(QByteArray::fromStdString(b.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object();
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
    // El quórum de ahora y la última votación oficial (asamblea/Quorum), para
    // las placas de la asamblea: {quorum.texto}, {votacion.resultado}…
    if (quorum_) {
        const nlohmann::json q = quorum_->estado();
        if (q.value("configurado", false))
            ctx["quorum"] = QJsonObject{
                {"presentes", q.value("presentes", 0)},
                {"necesarios", q.value("necesarios", 0)},
                {"miembros", q.value("miembros", 0)},
                {"hay", q.value("hay_quorum", false)},
                {"estado", q.value("hay_quorum", false) ? tr("CON QUÓRUM") : tr("SIN QUÓRUM")},
                {"texto", tr("%1 presentes de %2").arg(q.value("presentes", 0)).arg(q.value("miembros", 0))}};
        const nlohmann::json of = quorum_->oficiales();
        if (!of.empty()) {
            const nlohmann::json& v = of.back();
            ctx["votacion"] = QJsonObject{
                {"asunto", QString::fromStdString(v.value("asunto", std::string()))},
                {"afirmativos", v.value("afirmativos", 0)},
                {"negativos", v.value("negativos", 0)},
                {"abstenciones", v.value("abstenciones", 0)},
                {"presentes", v.value("presentes", 0)},
                {"necesarios", v.value("necesarios", 0)},
                {"resultado", v.value("aprobado", false) ? "APROBADO" : "RECHAZADO"},
                {"color", v.value("aprobado", false) ? "#2e7d32" : "#c62828"}};
        }
    }
    ctx["hora"] = QTime::currentTime().toString("HH:mm");
    // Campos vivos (T43): cambian cada segundo; en un vínculo «vivo» el gráfico se
    // actualiza sin volver a entrar.
    ctx["hora_s"] = QTime::currentTime().toString("HH:mm:ss");
    if (bancas_) {
        const nlohmann::json b = bancas_->estadoJson();
        if (!b.value("al_aire", nlohmann::json()).is_null()) {
            const long long usado = b.value("usado_s", 0LL), asignado = b.value("asignado_s", 0LL);
            const auto mmss = [](long long s) { return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0')); };
            QJsonObject o = ctx.value("orador").toObject();
            o["cronometro"] = mmss(usado);
            o["restante"] = asignado > 0 ? (usado > asignado ? "+" + mmss(usado - asignado) : mmss(asignado - usado)) : QString();
            ctx["orador"] = o;
        }
    }
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
    if (const std::string e = conmutador_->poner(name.toStdString()); !e.empty()) emit notify(qs(e), "error");
}

QString StudioViewModel::escenaArmada() const { return qs(conmutador_->armada()); }

void StudioViewModel::publicarTally() {
    auto* p = qobject_cast<PantallasViewModel*>(pantallas());
    if (p == nullptr) return;
    const QString aire = qs(rt_.engine().currentScene());
    const QString armada = escenaArmada();
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
    if (const std::string e = conmutador_->armar(name.toStdString()); !e.empty()) emit notify(qs(e), "aviso");
}

void StudioViewModel::take() { takeCon(QString(), -1); }

void StudioViewModel::takeCon(const QString& tipo, int ms) {
    const bool habiaArmada = !conmutador_->armada().empty();
    if (const std::string e = conmutador_->take(tipo.toStdString(), ms); !e.empty())
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
}

void StudioViewModel::setScenePreview(const QString& name) {
    // 960 px de ancho alcanza para el lienzo del editor y no castiga la GPU.
    rt_.engine().enableScenePreview(name.toStdString(), 960);
}

QVariantMap StudioViewModel::latidoUi() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(rt_.latido().estado().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().toVariantMap();
}

QString StudioViewModel::latidoPoliticaUi(const QVariantMap& cambios) {
    const auto j = nlohmann::json::parse(QJsonDocument(QJsonObject::fromVariantMap(cambios)).toJson().toStdString(),
                                         nullptr, false);
    return QString::fromStdString(rt_.latido().ponerPolitica(j));
}

QVariantList StudioViewModel::alarmasActivasUi() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(alerts_->activas().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).array().toVariantList();
}

int StudioViewModel::reconocerAlarmaUi(qint64 id) { return alerts_->reconocer(static_cast<std::uint64_t>(id)); }

bool StudioViewModel::alarmaCritica() const {
    for (const auto& e : alerts_->activas())
        if (e.value("sonido", std::string()) == "critico" && !e.value("silenciada", false)) return true;
    return false;
}

bool StudioViewModel::silenciarAlarmaUi(const QString& disparador, bool silenciar) {
    const auto r = alerts_->update(disparador.toStdString(), nlohmann::json{{"silenciada", silenciar}});
    return !r.contains("error");
}

QObject* StudioViewModel::alarmaSonora() const {
    // Devuelto por un Q_INVOKABLE y sin padre, QML se lo queda (JavaScriptOwnership) y su
    // recolector lo borraba: al cerrar la app se borraba dos veces (cierre con SIGSEGV, y el
    // launcher lo tomaba por una caída). Es de acá.
    if (alarma_) QQmlEngine::setObjectOwnership(alarma_.get(), QQmlEngine::CppOwnership);
    return alarma_.get();
}

bool StudioViewModel::focoEnTexto() const {
    QObject* f = QGuiApplication::focusObject();
    return f != nullptr && (f->inherits("QQuickTextInput") || f->inherits("QQuickTextEdit"));
}

bool StudioViewModel::corteDirectoEditor() const { return ajustesEscenas_->corteDirectoEditor(); }

void StudioViewModel::setCorteDirectoEditor(bool si) {
    if (ajustesEscenas_->setCorteDirectoEditor(si)) emit escenasAjustesChanged();
}

QString StudioViewModel::ftbUi(bool activar, int ms, const QString& audio) {
    return qs(seguridadAire_->ftb(activar, ms, audio.toStdString())); // placaChanged llega por alCambiar
}

QString StudioViewModel::placaUi(bool activar, const QString& texto, const QString& imagen, int ms) {
    return qs(seguridadAire_->placa(activar, texto.toStdString(), imagen.toStdString(), ms));
}

QVariantMap StudioViewModel::estadoPlacaUi() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(seguridadAire_->estado().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().toVariantMap();
}

QStringList StudioViewModel::textosPlaca() const {
    QStringList l{tr("Volvemos enseguida"), tr("Cuarto intermedio"), tr("Minuto de silencio"), tr("Sesión suspendida"),
                  tr("Dificultades técnicas")};
    const QString usado = QString::fromStdString(rt_.engine().estadoPlaca().value("placa_texto", std::string()));
    if (!usado.isEmpty() && !l.contains(usado)) l.prepend(usado);
    return l;
}

void StudioViewModel::restaurarPlaca() {
    if (seguridadAire_->restaurar())
        emit notify(tr("Se restauró lo que estaba al aire antes del corte (placa o negro)"), "warn");
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

QObject* StudioViewModel::bancasQml() const { return bancas_.get(); }
QObject* StudioViewModel::debateQml() const { return debate_.get(); }
QObject* StudioViewModel::configuracionesQml() const { return configuraciones_.get(); }
QObject* StudioViewModel::dcnQml() const { return dcn_.get(); }
QObject* StudioViewModel::sesionRemotaQml() const { return sesionRemota_.get(); }
QObject* StudioViewModel::votacionesQml() const { return votaciones_.get(); }

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
    const int n = vocabulario_->cargarCuerpo(cuerpo_->cuerpo());
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
    const QString aire = qs(rt_.engine().currentScene());
    const QString armada = escenaArmada();
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
        // Para el multiview (tally rojo y verde por celda).
        QStringList aireQ, armadaQ;
        for (const auto& f : fuentesAire) aireQ << QString::fromStdString(f.get<std::string>());
        for (const auto& f : fuentesDe(armada)) armadaQ << QString::fromStdString(f.get<std::string>());
        const QVariantMap t{{"aire", aireQ}, {"armada", armadaQ}};
        if (t != tally_) {
            tally_ = t;
            emit tallyChanged();
        }
    }
    {
        // Lo que se reproduce al aire y cuánto le queda (al segundo).
        QVariantList medios;
        nlohmann::json mediosJ = nlohmann::json::array();
        for (const auto& m : rt_.engine().mediaSources()) {
            if (m.state != "playing" || m.durationMs <= 0) continue;
            if (std::find(fuentesAire.begin(), fuentesAire.end(), m.name) == fuentesAire.end()) continue;
            const int resta = static_cast<int>((m.durationMs - m.timeMs + 999) / 1000);
            medios.append(QVariantMap{{"fuente", qs(m.name)}, {"restante_s", resta}, {"duracion_s", static_cast<int>(m.durationMs / 1000)}});
            mediosJ.push_back({{"fuente", m.name}, {"restante_s", resta}});
        }
        if (medios != mediosAlAire_) {
            mediosAlAire_ = medios;
            emit mediosChanged();
            bus_->publicar("aire/medios", mediosJ);
        }
    }
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
        muestrearSalud();
        bus_->publicar("audio/loudness", rt_.engine().loudness().json());
        nlohmann::json sin = nlohmann::json::array();
        for (const QString& e : sinSenal()) sin.push_back(e.toStdString());
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
QObject* StudioViewModel::controlWebQml() const { return controlWeb_.get(); }

QString StudioViewModel::temaWeb() const { return qs(rt_.config().temaWeb); }

QVariantList StudioViewModel::temasWeb() const {
    QVariantList out;
    for (const auto& t : control::temasWeb())
        out << QVariantMap{{"id", t.id},       {"nombre", t.nombre},         {"descripcion", t.descripcion},
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
    return ordenDia_ ? qs(ordenDia_->capitulosTexto(recordStartMs())) : QString();
}

QStringList StudioViewModel::nombresParaDsk() const {
    QStringList out;
    for (const std::string& n : rt_.engine().sceneNames()) out << qs(n);
    for (const std::string& n : rt_.engine().sourceNames())
        if (!out.contains(qs(n))) out << qs(n);
    return out;
}

QString StudioViewModel::hablarARemoto(const QString& integrante, bool activo) {
    if (!presidencia_) return tr("La sesión remota no está andando.");
    return QString::fromStdString(presidencia_->hablarARemoto(integrante.toStdString(), activo));
}

void StudioViewModel::silenciarTodos() {
    if (!presidencia_) return;
    QPointer<StudioViewModel> yo(this);
    presidencia_->silenciarTodos("operador", [yo](const std::string& e) {
        if (yo && !e.empty()) emit yo->notify(QString::fromStdString(e), "error");
    });
}

void StudioViewModel::hablaPresidencia(bool activo) {
    if (!presidencia_) return;
    QPointer<StudioViewModel> yo(this);
    presidencia_->hablaPresidencia(activo, "operador", [yo](const std::string& e) {
        if (yo && !e.empty()) emit yo->notify(QString::fromStdString(e), "error");
    });
}

bool StudioViewModel::presidenciaHabla() const { return presidencia_ && presidencia_->presidenciaHabla(); }

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

void StudioViewModel::esperarReplay(const std::string& antes, int ms, std::function<void(const QString&)> listo) {
    const std::string ahora = rt_.engine().ultimoReplay();
    if (!ahora.empty() && ahora != antes) return listo(qs(ahora));
    if (ms <= 0) return listo(QString());
    QTimer::singleShot(200, this, [this, antes, ms, listo = std::move(listo)]() mutable {
        esperarReplay(antes, ms - 200, std::move(listo));
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

QString StudioViewModel::armarRepeticion(const QString& archivo) {
    static const std::string kEscena = "Repetición", kFuente = "Repetición (clip)";
    BroadcastEngine& e = rt_.engine();
    const auto escenas = e.sceneNames();
    if (std::find(escenas.begin(), escenas.end(), kEscena) == escenas.end() && !rt_.addScene(kEscena))
        return qs(rt_.lastError());
    const std::string ajustes =
        nlohmann::json{{"is_local_file", true}, {"local_file", archivo.toStdString()}, {"looping", false},
                       {"restart_on_activate", true}, {"close_when_inactive", true}}
            .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    const auto fuentes = e.sceneSources(kEscena);
    if (std::find(fuentes.begin(), fuentes.end(), kFuente) == fuentes.end()) {
        SourceSpec spec;
        spec.id = "ffmpeg_source";
        spec.name = kFuente;
        spec.settingsJson = ajustes;
        if (!rt_.addSource(kEscena, spec)) return qs(rt_.lastError());
    } else if (!rt_.updateSourceSettings(kFuente, ajustes)) {
        return qs(rt_.lastError());
    }
    scenes_->reload();
    emit escenasCambiaron();
    rt_.save();
    armarEscena(qs(kEscena));
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
            {"que_protege", "las páginas de la lista piden escribir CONFIRMAR con un motivo; los ajustes que reinician el gateway o el programa no se aplican"}};
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
    return (conference_ ? conference_->peopleConnected() : 0) + (sesionRemota_ ? sesionRemota_->conectados() : 0);
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

bool StudioViewModel::afv(const QString& fuente) const { return conmutador_->afv(fuente.toStdString()); }

void StudioViewModel::setAfv(const QString& fuente, bool sigue) { conmutador_->setAfv(fuente.toStdString(), sigue); }

QStringList StudioViewModel::afvFuentes() const {
    QStringList out;
    for (const std::string& f : conmutador_->afvFuentes()) out << qs(f);
    return out;
}

void StudioViewModel::tbar(double t, bool soltar) {
    if (const std::string e = conmutador_->tbar(t, soltar); !e.empty())
        emit notify(qs(e), conmutador_->armada().empty() ? "aviso" : "error");
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

// --- Multiview ----------------------------------------------------------------------------
// Varias multivistas (pestañas de la página Multiview). Config/multiview.json:
// {"vistas": [{id, nombre, fuentes, distribucion, ...}], "actual": id}. El formato
// viejo (una sola, plana) se lee como «Multiview 1».

namespace ba::app {

namespace {
QVariantMap multiviewDeFabrica() {
    return {{"fuentes", QStringList{"program", "armada"}},
            {"distribucion", "control"},
            {"separacion", 8},
            {"fondo", "#202024"},
            {"borde", 1},
            {"colorBorde", "#5a5a64"},
            {"grosorTally", 2},
            {"nombres", true},
            {"tamNombres", 18},
            {"tally", true},
            {"vumetros", true}};
}
QString idNuevo() { return QString::number(QDateTime::currentMSecsSinceEpoch(), 36); }
} // namespace

void StudioViewModel::leerMultiviews() const {
    if (multiviewLeido_) return;
    auto* yo = const_cast<StudioViewModel*>(this);
    yo->multiviewLeido_ = true;
    QFile f(QString::fromStdWString((rt_.dataRoot() / "Config" / "multiview.json").wstring()));
    QVariantMap raiz;
    if (f.open(QIODevice::ReadOnly)) raiz = QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
    QVariantList vistas = raiz.value("vistas").toList();
    if (vistas.isEmpty()) {
        QVariantMap v = multiviewDeFabrica();
        for (auto it = raiz.begin(); it != raiz.end(); ++it)
            if (it.key() != "vistas" && it.key() != "actual") v[it.key()] = it.value(); // formato viejo
        v["id"] = "1";
        v["nombre"] = tr("Multiview %1").arg(1);
        vistas << v;
    }
    // Lo que falte en una vista (de una versión vieja), de fábrica.
    for (QVariant& x : vistas) {
        QVariantMap v = x.toMap();
        const QVariantMap fab = multiviewDeFabrica();
        for (auto it = fab.begin(); it != fab.end(); ++it)
            if (!v.contains(it.key())) v[it.key()] = it.value();
        x = v;
    }
    yo->multiviews_ = vistas;
    yo->multiviewActual_ = raiz.value("actual").toString();
    if (indiceMultiview(multiviewActual_) < 0) yo->multiviewActual_ = vistas.first().toMap().value("id").toString();
}

int StudioViewModel::indiceMultiview(const QString& id) const {
    for (int i = 0; i < multiviews_.size(); ++i)
        if (multiviews_[i].toMap().value("id").toString() == id) return i;
    return -1;
}

void StudioViewModel::guardarMultiviews() {
    const std::filesystem::path dir = rt_.dataRoot() / "Config";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    QFile f(QString::fromStdWString((dir / "multiview.json").wstring()));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(QJsonObject::fromVariantMap({{"vistas", multiviews_}, {"actual", multiviewActual_}}))
                    .toJson(QJsonDocument::Indented));
    emit multiviewChanged();
}

QVariantList StudioViewModel::multiviews() const {
    leerMultiviews();
    return multiviews_;
}

QString StudioViewModel::multiviewActual() const {
    leerMultiviews();
    return multiviewActual_;
}

QVariantMap StudioViewModel::multiview() const { return multiviewDe(multiviewActual()); }

QVariantMap StudioViewModel::multiviewDe(const QString& id) const {
    leerMultiviews();
    const int i = indiceMultiview(id.isEmpty() ? multiviewActual_ : id);
    return i >= 0 ? multiviews_[i].toMap() : QVariantMap{};
}

void StudioViewModel::setMultiview(const QVariantMap& cambios) { setMultiviewDe(multiviewActual(), cambios); }

void StudioViewModel::setMultiviewDe(const QString& id, const QVariantMap& cambios) {
    leerMultiviews();
    const int i = indiceMultiview(id);
    if (i < 0) return;
    QVariantMap v = multiviews_[i].toMap();
    for (auto it = cambios.begin(); it != cambios.end(); ++it)
        if (it.key() != "id") v[it.key()] = it.value();
    multiviews_[i] = v;
    guardarMultiviews();
}

void StudioViewModel::setMultiviewActual(const QString& id) {
    leerMultiviews();
    if (indiceMultiview(id) < 0 || id == multiviewActual_) return;
    multiviewActual_ = id;
    guardarMultiviews();
}

QString StudioViewModel::nuevaMultiview(const QString& nombre, bool copiarActual) {
    leerMultiviews();
    QVariantMap v = copiarActual ? multiview() : multiviewDeFabrica();
    v["id"] = idNuevo();
    v["nombre"] = nombre.trimmed().isEmpty() ? tr("Multiview %1").arg(multiviews_.size() + 1) : nombre.trimmed();
    multiviews_ << v;
    multiviewActual_ = v["id"].toString();
    guardarMultiviews();
    return multiviewActual_;
}

void StudioViewModel::renombrarMultiview(const QString& id, const QString& nombre) {
    if (nombre.trimmed().isEmpty()) return;
    setMultiviewDe(id, {{"nombre", nombre.trimmed()}});
}

bool StudioViewModel::borrarMultiview(const QString& id) {
    leerMultiviews();
    const int i = indiceMultiview(id);
    if (i < 0 || multiviews_.size() <= 1) return false; // siempre queda una
    multiviews_.removeAt(i);
    if (multiviewActual_ == id) multiviewActual_ = multiviews_[std::max(0, i - 1)].toMap().value("id").toString();
    guardarMultiviews();
    return true;
}

QVariantList StudioViewModel::monitores() const {
    // Los de Salidas → Pantallas: orden físico e ids estables (una sola lista).
    return monitores_ ? monitores_->conectadosEnOrden() : QVariantList{};
}

QStringList StudioViewModel::multiviewsConVentana() const {
    QStringList out;
    if (!ventanas_) return out;
    for (const auto& w : ventanas_->ventanas())
        for (const QString& c : w.cosas)
            if (c.startsWith("multiview:") && !out.contains(c.mid(10))) out << c.mid(10);
    return out;
}

namespace {
/// Una ventana de 16:9 centrada en el monitor `g` (o escalonada).
QRect geometriaEn(QRect g, int escalon) {
    if (!g.isValid()) g = QRect(0, 0, 1920, 1080);
    const int w = std::min(1280, g.width() - 80), h = w * 9 / 16;
    return QRect(g.x() + (g.width() - w) / 2 + escalon * 40, g.y() + (g.height() - h) / 2 + escalon * 40, w, h);
}
QRect rectDe(const QVariantMap& m) {
    return QRect(m.value("x").toInt(), m.value("y").toInt(), m.value("ancho").toInt(), m.value("alto").toInt());
}
} // namespace

void StudioViewModel::abrirMultiviewEnVentana(const QString& id, const QString& monitor, bool pantallaCompleta) {
    if (!ventanas_ || !monitores_ || multiviewDe(id).isEmpty()) return;
    // La ventana de esa multivista se cierra y se abre donde se pide (así «Llevar
    // la ventana» la manda al monitor elegido aunque ya estuviera abierta).
    ventanas_->cerrarCosa("multiview:" + id);
    // `monitor`: id, número de orden, nombre o nombre del sistema (Monitores::resolver).
    const QString mid = monitor.isEmpty() ? QString() : monitores_->resolver(monitor);
    QVariantMap donde, principal;
    for (const QVariant& v : monitores_->conectadosEnOrden()) {
        const QVariantMap m = v.toMap();
        if (m.value("id").toString() == mid) donde = m;
        if (m.value("principal").toBool()) principal = m;
    }
    const bool hay = !donde.isEmpty();
    ventanas_->abrirEn({"multiview:" + id}, {}, hay ? mid : principal.value("id").toString(),
                       geometriaEn(rectDe(hay ? donde : principal), 0), pantallaCompleta && hay);
}

int StudioViewModel::abrirTodasLasMultiviews() {
    if (!ventanas_ || !monitores_) return 0;
    cerrarVentanasMultiview();
    QVariantMap principal;
    QVariantList otros;
    for (const QVariant& v : monitores_->conectadosEnOrden()) {
        if (v.toMap().value("principal").toBool())
            principal = v.toMap();
        else
            otros << v;
    }
    const QVariantList vistas = multiviews();
    int escalon = 0;
    for (int i = 0; i < vistas.size(); ++i) {
        const QString id = vistas[i].toMap().value("id").toString();
        if (i < otros.size()) {
            const QVariantMap m = otros[i].toMap(); // una por monitor, completa, en el orden físico
            ventanas_->abrirEn({"multiview:" + id}, {}, m.value("id").toString(), geometriaEn(rectDe(m), 0), true);
        } else {
            ventanas_->abrirEn({"multiview:" + id}, {}, principal.value("id").toString(),
                               geometriaEn(rectDe(principal), escalon++), false);
        }
    }
    return vistas.size();
}

int StudioViewModel::cerrarVentanasMultiview() {
    if (!ventanas_) return 0;
    std::vector<int> cerrar;
    for (const auto& w : ventanas_->ventanas())
        if (w.cosas.size() == 1 && w.cosas.first().startsWith("multiview:")) cerrar.push_back(w.id);
    for (int c : cerrar) ventanas_->cerrar(c);
    return static_cast<int>(cerrar.size());
}

void StudioViewModel::restablecerMultiview() {
    const QVariantMap v = multiview();
    QVariantMap fab = multiviewDeFabrica();
    fab["nombre"] = v.value("nombre");
    setMultiview(fab);
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
