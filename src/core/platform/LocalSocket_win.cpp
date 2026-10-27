// ============================================================================
// src/core/platform/LocalSocket_win.cpp — Named pipes (Windows)
//
// Todas las operaciones son solapadas (FILE_FLAG_OVERLAPPED) sobre un evento
// por objeto: es la unica forma de tener accept/connect/espera con timeout y
// cancelacion en un pipe. `read` no bloquea gracias a PeekNamedPipe;
// `waitReadable` usa una lectura de 0 bytes, que en un pipe en modo byte se
// completa cuando llegan datos o cuando el otro lado cierra.
// ============================================================================

#include "platform/LocalSocket.hpp"
#include "i18n/I18n.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// sddl.h necesita windows.h antes (bloque aparte: clang-format no lo reordena).
#include <sddl.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace ba::platform {

namespace {

constexpr DWORD kPipeBuffer = 64 * 1024;

void setErr(std::string* err, const std::string& what) {
    if (err != nullptr) {
        *err = what + " (GetLastError=" + std::to_string(GetLastError()) + ")";
    }
}

bool validName(const std::string& name) {
    if (name.empty() || name.size() > 64) {
        return false;
    }
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '.' || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

/// `\\.\pipe\BroadcastAppliance.<sesion>.<nombre>`. Los pipes son globales a
/// la maquina: la sesion separa usuarios logueados a la vez (y la DACL impide
/// el acceso cruzado de todos modos).
std::wstring pipePath(const std::string& name) {
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    std::wstring p = L"\\\\.\\pipe\\BroadcastAppliance." + std::to_wstring(session) + L".";
    for (char c : name) {
        p.push_back(static_cast<wchar_t>(c));
    }
    return p;
}

/// Descriptor de seguridad: acceso total para el usuario actual y SYSTEM,
/// nadie mas. Se devuelve con LocalAlloc (liberar con LocalFree).
PSECURITY_DESCRIPTOR makeUserOnlySecurity() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return nullptr;
    }
    DWORD len = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &len);
    std::vector<unsigned char> buf(len);
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (GetTokenInformation(token, TokenUser, buf.data(), len, &len)) {
        const auto* user = reinterpret_cast<TOKEN_USER*>(buf.data());
        LPWSTR sid = nullptr;
        if (ConvertSidToStringSidW(user->User.Sid, &sid)) {
            // D:P = DACL protegida (sin herencia); GA = GENERIC_ALL.
            const std::wstring sddl = std::wstring(L"D:P(A;;GA;;;") + sid + L")(A;;GA;;;SY)";
            ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr);
            LocalFree(sid);
        }
    }
    CloseHandle(token);
    return sd;
}

/// Completa una operacion solapada esperando hasta `timeoutMs` (<0 infinito).
/// En timeout cancela la operacion y espera a que la cancelacion termine
/// (obligatorio: el OVERLAPPED no puede salir de alcance con I/O pendiente).
/// :returns: true si completo bien; `bytes` recibe lo transferido.
bool finishOverlapped(HANDLE h, OVERLAPPED& ov, BOOL started, int timeoutMs, DWORD& bytes) {
    bytes = 0;
    if (started) {
        return GetOverlappedResult(h, &ov, &bytes, FALSE) != FALSE;
    }
    if (GetLastError() != ERROR_IO_PENDING) {
        return false;
    }
    const DWORD ms = timeoutMs < 0 ? INFINITE : static_cast<DWORD>(timeoutMs);
    if (WaitForSingleObject(ov.hEvent, ms) == WAIT_OBJECT_0) {
        return GetOverlappedResult(h, &ov, &bytes, FALSE) != FALSE;
    }
    CancelIoEx(h, &ov);
    GetOverlappedResult(h, &ov, &bytes, TRUE);
    SetLastError(WAIT_TIMEOUT);
    return false;
}

OVERLAPPED makeOverlapped(void* event) {
    OVERLAPPED ov{};
    ov.hEvent = static_cast<HANDLE>(event);
    ResetEvent(ov.hEvent);
    return ov;
}

} // namespace

