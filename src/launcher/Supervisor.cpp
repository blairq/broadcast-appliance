// Supervisor.cpp
//
// Implementacion del supervisor del launcher (ver Supervisor.hpp). Sin codigo
// de SO propio: procesos, instancia unica y canal de control van por
// ba::platform. La unica excepcion es el minidump de Windows, que se mueve a
// la capa de plataforma con Crashpad (plan multiplataforma, fase 9).

#include "Supervisor.hpp"
#include "ajustes/Ajustes.hpp"

#include "Integrity.hpp"
#include "Log.hpp"
#include "i18n/I18n.hpp"
#include "RecoveryUi.hpp"
#include "ShadowCopy.hpp"
#include "Sha256.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <chrono>
#include <vector>
#include <filesystem>
#include <ctime>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ba::launcher {

using ba::i18n::llenar;
using ba::i18n::traducir;

std::atomic<bool> Supervisor::stopRequested_{false};

namespace {

// UTF-8 (los textos traducidos) a ancho, el título de la ventana de recuperación.
std::wstring ancho(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())).wstring(); }

constexpr int kPollMs = 250;
// La ventana y el tope del circuit breaker, el cuelgue, la gracia de arranque y
// cuántas fallas se reinician sin preguntar van en Config (launcher.json): ver
// Supervisor::Config. El dialogo de recuperacion BLOQUEA el reinicio hasta que
// alguien hace clic: una caida aislada al aire se reinicia sola; recien con
// fallas repetidas en la ventana se le pregunta al operador.
inline int kGracefulMs() { return ba::ajustes::entero("launcher.cierreOrdenadoMs", 5000); } // espera tras el pedido de cierre
inline int kTerminateMs() { return ba::ajustes::entero("launcher.terminarMs", 3000); } // espera tras SIGTERM antes de matar
// Gracia de arranque (cfg_.graciaArranqueMs): un hijo recien lanzado todavia no
// late. Sin esto el watchdog lo declaraba colgado en el mismo milisegundo y lo
// mataba. Un crash durante el arranque se detecta igual (el proceso sale).
// Si todavía no latió NUNCA, se lo espera más: el primer arranque después de
// instalar o actualizar, macOS verifica la firma de cientos de binarios antes
// de dejarlos correr (9 s solo para cargar, comprobado) y a los 20 s se lo
// mataba en pleno arranque. Un cuelgue de verdad al arrancar se ve igual.
inline std::uint64_t kPrimerLatidoMs() { return static_cast<std::uint64_t>(ba::ajustes::entero("launcher.primerLatidoS", 90)) * 1000; }
// Espera antes de relanzar un hijo que salio con exit_code::kEnvironment.
inline std::uint64_t kEnvironmentRetryMs() { return static_cast<std::uint64_t>(ba::ajustes::entero("launcher.reintentoEntornoS", 60)) * 1000; }
// Un hijo que anduvo tanto tiempo con el latido fresco se considera estable: sus
// caídas anteriores se olvidan y la próxima se relanza sin espera (Relanzador).
inline std::uint64_t kEstableMs() { return static_cast<std::uint64_t>(ba::ajustes::entero("launcher.estableS", 120)) * 1000; }
// Cada cuánto se mira si terminó el aire que frena el reemplazo de un hijo de otra versión.
constexpr std::uint64_t kMirarAireMs = 5000;

std::uint64_t nowMs() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

/// Duerme `ms` en tramos cortos, cortando antes si se pidio parar.
void sleepInterruptible(int ms, const std::atomic<bool>& stop) {
    for (int waited = 0; waited < ms && !stop.load(); waited += 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}


} // namespace

Supervisor::Supervisor(Config cfg) : cfg_(std::move(cfg)), volcados_(std::make_unique<Volcados>(cfg_.logsDir)) {}

Supervisor::~Supervisor() {
    pararFondo_ = true;
    if (fondo_.joinable()) fondo_.join();
}

void Supervisor::requestStop() { stopRequested_.store(true); }

bool Supervisor::inStartupGrace(bool isA) const {
    const std::uint64_t at = launchedAtMs_[isA ? 1 : 0];
    if (at == 0) return false;
    const std::uint64_t pasado = nowMs() - at;
    if (pasado < cfg_.graciaArranqueMs) return true;
    const bool latio =
        health_.read(isA ? cfg_.slotA : cfg_.slotB).lastBeatQpc != latidoAlLanzar_[isA ? 1 : 0];
    return !latio && pasado < kPrimerLatidoMs();
}

