// ============================================================================
// src/app/arranque/AppRuntime.cpp — ver AppRuntime.hpp
//
// El núcleo de la fachada: leer la configuración, arrancar y cerrar, el hilo dueño, las
// vueltas (ticControl, ticMotor, latirInterfaz), guardar, y lo que delega en cada pieza.
// ============================================================================

#include "arranque/RuntimeMotor.hpp"

#include <nlohmann/json.hpp>

#include "audio/ObjetivoLoudness.hpp"
#include "emision/BroadcastEngine.hpp"
#include "emision/DocumentoAire.hpp"
#include "emision/FotoMotor.hpp"
#include "emision/LatidoAire.hpp"
#include "emision/OperatorFeed.hpp"
#include "emision/PedidosSalida.hpp"
#include "emision/SalidasConfig.hpp"
#include "ipc/ControlService.hpp"
#include "ipc/HealthBlock.hpp"
#include "persist/ArchivoConfig.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include <QtGlobal>

#include "GpuPerdida.hpp" // motor: la GPU perdida (F3-09)
#include "Reloj.hpp"      // motor: iniciarGStreamer
#include "ajustes/Ajustes.hpp"
#include "arranque/AireDurable.hpp"
#include "arranque/AjustesMotor.hpp"
#include "arranque/ArchivoConfig.hpp"
#include "arranque/ArranqueAviso.hpp"
#include "arranque/InicioConElSistema.hpp"
#include "arranque/Invitados.hpp"
#include "arranque/ModoConsola.hpp"
#include "arranque/PublicadorFoto.hpp"
#include "arranque/RepositorioEscenas.hpp"
#include "arranque/SaludProceso.hpp"
#include "arranque/ServicioEmision.hpp"
#include "arranque/ServicioSalidas.hpp"
#include "diag/Metricas.hpp"
#include "diag/Registro.hpp"
#include "emision/ChatOverlaySource.hpp"
#include "emision/FuentePdf.hpp"
#include "emision/FuenteQr.hpp"
#include "emision/FuentesApp.hpp"
#include "emision/RetornosInvitados.hpp"
#include "emision/TiposBasicos.hpp"
#include "escenas/FuenteGrilla.hpp"
#include "evento/FotosConfig.hpp"
#include "graficos/GraficoSource.hpp"
#include "i18n/I18nTexto.hpp"
#include "persist/ColaEscritura.hpp"
#include "platform/GstEntorno.hpp"
#include "platform/Paths.hpp"
#include "platform/Process.hpp"
#include "video/ElementosGst.hpp"
#include "video/FuentesCaptura.hpp"