// ============================================================================
// LocalStream
// ============================================================================

LocalStream::~LocalStream() { close(); }

LocalStream::LocalStream(LocalStream&& other) noexcept : pipe_(other.pipe_), event_(other.event_) {
    other.pipe_ = nullptr;
    other.event_ = nullptr;
}

LocalStream& LocalStream::operator=(LocalStream&& other) noexcept {
    if (this != &other) {
        close();
        pipe_ = other.pipe_;
        event_ = other.event_;
        other.pipe_ = nullptr;
        other.event_ = nullptr;
    }
    return *this;
}

bool LocalStream::connect(const std::string& name, int timeoutMs, LocalStream& out, std::string* err) {
    out.close();
    if (!validName(name)) {
        if (err != nullptr) {
            *err = "nombre de canal invalido: '" + name + "'";
        }
        return false;
    }
    const std::wstring path = pipePath(name);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs < 0 ? 0 : timeoutMs);
    for (;;) {
        // SECURITY_IDENTIFICATION: el servidor puede identificarnos pero no
        // actuar en nuestro nombre (mitiga un servidor impostor).
        HANDLE h =
            CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                        FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            out.pipe_ = h;
            out.event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            return true;
        }
        const DWORD e = GetLastError();
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline || (e != ERROR_FILE_NOT_FOUND && e != ERROR_PIPE_BUSY)) {
            setErr(err, ba::i18n::llenar(ba::i18n::traducir("ipc", "no se pudo conectar a '%1'"), {name}));
            return false;
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        if (e == ERROR_PIPE_BUSY) {
            WaitNamedPipeW(path.c_str(), static_cast<DWORD>(left));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(left < 10 ? left : 10));
        }
    }
}

bool LocalStream::write(const std::uint8_t* data, std::size_t len) {
    if (pipe_ == nullptr) {
        return false;
    }
    HANDLE h = static_cast<HANDLE>(pipe_);
    std::size_t done = 0;
    while (done < len) {
        const DWORD chunk = static_cast<DWORD>((len - done) > kPipeBuffer ? kPipeBuffer : (len - done));
        OVERLAPPED ov = makeOverlapped(event_);
        DWORD n = 0;
        const BOOL started = WriteFile(h, data + done, chunk, nullptr, &ov);
        if (!finishOverlapped(h, ov, started, -1, n) || n == 0) {
            return false;
        }
        done += n;
    }
    return true;
}

int LocalStream::read(std::uint8_t* buf, std::size_t cap) {
    if (pipe_ == nullptr) {
        return -1;
    }
    HANDLE h = static_cast<HANDLE>(pipe_);
    DWORD avail = 0;
    if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) {
        return -1; // ERROR_BROKEN_PIPE: el otro lado cerro
    }
    if (avail == 0) {
        return 0;
    }
    const DWORD want = static_cast<DWORD>(cap < avail ? cap : avail);
    OVERLAPPED ov = makeOverlapped(event_);
    DWORD n = 0;
    const BOOL started = ReadFile(h, buf, want, nullptr, &ov);
    if (!finishOverlapped(h, ov, started, -1, n)) {
        return -1;
    }
    return static_cast<int>(n);
}

bool LocalStream::waitReadable(int timeoutMs) {
    if (pipe_ == nullptr) {
        return true;
    }
    HANDLE h = static_cast<HANDLE>(pipe_);
    DWORD avail = 0;
    if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr) || avail > 0) {
        return true; // cerrado o con datos
    }
    // Lectura de 0 bytes: completa cuando hay datos o el pipe se rompe.
    OVERLAPPED ov = makeOverlapped(event_);
    DWORD n = 0;
    static std::uint8_t dummy = 0;
    const BOOL started = ReadFile(h, &dummy, 0, nullptr, &ov);
    if (finishOverlapped(h, ov, started, timeoutMs, n)) {
        return true;
    }
    return GetLastError() != WAIT_TIMEOUT; // error = cerrado -> "legible"
}

