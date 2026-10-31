// ============================================================================
// src/app/ui/StudioViewModel.cpp — ver StudioViewModel.hpp
// ============================================================================

#include "ui/StudioViewModel.hpp"

#include "agent/Despachador.hpp"
#include "agent/FlujosTexto.hpp"
#include "agent/Notificador.hpp"
#include "agent/ServidorModeloLocal.hpp"
#include "agent/Voz.hpp"
#include "arranque/AppRuntime.hpp"
#include "arranque/Configuraciones.hpp"
#include "arranque/EnergiaAire.hpp"
#include "arranque/InterfazConfig.hpp"
#include "asamblea/Bancas.hpp"
#include "asamblea/CuerpoViewModel.hpp"
#include "asamblea/OrdenDelDia.hpp"
#include "asamblea/SesionRemota.hpp"
#include "comun/Duracion.hpp"
#include "control/BusEventos.hpp"
#include "control/ControlWeb.hpp"
#include "control/Dsk.hpp"
#include "diag/DiarioDeArranque.hpp"
#include "diag/Registro.hpp"
#include "diag/VigiaBucle.hpp"
#include "dispositivos/Dispositivos.hpp"
#include "dispositivos/Registro.hpp"
#include "emision/AvisoFueraDeUi.hpp"
#include "emision/VentanillaMotor.hpp"
#include "escenas/Conmutador.hpp"
#include "escenas/ControlGrillas.hpp"
#include "escenas/RespaldoEntradas.hpp"
#include "graficos/Emisor.hpp"
#include "graficos/GraficoSource.hpp"
#include "graficos/Vinculos.hpp"
#include "medios/Playlist.hpp"
#include "platform/Paths.hpp"
#include "plugins/Plugins.hpp"
#include "sala/CupoSala.hpp"
#include "ui/AlarmasViewModel.hpp"
#include "ui/AtencionViewModel.hpp"
#include "ui/BitacoraEvento.hpp"
#include "ui/CarruselViewModel.hpp"
#include "ui/ChatOverlayRenderer.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/CuentasViewModel.hpp"
#include "ui/DashboardModel.hpp"
#include "ui/DocumentosViewModel.hpp"
#include "ui/EventoViewModel.hpp"
#include "ui/ExtensionesModel.hpp"
#include "ui/FrameService.hpp"
#include "ui/FuenteQrPintor.hpp"
#include "ui/GraficoViewModel.hpp"
#include "ui/HistoricoViewModel.hpp"
#include "ui/InputsViewModel.hpp"
#include "ui/LineaViewModel.hpp"
#include "ui/ListModels.hpp"
#include "ui/MediosViewModel.hpp"
#include "ui/OperatorMessages.hpp"
#include "ui/PantallasViewModel.hpp"
#include "ui/PdfEscena.hpp"
#include "ui/PersonasViewModel.hpp"
#include "ui/PrompterViewModel.hpp"
#include "ui/PropertiesModel.hpp"
#include "ui/PublicoViewModel.hpp"
#include "ui/QrViewModel.hpp"
#include "ui/RecursosViewModel.hpp"
#include "ui/RegistroViewModel.hpp"
#include "ui/RielConfig.hpp"
#include "ui/RotuladorViewModel.hpp"
#include "ui/SettingsModel.hpp"
#include "ui/TemaEventoViewModel.hpp"
#include "ui/TotemsViewModel.hpp"
#include "video/SuperRes.hpp"

#include <QClipboard>
#include <QGuiApplication>
#include <QJsonDocument>

namespace ba::app {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

} // namespace

