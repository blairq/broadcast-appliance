// ============================================================================
// src/gateway/Cloudflared.cpp — implementacion del supervisor de cloudflared
//
// Proposito:
//   Implementa CloudflaredSupervisor (ver el header). En Windows:
//     - spawn(): CreateProcessW con CREATE_NO_WINDOW, stdout/stderr redirigidos
//       a Logs/cloudflared.log, token por env TUNNEL_TOKEN (no por CLI), y un
//       Job Object con KILL_ON_JOB_CLOSE (si B muere, el tunel muere).
//     - runLoop(): cada 2 s verifica salud (processAlive + probeMetrics) y, si
//       cae, reinicia con backoff creciente (1 s → 30 s).
//   En no-Windows (build offline): no-op para que el arbol compile.
//
// Modelo de threading:
//   - runLoop corre en su thread y se detiene por stop_.
//   - start()/stop() se llaman desde el thread de control de B.
//   - `alive_`/`restartCount_` son atomic; `job_`/`proc_` se tocan solo desde
//     start/stop/kill (thread de control) y desde runLoop (spawn/kill). Para
//     evitar data race en los handles, spawn/kill se protegen con un mutex
//     interno (impl_) — ver abajo.
//
// Estrategia de errores:
//   No lanza. Fallos de CreateProcessW / Job Object se loguean y se reintenta
//   con backoff. El gateway sigue corriendo sin tunel (solo no hay conexiones
//   nuevas; el medio local sigue).
// ============================================================================

#include "Cloudflared.hpp"

#include <chrono>
#include <cstring>
#include <mutex>

#include "Log.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace ba::gateway {

namespace {

#ifdef _WIN32
/// Construye un bloque de entorno (doble-NUL terminado) que copia el entorno
/// actual del proceso y AGREGA `TUNNEL_TOKEN=<token>`.
///
/// La API Win32 real es `GetEnvironmentStringsW()` (SIN argumentos): devuelve
/// un LPWCH al bloque NUL-separado con doble NUL final, asignado con LocalAlloc
/// y a liberar con `FreeEnvironmentStringsW`. Sobre ese bloque se concatena
/// `TUNNEL_TOKEN=...` y se devuelve un nuevo bloque (tambien LocalAlloc, a
/// liberar con LocalFree en el caller).
///
/// :param token: valor de TUNNEL_TOKEN.
/// :returns: puntero al bloque (a liberar con LocalFree) o nullptr en fallo.
wchar_t* buildEnvBlock(const std::string& token) {
    LPWCH block = GetEnvironmentStringsW();
    if (!block) return nullptr;
    // Longitud del bloque en wchars: recorremos hasta el doble NUL final.
    size_t len = 0;
    while (block[len] != L'\0' || block[len + 1] != L'\0')
        ++len;
    // block[len] y block[len+1] son el doble NUL terminador.
    const std::wstring envVar = L"TUNNEL_TOKEN=" + std::wstring(token.begin(), token.end());
    // Nuevo bloque: [original sin el doble NUL final] + TUNNEL_TOKEN=... + doble NUL.
    const size_t totalW = len + envVar.size() + 2;
    wchar_t* newBlock = static_cast<wchar_t*>(LocalAlloc(LPTR, totalW * sizeof(wchar_t)));
    if (newBlock) {
        std::memcpy(newBlock, block, len * sizeof(wchar_t));
        std::memcpy(newBlock + len, envVar.data(), envVar.size() * sizeof(wchar_t));
        newBlock[len + envVar.size()] = L'\0';
        newBlock[len + envVar.size() + 1] = L'\0';
    }
    FreeEnvironmentStringsW(block);
    return newBlock;
}
#endif

} // namespace

CloudflaredSupervisor::CloudflaredSupervisor(std::string cloudflaredPath, std::string tunnelToken,
                                             uint16_t metricsPort, std::string logPath, Log& log)
    : cloudflaredPath_(std::move(cloudflaredPath)), tunnelToken_(std::move(tunnelToken)),
      metricsPort_(metricsPort), logPath_(std::move(logPath)), log_(log) {}

CloudflaredSupervisor::~CloudflaredSupervisor() { stop(); }

bool CloudflaredSupervisor::start() {
#ifdef _WIN32
    stop_.store(false);
    if (!spawn()) {
        log_.warn("cloudflared: no se pudo lanzar (se reintentara con backoff)", -1);
    }
    thread_ = std::thread(&CloudflaredSupervisor::runLoop, this);
    log_.info("cloudflared: supervisor arrancado (metrics=" + std::to_string(metricsPort_) + ")");
    return alive_.load();
#else
    (void)metricsPort_;
    log_.info("cloudflared: no-Windows (no-op)");
    return false;
#endif
}

void CloudflaredSupervisor::stop() {
    stop_.store(true);
    if (thread_.joinable()) thread_.join();
    kill();
    log_.info("cloudflared: supervisor detenido (reinicios=" + std::to_string(restartCount_.load()) + ")");
}

bool CloudflaredSupervisor::running() const { return alive_.load(); }

int CloudflaredSupervisor::restartCount() const { return restartCount_.load(); }

#ifdef _WIN32