bool LocalStream::isOpen() const { return pipe_ != nullptr; }

void LocalStream::close() {
    if (pipe_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(pipe_));
        pipe_ = nullptr;
    }
    if (event_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(event_));
        event_ = nullptr;
    }
}

// ============================================================================
// LocalServer
// ============================================================================

LocalServer::~LocalServer() { close(); }

LocalServer::LocalServer(LocalServer&& other) noexcept
    : name_(std::move(other.name_)), pending_(other.pending_), event_(other.event_),
      security_(other.security_) {
    other.pending_ = nullptr;
    other.event_ = nullptr;
    other.security_ = nullptr;
}

LocalServer& LocalServer::operator=(LocalServer&& other) noexcept {
    if (this != &other) {
        close();
        name_ = std::move(other.name_);
        pending_ = other.pending_;
        event_ = other.event_;
        security_ = other.security_;
        other.pending_ = nullptr;
        other.event_ = nullptr;
        other.security_ = nullptr;
    }
    return *this;
}

bool LocalServer::createInstance(bool first, std::string* err) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = security_;
    sa.bInheritHandle = FALSE;
    const DWORD openMode =
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0);
    const DWORD pipeMode = PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS;
    HANDLE h = CreateNamedPipeW(pipePath(name_).c_str(), openMode, pipeMode, PIPE_UNLIMITED_INSTANCES,
                                kPipeBuffer, kPipeBuffer, 0, &sa);
    if (h == INVALID_HANDLE_VALUE) {
        setErr(err, first ? ba::i18n::llenar(ba::i18n::traducir("ipc", "el canal '%1' ya tiene un servidor activo o no se pudo crear"), {name_})
                          : std::string("CreateNamedPipeW"));
        return false;
    }
    pending_ = h;
    return true;
}

bool LocalServer::listen(const std::string& name, LocalServer& out, std::string* err) {
    out.close();
    if (!validName(name)) {
        if (err != nullptr) {
            *err = "nombre de canal invalido: '" + name + "'";
        }
        return false;
    }
    out.name_ = name;
    out.security_ = makeUserOnlySecurity();
    if (out.security_ == nullptr) {
        setErr(err, ba::i18n::traducir("ipc", "no se pudo armar el descriptor de seguridad del pipe"));
        out.close();
        return false;
    }
    out.event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!out.createInstance(true, err)) {
        out.close();
        return false;
    }
    return true;
}

bool LocalServer::accept(int timeoutMs, LocalStream& out, std::string* err) {
    out.close();
    if (pending_ == nullptr) {
        if (err != nullptr) {
            *err = "el servidor no esta escuchando";
        }
        return false;
    }
    HANDLE h = static_cast<HANDLE>(pending_);
    OVERLAPPED ov = makeOverlapped(event_);
    DWORD n = 0;
    bool connected = false;
    if (ConnectNamedPipe(h, &ov)) {
        connected = true;
    } else if (GetLastError() == ERROR_PIPE_CONNECTED) {
        connected = true; // el cliente llego entre CreateNamedPipe y aca
    } else {
        connected = finishOverlapped(h, ov, FALSE, timeoutMs, n);
    }
    if (!connected) {
        if (GetLastError() == WAIT_TIMEOUT) {
            if (err != nullptr) {
                *err = "timeout";
            }
        } else {
            setErr(err, "ConnectNamedPipe");
        }
        return false;
    }
    // La instancia conectada pasa al stream y se prepara otra para el proximo
    // cliente.
    out.pipe_ = h;
    out.event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    pending_ = nullptr;
    createInstance(false, nullptr);
    return true;
}

void LocalServer::close() {
    if (pending_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(pending_));
        pending_ = nullptr;
    }
    if (event_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(event_));
        event_ = nullptr;
    }
    if (security_ != nullptr) {
        LocalFree(security_);
        security_ = nullptr;
    }
}

bool LocalServer::isListening() const { return pending_ != nullptr; }

} // namespace ba::platform