namespace ba::app {

namespace {

namespace fs = std::filesystem;

std::atomic<bool> g_stop{false};

} // namespace

// ============================================================================
// Las piezas
// ============================================================================

AppRuntime::AppRuntime()
    : engine_(std::make_unique<BroadcastEngine>()),
      salud_(std::make_unique<SaludProceso>(*this)),
      operator_(std::make_unique<OperatorFeed>()),
      foto_(std::make_unique<PublicadorFoto>(*this)),
      escenas_(std::make_unique<RepositorioEscenas>(*this)),
      servicioSalidas_(std::make_unique<ServicioSalidas>(*this)),
      emision_(std::make_unique<ServicioEmision>(*this)),
      aire_(std::make_unique<AireDurable>(*this)),
      invitados_(std::make_unique<Invitados>(*this)) {}

AppRuntime::~AppRuntime() { salud_->esperarReinicio(); }

// ============================================================================
// Lo que es del dueño, a mano, y lo que se lee desde cualquier hilo (antes inline en el
// header: el motor y el feed van por unique_ptr y el header no los incluye)
// ============================================================================

AppConfig& AppRuntime::config(std::source_location donde) {
    engine_->turno().entrarDirecto(donde);
    return cfg_;
}

const AppConfig& AppRuntime::config(std::source_location donde) const {
    engine_->turno().entrarDirecto(donde);
    return cfg_;
}

GatewaySettings& AppRuntime::gateway(std::source_location donde) {
    engine_->turno().entrarDirecto(donde);
    return gateway_;
}

LauncherSettings& AppRuntime::launcher(std::source_location donde) {
    engine_->turno().entrarDirecto(donde);
    return launcher_;
}

ApiMotor& AppRuntime::engine(std::source_location donde) {
    engine_->turno().contarDirecta(donde);
    return *engine_;
}

NivelesMotor& AppRuntime::niveles() { return engine_->nivelesCanal(); }

bool AppRuntime::haySalida(bool contarGrabacion) const { return engine_->haySalida(contarGrabacion); }

const TurnoMotor& AppRuntime::turno() const { return engine_->turno(); }

void AppRuntime::alFallarMotor(std::function<void(const AvisoMotor&)> fn) { engine_->alFallarMotor(std::move(fn)); }

ApiMotor& AppRuntime::motorEntreHilos() { return *engine_; }

ApiMotor& AppRuntime::motorPropio() const { return *engine_; }

OperatorFeed& AppRuntime::operatorFeed() { return *operator_; }

void AppRuntime::setLobbyMonitor(int slot, bool on) { operator_->setLobbyMonitor(slot, on); }

// ============================================================================
// El hilo dueño, los errores y el cierre pedido
// ============================================================================

void AppRuntime::requestStop() { g_stop.store(true); }

bool AppRuntime::stopRequested() { return g_stop.load(); }

std::string& AppRuntime::errorDelHilo() {
    thread_local std::string e;
    return e;
}

const std::string& AppRuntime::lastError() const { return errorDelHilo(); }

void AppRuntime::setEncolarEnDueno(std::function<void(std::function<void()>)> encolar) {
    encolarEnDueno_ = std::move(encolar);
}

void AppRuntime::cederDueno() { dueno_ = std::this_thread::get_id(); }

bool AppRuntime::esDueno() const { return dueno_.load() == std::this_thread::get_id(); }

void AppRuntime::enDueno(std::function<void()> fn) const {
    if (esDueno() || !encolarEnDueno_) return fn();
    encolarEnDueno_(std::move(fn));
}

void AppRuntime::despuesEnDueno(std::function<void()> fn) const {
    if (!encolarEnDueno_) return fn();
    encolarEnDueno_(std::move(fn));
}

void AppRuntime::avisar(Cambio c) const { foto_->avisar(c); }

// ============================================================================
// Configurar, arrancar, las vueltas y cerrar
// ============================================================================

void AppRuntime::configure(int argc, char** argv) {
    // Binarios de la raiz del bundle; datos del disco real (BA_DATA_ROOT).
    bin_ = ba::platform::bundleRoot();
    // La configuración activa (Configuraciones/<nombre>/): todo lo de la app sale de acá;
    // lo de la máquina (Modelos/, Herramientas/, Logs/, Runtime/, Credenciales/) se pide
    // aparte con ba::platform::dataRoot().
    root_ = ba::platform::configRoot();
    if (bin_.empty()) {
        bin_ = fs::current_path();
    }
    if (root_.empty()) {
        root_ = bin_;
    }
    // La raíz única de Config/ del proceso (persist::archivoConfig): la misma para todos.
    ba::persist::ponerRaizConfig(root_);
    // Volver a la configuración de un evento (histórico, H6): se preparó con
    // la app andando y se aplica acá, antes de que nadie lea Config/.
    {
        const auto ahora = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
        const nlohmann::json r = ba::evento::FotosConfig::aplicarPendiente(ba::persist::raizConfig() / "Config", ahora);
        if (!r.is_null())
            ba::diag::registro().linea(ba::diag::Nivel::Aviso, "historico", 0,
                                       ba::i18n::llenar(ba::i18n::traducir("historico", "se volvió a la configuración de «%1»: %2 (lo de antes en Config/.restaurar/%3)"),
                                                        {r.value("desde", std::string()),
                                                         r.value("aplicados", nlohmann::json::array()).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                                                         r.value("respaldo", std::string())}));
    }
    const fs::path cfgPath = argValue(argc, argv, "--config", appConfigPath(root_).string());
    cfg_ = loadAppConfig(cfgPath, root_);
    ba::ajustes::cargar(cfg_.ajustes); // los avanzados de Configuración, lo antes posible
    gateway_ = loadGatewaySettings(gatewayConfigPath(root_));
    launcher_ = loadLauncherSettings(launcherConfigPath(root_));
    applyCli(cfg_, argc, argv, root_);
    // En las pruebas, un hilo que no participa del turno y entra al motor es un error.
    TurnoMotor::estricto(cfg_.modoPrueba);
}

int AppRuntime::start() {
    // El que arranca es el dueño del motor (la interfaz o el bucle de la consola): toma el
    // turno al usarlo, como el plano de control (F3-03, emision/TurnoMotor.hpp).
    dueno_ = std::this_thread::get_id();
    TurnoMotor::participar("dueño");
    // Lo que dejó la corrida anterior, ANTES de que nada lo pise (--record y --stream
    // escriben el documento apenas arrancan): hasta que termine de volver como estaba,
    // en el disco sigue diciendo lo de antes (AireDurable::writeOnAirState espera a listo()).
    aire_->leerAireAnterior();
    // --- Salud: A late en su slot para el watchdog del launcher.
    salud_->abrir();
    // El arranque, medido como el de la interfaz (StudioViewModel): cada etapa de más de
    // 150 ms queda en el registro, desde acá (GStreamer incluido: la retoma tras una
    // caída tiene que estar en < 3 s, F3-10, y estas marcas dicen dónde se va).
    const auto relojArranque = std::chrono::steady_clock::now();
    auto ultimaMarca = relojArranque;
    const auto marca = [&ultimaMarca](const std::string& que) {
        const auto ahora = std::chrono::steady_clock::now();
        const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(ahora - ultimaMarca).count();
        if (ms > 150)
            ba::diag::registro().info(
                ba::i18n::llenar(ba::i18n::traducir("arranque", "arranque: %1 tardó %2 ms"), {que, std::to_string(ms)}));
        ultimaMarca = ahora;
    };
    // GSTREAMER PRIMERO, EN OTRO HILO. La primera vez en una PC (o con el disco en
    // otra letra) rehace su lista de plugins: cada uno pasa por el escáner y uno
    // que se traba mirando el hardware cuesta 60 s. En otra PC tardó más de 2
    // minutos; con gst_init en este hilo el cartel quedaba quieto («No responde»),
    // el latido del arranque se cortaba a los 2 minutos y el launcher lo mataba;
    // la lista se guarda al final, así que cada reinicio empezaba de cero y la
    // app no arrancaba nunca (2026-10-14). Mientras GStreamer revisa, este hilo
    // late y repinta el cartel, sin tope: no puede quedar colgado para siempre,
    // GStreamer corta a cada plugin a los 60 s.
    {
        // Antes de revisar, GStreamer cuenta los plugins: el cartel dice cuántos son,
        // cuál está revisando y cuántos lleva («37 de 312»).
        const bool rehacer = ba::platform::registroPorRehacer();
        arranque::avisar(rehacer ? ba::i18n::traducir("arranque", "Contando los plugins de video…")
                                 : ba::i18n::traducir("arranque", "Iniciando GStreamer…"));
        const auto desde = std::chrono::steady_clock::now();
        ba::motor::iniciarGStreamer(nullptr, [&]() {
            salud_->latir();
            const long long s =
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - desde).count();
            const std::string reloj = std::to_string(s) + " s";
            const std::string primera =
                rehacer ? " · " + ba::i18n::traducir("arranque", "la primera vez en esta PC tarda más") : "";
            const ba::motor::ProgresoGst pg = ba::motor::progresoGStreamer();
            arranque::Aviso a;
            if (pg.total > 0 && pg.revisados > 0) {
                const int n = std::min(pg.revisados, pg.total);
                a.que = ba::i18n::llenar(ba::i18n::traducir("arranque", "Revisando los plugins de video: %1 de %2"),
                                         {std::to_string(n), std::to_string(pg.total)});
                a.detalle = pg.actual + " · " + reloj + primera;
                a.parte = static_cast<double>(n) / pg.total;
            } else if (pg.total > 0) {
                a.que = ba::i18n::llenar(ba::i18n::traducir("arranque", "Revisando los plugins de video: son %1"),
                                         {std::to_string(pg.total)});
                a.detalle = reloj + primera;
                a.parte = 0;
            } else {
                a.que = rehacer ? ba::i18n::traducir("arranque", "Contando los plugins de video…")
                                : ba::i18n::traducir("arranque", "Iniciando GStreamer…");
                if (s >= 2) a.detalle = reloj;
            }
            arranque::seguir(a);
        });
    }
    marca(ba::i18n::traducir("arranque", "GStreamer"));

