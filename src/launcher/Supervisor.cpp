// Supervisor.cpp
//
// Implementacion del supervisor del launcher (ver Supervisor.hpp).
// Win32 real bajo _WIN32; fuera de Windows es funcional (stub de dump/terminate)
// para que el arbol compile y el flujo se pueda razonar.

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

namespace {

constexpr const wchar_t* kInstanceMutex = L"Local\\BroadcastAppliance.Instance";
constexpr const wchar_t* kControlPipe = L"\\\\.\\pipe\\BroadcastAppliance.Control";
constexpr int kPollMs = 250;
constexpr std::uint32_t kProcVersion = 1;
constexpr std::uint64_t kCircuitWindowMs = 2 * 60 * 1000; // 2 min.
constexpr int kCircuitThreshold = 5;                      // >5 crashes.

std::uint64_t nowMs() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// Backoff 0/1/2/5 s con tope de 10 s (spec 3.1.8).
int backoffSeconds(int consecutive) {
    static const int seq[] = {0, 1, 2, 5};
    if (consecutive >= 0 && consecutive < 4) {
        return seq[consecutive];
    }
    return 10;
}

bool pidAlive(std::uint32_t pid) {
    if (pid == 0) {
        return false;
    }
#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) {
        return false;
    }
    DWORD code = 0;
    const bool ok = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return ok;
#else
    (void)pid;
    return true;
#endif
}

void terminateProcess(void* handle) {
#ifdef _WIN32
    if (handle) {
        TerminateProcess(static_cast<HANDLE>(handle), 0x42);
    }
#else
    (void)handle;
#endif
}

void writeMinidump(void* handle, std::uint32_t pid, const std::wstring& path) {
#ifdef _WIN32
    // dbghelp.dll se carga a runtime (LoadLibraryW/GetProcAddress), sin import
    // lib. MinGW no define los tipos/constantes de minidump en su dbghelp.h
    // (stub minimo), por lo que se los declara aca de forma local.
    typedef DWORD MINIDUMP_TYPE;
    typedef void* LPMDRAWBUGINFO;
    typedef BOOL(WINAPI * MiniDumpWriteDumpFn)(HANDLE, DWORD, LPCVOID, MINIDUMP_TYPE, LPMDRAWBUGINFO, LPVOID,
                                               DWORD);
    HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
    if (!dbghelp) {
        BA_LOG_WARN("dump", 0, "dbghelp.dll no disponible; sin minidump");
        return;
    }
    auto fn = reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
    if (!fn) {
        FreeLibrary(dbghelp);
        BA_LOG_WARN("dump", 0, "MiniDumpWriteDump no encontrado; sin minidump");
        return;
    }
    // MiniDumpNormal (0x0) | MiniDumpWithThreadInfo (0x400), como literales
    // porque MinGW no expone las constantes de dbghelp.
    const BOOL ok =
        fn(static_cast<HANDLE>(handle), pid, path.c_str(), 0x00000000 | 0x00000400, nullptr, nullptr, 0);
    FreeLibrary(dbghelp);
    if (!ok) {
        BA_LOG_WARN("dump", static_cast<int>(GetLastError()), "MiniDumpWriteDump fallo");
    } else {
        BA_LOG_INFO("dump", 0, std::string("minidump escrito: ") + std::string(path.begin(), path.end()));
    }
#else
    (void)handle;
    (void)pid;
    (void)path;
    BA_LOG_INFO("dump", 0, "stub: minidump (no-Win)");
#endif
}

std::wstring join(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) {
        return b;
    }
    if (a.back() == L'\\' || a.back() == L'/') {
        return a + b;
    }
    return a + L"\\" + b;
}

} // namespace

Supervisor::Supervisor(Config cfg) : cfg_(std::move(cfg)) {}

Supervisor::~Supervisor() { releaseInstance(); }