StudioViewModel::StudioViewModel(AppRuntime& rt, VentanillaMotor& ventanilla, QObject* parent)
    : QObject(parent), rt_(rt), ventanilla_(ventanilla) {
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
    // Las escenas y las fuentes del editor: de la foto del motor (se actualizan solas con lo
    // que avisa la ventanilla); lo que se cambia, se pide (F3-03).
    scenes_ = std::make_unique<ScenesModel>(ventanilla_);
    marca("scenes");
    sources_ = std::make_unique<SourcesModel>(rt, ventanilla_);
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
    // Las vistas del motor se leen sin el turno; pedirlas y soltarlas se le encarga al dueño.
    frames_ = std::make_unique<FrameService>(
        rt.engine(),
        [this](std::function<void(BroadcastEngine&)> fn) {
            ventanilla_.pedir("vista", [&rt = rt_, fn = std::move(fn)]() { fn(rt.engine()); });
        },
        ventanilla_.foto().ajustes.invitados);
    marca("frames");
    frames_->setLobbySlots(rt.gateway().lobbySlots);
    // Las salidas (ui/SalidasViewModel): el conmutador de escenas de cada una, el FTB y la
    // placa, y cuál maneja esta interfaz. Acá se refleja lo de la activa (escenas, fuentes,
    // mezcla); las pantallas de tally de la sala y el monitor «armada» siguen a la por omisión.
    salidas_ = std::make_unique<SalidasViewModel>(SalidasViewModel::Partes{
        .rt = rt,
        .ventanilla = ventanilla_,
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
    // (La lista de escenas sabe sola qué está al aire y qué armado: lo lee de la foto.)
    connect(salidas_.get(), &SalidasViewModel::alAireActivaCambio, this, [this](const QString& escena) {
        sources_->setScene(escena); // el editor de fuentes sigue a la escena al aire
        emit sceneChanged();
    });
    connect(salidas_.get(), &SalidasViewModel::armadaActivaCambio, this, [this]() { emit sceneChanged(); });
    connect(salidas_.get(), &SalidasViewModel::notify, this, &StudioViewModel::notify);
    connect(salidas_.get(), &SalidasViewModel::afvActivaCambio, this, &StudioViewModel::statusChanged);
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
    // Los mandos de las escenas: armarlas, ordenarlas, el preview y el TAKE de la activa, el
    // AFV, la repetición (ui/EscenasViewModel).
    escenas_ = std::make_unique<EscenasViewModel>(
        EscenasViewModel::Partes{.rt = rt, .ventanilla = ventanilla_, .salidas = *salidas_, .fuentes = *sources_});
    connect(escenas_.get(), &EscenasViewModel::notify, this, &StudioViewModel::notify);
    connect(escenas_.get(), &EscenasViewModel::listaCambio, this, &StudioViewModel::escenasCambiaron);
    connect(escenas_.get(), &EscenasViewModel::escenaCambio, this, &StudioViewModel::sceneChanged);
    props_ = std::make_unique<PropertiesModel>(rt);
    marca("props");
    conference_ = std::make_unique<ConferenceViewModel>(rt, ventanilla_);
    // Lo que se maneja de la sala en caliente: el cupo, la configuración, las direcciones,
    // la votación, las preguntas, el PIN y el túnel (ui/SalaViewModel).
    sala_ = std::make_unique<SalaViewModel>(SalaViewModel::Partes{
        .rt = rt,
        .ventanilla = ventanilla_,
        .conferencia = *conference_,
        .recursos = *recursos_,
        .enVivo = [this]() { return modoVivo(); },
        .conAire = [this]() { return streaming_ || recording_; },
        .evento = [this]() { return evento_.get(); },
        .chat = [this]() { return chatOverlay_.get(); },
        .enElCuerpo = [this](const std::string& nombre) {
            return asamblea_ ? asamblea_->cuerpo().cuerpo().buscar(nombre) : nlohmann::json::object();
        }});
    connect(sala_.get(), &SalaViewModel::notify, this, &StudioViewModel::notify);
    connect(sala_.get(), &SalaViewModel::cupoCambio, this, [this](int n) { frames_->setGuests(n); });
    // Grabar, transmitir y lo que los rodea: destinos, señal limpia, ISO, disco, loudness,
    // retornos y la salud de cada destino (ui/EmisionViewModel).
    emision_ = std::make_unique<EmisionViewModel>(EmisionViewModel::Partes{
        .rt = rt, .ventanilla = ventanilla_, .sala = *conference_, .grabando = [this]() { return recording_; }});
    connect(emision_.get(), &EmisionViewModel::notify, this, &StudioViewModel::notify);
    connect(emision_.get(), &EmisionViewModel::refrescar, this, &StudioViewModel::onTick);
    connect(emision_.get(), &EmisionViewModel::estadoCambio, this, &StudioViewModel::statusChanged);
    // (Los destinos de cada salida van en su fila: los trae la foto.)
    salidas_->publicar(); // el gateway se entera de las salidas apenas hay con quién hablar
    marca("conference");
    connect(conference_.get(), &ConferenceViewModel::notify, this, &StudioViewModel::notify);
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
        evento_ = std::make_unique<EventoViewModel>(rt, ventanilla_, conference_.get(), emisor_.get(), c.saltear());
        marca("evento");
    }
    connect(evento_.get(), &EventoViewModel::notify, this, &StudioViewModel::notify);
    // Un evento con página web y dirección propia: al ponerlo en curso, la dirección
    // pública pasa a ser su subdominio (con la API key de pinggy). Reinicia la sala:
    // al aire no se toca (se avisa).
    connect(evento_.get(), &EventoViewModel::pedirDireccion, sala_.get(), &SalaViewModel::pedirDireccion);
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
    // La votación y las preguntas son de la sala (modelo); el público y el MCP las usan.
    publico_->setVotacion(&sala_->votacion());
    publico_->setPreguntas(&sala_->preguntas());
    connect(publico_.get(), &PublicoViewModel::notify, this, &StudioViewModel::notify);
    medios_ = std::make_unique<MediosViewModel>(rt, this);
    playlist_ = std::make_unique<medios::Playlist>(rt, this);
    playlist_->setArmar([this](const QString& escena) { return salidas_->conmutador().armar(escena.toStdString()); });
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
    // La app misma: el modo en vivo, la versión y el reinicio, los eventos, la caída de la
    // que se vuelve y lo que se corta al cerrar (ui/AppViewModel).
    app_ = std::make_unique<AppViewModel>(AppViewModel::Partes{
        .rt = rt,
        .transmitiendo = [this]() { return streaming_; },
        .grabando = [this]() { return recording_; },
        .personas = [this]() { return personasConectadas(); },
        .asistente = [this]() { return asistente_ ? &asistente_->agente() : nullptr; }});
    connect(app_.get(), &AppViewModel::notify, this, &StudioViewModel::notify);
    connect(app_.get(), &AppViewModel::quitRequested, this, &StudioViewModel::quitRequested);
    // Lo que se corta al cerrar y el modo en vivo cambian con las pastillas.
    connect(this, &StudioViewModel::statusChanged, app_.get(), &AppViewModel::estadoCambio);
    marca("system");
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
            bitacoraEvento_ = std::make_unique<BitacoraEvento>(rt_, ventanilla_, *evento_, *bus_);
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
        settings_->setCupoEnCaliente([this](int n) { return sala_->cupo().cambiar(n); });
        settings_->setConfigSala(&sala_->config());
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
        // «Priorizar programa» por calor (F4-05; la superresolución la atiende Imagen y los
        // invitados, la sala): los modelos locales que no atienden se sueltan (el
        // llama-server se duerme; los reconocedores sin uso se descargan ya) y las vistas
        // del motor que nadie mira se duermen.
        recursos_->alDegradar("modelos", [this](bool apagar) {
            agent::ServidorModeloLocal::el().porCalor(apagar);
            if (asistente_) asistente_->voz().porCalor(apagar);
            return true;
        });
        recursos_->alDegradar("vistas", [&rt](bool apagar) {
            rt.engine().soltarVistasSinMirar(apagar);
            return true;
        });
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
    // Dónde se muestra cada cosa en esta máquina: el catálogo, los monitores, las ventanas
    // sueltas y las multivistas (ui/EscritorioViewModel). Después de las cámaras, el
    // tablero y las pantallas de la red: el catálogo los lee a todos.
    escritorio_ = std::make_unique<EscritorioViewModel>(EscritorioViewModel::Partes{
        .rt = rt,
        .cuadros = *frames_,
        .imagen = *imagen_,
        .tablero = dashboard_.get(),
        .pantallas = pantallas_.get(),
        .totems = [this]() { return totems_ ? totems_->paraElegir() : QVariantList{}; }});
    connect(this, &StudioViewModel::escenasCambiaron, escritorio_.get(), &EscritorioViewModel::refrescarCatalogo);
    marca("ventanas");
    escritorio_->refrescarCatalogo();
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
        .ventanilla = ventanilla_,
        .bus = *bus_,
        .sala = *conference_,
        .direcciones = sala_->direcciones(),
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
                                                                           .direcciones = sala_->direcciones(),
                                                                           .asamblea = *asamblea_,
                                                                           .camaras = &imagen_->camaras(),
                                                                           .plugins = plugins_.get(),
                                                                           .pdfEscena = pdfEscena_.get(),
                                                                           .salidas = [this]() { return salidas_->paraSuperficies(); }});
    connect(control_.get(), &ControlViewModel::notify, this, &StudioViewModel::notify);
    connect(control_.get(), &ControlViewModel::mostrarQr, this, &StudioViewModel::mostrarQr);
    connect(control_.get(), &ControlViewModel::dskCambio, this, &StudioViewModel::publicarTally);
    control_->dsk().setSalida(salidas_->activaId());
    linea_ = std::make_unique<LineaViewModel>(*evento_, rt_, ventanilla_, frames_.get(), &control_->acciones(), bus_.get());
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
    // (Las fuentes del editor llegan con la foto del motor.)
    connect(conference_.get(), &ConferenceViewModel::sourceAdded, this, [this]() { mixer_->reload(); });

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
    // (Llegan en el dueño del motor: se juntan acá y se reflejan en el bucle de la interfaz.)
    observadorRt_ = rt_.observar([this](AppRuntime::Cambio c) {
        const unsigned bit = 1u << static_cast<unsigned>(c);
        if (avisosPendientes_.fetch_or(bit) == 0)
            QMetaObject::invokeMethod(this, &StudioViewModel::refrescarAvisos, Qt::QueuedConnection);
    });
    // Lo que muestra la raíz (las pastillas, el lienzo, la escena del editor) sale de la foto
    // del motor: se refleja cuando llega una nueva (F3-03).
    connect(&ventanilla_, &VentanillaMotor::cambio, this, &StudioViewModel::alCambiarFoto);
    // Todos los enlaces del aparato (página Enlaces y MCP enlaces_*): cómo preguntar y
    // prender cada servicio, sin que el modelo conozca sus clases.
    {
        EnlacesViewModel::Fuentes f;
        // La sala, el túnel, las direcciones y el lobby.
        sala_->alimentarEnlaces(f);
        f.webEvento = [this]() -> QVariantMap {
            if (!evento_) return {};
            return {{"hay", evento_->web().value("hay").toBool()}, {"evento", evento_->abierto()}};
        };
        // La API remota, el Satellite, el control web y los pedidos de cada aparato.
        control_->alimentarEnlaces(f);
        enlaces_ = std::make_unique<EnlacesViewModel>(std::move(f));
        auto* e = enlaces_.get();
        const auto avisar = [e]() { e->avisarCambio(); };
        sala_->avisarA(*e);
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
    connect(totems_.get(), &TotemsViewModel::cambio, escritorio_.get(), &EscritorioViewModel::refrescarCatalogo);
    if (qr_) qr_->setTotems([this]() { return totems_ ? totems_->paraElegir() : QVariantList{}; });
    escritorio_->refrescarCatalogo();
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
        // Al motor, solo cuando cambia (se le pide al dueño).
        const auto enUso = [this](const std::string& salida, bool si) {
            const auto it = retornoEnUso_.find(salida);
            if (it != retornoEnUso_.end() && it->second == si) return;
            retornoEnUso_[salida] = si;
            ventanilla_.pedir("retorno_en_uso", [&rt = rt_, salida, si]() { rt.engine().retornoEnUso(si, salida); });
        };
        enUso({}, alguien || ahora - retornoVistoMs_ < 10000);
        // Las demás salidas: su retorno (BA.Salida.<id>) solo mientras el gateway dice
        // que alguien lo mira, con los mismos 10 s de gracia.
        const QJsonObject miranSalidas = conf != nullptr ? conf->client().miranSalidas() : QJsonObject();
        for (const FotoMotor::Salida& s : ventanilla_.foto().salidas) {
            if (s.porOmision) continue;
            const QString q = qs(s.id);
            if (miranSalidas.value(q).toInt() > 0) salidaVistaMs_[q] = ahora;
            enUso(s.id, ahora - salidaVistaMs_.value(q, ahora - 10000) < 10000);
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
    // Lo que sale de la por omisión: de la foto del motor (la última vuelta de su reloj).
    const FotoMotor::EstadoAire& st = ventanilla_.foto().estado;
    if (st.graba && !recording_) {
        recSince_ = std::chrono::steady_clock::now();
        recSinceMs_ = QDateTime::currentMSecsSinceEpoch();
    }
    if (st.transmite && !streaming_) {
        streamSince_ = std::chrono::steady_clock::now();
        lastBytes_ = 0;
        lastBytesAt_ = streamSince_;
    }
    // Un corte pedido es un aviso corto; uno que nadie pidio (red, servidor,
    // disco lleno) queda fijo en la barra del operador hasta que lo cierre.
    const auto dropped = [this](const QString& title, const QString& text) {
        asistente_->mensajes().post(
            {{"kind", "message"}, {"priority", "critical"}, {"title", title.toStdString()}, {"text", text.toStdString()}});
    };
    if (recording_ && !st.graba) {
        if (st.corteGrabacionPedido) {
            emit notify(tr("Grabación detenida"), "info");
        } else if (st.grabacionCaida) {
            // Se cortó y el motor la rearma sola en otra parte (F1-05). La interfaz NUNCA
            // la para por su cuenta: la grabación es del motor, que la vuelve a armar (y un
            // instante sin grabar también pasa al rearmar el motor, p. ej. tras perder la GPU:
            // pararla ahí la mataba). Queda la alarma record_down hasta que vuelva.
            dropped(tr("Se cortó la grabación"),
                    tr("La grabación se cortó y vuelve sola en otra parte del archivo. Si no vuelve, revisá el disco."));
        }
    }
    // Una transmisión que se corta sin que nadie la pare la reconecta el motor solo (se
    // avisa abajo, con «reconectando…»). La interfaz no la para: un instante sin
    // transmitir también pasa al rearmar el motor (la GPU perdida), y pararla ahí la mataba.
    if (streaming_ && !st.transmite && st.corteTransmisionPedido) emit notify(tr("Transmisión detenida"), "warn");
    // Si cambio el aire o la grabacion, el tally lo tiene que saber: una
    // pantalla que dice «al aire» con la transmision cortada miente, y es la
    // clase de mentira por la que alguien se confia.
    const bool cambioElAire = recording_ != st.graba || streaming_ != st.transmite;
    recording_ = st.graba;
    streaming_ = st.transmite;
    if (cambioElAire) publicarTally();
    // Avisar UNA vez por problema, no en cada vuelta del reloj.
    if (st.reconectando && !reconnecting_)
        emit notify(tr("Se cortó la transmisión: reconectando… (%1)").arg(qs(st.problema)), "error");
    else if (!st.reconectando && reconnecting_ && st.transmite)
        emit notify(tr("Transmisión recuperada"), "ok");
    reconnecting_ = st.reconectando;
    outputProblem_ = qs(st.problema);
    fps_ = st.fps;
    encoder_ = qs(st.codificador);
    dropped_ = st.descartados;
    total_ = st.cuadros;
    // Bitrate real de la transmision (promedio de ~1 s).
    const auto now = std::chrono::steady_clock::now();
    const double dt = std::chrono::duration<double>(now - lastBytesAt_).count();
    if (st.transmite && dt >= 1.0) {
        kbps_ = static_cast<int>((st.bytes - lastBytes_) * 8.0 / 1000.0 / dt);
        lastBytes_ = st.bytes;
        lastBytesAt_ = now;
    } else if (!st.transmite) {
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
// Lo que muestran las pastillas y el lienzo: de la foto del motor (F3-03).
QString StudioViewModel::currentScene() const {
    const FotoMotor::Salida* s = ventanilla_.foto().salida(salidas_ ? salidas_->activaId() : std::string());
    return s != nullptr ? qs(s->alAire) : QString();
}
QString StudioViewModel::transitionType() const { return qs(ventanilla_.foto().ajustes.transicion); }
double StudioViewModel::canvasAspect() const {
    const FotoMotor::AjustesVideo& v = ventanilla_.foto().ajustes;
    return v.baseAlto > 0 ? double(v.baseAncho) / v.baseAlto : 16.0 / 9.0;
}
QString StudioViewModel::canvasLabel() const {
    const FotoMotor::AjustesVideo& v = ventanilla_.foto().ajustes;
    return QString("%1x%2 · %3 fps").arg(v.salidaAncho).arg(v.salidaAlto).arg(qs(fpsLegible(v.fpsNum, v.fpsDen)));
}
QString StudioViewModel::streamServer() const { return qs(ventanilla_.foto().ajustes.servidor); }
bool StudioViewModel::streamKeyAvailable() const { return ventanilla_.foto().ajustes.claveEnEntorno; }
bool StudioViewModel::streamKeySaved() const { return ventanilla_.foto().ajustes.claveGuardada; }
QString StudioViewModel::room() const { return qs(ventanilla_.foto().ajustes.sala); }
QObject* StudioViewModel::direccionesQml() const { return sala_->direccionesQml(); }
QObject* StudioViewModel::salaQml() const {
    QQmlEngine::setObjectOwnership(sala_.get(), QQmlEngine::CppOwnership);
    return sala_.get();
}
int StudioViewModel::guests() const { return ventanilla_.foto().ajustes.invitados; }
QString StudioViewModel::theme() const { return qs(ventanilla_.foto().ajustes.tema); }

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
QObject* StudioViewModel::system() const { return app_->sistemaQml(); }
QObject* StudioViewModel::appQml() const {
    QQmlEngine::setObjectOwnership(app_.get(), QQmlEngine::CppOwnership);
    return app_.get();
}
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
QObject* StudioViewModel::catalogoQml() const { return escritorio_->catalogoQml(); }

QObject* StudioViewModel::pantallasConectadasQml() const { return escritorio_->monitoresQml(); }
QObject* StudioViewModel::ventanasQml() const { return escritorio_->ventanasQml(); }
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

    // La sala: los invitados en el orden en que los ve el operador, quién habla y quién sigue.
    sala_->completarVinculos(ctx);
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
    // La hora ({hora}, {hora_s}) la pone Vinculos: su reloj sigue sin la interfaz.
    // El punto en tratamiento, el orador presencial (manda sobre el de la sala), el
    // quórum y la última votación oficial: los de la asamblea.
    if (asamblea_) asamblea_->completarVinculos(ctx);
    return ctx;
}

QString StudioViewModel::escenaArmada() const {
    const FotoMotor::Salida* s = ventanilla_.foto().salida(salidas_->activaId());
    return s != nullptr ? qs(s->armada) : QString();
}

void StudioViewModel::publicarTally() {
    auto* p = qobject_cast<PantallasViewModel*>(pantallas());
    if (p == nullptr) return;
    // Las pantallas de tally de la sala: las de la por omisión (no la activa de esta interfaz).
    const FotoMotor& foto = ventanilla_.foto();
    const FotoMotor::Salida* omision = foto.salida({});
    if (omision == nullptr) return;
    const QString aire = qs(omision->alAire);
    const QString armada = qs(omision->armada);
    const auto fuentesDe = [&foto](const QString& escena) {
        QStringList out;
        for (const std::string& f : foto.fuentesDe(escena.toStdString())) out << qs(f);
        return out;
    };
    QStringList enAire = fuentesDe(aire);
    for (const std::string& f : omision->dsk)
        if (!enAire.contains(qs(f))) enAire << qs(f);
    p->publicarTally(aire, armada, enAire, fuentesDe(armada), streaming(), recording());
}

void StudioViewModel::copiar(const QString& texto) {
    if (QClipboard* c = QGuiApplication::clipboard()) {
        c->setText(texto);
        emit notify("Copiado", "ok");
    }
}

void StudioViewModel::save() {
    if (rt_.save()) {
        emit notify(tr("Guardado"), "ok");
    } else {
        emit notify(tr("No se pudo guardar: %1").arg(qs(rt_.lastError())), "error");
    }
}

void StudioViewModel::refreshModels() {
    // Las escenas y las fuentes salen de la foto del motor y se actualizan solas: acá, que
    // la vista las vuelva a leer (y el editor, si su escena ya no existe, a la del aire).
    emit escenasCambiaron();
    if (ventanilla_.foto().escena(sources_->scene().toStdString()) == nullptr) sources_->setScene(currentScene());
    mixer_->reload();
    emit sceneChanged();
    emit statusChanged();
}

void StudioViewModel::alCambiarFoto(unsigned partes) {
    if ((partes & FotoMotor::Ajustes) != 0) {
        // Se aplicó la configuración (la GUI o settings_set del MCP): lo que rige ya.
        emit canvasChanged();
        emit themeChanged();
        emit sceneChanged();
        emit statusChanged();
        frames_->setGuests(ventanilla_.foto().ajustes.invitados);
    }
    if ((partes & FotoMotor::Escenas) != 0) {
        emit escenasCambiaron(); // el árbol por carpetas se rearma (barato)
        // Si la escena que mostraba el editor ya no existe, sigue a la del aire.
        if (ventanilla_.foto().escena(sources_->scene().toStdString()) == nullptr) sources_->setScene(currentScene());
    }
}

void StudioViewModel::refrescarAvisos() {
    // Lo que todavía se lee directo del runtime (las áreas sin migrar a la foto del motor:
    // la mezcla, la imagen, el control). Las escenas, las fuentes, lo que sale y los
    // ajustes que muestra la raíz llegan por la foto (alCambiarFoto y cada modelo).
    const unsigned b = avisosPendientes_.exchange(0);
    const auto hubo = [b](AppRuntime::Cambio c) { return (b & (1u << static_cast<unsigned>(c))) != 0; };
    using C = AppRuntime::Cambio;
    if (hubo(C::Ajustes)) {
        control_->ajustesAplicados(); // el tema de las páginas web, si cambió
        imagen_->ajustesAplicados(); // Mejorar imagen general y el legalizador rigen ya
    }
    if (hubo(C::Audio) && mixer_->rowCount() > 0)
        emit mixer_->dataChanged(mixer_->index(0), mixer_->index(mixer_->rowCount() - 1),
                                 {MixerModel::VolumeDbRole, MixerModel::MutedRole});
    // Una entrada de audio nueva (audio_input_add del MCP, un plugin): la lista de
    // fuentes de la página Audio la muestra sin tener que volver a entrar.
    if (hubo(C::Audio) || hubo(C::Fuentes)) mezcla_->revisarFuentes();
}

QObject* StudioViewModel::emisionQml() const {
    QQmlEngine::setObjectOwnership(emision_.get(), QQmlEngine::CppOwnership);
    return emision_.get();
}

QObject* StudioViewModel::escenasQml() const {
    QQmlEngine::setObjectOwnership(escenas_.get(), QQmlEngine::CppOwnership);
    return escenas_.get();
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
    sources_->setScene(currentScene());
    mixer_->setSalida(activa);
    if (mezcla_) mezcla_->setSalida(activa);
    if (control_) control_->dsk().setSalida(activa);
    emit sceneChanged();
    emit statusChanged();
    emit nivelesChanged();
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

QObject* StudioViewModel::cuerpoQml() const { return asamblea_->cuerpoQml(); }

QObject* StudioViewModel::bancasQml() const { return asamblea_->bancasQml(); }
QObject* StudioViewModel::debateQml() const { return asamblea_->debateQml(); }
QObject* StudioViewModel::configuracionesQml() const { return app_->configuracionesQml(); }
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

QObject* StudioViewModel::busQml() const { return bus_.get(); }

void StudioViewModel::publicarEventos() {
    if (!bus_) return;
    // Volviendo de una caída (cuánto tardó el aire en volver) y el modo en vivo que cambia.
    app_->vigilar(*bus_);
    // T47: al arrancar una sesión en vivo (el guion corre), la sala cambia de PIN
    // si cambió el día (el PIN es uno por día: el mismo día, el mismo PIN).
    {
        const bool ev = evento_ != nullptr && evento_->property("enVivo").toBool();
        if (ev && !eventoEnVivo_) sala_->alEmpezarElVivo();
        eventoEnVivo_ = ev;
    }
    ++busTicks_;
    // Lo que está al aire de cada salida (la escena, la armada, el tally, los medios, la
    // T-bar, las capas y la mezcla de cada una), al bus y al multiview.
    salidas_->publicarEnBus(*bus_, *mixer_, control_->dsk(), streaming_, recording_);
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
        bus_->publicar("audio/loudness", ventanilla_.foto().loudness.value(BroadcastEngine::kSalidaPorOmision, nlohmann::json::object()));
        nlohmann::json sin = nlohmann::json::array();
        for (const QString& e : sinSenal()) sin.push_back(e.toStdString());
        bus_->publicar("entrada/senal", {{"sin_senal", sin}});
        if (asamblea_) bus_->publicar("oradores/al_aire", asamblea_->bancas().oradorParaZocalo());
        sala_->publicarParticipantes(*bus_);
    }
}

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
QObject* StudioViewModel::accesoRemotoQml() const { return sala_->accesoRemotoQml(); }
QObject* StudioViewModel::seguirMicrofonoQml() const { return asamblea_->seguirMicrofonoQml(); }
QObject* StudioViewModel::ordenDelDiaQml() const { return asamblea_->ordenDelDiaQml(); }
QObject* StudioViewModel::informesQml() const { return asamblea_->informesQml(); }
QObject* StudioViewModel::quorumQml() const { return asamblea_->quorumQml(); }

QString StudioViewModel::capitulosPuntosUi() const {
    return qs(asamblea_->ordenDelDia().capitulosTexto(recordStartMs()));
}

QObject* StudioViewModel::playlistQml() const { return playlist_.get(); }
QObject* StudioViewModel::puestoCorreccionQml() const { return asistente_->puestoCorreccionQml(); }

} // namespace ba::app

namespace ba::app {

bool StudioViewModel::alAire() const { return ventanilla_.foto().estado.haySalida; }
bool StudioViewModel::transmitiendo() const { return ventanilla_.foto().estado.hayTransmision; }

int StudioViewModel::personasConectadas() const {
    // Los invitados de la sala (lobby, programa, fila de oradores) y los integrantes
    // conectados desde el navegador (sesión remota; se crea más tarde, puede no estar).
    return (conference_ ? conference_->peopleConnected() : 0) +
           (asamblea_ ? asamblea_->sesionRemota()->conectados() : 0);
}

std::filesystem::path StudioViewModel::datosRaiz() const { return rt_.dataRoot(); }

} // namespace ba::app

namespace ba::app {

QObject* StudioViewModel::multivistasQml() const { return escritorio_->multivistasQml(); }
QObject* StudioViewModel::escritorioQml() const {
    QQmlEngine::setObjectOwnership(escritorio_.get(), QQmlEngine::CppOwnership);
    return escritorio_.get();
}

} // namespace ba::app