    // EL LATIDO DEL ARRANQUE (SaludProceso): arrancar no es estar colgado.
    salud_->empezarLatidoArranque();

    EnginePaths paths;
    paths.configDir = ba::persist::archivoConfig("plugin_config").string();

    std::printf("ba-app: %s\n",
                ba::i18n::llenar(cfg_.safeMode ? ba::i18n::traducir("arranque", "arrancando motor %1x%2@%3, codificador=%4 (modo seguro)")
                                               : ba::i18n::traducir("arranque", "arrancando motor %1x%2@%3, codificador=%4"),
                                 {std::to_string(cfg_.video.baseWidth), std::to_string(cfg_.video.baseHeight),
                                  std::to_string(cfg_.video.fpsNum), cfg_.encoder.videoEncoder})
                    .c_str());
    arranque::avisar(ba::i18n::traducir("arranque", "Arrancando el motor de video y audio…"));
    // El reloj del estudio (Video → Sincronía): con él arranca el motor.
    {
        const nlohmann::json r = config::leerObjeto(ba::persist::archivoConfig("video-reloj.json"));
        if (!r.empty()) {
            engine_->ponerReloj(r.value("tipo", std::string("sistema")), r.value("servidor", std::string()),
                               r.value("puerto", 0), r.value("dominio", 0));
            if (r.value("publicar_puerto", 0) > 0) engine_->publicarReloj(r.value("publicar_puerto", 0));
        }
    }
    // Los tipos que pinta la app (antes de cargar escenas: sus valores de fábrica).
    chatoverlay::registerSourceType();
    graficos::registerSourceType();
    registrarTiposBasicos();
    video::registrarTiposCaptura();
    fuentesqr::registrar();
    fuentespdf::registrar();
    grilla::registrarTipo();
    marca(ba::i18n::traducir("arranque", "el reloj y los tipos de fuente"));
    if (!engine_->startup(paths, cfg_.video, AudioConfig{})) {
        errorDelHilo() = engine_->lastError();
        std::printf("ba-app: %s\n",
                    ba::i18n::llenar(ba::i18n::traducir("arranque", "el motor no arranco: %1"), {errorDelHilo()}).c_str());
        salud_->terminado();
        salud_->pararLatidoArranque();
        if (engine_->sinGStreamer()) {
            std::printf("ba-app: %s\n",
                        ba::i18n::traducir("arranque", "sin GStreamer en el programa (bin/64bit): el bundle está incompleto.").c_str());
            return ba::platform::exit_code::kEnvironment;
        }
        return 2;
    }
    marca(ba::i18n::traducir("arranque", "el motor"));
    aplicarAjustesMotor(cfg_, *engine_); // placa sin uso, entradas, transmisión, disco (Configuración)
    salud_->latir();
    started_ = true;
    // El audio de cada fuente cambió (por el camino que sea): la parte Mezcla de la foto.
    engine_->alCambiarAudioFuente([this](const std::string& f, const char* que) { foto_->anotarCambioAudio(f, que); });
    // Los cambios de ajustes que piden piezas sin el motor a mano (rótulos,
    // gráficos) pasan por acá: así quedan también en la configuración.
    fuentes::ponerCambiar([this](const std::string& n, const nlohmann::json& c) { return updateSourceSettings(n, c.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)); });
    // El objetivo guardado (Config/loudness.json), también al motor (AGC y techo).
    objetivoLoudness().cargar();
    // El latido que mira los cuadros del motor (hilo propio): una traba de la
    // interfaz ya no hace que el supervisor corte el aire. Desde acá late él.
    salud_->pararLatidoArranque();
    salud_->iniciarLatidoAire(root_, [this]() -> std::uint64_t { return engine_->cuadrosPrograma(); });

    marca(ba::i18n::traducir("arranque", "loudness y latido"));
    arranque::avisar(ba::i18n::traducir("arranque", "Preparando los lugares de los invitados…"));
    invitados_->agregar();
    marca(ba::i18n::traducir("arranque", "los invitados"));
    arranque::avisar(ba::i18n::traducir("arranque", "Abriendo los micrófonos…"));
    escenas_->agregarEntradasDeAudio();
    marca(ba::i18n::traducir("arranque", "las entradas de audio"));
    arranque::avisar(ba::i18n::traducir("arranque", "Armando las escenas…"));
    // Lo que oye cada invitado (mix-minus de fábrica): lo guardado en Config/retornos.json.
    emision::RetornosInvitados(*this).cargar();
    escenas_->armarEnElMotor();
    // Un app.json de antes: los ids nuevos de las escenas, al disco ya (la línea, el DSK y
    // los atajos se pasan a ellos; si no se guardaran, el próximo arranque daría otros).
    if (cfg_.escenasIdsNuevos) {
        cfg_.escenasIdsNuevos = false;
        save();
    }
    marca(ba::i18n::traducir("arranque", "las escenas"));
    engine_->setCurrentScene(cfg_.currentScene);
    // Las demás salidas (Config/salidas.json), con las fuentes ya puestas: cada una
    // arranca con los volúmenes de la por omisión (su escena vuelve abajo, con al-aire.json).
    servicioSalidas_->crearSalidasGuardadas();
    // El retorno, al tamaño con que el gateway lo manda (escalado en la GPU, F2-13).
    engine_->ponerTopeRetorno(gateway_.programVideoW, gateway_.programVideoH);
    if (cfg_.returnFeeds && !engine_->publishReturnFeeds()) {
        std::printf("ba-app: %s\n",
                    ba::i18n::llenar(ba::i18n::traducir("arranque", "sin retorno hacia B: %1"), {engine_->lastError()}).c_str());
    }
    marca(ba::i18n::traducir("arranque", "la escena al aire y el retorno"));
    // Lobby: la voz de quien espera (monitoreo) y lo que ve del operador.
    operator_->startLobbyMonitor(gateway_.lobbySlots);
    if (!operator_->configure(*engine_, cfg_.operatorVideo, cfg_.operatorAudio)) {
        std::printf("ba-app: %s\n", ba::i18n::traducir("arranque", "feed del operador incompleto (fuente inexistente)").c_str());
    }

    marca(ba::i18n::traducir("arranque", "el lobby y el operador"));
    {
        std::string candidatos;
        for (const auto& id : engine_->videoEncoderCandidates(cfg_.encoder)) candidatos += " " + id;
        std::printf("ba-app: %s\n",
                    ba::i18n::llenar(ba::i18n::traducir("arranque", "codificadores candidatos:%1"), {candidatos}).c_str());
    }

    marca(ba::i18n::traducir("arranque", "los codificadores"));
    if (cfg_.replaySegundos > 0) aplicarReplay();
    // VOLVER COMO ESTABA, todo acá y de una (F3-04): la escena de cada salida, el negro
    // y la placa, y lo que salía. Antes la placa volvía desde un temporizador de la
    // interfaz, un segundo y medio después y solo con interfaz.
    arranque::avisar(ba::i18n::traducir("arranque", "Volviendo a como estaba…"));
    aire_->restaurarAire();
    marca(ba::i18n::traducir("arranque", "volver como estaba"));
    if (cfg_.modoPrueba && !cfg_.retomaEnPrueba) {
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "arranque", 0,
                                   ba::i18n::traducir("arranque", "modo prueba (--test): no se retoman las salidas (grabación, transmisión)"));
    }
    aire_->listo();
    // Si arranca solo con el equipo y se movió la carpeta: la ruta nueva.
    inicio::revisar();
    // El tope de la latencia del estudio guardado (P0.6).
    if (cfg_.estudioTopeMs != -1) {
        engine_->ponerTopeEstudio(cfg_.estudioTopeMs);
    }
    // Que no falte ningún elemento de GStreamer sin que se sepa (P0.3).
    marca(ba::i18n::traducir("arranque", "estado y latencia"));
    video::revisarElementosGstAlArrancar();
    marca(ba::i18n::traducir("arranque", "revisar los elementos de GStreamer"));

    salud_->abrirCanal();
    std::printf("ba-app: %s\n",
                ba::i18n::llenar(ba::i18n::traducir("arranque", "en marcha (pid %1)"), {std::to_string(pid())}).c_str());
    return 0;
}