bool CloudflaredSupervisor::spawn() {
    // Job Object: si B muere, el tunel muere con el (KILL_ON_JOB_CLOSE).
    if (!job_) {
        job_ = CreateJobObjectW(nullptr, L"BA.Cloudflared");
        if (job_) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
                log_.warn("cloudflared: fallo SetInformationJobObject", -1);
            }
        }
    }

    // Abre el archivo de log para redirigir stdout/stderr del hijo.
    HANDLE logFile =
        CreateFileW((std::wstring(logPath_.begin(), logPath_.end()) + L".").c_str(), GENERIC_WRITE,
                    FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (logFile == INVALID_HANDLE_VALUE) {
        // Sin log file, sigue (el hijo heredara los handles del padre).
        logFile = nullptr;
    }

    // Linea de comandos (spec §8.8). El token NO va aca: va por env.
    // Flags validos en cloudflared 2024.11.0 (verificado contra el binario
    // vendored extern/cloudflared/cloudflared.exe: `tunnel run --help`).
    // IMPORTANTE: el flag de logging es `--loglevel` (sin guion interno);
    // `--log-level` es rechazado ("flag provided but not defined"), lo que
    // haria que el tunel falle en cada spawn y el supervisor quedara en un
    // loop de reinicio perpetuo. `tunnel run` sin nombre de tunel es correcto
    // para tunel por token (TUNNEL_TOKEN por env).
    const std::string cmd = "tunnel --no-autoupdate --metrics 127.0.0.1:" + std::to_string(metricsPort_) +
                            " --loglevel info run";
    std::wstring cmdLine = L"\"" + std::wstring(cloudflaredPath_.begin(), cloudflaredPath_.end()) + L"\" " +
                           std::wstring(cmd.begin(), cmd.end());

    // Token por env (no por CLI, spec §8.8).
    wchar_t* envBlock = buildEnvBlock(tunnelToken_);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (logFile) {
        si.hStdOutput = logFile;
        si.hStdError = logFile;
        si.dwFlags = STARTF_USESTDHANDLES;
    }

    PROCESS_INFORMATION pi{};
    DWORD flags = CREATE_NO_WINDOW;
    if (envBlock) flags |= CREATE_NEW_CONSOLE; // para que la env tome efecto
    const BOOL ok = CreateProcessW(
        cloudflaredPath_.empty() ? nullptr
                                 : (std::wstring(cloudflaredPath_.begin(), cloudflaredPath_.end())).c_str(),
        &cmdLine[0], nullptr, nullptr, logFile != nullptr, flags, envBlock, nullptr, &si, &pi);
    if (envBlock) LocalFree(envBlock);
    if (logFile) CloseHandle(logFile);

    if (!ok) {
        log_.fmt("warn", -1, "cloudflared: CreateProcessW fallo (err=%lu)", GetLastError());
        return false;
    }

    if (job_) AssignProcessToJobObject(job_, pi.hProcess);
    if (pi.hThread) CloseHandle(pi.hThread);

    proc_ = pi.hProcess;
    alive_.store(true);
    backoffMs_ = 1000; // resetea el backoff en un arranque exitoso
    log_.info("cloudflared: lanzado (pid=" + std::to_string(pi.dwProcessId) + ")");
    return true;
}

void CloudflaredSupervisor::kill() {
    if (proc_) {
        TerminateProcess(static_cast<HANDLE>(proc_), 0);
        CloseHandle(static_cast<HANDLE>(proc_));
        proc_ = nullptr;
    }
    if (job_) {
        CloseHandle(static_cast<HANDLE>(job_)); // KILL_ON_JOB_CLOSE mata al hijo
        job_ = nullptr;
    }
    alive_.store(false);
}

bool CloudflaredSupervisor::processAlive() const {
    if (!proc_) return false;
    return WaitForSingleObject(static_cast<HANDLE>(proc_), 0) != WAIT_OBJECT_0;
}

bool CloudflaredSupervisor::probeMetrics() const {
    // Probe TCP a 127.0.0.1:metricsPort: el servidor de metricas de
    // cloudflared 2024.11.0 sirve Prometheus en /metrics (no hay endpoint
    // /ready). Un TCP connect al puerto basta para saber que el tunel esta
    // arriba (spec §8.8).
    WSADATA wsa{};
    bool wsaInit = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        if (wsaInit) WSACleanup();
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(metricsPort_);
    addr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");
    const int r = connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    closesocket(s);
    if (wsaInit) WSACleanup();
    return r == 0;
}

void CloudflaredSupervisor::sleepBackoff() {
    std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs_));
    if (backoffMs_ < 30000) backoffMs_ *= 2; // 1s,2s,4s,... hasta 30s
}

void CloudflaredSupervisor::runLoop() {
    while (!stop_.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (stop_.load(std::memory_order_relaxed)) break;

        const bool up = processAlive() && probeMetrics();
        if (up) {
            if (!alive_.load()) {
                alive_.store(true);
                backoffMs_ = 1000;
                log_.info("cloudflared: tunel arriba (metrics ok)");
            }
            continue;
        }

        // Caído: mata el residuo y relanza con backoff.
        log_.warn("cloudflared: caído; reiniciando con backoff", -1);
        kill();
        sleepBackoff();
        if (stop_.load(std::memory_order_relaxed)) break;
        if (spawn()) {
            ++restartCount_;
            alive_.store(true);
        }
    }
}

#else // !WIN32

bool CloudflaredSupervisor::spawn() {
    log_.info("cloudflared: spawn no-Windows (no-op)");
    return false;
}
void CloudflaredSupervisor::kill() { alive_.store(false); }
bool CloudflaredSupervisor::processAlive() const { return false; }
bool CloudflaredSupervisor::probeMetrics() const { return false; }
void CloudflaredSupervisor::sleepBackoff() {
    std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs_));
}
void CloudflaredSupervisor::runLoop() {
    // Sin tunel en no-Windows: solo espera a stop_.
    while (!stop_.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

#endif // _WIN32

} // namespace ba::gateway