int Supervisor::run() {
    if (!acquireInstance()) {
        // Segunda ejecucion: traer la UI al frente y salir (spec 3.1.1).
        BA_LOG_INFO("instance", 0, "ya hay una instancia; trayendo UI al frente (no-op) y saliendo");
        return 0;
    }

    setupErrorMode();

    const IntegrityReport integrityReport = verifyIntegrity();
    if (!integrityReport.ok) {
        // Spec 3.1.2: "mensaje claro en lugar de crashear". No es fatal: se
        // muestra el incidente con el estado de integridad y se deja al
        // operador decidir: salir o continuar.
        Incident inc;
        inc.title = L"Broadcast Appliance — Integridad";
        inc.showIntegrity = true;
        inc.integrity = integrityReport;
        const RecoveryAction action = RecoveryUi::present(inc);
        if (action == RecoveryAction::Exit) {
            BA_LOG_INFO("sup", 0, "operador eligio salir ante fallos de integridad");
            return 1;
        }
        BA_LOG_INFO("sup", 0, "operador eligio continuar pese a fallos de integridad");
    }

    // Copia sombra (spec 3.1.5): en esta fase es no-op; devuelve la raiz.
    const std::wstring execRoot = ShadowCopy::prepare(cfg_.root, L"v1");

    // Bloque de salud: el launcher es el owner (crea); los hijos abren.
    const std::size_t slotCount = std::max(cfg_.slotA, cfg_.slotB) + 1;
    const auto st = ba::ipc::HealthBlock::create(ba::ipc::HealthBlock::kName, slotCount, health_);
    if (!st.ok) {
        BA_LOG_ERROR("health", st.code, "no se pudo crear BA.Health");
        return 1;
    }
    BA_LOG_INFO("health", 0, "BA.Health creado con " + std::to_string(slotCount) + " slots");

    launchChildren(/*safeMode=*/false);
    watchdogLoop();
    orderedShutdown();
    (void)execRoot;
    return 0;
}

bool Supervisor::acquireInstance() {
#ifdef _WIN32
    hInstance_ = CreateMutexW(nullptr, FALSE, kInstanceMutex);
    if (hInstance_ == nullptr) {
        BA_LOG_ERROR("instance", static_cast<int>(GetLastError()), "CreateMutexW fallo");
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        return false; // otra instancia es la owner.
    }
    return true;
#else
    return true;
#endif
}

void Supervisor::releaseInstance() {
#ifdef _WIN32
    if (hInstance_) {
        ReleaseMutex(hInstance_);
        CloseHandle(hInstance_);
        hInstance_ = nullptr;
    }
#endif
}

void Supervisor::setupErrorMode() {
#ifdef _WIN32
    // Spec 3.1.4: los hijos heredan el modo de error.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    BA_LOG_INFO("errorMode", 0, "SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX)");
#else
    BA_LOG_INFO("errorMode", 0, "stub: SetErrorMode (no-Win)");
#endif
}

IntegrityReport Supervisor::verifyIntegrity() {
    const IntegrityReport report = Integrity::verify(cfg_.root);
    if (report.ok) {
        BA_LOG_INFO("integrity", 0, "integridad OK (" + std::to_string(report.checked) + " archivos)");
    } else {
        BA_LOG_ERROR("integrity", static_cast<int>(report.failures.size()),
                     "integridad: " + std::to_string(report.missing) + " ausentes, " +
                         std::to_string(report.mismatch) + " con hash distinto");
    }
    return report;
}

void Supervisor::launchChildren(bool safeMode) {
    safeMode_ = safeMode;
    // B primero, luego A (spec 3.1.6).
    launchOne(cfg_.slotB, gateway_, cfg_.gatewayExe, L"B", safeMode);
    launchOne(cfg_.slotA, app_, cfg_.appExe, L"A", safeMode);
}