void AppRuntime::tick() {
    ticControl();
    ticMotor();
    latirInterfaz();
}

void AppRuntime::ticControl() {
    salud_->atenderCanal();
    // El documento de aire lo escribe su dueño: este hilo (ver setDuenoDelAire).
    aire_->volcar();
}

void AppRuntime::latirInterfaz() { salud_->latirInterfaz(started_, hayAire_.load()); }

void AppRuntime::ticMotor() {
    // El reloj del motor es del dueño (entre el arranque del plano y la mudanza, no hace nada).
    if (!esDueno()) return;
    ++vueltasReloj_;
    // LO QUE NO ESPERA A NADIE (PLAN-MOTOR-SIN-ESPERAS): el pico del programa de cada salida
    // y su loudness, al canal de niveles, en CADA vuelta y SIN el turno (con los medidores
    // que se guardaron la última vez que se lo tuvo). Si la interfaz tiene el turno, trabada
    // en algo suyo, los vúmetros y el loudness no se quedan quietos.
    const auto medirSinTurno = [this]() {
        if (started_) engine_->nivelesCanal().medir(vueltasReloj_ % NivelesMotor::kLoudnessCadaVueltas == 0);
    };
    // El resto, con el turno: si la interfaz lo tiene (una vista que todavía entra directo,
    // trabada adentro), esa parte se saltea esta vuelta. El dueño no espera a la interfaz.
    const TurnoMotor& turno = engine_->turno();
    if (!turno.tomarHasta(TurnoMotor::participa() ? 5 : 0)) {
        ++vueltasSinTurno_;
        ba::diag::Metricas::proceso()
            .contador("ba_motor_vueltas_sin_turno_total",
                      QT_TRANSLATE_NOOP("metricas", "Vueltas del reloj del motor que no tuvieron el turno (la interfaz lo tenía): se midió igual"))
            .sumar();
        medirSinTurno();
        return;
    }
    struct Soltar {
        const TurnoMotor& t;
        ~Soltar() { t.soltar(); }
    } soltar{turno};
    ++ticsMotor_;
    // Lo que contó el hilo de salidas del motor (lo que no arrancó, el AAC con que transmite).
    engine_->atenderSalidas();
    if (started_) {
        const EngineStatus st = engine_->status();
        hayAire_ = st.streaming || st.recording;
        ajustesAire(); // la copia de los ajustes de aire, para el plano de control
        // (Un codificador que se cae en el aire, una grabación o una transmisión que se
        // cortan: las atiende el hilo de salidas del motor, ver BroadcastEngine::ticSalidas.)
        // Lo que sale de cada salida (la escena, el negro y la placa), para volver tras un
        // corte (P0.10, F3-04): el documento compara y solo se escribe si cambió algo.
        aire_->writeOnAirState();
        // La GPU del video se perdió (F3-09): el motor no se rearma solo, lo rearma la app.
        if (ba::motor::gpuPerdida()) aire_->rearmarPorGpu();
        servicioSalidas_->publicarSalidas(st);
        // Las entradas de audio que el sistema tardó en abrir (addGlobalAudio) se
        // enganchan cuando llegan: una vez por segundo. (En el motor nuevo no queda nada que
        // adoptar: BroadcastEngine::adoptPendingAudio devuelve 0; el audio de cada entrada lo
        // engancha el motor en su hilo, sin este reloj.)
        if (ticsMotor_ % 10 == 0) engine_->adoptPendingAudio();
        // Los medidores de cada salida (el pico del programa, el loudness), para medir sin el
        // turno: el vúmetro de arriba y el de la salida activa los toman del canal de niveles.
        NivelesMotor& nv = engine_->nivelesCanal();
        nv.setPorOmision(engine_->idDeSalida(""));
        nv.ponerMedidores(engine_->medidores());
        // Las fuentes con audio, según la foto del motor (sale después de armar una entrada).
        foto_->seguirCanalesDelMotor();
    }
    medirSinTurno();
    engine_->aplicarRecortesPendientes(); // los de cámaras que recién tienen tamaño
    // La foto que lee la interfaz: lo que se mueve solo (lo que sale, las medidas) en cada
    // vuelta; lo de la emisión (grabación, ISO, limpia, disco) y del equipo (el reloj, las
    // sesiones de codificador) dos veces por segundo.
    foto_->marcar(FotoMotor::Salidas | FotoMotor::Estado | FotoMotor::Medidas |
                  (ticsMotor_ % 5 == 0 ? static_cast<unsigned>(FotoMotor::Emision | FotoMotor::Equipo) : 0u));
    foto_->publicarFoto();
}

