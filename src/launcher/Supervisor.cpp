// Supervisor.cpp
//
// Implementacion del supervisor del launcher (ver Supervisor.hpp). Sin codigo
// de SO propio: procesos, instancia unica y canal de control van por
// ba::platform. La unica excepcion es el minidump de Windows, que se mueve a
// la capa de plataforma con Crashpad (plan multiplataforma, fase 9).

#include "Supervisor.hpp"

#include "Integrity.hpp"
#include "Log.hpp"
#include "RecoveryUi.hpp"
#include "ShadowCopy.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ba::launcher {

std::atomic<bool> Supervisor::stopRequested_{false};

namespace {

constexpr int kPollMs = 250;
constexpr std::uint32_t kProcVersion = 1;
constexpr std::uint64_t kCircuitWindowMs = 2 * 60 * 1000; // 2 min
constexpr int kCircuitThreshold = 5;                      // >5 crashes
// El dialogo de recuperacion BLOQUEA el reinicio hasta que alguien hace clic:
// una caida aislada al aire se reinicia sola; recien con fallas repetidas en
// la ventana del circuit breaker se le pregunta al operador.
constexpr int kRecoveryUiAfter = 2; // fallas seguidas que se reinician sin preguntar
constexpr int kGracefulMs = 5000;   // espera tras el pedido de cierre
constexpr int kTerminateMs = 3000;  // espera tras SIGTERM antes de matar
// Gracia de arranque: un hijo recien lanzado todavia no late. Sin esto el
// watchdog lo declaraba colgado en el mismo milisegundo y lo mataba. Un crash
// durante el arranque se detecta igual (el proceso sale).
constexpr std::uint64_t kStartupGraceMs = 20000;
// Si todavía no latió NUNCA, se lo espera más: el primer arranque después de
// instalar o actualizar, macOS verifica la firma de cientos de binarios antes
// de dejarlos correr (9 s solo para cargar, comprobado) y a los 20 s se lo
// mataba en pleno arranque. Un cuelgue de verdad al arrancar se ve igual.
constexpr std::uint64_t kPrimerLatidoMs = 90000;
// Espera antes de relanzar un hijo que salio con exit_code::kEnvironment.
constexpr std::uint64_t kEnvironmentRetryMs = 60000;

std::uint64_t nowMs() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

// Backoff 0/1/2/5 s con tope de 10 s (spec 3.1.8).
int backoffSeconds(int consecutive) {
    static const int seq[] = {0, 1, 2, 5};
    if (consecutive >= 0 && consecutive < 4) {
        return seq[consecutive];
    }
    return 10;
}

/// Duerme `ms` en tramos cortos, cortando antes si se pidio parar.
void sleepInterruptible(int ms, const std::atomic<bool>& stop) {
    for (int waited = 0; waited < ms && !stop.load(); waited += 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void writeMinidump(std::uint32_t pid, const std::filesystem::path& path) {
#ifdef _WIN32
    // dbghelp.dll se carga a runtime (sin import lib). MinGW no define los
    // tipos de minidump en su dbghelp.h, por eso se declaran aca.
    typedef DWORD MINIDUMP_TYPE;
    typedef BOOL(WINAPI * MiniDumpWriteDumpFn)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, void*, void*, void*);
    HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
    if (!dbghelp) {
        BA_LOG_WARN("dump", 0, "dbghelp.dll no disponible; sin minidump");
        return;
    }
    auto fn = reinterpret_cast<MiniDumpWriteDumpFn>(
        reinterpret_cast<void*>(GetProcAddress(dbghelp, "MiniDumpWriteDump")));
    HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!fn || !proc) {
        if (proc) {
            CloseHandle(proc);
        }
        FreeLibrary(dbghelp);
        BA_LOG_WARN("dump", 0, "no se pudo preparar el minidump");
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    HANDLE file = CreateFileW(path.wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        CloseHandle(proc);
        FreeLibrary(dbghelp);
        BA_LOG_WARN("dump", static_cast<int>(GetLastError()), "no se pudo abrir el destino del minidump");
        return;
    }
    // MiniDumpNormal | MiniDumpWithThreadInfo, como literales (MinGW).
    const BOOL ok = fn(proc, pid, file, 0x00000000 | 0x00001000, nullptr, nullptr, nullptr);
    CloseHandle(file);
    CloseHandle(proc);
    FreeLibrary(dbghelp);
    if (ok) {
        BA_LOG_INFO("dump", 0, "minidump escrito: " + path.string());
    } else {
        BA_LOG_WARN("dump", static_cast<int>(GetLastError()), "MiniDumpWriteDump fallo");
    }
#else
    (void)pid;
    (void)path;
    BA_LOG_INFO("dump", 0, "sin volcado de memoria en esta plataforma todavia (Crashpad: fase 9)");
#endif
}

} // namespace

Supervisor::Supervisor(Config cfg) : cfg_(std::move(cfg)) {}

Supervisor::~Supervisor() = default;

void Supervisor::requestStop() { stopRequested_.store(true); }

bool Supervisor::inStartupGrace(bool isA) const {
    const std::uint64_t at = launchedAtMs_[isA ? 1 : 0];
    if (at == 0) return false;
    const std::uint64_t pasado = nowMs() - at;
    if (pasado < kStartupGraceMs) return true;
    const bool latio = health_.read(isA ? cfg_.slotA : cfg_.slotB).lastBeatQpc != latidoAlLanzar_[isA ? 1 : 0];
    return !latio && pasado < kPrimerLatidoMs;
}

int Supervisor::run() {
    std::string err;
    if (!ba::platform::SingleInstance::acquire("launcher", instance_, &err)) {
        // Segunda ejecucion (spec 3.1.1): la primera sigue a cargo.
        BA_LOG_INFO("instance", 0, "no se inicia: " + err);
        return 0;
    }

    ba::platform::suppressCrashDialogs(); // spec 3.1.4; los hijos lo heredan

    const IntegrityReport integrityReport = verifyIntegrity();
    if (!integrityReport.ok && cfg_.recoveryUi) {
        // Spec 3.1.2: "mensaje claro en lugar de crashear". No es fatal.
        Incident inc;
        inc.title = L"Broadcast Appliance — Integridad";
        inc.showIntegrity = true;
        inc.integrity = integrityReport;
        if (RecoveryUi::present(inc) == RecoveryAction::Exit) {
            BA_LOG_INFO("sup", 0, "operador eligio salir ante fallos de integridad");
            return 1;
        }
        BA_LOG_INFO("sup", 0, "operador eligio continuar pese a fallos de integridad");
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
    const std::size_t slotCount = std::max(cfg_.slotA, cfg_.slotB) + 1;
    auto st = ba::ipc::HealthBlock::open(ba::ipc::HealthBlock::kName, health_);
    if (!st.ok) {
        st = ba::ipc::HealthBlock::create(ba::ipc::HealthBlock::kName, slotCount, health_);
    }
    if (!st.ok) {
        BA_LOG_ERROR("health", st.code, "no se pudo crear BA.Health");
        return 1;
    }
    BA_LOG_INFO("health", 0, "BA.Health listo con " + std::to_string(slotCount) + " slots");

    launchChildren();
    watchdogLoop();
    return 0;
}

void Supervisor::applyExecRoot(const std::filesystem::path& execRoot) {
    if (execRoot.empty() || execRoot == cfg_.root) {
        BA_LOG_INFO("shadow", 0, "ejecutando desde la raiz original");
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
    BA_LOG_INFO("shadow", 0, "ejecutando desde la copia sombra: " + execRoot.string());
}

IntegrityReport Supervisor::verifyIntegrity() {
    const IntegrityReport report = Integrity::verify(cfg_.root.wstring());
    if (report.ok) {
        BA_LOG_INFO("integrity", 0, "integridad OK (" + std::to_string(report.checked) + " archivos)");
    } else {
        BA_LOG_ERROR("integrity", static_cast<int>(report.failures.size()),
                     "integridad: " + std::to_string(report.missing) + " ausentes, " +
                         std::to_string(report.mismatch) + " con hash distinto");
    }
    return report;
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
    const ba::ipc::Snapshot snap = health_.read(slot);
    if (snap.pid != 0 && !health_.isStale(slot) && child(isA).attach(snap.pid, name)) {
        BA_LOG_INFO("sup", 0, "readopt: adoptado " + name + " pid=" + std::to_string(snap.pid));
        return;
    }
    startChild(isA);
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
    if (isA && safeMode_) {
        c.args.emplace_back("--safe-mode"); // circuit breaker
    }
    // Lo que el hijo imprime queda en Logs/ de la raiz REAL. A informa todo por
    // stdout/stderr y no tiene otro log. B ya deja sus lineas en Logs/ba.log,
    // pero lo que no pasa por su logger se perdia: libwebrtc, los asertos de
    // lwIP, un abort() o una biblioteca que no carga.
    // Un archivo por proceso, con el nombre del proceso: es lo que el visor
    // usa para saber de donde salio cada linea.
    c.outputFile = cfg_.logsDir / (isA ? "app.log" : "gateway.log");
    if (!child(isA).start(c)) {
        return false;
    }
    launchedAtMs_[isA ? 1 : 0] = nowMs();
    health_.setIdentity(slot, child(isA).pid(), kProcVersion);
    latidoAlLanzar_[isA ? 1 : 0] = health_.read(slot).lastBeatQpc;
    health_.setState(slot, ba::ipc::ProcessState::Running);
    return true;
}

void Supervisor::restartChild(bool isA) {
    if (isA) {
        appCrashTimes_.push_back(nowMs());
        const std::uint64_t now = nowMs();
        const std::uint64_t cutoff = now > kCircuitWindowMs ? now - kCircuitWindowMs : 0;
        appCrashTimes_.erase(std::remove_if(appCrashTimes_.begin(), appCrashTimes_.end(),
                                            [cutoff](std::uint64_t t) { return t < cutoff; }),
                             appCrashTimes_.end());
        if (static_cast<int>(appCrashTimes_.size()) > kCircuitThreshold && !safeMode_) {
            safeMode_ = true; // circuit breaker (spec 3.1.8)
            BA_LOG_WARN("sup", 0, "circuit breaker: A entra en modo seguro");
        }
        appNeedsRestore_ = true;
    }
    int& consecutive = isA ? appConsecutive_ : bConsecutive_;
    const int delay = backoffSeconds(consecutive);
    ++consecutive;
    if (delay > 0) {
        BA_LOG_INFO("sup", 0, std::string(isA ? "A" : "B") + ": reinicio en " + std::to_string(delay) + " s");
        sleepInterruptible(delay * 1000, stopRequested_);
        if (stopRequested_.load()) {
            return;
        }
    }
    if (startChild(isA) && isA) {
        appNeedsRestore_ = false;
    }
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
                     name +
                         " no puede funcionar en este entorno (ver su log: p. ej. sin sesion grafica); "
                         "reintento en " +
                         std::to_string(kEnvironmentRetryMs / 1000) + " s");
        sleepInterruptible(static_cast<int>(kEnvironmentRetryMs), stopRequested_);
        if (!stopRequested_.load()) {
            startChild(isA);
        }
        return;
    }
    BA_LOG_WARN("sup", code.value_or(-1), name + " termino inesperadamente; reiniciando con backoff");
    restartChild(isA);
}

void Supervisor::handleHang(bool isA) {
    const std::uint32_t slot = isA ? cfg_.slotA : cfg_.slotB;
    const std::string name = isA ? "A" : "B";
    BA_LOG_WARN("sup", 0, name + " colgado (heartbeat estancado); volcado + terminar");
    writeMinidump(child(isA).pid(), cfg_.logsDir / "dumps" / (name + ".dmp"));
    child(isA).kill();
    child(isA).waitExit(kTerminateMs);
    health_.setState(slot, ba::ipc::ProcessState::Hung);
    child(isA).close();
    restartChild(isA);
}

RecoveryAction Supervisor::presentRecovery(bool appDown, bool appHung, bool gwDown, bool gwHung) {
    Incident inc;
    inc.title = L"Broadcast Appliance — Recuperación";
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
    const std::uint64_t now = nowMs();
    if (!hasIntegrityReport_ || now - lastIntegrityCheckMs_ > 60000) {
        inc.integrity = Integrity::verify(cfg_.root.wstring());
        lastIntegrityReport_ = inc.integrity;
        lastIntegrityCheckMs_ = now;
        hasIntegrityReport_ = true;
    } else {
        inc.integrity = lastIntegrityReport_;
    }
    return RecoveryUi::present(inc);
}

void Supervisor::forceRestart() {
    BA_LOG_WARN("sup", 0, "forzar reinicio: terminando hijos y relanzando desde 0");
    for (bool isA : {true, false}) {
        child(isA).kill();
        child(isA).waitExit(kTerminateMs);
        child(isA).close();
    }
    health_.setState(cfg_.slotA, ba::ipc::ProcessState::Terminated);
    health_.setState(cfg_.slotB, ba::ipc::ProcessState::Terminated);
    appConsecutive_ = 0;
    bConsecutive_ = 0;
    recentFailures_ = 0;
    appCrashTimes_.clear();
    safeMode_ = false;
    appNeedsRestore_ = true;
    launchChildren();
}

void Supervisor::watchdogLoop() {
    while (!shuttingDown_) {
        if (stopRequested_.load()) {
            BA_LOG_INFO("sup", 0, "cierre pedido (señal/sesion)");
            orderedShutdown();
            break;
        }
        const bool appDown = !app_.alive();
        const bool gwDown = !gateway_.alive();
        const bool appHung = !appDown && !inStartupGrace(true) && health_.isStale(cfg_.slotA);
        const bool gwHung = !gwDown && !inStartupGrace(false) && health_.isStale(cfg_.slotB);

        // A salio con codigo 0: cierre ordenado del operador, no es un fallo.
        if (appDown && app_.exitCode() == 0) {
            BA_LOG_INFO("sup", 0, "A salio limpiamente (codigo 0); cierre ordenado");
            orderedShutdown();
            break;
        }

        if (appDown || appHung || gwDown || gwHung) {
            RecoveryAction action = RecoveryAction::Retry;
            const std::uint64_t failAt = nowMs();
            if (failAt - lastFailureMs_ > kCircuitWindowMs) {
                recentFailures_ = 0;
            }
            lastFailureMs_ = failAt;
            ++recentFailures_;
            if (cfg_.recoveryUi && recentFailures_ > kRecoveryUiAfter) {
                action = presentRecovery(appDown, appHung, gwDown, gwHung);
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
        c.waitExit(kGracefulMs)) {
        BA_LOG_INFO("sup", 0, name + " cerro por el canal de control");
        return;
    }
    if (c.terminate() && c.waitExit(kTerminateMs)) {
        BA_LOG_INFO("sup", 0, name + " cerro tras la señal de terminacion");
        return;
    }
    BA_LOG_WARN("sup", 0, name + " no cerro a tiempo; se fuerza");
    c.kill();
    c.waitExit(kTerminateMs);
}

void Supervisor::orderedShutdown() {
    if (shuttingDown_) {
        return;
    }
    shuttingDown_ = true;
    BA_LOG_INFO("sup", 0, "cierre ordenado: A primero (corta la emision), despues B");
    stopChild(true);
    stopChild(false);
    health_.setState(cfg_.slotA, ba::ipc::ProcessState::Terminated);
    health_.setState(cfg_.slotB, ba::ipc::ProcessState::Terminated);
    // Sin PIDs publicados: un launcher futuro no debe intentar readoptar.
    health_.setIdentity(cfg_.slotA, 0, kProcVersion);
    health_.setIdentity(cfg_.slotB, 0, kProcVersion);
    BA_LOG_INFO("sup", 0, "clean_shutdown marcado");
}

} // namespace ba::launcher