int Supervisor::run() {
    std::string err;
    // --reiniciar (cambiar de configuración): el anterior se está cerrando; se lo espera.
    const auto hasta = std::chrono::steady_clock::now() + std::chrono::milliseconds(cfg_.esperarInstanciaMs);
    while (!ba::platform::SingleInstance::acquire("launcher", instance_, &err)) {
        if (std::chrono::steady_clock::now() >= hasta) {
            // Segunda ejecucion (spec 3.1.1): la primera sigue a cargo.
            BA_LOG_INFO("instance", 0, llenar(traducir("instance", "no se inicia: %1"), {err}));
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    ba::platform::suppressCrashDialogs(); // spec 3.1.4; los hijos lo heredan

    const IntegrityReport integrityReport = verifyIntegrity();
    if (!integrityReport.ok && cfg_.recoveryUi) {
        // Spec 3.1.2: "mensaje claro en lugar de crashear". No es fatal.
        Incident inc;
        inc.title = ancho(traducir("recuperacion", "Broadcast Appliance — Integridad"));
        inc.showIntegrity = true;
        inc.integrity = integrityReport;
        if (RecoveryUi::present(inc) == RecoveryAction::Exit) {
            BA_LOG_INFO("sup", 0, traducir("sup", "operador eligio salir ante fallos de integridad"));
            return 1;
        }
        BA_LOG_INFO("sup", 0, traducir("sup", "operador eligio continuar pese a fallos de integridad"));
    }

    // Copia sombra (spec 3.1.5): se ejecuta desde una copia para poder
    // actualizar el bundle en disco con la app corriendo.
    if (cfg_.shadowCopy) {
        // La clave sale del CONTENIDO: con una clave fija, tras actualizar los
        // binarios se seguia ejecutando la copia vieja para siempre.
        applyExecRoot(ShadowCopy::prepare(cfg_.root.wstring(), ShadowCopy::versionKey(cfg_.root.wstring())));
    }

    // Bloque de salud: el launcher es el owner (crea); los hijos abren. Si ya
    // existe (launcher anterior caido con A/B vivos), se reabre para readoptar.
    const std::size_t slotCount = std::max<std::size_t>(ba::ipc::kSlotsSalud, std::max(cfg_.slotA, cfg_.slotB) + 1);
    auto st = ba::ipc::HealthBlock::open(ba::ipc::HealthBlock::kName, health_);
    if (!st.ok) {
        st = ba::ipc::HealthBlock::create(ba::ipc::HealthBlock::kName, slotCount, health_);
    }
    if (!st.ok) {
        BA_LOG_ERROR("health", st.code, traducir("health", "no se pudo crear BA.Health"));
        return 1;
    }
    BA_LOG_INFO("health", 0, llenar(traducir("health", "BA.Health listo con %1 slots"), {std::to_string(slotCount)}));

    launchChildren();
    // Lo que la verificación rápida no leyó (archivos nuevos o cambiados, o todo la
    // primera vez), con la app ya andando y a prioridad de fondo.
    fondo_ = std::thread([this]() { verificarEnElFondo(); });
    watchdogLoop();
    pararFondo_ = true;
    if (fondo_.joinable()) fondo_.join();
    return 0;
}

void Supervisor::applyExecRoot(const std::filesystem::path& execRoot) {
    if (execRoot.empty() || execRoot == cfg_.root) {
        BA_LOG_INFO("shadow", 0, traducir("shadow", "ejecutando desde la raiz original"));
        return;
    }
    // Reapunta A y B a la copia conservando su ruta relativa (bin/64bit/..).
    // El workDir sigue siendo la raiz REAL: Config/, Logs/ y Recordings/ viven
    // en el disco, no en la copia (spec 3.1.5).
    auto rebase = [&](const std::filesystem::path& exe) {
        const std::filesystem::path rel = exe.lexically_relative(cfg_.root);
        return (rel.empty() || *rel.begin() == "..") ? exe : execRoot / rel;
    };
    cfg_.appExe = rebase(cfg_.appExe);
    cfg_.gatewayExe = rebase(cfg_.gatewayExe);
    BA_LOG_INFO("shadow", 0, llenar(traducir("shadow", "ejecutando desde la copia sombra: %1"), {execRoot.string()}));
}

std::filesystem::path Supervisor::cacheIntegridad() const {
    return Integrity::cacheEn(cfg_.dataRoot.empty() ? cfg_.root : cfg_.dataRoot);
}

std::filesystem::path Supervisor::marcaFallo() const {
    return cacheIntegridad().parent_path() / "integridad-fallo.txt";
}

IntegrityReport Supervisor::verifyIntegrity() {
    // ANTES DE ARRANCAR, SOLO LO BARATO: que cada archivo esté y coincida en tamaño y
    // fecha con el caché, sin leer contenido (el antivirus no escanea, la app aparece
    // ya). Lo demás, en el fondo (verificarEnElFondo). Si la vez anterior el fondo
    // encontró algo, esta vez es completa: el diálogo aparece antes de arrancar.
    std::error_code ec;
    const bool huboFallo = std::filesystem::exists(marcaFallo(), ec);
    const IntegrityReport report =
        Integrity::verify(cfg_.root.wstring(), huboFallo ? std::filesystem::path() : cacheIntegridad(), 0,
                          huboFallo ? Integrity::Modo::Completa : Integrity::Modo::Rapida);
    if (huboFallo && report.ok) std::filesystem::remove(marcaFallo(), ec);
    if (report.ok) {
        BA_LOG_INFO("integrity", 0,
                    llenar(traducir("integrity",
                                    "integridad OK antes de arrancar (%1 archivos, %2 leídos, %3 para leer en el fondo)"),
                           {std::to_string(report.checked), std::to_string(report.hashed),
                            std::to_string(report.pendientes)}));
    } else {
        BA_LOG_ERROR("integrity", static_cast<int>(report.failures.size()),
                     llenar(traducir("integrity", "integridad: %1 ausentes, %2 con hash distinto"),
                            {std::to_string(report.missing), std::to_string(report.mismatch)}));
    }
    return report;
}

void Supervisor::verificarEnElFondo() {
    const auto t0 = std::chrono::steady_clock::now();
    const IntegrityReport r =
        Integrity::verify(cfg_.root.wstring(), cacheIntegridad(), 2, Integrity::Modo::Completa, &pararFondo_, true);
    if (pararFondo_) return; // se cerró antes de terminar: queda para la próxima
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    if (r.ok) {
        // El caché, YA: no depende de un cierre ordenado.
        Integrity::guardarCache(cacheIntegridad());
        BA_LOG_INFO("integrity", 0,
                    llenar(traducir("integrity", "integridad en el fondo OK (%1 leídos en %2 ms)"),
                           {std::to_string(r.hashed), std::to_string(ms)}));
        return;
    }
    BA_LOG_ERROR("integrity", static_cast<int>(r.failures.size()),
                 llenar(traducir("integrity",
                                 "integridad en el fondo: %1 ausentes, %2 con hash distinto (el próximo arranque lo muestra)"),
                        {std::to_string(r.missing), std::to_string(r.mismatch)}));
    std::error_code ec;
    std::filesystem::create_directories(marcaFallo().parent_path(), ec);
    std::ofstream m(marcaFallo());
    for (const auto& f : r.failures) m << toUtf8Path(f) << "\n";
}

void Supervisor::launchChildren() {
    // B primero, luego A (spec 3.1.6).
    launchOne(false);
    launchOne(true);
}

void Supervisor::launchOne(bool isA) {
    const std::uint32_t slot = isA ? cfg_.slotA : cfg_.slotB;
    const std::string name = isA ? "A" : "B";
    // Readopt (spec 3.1.9): si hay un PID publicado, vivo Y con heartbeat
    // fresco, adoptarlo. El heartbeat es la prueba de que es NUESTRO proceso:
    // un PID viejo podria haber sido reutilizado por cualquier otro programa.
    // El adoptado sigue escribiendo en su archivo de Logs/, que ya tiene
    // abierto: no se rota ni se encabeza hasta que haya que relanzarlo.
    // Y tiene que ser de ESTA versión: tras actualizar, un A o B viejo que quedó
    // corriendo no se adopta; se cierra en orden y se lanza el nuevo. Con salidas
    // al aire eso cortaría la emisión: se lo adopta por ahora, se avisa y se lo
    // reemplaza en cuanto no haya aire (watchdogLoop).
    const ba::ipc::Snapshot snap = health_.read(slot);
    const bool vivo = snap.pid != 0 && !health_.isStale(slot) && child(isA).attach(snap.pid, name);
    const bool otra = vivo && snap.version != ba::ipc::kVersionProceso;
    const std::string pid = std::to_string(snap.pid);
    const std::string v = std::to_string(snap.version);
    const std::string esperada = std::to_string(ba::ipc::kVersionProceso);
    switch (decidirReadopcion(vivo, snap.version, ba::ipc::kVersionProceso, otra && hayAire(health_, runtimeDir()))) {
        case Readopcion::Lanzar:
            startChild(isA);
            return;
        case Readopcion::Adoptar:
            BA_LOG_INFO("sup", 0, llenar(traducir("sup", "readopt: adoptado %1 pid=%2"), {name, pid}));
            relanzador(isA).lanzado(nowMs());
            return;
        case Readopcion::Posponer:
            BA_LOG_WARN("sup", 0,
                        llenar(traducir("sup", "readopt: %1 pid=%2 es de otra versión (%3; esta es %4) y hay salidas al "
                                               "aire: se adopta hasta que terminen y después se reemplaza"),
                               {name, pid, v, esperada}));
            otraVersion_[isA ? 1 : 0] = true;
            relanzador(isA).lanzado(nowMs());
            return;
        case Readopcion::Reemplazar:
            BA_LOG_WARN("sup", 0,
                        llenar(traducir("sup", "readopt: %1 pid=%2 es de otra versión (%3; esta es %4): se cierra y se "
                                               "lanza el de esta versión"),
                               {name, pid, v, esperada}));
            reemplazarOtraVersion(isA);
            return;
    }
}

void Supervisor::reemplazarOtraVersion(bool isA) {
    stopChild(isA);
    child(isA).close();
    startChild(isA);
}

std::filesystem::path Supervisor::runtimeDir() const {
    return (cfg_.dataRoot.empty() ? cfg_.root : cfg_.dataRoot) / "Runtime";
}

bool Supervisor::startChild(bool isA) {
    const std::uint32_t slot = isA ? cfg_.slotA : cfg_.slotB;
    ChildProcess::Config c;
    c.exePath = isA ? cfg_.appExe : cfg_.gatewayExe;
    // Los hijos trabajan en la raiz de los DATOS (y la reciben en BA_DATA_ROOT):
    // en macOS es la carpeta al lado de la .app, no el bundle.
    c.workDir = cfg_.dataRoot.empty() ? cfg_.root : cfg_.dataRoot;
    c.name = isA ? "A" : "B";
    c.args = isA ? cfg_.appArgs : cfg_.gatewayArgs;
    if (isA && appNeedsRestore_) {
        c.args.emplace_back("--restore"); // spec 3.1.8
    }
    if (isA) {
        // El modo seguro escalonado (F1-10): el nivel y lo que se apaga por la causa.
        for (const std::string& a : modoSeguro_.argumentos()) c.args.push_back(a);
    }
    // Lo que el hijo imprime queda en Logs/ de la raiz REAL. A informa todo por
    // stdout/stderr y no tiene otro log. B ya deja sus lineas en Logs/ba.log,
    // pero lo que no pasa por su logger se perdia: libwebrtc, los asertos de
    // lwIP, un abort() o una biblioteca que no carga.
    // Un archivo por proceso, con el nombre del proceso: es lo que el visor
    // usa para saber de donde salio cada linea.
    c.outputFile = cfg_.logsDir / (isA ? "app.log" : "gateway.log");
    // El canal para que avise si se cae (y el volcado salga con la excepción).
    c.env = volcados_->canalPara(c.name, c.outputFile);
    if (!child(isA).start(c)) {
        return false;
    }
    volcados_->lanzado(c.name, child(isA).pid());
    launchedAtMs_[isA ? 1 : 0] = nowMs();
    relanzador(isA).lanzado(launchedAtMs_[isA ? 1 : 0]);
    otraVersion_[isA ? 1 : 0] = false; // lo que se lanza es siempre de esta versión
    health_.setIdentity(slot, child(isA).pid(), ba::ipc::kVersionProceso);
    latidoAlLanzar_[isA ? 1 : 0] = health_.read(slot).lastBeatQpc;
    health_.setState(slot, ba::ipc::ProcessState::Running);
    return true;
}

void Supervisor::restartChild(bool isA) {
    if (isA) {
        appCrashTimes_.push_back(nowMs());
        const std::uint64_t now = nowMs();
        const std::uint64_t cutoff = now > cfg_.ventanaCaidasMs ? now - cfg_.ventanaCaidasMs : 0;
        appCrashTimes_.erase(std::remove_if(appCrashTimes_.begin(), appCrashTimes_.end(),
                                            [cutoff](std::uint64_t t) { return t < cutoff; }),
                             appCrashTimes_.end());
        if (static_cast<int>(appCrashTimes_.size()) > cfg_.maxCaidas && modoSeguro_.nivel < ba::fault::kNivelMaximo) {
            // Cortacircuito (spec 3.1.8), ESCALONADO (F1-10): un escalón por disparo.
            // Los niveles 1 y 2 restauran las escenas; el 3 es el de siempre. Si la
            // última caída se atribuyó a una pieza (Volcados), esa se apaga ya,
            // aunque su nivel no haya llegado. La ventana vuelve a empezar: el
            // escalón siguiente pide otra tanda de caídas.
            ++modoSeguro_.nivel;
            if (ultimaPiezaA_) modoSeguro_.extra.insert(*ultimaPiezaA_);
            appCrashTimes_.clear();
            std::string apagadas;
            for (ba::fault::Pieza p : modoSeguro_.apagadas()) apagadas += std::string(apagadas.empty() ? "" : ",") + ba::fault::clave(p);
            BA_LOG_WARN("sup", 0,
                        llenar(traducir("sup", "cortacircuito: A pasa al modo seguro nivel %1 (sin: %2)"),
                               {std::to_string(modoSeguro_.nivel), apagadas}));
        }
        appNeedsRestore_ = true;
    }
    // Se AGENDA, no se duerme: mientras uno espera su backoff el bucle sigue
    // vigilando (y relanzando) al otro. Sin espera, se relanza en esta misma vuelta.
    const std::uint64_t espera = relanzador(isA).caida(nowMs());
    if (espera > 0) {
        BA_LOG_INFO("sup", 0, llenar(traducir("sup", "%1: reinicio en %2 s"), {isA ? "A" : "B", std::to_string(espera / 1000)}));
    }
    relanzarAgendados();
}

void Supervisor::relanzarAgendados() {
    if (stopRequested_.load()) return;
    const std::uint64_t ahora = nowMs();
    // B primero, como al arrancar (spec 3.1.6).
    for (bool isA : {false, true}) {
        if (!relanzador(isA).toca(ahora)) continue;
        if (startChild(isA)) {
            if (isA) appNeedsRestore_ = false;
        } else {
            // No arrancó: otra vuelta del backoff (no se reintenta cada 250 ms).
            const std::uint64_t espera = relanzador(isA).caida(ahora);
            BA_LOG_WARN("sup", 0,
                        llenar(traducir("sup", "%1 no se pudo lanzar; reintento en %2 s"),
                               {isA ? "A" : "B", std::to_string(espera / 1000)}));
        }
    }
}


void Supervisor::anotarCaida(bool isA, const std::string& causa, std::optional<int> code, const Volcado* v) {
    // Runtime/ultima-caida.json lo lee la app al volver (el cartel con la causa);
    // Runtime/caidas.jsonl queda como historial (supervisor_caidas).
    const std::filesystem::path dir = runtimeDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    nlohmann::json j{{"componente", isA ? "app" : "gateway"},
                     {"causa", causa},
                     {"codigo", code ? nlohmann::json(*code) : nlohmann::json()},
                     {"volcado", v == nullptr || v->dmp.empty() ? nlohmann::json() : nlohmann::json(v->dmp.filename().string())},
                     {"hora_ms", ms}};
    // De quién fue (F1-12): el módulo de la pila del que se cayó y la pieza que el
    // modo seguro puede apagar (F1-10). Para saber qué aislar (F3-05).
    const ba::fault::Atribucion at = v != nullptr ? v->atribucion : ba::fault::Atribucion{};
    if (!at.modulo.empty()) j["modulo"] = at.modulo;
    if (at.pieza) j["pieza"] = ba::fault::clave(*at.pieza);
    if (isA) ultimaPiezaA_ = at.pieza;
    {
        std::ofstream o(dir / "ultima-caida.json", std::ios::trunc);
        o << j.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
    }
    std::ofstream h(dir / "caidas.jsonl", std::ios::app);
    h << j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
}

void Supervisor::handleCrash(bool isA) {
    const std::optional<int> code = child(isA).exitCode();
    const std::string name = isA ? "A" : "B";
    child(isA).close();
    if (code == ba::platform::exit_code::kEnvironment) {
        // El hijo avisa que el entorno no le permite funcionar (p. ej. A sin
        // contexto grafico). Relanzarlo cada pocos segundos solo llenaria el
        // log: se espera bastante y se reintenta por si el entorno cambia
        // (alguien inicia la sesion grafica).
        BA_LOG_ERROR("sup", *code,
                     llenar(traducir("sup", "%1 no puede funcionar en este entorno (ver su log: p. ej. sin sesion "
                                            "grafica); reintento en %2 s"),
                            {name, std::to_string(kEnvironmentRetryMs() / 1000)}));
        relanzador(isA).agendar(nowMs(), kEnvironmentRetryMs());
        return;
    }
    // La sala cerrada EN ORDEN (código 0): la app la reinició a propósito para que
    // relea gateway.json (Configuración → aplicar). No es una caída: no va al cartel
    // «se reinició» ni cuenta para el cortacircuito; se relanza ya.
    // Dos seguidas en menos de 10 s ya no es un reinicio pedido: camino normal.
    if (!isA && esCierreOrdenadoB(code)) {
        ultimoCierreOrdenadoB_ = nowMs();
        BA_LOG_INFO("sup", 0, traducir("sup", "B se cerró en orden (reinicio de la sala); se relanza"));
        startChild(false);
        return;
    }
    BA_LOG_WARN("sup", code.value_or(-1), llenar(traducir("sup", "%1 termino inesperadamente; reiniciando con backoff"), {name}));
    // Si avisó la caída, la causa sale del volcado (qué excepción y en qué módulo).
    const std::optional<Volcado> v = volcados_->tomarCaida(name);
    anotarCaida(isA, v ? v->causa : causaDeCodigo(code), code, v ? &*v : nullptr);
    restartChild(isA);
}

bool Supervisor::esCierreOrdenadoB(std::optional<int> code) {
    if (!code) code = gateway_.exitCode();
    return code && *code == 0 && !volcados_->hayCaida("B") && nowMs() - ultimoCierreOrdenadoB_ > 10000;
}

void Supervisor::handleHang(bool isA) {
    const std::uint32_t slot = isA ? cfg_.slotA : cfg_.slotB;
    const std::string name = isA ? "A" : "B";
    if (volcados_->hayCaida(name)) {
        // No es un cuelgue: se cayó (ya avisó y está volcado) y Windows lo
        // retiene mientras arma su informe. Se lo trata como la caída que es.
        const std::optional<Volcado> v = volcados_->tomarCaida(name);
        BA_LOG_WARN("sup", 0,
                    llenar(traducir("sup", "%1 se cayó (%2); terminar y relanzar"), {name, v ? v->causa : std::string("?")}));
        anotarCaida(isA, v ? v->causa : traducir("caidas", "se cayó"), std::nullopt, v ? &*v : nullptr);
    } else {
        BA_LOG_WARN("sup", 0, llenar(traducir("sup", "%1 colgado (heartbeat estancado); volcado + terminar"), {name}));
        // Uno por cuelgue, con la hora y un .json que dice qué pasó (Volcados.hpp).
        const Volcado v = volcados_->porCuelgue(name, child(isA).pid(), cfg_.logsDir / (isA ? "app.log" : "gateway.log"));
        anotarCaida(isA, traducir("caidas", "se colgó (dejó de latir)"), std::nullopt, &v);
    }
    child(isA).kill();
    child(isA).waitExit(kTerminateMs());
    health_.setState(slot, ba::ipc::ProcessState::Hung);
    child(isA).close();
    restartChild(isA);
}

void Supervisor::presentRecovery(bool appDown, bool appHung, bool gwDown, bool gwHung) {
    if (preguntando_->load()) return; // ya hay un cartel abierto
    Incident inc;
    inc.title = ancho(traducir("recuperacion", "Broadcast Appliance — Recuperación"));
    inc.childADown = appDown || appHung;
    inc.childBDown = gwDown || gwHung;
    if (appDown) {
        inc.appExitCode = static_cast<std::uint32_t>(app_.exitCode().value_or(-1));
        inc.appCodeValid = true;
    }
    if (gwDown) {
        inc.gatewayExitCode = static_cast<std::uint32_t>(gateway_.exitCode().value_or(-1));
        inc.gwCodeValid = true;
    }
    // Un hijo caido podria deberse a un archivo corrupto: se muestra la
    // integridad, cacheada para no re-hashear el disco en un loop de crashes.
    inc.showIntegrity = true;
    // (Se verifica en el hilo del cartel: puede tardar y aquí no se frena nada.)
    const bool verificar = true;
    preguntando_->store(true);
    // Desprendido: el diálogo puede quedar abierto minutos y el launcher puede cerrar
    // antes; lo compartido va por shared_ptr.
    std::thread([inc, verificar, root = cfg_.root, decision = decisionRecuperacion_, preg = preguntando_]() mutable {
        if (verificar) inc.integrity = Integrity::verify(root.wstring());
        const RecoveryAction a = RecoveryUi::present(inc);
        if (a == RecoveryAction::Exit || a == RecoveryAction::ForceRestart) decision->store(static_cast<int>(a));
        preg->store(false);
    }).detach();
}

void Supervisor::forceRestart() {
    BA_LOG_WARN("sup", 0, traducir("sup", "forzar reinicio: terminando hijos y relanzando desde 0"));
    for (bool isA : {true, false}) {
        child(isA).kill();
        child(isA).waitExit(kTerminateMs());
        child(isA).close();
    }
    health_.setState(cfg_.slotA, ba::ipc::ProcessState::Terminated);
    health_.setState(cfg_.slotB, ba::ipc::ProcessState::Terminated);
    relanzador(true).olvidar();
    relanzador(false).olvidar();
    recentFailures_ = 0;
    appCrashTimes_.clear();
    modoSeguro_ = {};
    ultimaPiezaA_.reset();
    appNeedsRestore_ = true;
    launchChildren();
}

void Supervisor::watchdogLoop() {
    while (!shuttingDown_) {
        if (stopRequested_.load()) {
            BA_LOG_INFO("sup", 0, traducir("sup", "cierre pedido (señal/sesion)"));
            orderedShutdown();
            break;
        }
        // El que espera su relanzamiento agendado no está «caído» de nuevo: ya se lo
        // atendió; se lo lanza cuando toque (relanzarAgendados, al final de la vuelta).
        const bool appFuera = relanzador(true).agendado();
        const bool gwFuera = relanzador(false).agendado();
        const bool appDown = !appFuera && !app_.alive();
        const bool gwDown = !gwFuera && !gateway_.alive();
        const bool appHung = !appFuera && !appDown && !inStartupGrace(true) && health_.isStale(cfg_.slotA, ba::ipc::HealthBlock::nowQpc(), cfg_.cuelgueMs * 1000000ULL);
        const bool gwHung = !gwFuera && !gwDown && !inStartupGrace(false) && health_.isStale(cfg_.slotB, ba::ipc::HealthBlock::nowQpc(), cfg_.cuelgueMs * 1000000ULL);

        // A salio con codigo 0: cierre ordenado del operador, no es un fallo.
        if (appDown && app_.exitCode() == 0) {
            BA_LOG_INFO("sup", 0, traducir("sup", "A salio limpiamente (codigo 0); cierre ordenado"));
            orderedShutdown();
            break;
        }

        // La sala cerrada EN ORDEN (reinicio pedido por la app, p. ej. al aplicar la
        // configuración o cambiar el acceso remoto) no es una falla: va directo a
        // handleCrash, que la relanza, SIN contar para el cartel de recuperación. Antes
        // contaba: al tercer reinicio seguido salía el diálogo de «la sala se cerró» y
        // la sala quedaba abajo esperando que alguien contestara.
        if (gwDown && !appDown && !appHung && esCierreOrdenadoB()) {
            handleCrash(false);
            sleepInterruptible(kPollMs, stopRequested_);
            continue;
        }

        // Lo que contestaron en el cartel (si contestaron): se toma acá, sin haber frenado nada.
        const int decision = decisionRecuperacion_->exchange(-1);
        if (decision == static_cast<int>(RecoveryAction::Exit)) {
            BA_LOG_INFO("sup", 0, traducir("sup", "recuperación: el operador eligió salir"));
            orderedShutdown();
            break;
        }
        if (decision == static_cast<int>(RecoveryAction::ForceRestart)) {
            forceRestart();
            sleepInterruptible(kPollMs, stopRequested_);
            continue;
        }

        if (appDown || appHung || gwDown || gwHung) {
            RecoveryAction action = RecoveryAction::Retry;
            const std::uint64_t failAt = nowMs();
            if (failAt - lastFailureMs_ > cfg_.ventanaCaidasMs) {
                recentFailures_ = 0;
            }
            lastFailureMs_ = failAt;
            ++recentFailures_;
            // Se pregunta solo si la que falla una y otra vez es la interfaz/motor (A): la
            // sala sola (B) se relanza sin molestar (la app ya avisa en su pantalla).
            if (cfg_.recoveryUi && recentFailures_ > cfg_.reiniciosSinPreguntar && (appDown || appHung)) {
                presentRecovery(appDown, appHung, gwDown, gwHung);
            }
            if (action == RecoveryAction::Exit) {
                orderedShutdown();
                break;
            }
            if (action == RecoveryAction::ForceRestart) {
                forceRestart();
            } else {
                if (appDown) {
                    handleCrash(true);
                } else if (appHung) {
                    handleHang(true);
                }
                if (gwDown) {
                    handleCrash(false);
                } else if (gwHung) {
                    handleHang(false);
                }
            }
        }
        // Los que andan bien hace rato: sus caídas viejas se olvidan (sin esto, en
        // un evento largo cada caída aislada esperaba más que la anterior, hasta 10 s).
        const std::uint64_t ahora = nowMs();
        const bool anda[2] = {!gwFuera && !gwDown && !gwHung && !inStartupGrace(false),
                              !appFuera && !appDown && !appHung && !inStartupGrace(true)};
        for (bool isA : {false, true}) {
            if (anda[isA ? 1 : 0] && relanzador(isA).vigilar(ahora, true, kEstableMs()))
                BA_LOG_INFO("sup", 0, llenar(traducir("sup", "%1 estable: las caídas anteriores se olvidan"), {isA ? "A" : "B"}));
        }
        // Un hijo readoptado de otra versión espera a que no haya aire para irse.
        if ((otraVersion_[0] || otraVersion_[1]) && ahora - ultimaMiradaAire_ >= kMirarAireMs) {
            ultimaMiradaAire_ = ahora;
            if (!hayAire(health_, runtimeDir())) {
                for (bool isA : {false, true}) {
                    if (!otraVersion_[isA ? 1 : 0] || !anda[isA ? 1 : 0]) continue;
                    BA_LOG_WARN("sup", 0,
                                llenar(traducir("sup", "%1 de otra versión: ya no hay aire, se reemplaza"), {isA ? "A" : "B"}));
                    reemplazarOtraVersion(isA);
                }
            }
        }
        relanzarAgendados();
        sleepInterruptible(kPollMs, stopRequested_);
    }
}

void Supervisor::stopChild(bool isA) {
    ChildProcess& c = child(isA);
    if (!c.alive()) {
        return;
    }
    const std::string name = isA ? "A" : "B";
    // 1) Pedido por el canal de control. 2) SIGTERM (POSIX). 3) Matar.
    if (control_.requestShutdown(isA ? ControlChannel::kAppChannel : ControlChannel::kGatewayChannel) &&
        c.waitExit(kGracefulMs())) {
        BA_LOG_INFO("sup", 0, llenar(traducir("sup", "%1 cerro por el canal de control"), {name}));
        return;
    }
    if (c.terminate() && c.waitExit(kTerminateMs())) {
        BA_LOG_INFO("sup", 0, llenar(traducir("sup", "%1 cerro tras la señal de terminacion"), {name}));
        return;
    }
    BA_LOG_WARN("sup", 0, llenar(traducir("sup", "%1 no cerro a tiempo; se fuerza"), {name}));
    c.kill();
    c.waitExit(kTerminateMs());
}

void Supervisor::orderedShutdown() {
    if (shuttingDown_) {
        return;
    }
    shuttingDown_ = true;
    BA_LOG_INFO("sup", 0, traducir("sup", "cierre ordenado: A primero (corta la emision), despues B"));
    stopChild(true);
    stopChild(false);
    health_.setState(cfg_.slotA, ba::ipc::ProcessState::Terminated);
    health_.setState(cfg_.slotB, ba::ipc::ProcessState::Terminated);
    // Sin PIDs publicados: un launcher futuro no debe intentar readoptar.
    health_.setIdentity(cfg_.slotA, 0, ba::ipc::kVersionProceso);
    health_.setIdentity(cfg_.slotB, 0, ba::ipc::kVersionProceso);
    Integrity::guardarCache(cacheIntegridad());
    BA_LOG_INFO("sup", 0, traducir("sup", "clean_shutdown marcado"));
}

} // namespace ba::launcher