void AppRuntime::stop() {
    const TurnoMotor::Guarda turno(engine_->turno());
    if (!started_) {
        return;
    }
    started_ = false;
    // Cierre ordenado: nada que retomar (salvo el corte de luz con poca batería) y el
    // documento de aire a disco, que el plano de control puede estar ya parado.
    aire_->cerrar();
    // Las grabaciones por entrada, ANTES que el resto: cada una tiene su vista
    // de render y su archivo abierto, y si el cierre las deja vivas el archivo
    // queda sin cerrar (o sea, sin indice y a veces sin poder abrirse).
    engine_->stopAllSourceRecordings();
    engine_->stopCleanFeed();
    salud_->latido().detener(); // antes de apagar el motor (lee sus cuadros)
    std::printf("ba-app: %s\n", ba::i18n::traducir("arranque", "cerrando (salidas, escena, motor)").c_str());
    salud_->cerrarCanal();
    operator_->shutdown(); // antes de apagar el motor: suelta vista y fuentes
    engine_->shutdown();
    salud_->terminado();
    // Lo encolado para el disco (el estado de aire de recién, los ajustes), escrito
    // antes de irse.
    if (!ba::persist::cola().vaciar())
        ba::diag::registro().linea(ba::diag::Nivel::Error, "persistencia", 0,
                                   ba::i18n::traducir("persistencia", "al cerrar quedó algo sin guardar (el disco no responde)"));
    std::printf("ba-app: %s\n", ba::i18n::traducir("arranque", "cierre ordenado").c_str());
}

// ============================================================================
// La configuración
// ============================================================================