void Supervisor::launchOne(std::uint32_t slot, ChildProcess& child, const std::wstring& exe,
                           const std::wstring& name, bool /*safeMode*/) {
    // Readopt (spec 3.1.9): si hay un PID publicado y vivo, adoptarlo.
    const ba::ipc::Snapshot snap = health_.read(slot);
    if (snap.pid != 0 && pidAlive(snap.pid)) {
        if (child.attach(snap.pid)) {
            BA_LOG_INFO("sup", 0,
                        std::string("readopt: adoptado ") + std::string(name.begin(), name.end()) +
                            " pid=" + std::to_string(snap.pid));
            return;
        }
    }

    ChildProcess::Config c;
    c.exePath = exe;
    c.workDir = cfg_.root;
    c.name = name;
    if (name == L"A" && appNeedsRestore_) {
        c.args = L"--restore";
    }
    if (child.start(c)) {
        health_.setIdentity(slot, child.pid(), kProcVersion);
        health_.setState(slot, ba::ipc::ProcessState::Running);
    } else {
        BA_LOG_ERROR("sup", 0, std::string("no se pudo lanzar ") + std::string(name.begin(), name.end()));
    }
}

void Supervisor::relaunch(bool isA) {
    ChildProcess& child = isA ? app_ : gateway_;
    const std::uint32_t slot = isA ? cfg_.slotA : cfg_.slotB;
    const std::wstring name = isA ? L"A" : L"B";
    const std::wstring exe = isA ? cfg_.appExe : cfg_.gatewayExe;

    ChildProcess::Config c;
    c.exePath = exe;
    c.workDir = cfg_.root;
    c.name = name;
    if (isA && appNeedsRestore_) {
        c.args = L"--restore"; // spec 3.1.8: A relanzado tras crash arranca con --restore.
    }
    if (child.start(c)) {
        health_.setIdentity(slot, child.pid(), kProcVersion);
        health_.setState(slot, ba::ipc::ProcessState::Running);
        if (isA) {
            appConsecutive_ = 0;
            appNeedsRestore_ = false;
        } else {
            bConsecutive_ = 0;
        }
    } else {
        BA_LOG_ERROR("sup", 0, std::string("reinicio fallido de ") + std::string(name.begin(), name.end()));
    }
}

void Supervisor::restartChild(bool isA) {
    if (isA) {
        appCrashTimes_.push_back(nowMs());
        const std::uint64_t cutoff = nowMs() - kCircuitWindowMs;
        appCrashTimes_.erase(std::remove_if(appCrashTimes_.begin(), appCrashTimes_.end(),
                                            [cutoff](std::uint64_t t) { return t < cutoff; }),
                             appCrashTimes_.end());
        if (static_cast<int>(appCrashTimes_.size()) > kCircuitThreshold) {
            safeMode_ = true; // circuit breaker (spec 3.1.8).
            BA_LOG_WARN("sup", 0, "circuit breaker: A entra en modo seguro");
        }
        appConsecutive_++;
        appNeedsRestore_ = true;
    } else {
        bConsecutive_++;
    }

    const int delay = backoffSeconds(isA ? appConsecutive_ : bConsecutive_);
    if (delay > 0) {
        sleepMs(delay * 1000);
    }
    relaunch(isA);
}

void Supervisor::handleCrash(bool isA) {
    ChildProcess& child = isA ? app_ : gateway_;
    const std::uint32_t code = child.exitCode();
    const std::string name = isA ? "A" : "B";

    // Cierre ordenado del operador: A sale con codigo 0 => no es un crash.
    if (isA && !child.alive() && code == 0) {
        BA_LOG_INFO("sup", 0, "A salio limpiamente (codigo 0); cierre ordenado");
        orderedShutdown();
        return;
    }

    BA_LOG_WARN("sup", static_cast<int>(code), name + " murio; reiniciando con backoff");
    child.close();
    restartChild(isA);
}

void Supervisor::handleHang(bool isA) {
    ChildProcess& child = isA ? app_ : gateway_;
    const std::uint32_t slot = isA ? cfg_.slotA : cfg_.slotB;
    const std::string name = isA ? "A" : "B";

    BA_LOG_WARN("sup", 0, name + " colgado (heartbeat estancado); dump + terminate");
    const std::wstring dumpPath = join(cfg_.logsDir, L"dumps") + L"\\" + (isA ? L"A" : L"B") + L".dmp";
    writeMinidump(child.handle(), child.pid(), dumpPath);
    terminateProcess(child.handle());
    health_.setState(slot, ba::ipc::ProcessState::Hung);
    child.close();
    restartChild(isA);
}