bool AppRuntime::setOperatorFeed(const std::string& videoSource, const std::string& audioSource) {
    const bool ok = operator_->configure(*engine_, videoSource, audioSource);
    cfg_.operatorVideo = videoSource;
    cfg_.operatorAudio = audioSource;
    fotoSucia(FotoMotor::Configuracion); // el micrófono del operador (la voz lo lee de la foto)
    if (!ok) errorDelHilo() = ba::i18n::traducir("arranque", "no existe la fuente elegida para el operador");
    return ok;
}

bool AppRuntime::applyVideo(const VideoConfig& v) {
    if (!engine_->resetVideo(v)) {
        errorDelHilo() = engine_->lastError();
        return false;
    }
    cfg_.video = v;
    escenas_->reubicarFuentes();
    return true;
}

bool AppRuntime::applyEncoder(const EncoderConfig& e) {
    if (!engine_->resetEncoders()) {
        errorDelHilo() = engine_->lastError();
        return false;
    }
    cfg_.encoder = e;
    return true;
}

bool AppRuntime::save() {
    // La configuración es del dueño: desde el plano de control se la encarga (lo que sale
    // ya quedó en el documento de aire, que no espera a nadie).
    if (!esDueno()) {
        enDueno([this]() { save(); });
        return true;
    }
    std::string err;
    // EN MODO SEGURO NO SE GUARDA. Es un estado degradado y transitorio: lo
    // que haya en memoria vale para esta corrida y nada mas. Escribirlo seria
    // hacer permanente una situacion de emergencia, y ya paso una vez
    // (escenas vaciadas). La copia de seguridad se deja igual, por si alguien
    // llega hasta aca con el app.json ya dañado de antes.
    if (cfg_.safeMode) {
        if (!safeBackupDone_) {
            safeBackupDone_ = true;
            std::error_code ec;
            std::filesystem::path backup = cfg_.configPath;
            backup += ".antes-del-modo-seguro";
            if (std::filesystem::exists(cfg_.configPath, ec) && !std::filesystem::exists(backup, ec)) {
                std::filesystem::copy_file(cfg_.configPath, backup, ec);
                std::printf("ba-app: %s\n",
                            ba::i18n::llenar(ba::i18n::traducir("config", "modo seguro: copia de la configuración en %1"),
                                             {backup.string()})
                                .c_str());
            }
        }
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "config", 0,
                                   ba::i18n::traducir("config", "modo seguro: NO se guarda la configuración. Lo que cambies "
                                                                "ahora vale solo hasta que se cierre."));
        return true;
    }
    gateway_.guests = cfg_.guests; // B lee la cantidad de invitados de A
    if (!saveAppConfig(cfg_.configPath, cfg_, root_, &err) ||
        !saveGatewaySettings(gatewayConfigPath(root_), gateway_, &err) ||
        !saveLauncherSettings(launcherConfigPath(root_), launcher_, &err)) {
        errorDelHilo() = err;
        return false;
    }
    return true;
}

// ============================================================================
// Lo que delega en las piezas (con el turno donde lo tomaba siempre)
// ============================================================================

// --- La foto y los avisos (PublicadorFoto) ----------------------------------------------
void AppRuntime::setFoto(std::function<void(std::shared_ptr<const FotoMotor>, unsigned)> publicar) { foto_->setFoto(std::move(publicar)); }
int AppRuntime::aportarALaFoto(std::function<void(FotoMotor&, unsigned)> aporte) { return foto_->aportarALaFoto(std::move(aporte)); }
void AppRuntime::dejarDeAportar(int id) { foto_->dejarDeAportar(id); }
void AppRuntime::fotoSucia(unsigned partes) const { foto_->fotoSucia(partes); }
void AppRuntime::publicarFoto() { foto_->publicarFoto(); }
std::shared_ptr<const FotoMotor> AppRuntime::ultimaFoto() const { return foto_->ultimaFoto(); }
int AppRuntime::observar(std::function<void(Cambio)> fn) { return foto_->observar(std::move(fn)); }
void AppRuntime::dejarDeObservar(int id) { foto_->dejarDeObservar(id); }

// --- Las salidas del aparato (ServicioSalidas) ------------------------------------------
nlohmann::json AppRuntime::salidasPublicadas() const { return servicioSalidas_->salidasPublicadas(); }
nlohmann::json AppRuntime::salidas() const { return servicioSalidas_->salidas(); }
std::string AppRuntime::crearSalida(const std::string& id, const std::string& nombre, const std::string& color) {
    return servicioSalidas_->crearSalida(id, nombre, color);
}
std::string AppRuntime::editarSalida(const std::string& id, const nlohmann::json& cambios) { return servicioSalidas_->editarSalida(id, cambios); }
std::string AppRuntime::borrarSalida(const std::string& id, const std::string& susDestinos) {
    const TurnoMotor::Guarda turno(engine_->turno());
    return servicioSalidas_->borrarSalida(id, susDestinos);
}
EncoderConfig AppRuntime::codificadorDe(const std::string& salida) const { return servicioSalidas_->codificadorDe(salida); }
std::string AppRuntime::setTopeSalidas(int tope) { return servicioSalidas_->setTopeSalidas(tope); }
audio::ObjetivoLoudness AppRuntime::objetivoLoudness() { return servicioSalidas_->objetivoLoudness(); }

// --- Lo que sale (ServicioEmision) -------------------------------------------------------
AppRuntime::AjustesAire AppRuntime::ajustesAire() const { return emision_->ajustesAire(); }
std::string AppRuntime::carpetaGrabacion() const { return emision_->carpetaGrabacion(); }

void AppRuntime::setCarpetaDelEvento(fs::path carpeta) {
    // Es del dueño: la lee ajustesAire() al armar la copia del plano, y desde otro hilo
    // ajustesAire() devuelve esa copia. Puesta desde la interfaz, la grabación que se
    // retomaba enseguida salía con la copia vieja (sin carpeta): iba a Recordings/.
    if (!esDueno() && encolarEnDueno_)
        return encolarEnDueno_([this, c = std::move(carpeta)]() mutable { setCarpetaDelEvento(std::move(c)); });
    emision_->ponerCarpetaDelEvento(std::move(carpeta));
    retomarGrabaciones();
}

void AppRuntime::retomarGrabaciones() {
    if (!esDueno() && encolarEnDueno_) return encolarEnDueno_([this]() { retomarGrabaciones(); });
    const TurnoMotor::Guarda turno(engine_->turno());
    aire_->retomarGrabaciones();
}

bool AppRuntime::startRecording(const std::string& salida, AlResultado listo) {
    const TurnoMotor::Guarda turno(engine_->turno());
    return emision_->startRecording(salida, std::move(listo));
}
void AppRuntime::stopRecording(const std::string& salida) {
    const TurnoMotor::Guarda turno(engine_->turno());
    emision_->stopRecording(salida);
}
bool AppRuntime::startSourceRecording(const std::string& source) { return emision_->startSourceRecording(source); }
std::string AppRuntime::grabarAparte(const std::string& fuente) { return emision_->grabarAparte(fuente); }
bool AppRuntime::stopSourceRecording(const std::string& source) { return emision_->stopSourceRecording(source); }
void AppRuntime::stopAllSourceRecordings() { emision_->stopAllSourceRecordings(); }
std::string AppRuntime::iniciarSenalLimpia(const std::string& destino, const std::string& salida) {
    return emision_->iniciarSenalLimpia(destino, salida);
}
void AppRuntime::detenerSenalLimpia(const std::string& salida) { emision_->detenerSenalLimpia(salida); }
std::string AppRuntime::aplicarReplay() { return emision_->aplicarReplay(); }
std::string AppRuntime::ponerReplay(int segundos, const std::string& salida) { return emision_->ponerReplay(segundos, salida); }
bool AppRuntime::startStreaming(const std::string& server, const std::string& key, bool cambiarEnCaliente,
                                const std::string& salida, AlResultado listo) {
    const TurnoMotor::Guarda turno(engine_->turno());
    return emision_->startStreaming(server, key, cambiarEnCaliente, salida, std::move(listo));
}
void AppRuntime::stopStreaming(const std::string& salida) {
    const TurnoMotor::Guarda turno(engine_->turno());
    emision_->stopStreaming(salida);
}
bool AppRuntime::salirAlAireManual(const std::string& server, const std::string& key, bool cambiarEnCaliente,
                                   const std::string& salida, AlResultado listo) {
    const TurnoMotor::Guarda turno(engine_->turno());
    return emision_->salirAlAireManual(server, key, cambiarEnCaliente, salida, std::move(listo));
}
void AppRuntime::alVivoManual(std::function<void(const std::string& servidor)> f) { emision_->alVivoManual(std::move(f)); }
bool AppRuntime::streamKeySaved() const { return emision_->streamKeySaved(); }
bool AppRuntime::streamStopWanted() const { return !emision_->pedidos().de(salidas::kPorOmision).transmitePedida(); }
bool AppRuntime::recordStopWanted() const { return !emision_->pedidos().de(salidas::kPorOmision).grabaPedida(); }
nlohmann::json AppRuntime::destinos() const { return emision_->destinos(); }
std::string AppRuntime::guardarDestino(const nlohmann::json& d, std::string* idOut) { return emision_->guardarDestino(d, idOut); }
std::string AppRuntime::borrarDestino(const std::string& id) {
    const TurnoMotor::Guarda turno(engine_->turno());
    return emision_->borrarDestino(id);
}
std::string AppRuntime::iniciarDestino(const std::string& id) {
    const TurnoMotor::Guarda turno(engine_->turno());
    return emision_->iniciarDestino(id);
}
nlohmann::json AppRuntime::discoGrabacion() const {
    const TurnoMotor::Guarda turno(engine_->turno());
    return emision_->discoGrabacion();
}
void AppRuntime::detenerDestino(const std::string& id) {
    const TurnoMotor::Guarda turno(engine_->turno());
    emision_->detenerDestino(id);
}