RecoveryAction Supervisor::presentRecovery(bool appDown, bool appHung, bool gwDown, bool gwHung) {
    Incident inc;
    inc.title = L"Broadcast Appliance — Recuperación";
    inc.childADown = appDown || appHung;
    inc.childBDown = gwDown || gwHung;
    if (appDown) {
        inc.appExitCode = app_.exitCode();
        inc.appCodeValid = true;
    }
    if (gwDown) {
        inc.gatewayExitCode = gateway_.exitCode();
        inc.gwCodeValid = true;
    }

    // Si procede, mostrar el estado de integridad del disco: un hijo caido
    // podria deberse a un archivo corrupto/ausente. Se cachea el reporte para
    // no re-hashear todo el disco en cada iteracion de un loop de crashes.
    inc.showIntegrity = true;
    const std::uint64_t now = nowMs();
    if (!hasIntegrityReport_ || now - lastIntegrityCheckMs_ > 60000) {
        inc.integrity = Integrity::verify(cfg_.root);
        lastIntegrityReport_ = inc.integrity;
        lastIntegrityCheckMs_ = now;
        hasIntegrityReport_ = true;
    } else {
        inc.integrity = lastIntegrityReport_;
    }

    BA_LOG_WARN("sup", 0,
                "UI de recuperacion: A caido=" + std::to_string(inc.childADown) + " B caido=" +
                    std::to_string(inc.childBDown) + " integridad_ok=" + std::to_string(inc.integrity.ok));
    return RecoveryUi::present(inc);
}

void Supervisor::forceRestart() {
    BA_LOG_WARN("sup", 0, "forzar reinicio: terminando hijos y relanzando desde 0");
    terminateProcess(app_.handle());
    terminateProcess(gateway_.handle());
    health_.setState(cfg_.slotA, ba::ipc::ProcessState::Terminated);
    health_.setState(cfg_.slotB, ba::ipc::ProcessState::Terminated);
    app_.close();
    gateway_.close();
    // El operador toma el control: limpiar la politica de reinicio.
    appConsecutive_ = 0;
    bConsecutive_ = 0;
    appCrashTimes_.clear();
    safeMode_ = false;
    appNeedsRestore_ = true; // A relanzado arranca con --restore.
    launchChildren(/*safeMode=*/false);
}

void Supervisor::watchdogLoop() {
    while (!shuttingDown_) {
        const bool appDown = !app_.alive();
        const bool appHung = !appDown && health_.isStale(cfg_.slotA);
        const bool gwDown = !gateway_.alive();
        const bool gwHung = !gwDown && health_.isStale(cfg_.slotB);

        // Cierre ordenado del operador: A salio limpiamente (codigo 0) => no es
        // un fallo; se cierra sin pasar por la UI de recuperacion.
        if (appDown && app_.exitCode() == 0) {
            orderedShutdown();
            continue;
        }

        if (appDown || appHung || gwDown || gwHung) {
            const RecoveryAction action = presentRecovery(appDown, appHung, gwDown, gwHung);
            if (action == RecoveryAction::Exit) {
                orderedShutdown();
            } else if (action == RecoveryAction::ForceRestart) {
                forceRestart();
            } else {
                // Retry o Dismissed: continuar con el flujo normal de reinicio.
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

        sleepMs(kPollMs);
    }
}

void Supervisor::orderedShutdown() {
    if (shuttingDown_) {
        return;
    }
    BA_LOG_INFO("sup", 0, "cierre ordenado: pidiendo salida a A y B");
    control_.requestShutdown(kControlPipe, L"A", 3000);
    control_.requestShutdown(kControlPipe, L"B", 3000);

    // Espera acotada a que terminen por si mismos.
    for (int i = 0; i < 20 && (app_.alive() || gateway_.alive()); ++i) {
        sleepMs(250);
    }

    health_.setState(cfg_.slotA, ba::ipc::ProcessState::Terminated);
    health_.setState(cfg_.slotB, ba::ipc::ProcessState::Terminated);
    BA_LOG_INFO("sup", 0, "clean_shutdown marcado");
    shuttingDown_ = true;
}

} // namespace ba::launcher