// --- Escenas y fuentes (RepositorioEscenas) ----------------------------------------------
bool AppRuntime::addScene(const std::string& name) { return escenas_->addScene(name); }
bool AppRuntime::removeScene(const std::string& name) { return escenas_->removeScene(name); }
bool AppRuntime::copyScene(const std::string& from, const std::string& to) { return escenas_->copyScene(from, to); }
bool AppRuntime::setSceneProtected(const std::string& name, bool protegida) { return escenas_->setSceneProtected(name, protegida); }
bool AppRuntime::sceneProtected(const std::string& name) const { return escenas_->sceneProtected(name); }
bool AppRuntime::renameScene(const std::string& from, const std::string& to) { return escenas_->renameScene(from, to); }
std::string AppRuntime::sceneFolder(const std::string& name) const { return escenas_->sceneFolder(name); }
bool AppRuntime::setSceneFolder(const std::string& name, const std::string& folder) { return escenas_->setSceneFolder(name, folder); }
std::vector<std::string> AppRuntime::sceneFolders() const { return escenas_->sceneFolders(); }
std::vector<std::string> AppRuntime::orderedSceneNames() const { return escenas_->orderedSceneNames(); }
bool AppRuntime::moveScene(const std::string& name, const std::string& before) { return escenas_->moveScene(name, before); }
bool AppRuntime::setScene(const std::string& name, const std::string& salida) {
    const TurnoMotor::Guarda turno(engine_->turno());
    return escenas_->setScene(name, salida);
}
bool AppRuntime::setSceneCon(const std::string& name, const std::string& tipo, int ms, const std::string& salida) {
    const TurnoMotor::Guarda turno(engine_->turno());
    return escenas_->setSceneCon(name, tipo, ms, salida);
}
void AppRuntime::anotarEscenaAlAire(const std::string& name, const std::string& salida) {
    const TurnoMotor::Guarda turno(engine_->turno());
    escenas_->anotarEscenaAlAire(name, salida);
}
bool AppRuntime::setTransition(const std::string& type, int durationMs) { return escenas_->setTransition(type, durationMs); }
bool AppRuntime::addSource(const std::string& scene, const SourceSpec& spec) { return escenas_->addSource(scene, spec); }
bool AppRuntime::removeSource(const std::string& scene, const std::string& name) { return escenas_->removeSource(scene, name); }
bool AppRuntime::moveSource(const std::string& scene, const std::string& name, int delta) { return escenas_->moveSource(scene, name, delta); }
bool AppRuntime::renameSource(const std::string& from, const std::string& to) { return escenas_->renameSource(from, to); }
bool AppRuntime::setSourceVisible(const std::string& scene, const std::string& name, bool visible) {
    return escenas_->setSourceVisible(scene, name, visible);
}
bool AppRuntime::setSourceRect(const std::string& scene, const std::string& name, const CanvasRect& rect) {
    return escenas_->setSourceRect(scene, name, rect);
}
bool AppRuntime::setSourceEfectos(const std::string& scene, const std::string& name, const EfectosCapa& efectos) {
    return escenas_->setSourceEfectos(scene, name, efectos);
}
bool AppRuntime::setSourceCrop(const std::string& scene, const std::string& name, float izquierda, float arriba, float derecha,
                               float abajo) {
    return escenas_->setSourceCrop(scene, name, izquierda, arriba, derecha, abajo);
}
bool AppRuntime::updateSourceSettings(const std::string& name, const std::string& settingsJson) {
    return escenas_->updateSourceSettings(name, settingsJson);
}
bool AppRuntime::setGananciaGrupo(const std::string& grupo, float mul, const std::string& salida) {
    return escenas_->setGananciaGrupo(grupo, mul, salida);
}
bool AppRuntime::setVolume(const std::string& name, float mul, const std::string& salida) { return escenas_->setVolume(name, mul, salida); }
bool AppRuntime::setMuted(const std::string& name, bool muted, const std::string& salida) { return escenas_->setMuted(name, muted, salida); }
std::optional<SourceSpec> AppRuntime::specDe(const std::string& scene, const std::string& name) const { return escenas_->specDe(scene, name); }
bool AppRuntime::addAudioInput(const SourceSpec& spec) { return escenas_->addAudioInput(spec); }
bool AppRuntime::removeAudioInput(const std::string& name) { return escenas_->removeAudioInput(name); }

// --- El aire que sobrevive a un corte (AireDurable) --------------------------------------
void AppRuntime::setDuenoDelAire(std::function<void()> despertar) { aire_->setDuenoDelAire(std::move(despertar)); }
bool AppRuntime::corteAnterior() const { return aire_->corteAnterior(); }
const salidas::AlAire& AppRuntime::aireAnterior() const { return aire_->aireAnterior(); }
bool AppRuntime::lineaDesconocida() const { return aire_->lineaDesconocida(); }
bool AppRuntime::placaRestaurada() const { return aire_->placaRestaurada(); }
salidas::DocumentoAire& AppRuntime::documentoAire() { return aire_->documento(); }
void AppRuntime::aireCambio() { aire_->aireCambio(); }
std::vector<std::string> AppRuntime::avisosPersistencia() const { return aire_->avisosPersistencia(); }
void AppRuntime::avisarPersistencia(const std::string& texto) { aire_->avisarPersistencia(texto); }
std::vector<std::string> AppRuntime::cerrarGrabacionesPorEnergia() {
    const TurnoMotor::Guarda turno(engine_->turno());
    return aire_->cerrarGrabacionesPorEnergia();
}
void AppRuntime::retomarGrabacionesPorEnergia() {
    const TurnoMotor::Guarda turno(engine_->turno());
    aire_->retomarGrabacionesPorEnergia();
}
bool AppRuntime::emergenciaEnergia() const { return aire_->emergenciaEnergia(); }

// --- Invitados y salud del proceso ------------------------------------------------------
void AppRuntime::ajustarInvitados(int n) { invitados_->ajustar(n); }
LatidoAire& AppRuntime::latido() { return salud_->latido(); }
std::uint32_t AppRuntime::pid() const { return SaludProceso::pid(); }
bool AppRuntime::gatewayAlive() const { return salud_->gatewayAlive(); }
std::uint32_t AppRuntime::gatewayPid() const { return salud_->gatewayPid(); }
void AppRuntime::reiniciarSalaEnElFondo(std::function<void(const std::string& error)> listo) {
    salud_->reiniciarSalaEnElFondo(std::move(listo));
}

} // namespace ba::app
