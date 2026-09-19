// ============================================================================
// src/core/platform/LocalSocket_posix.cpp — Sockets Unix (Linux y macOS)
// ============================================================================

#include "platform/LocalSocket.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <thread>

#include "platform/Paths.hpp"

namespace ba::platform {

namespace {

void setErr(std::string* err, const std::string& msg) {
    if (err != nullptr) {
        *err = msg;
    }
}

std::string sysErr(const char* what) { return std::string(what) + ": " + std::strerror(errno); }

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

/// Direccion del socket para `name`, o vacio si no entra en sun_path.
///
/// Linux: espacio de nombres ABSTRACTO ("\0broadcast-appliance-<uid>-<name>"):
/// no existe en ningun disco (el appliance corre desde un pendrive y no deja
/// nada en la maquina), desaparece solo cuando muere el proceso (no hay
/// huerfanos) y el UID en el nombre separa a los usuarios; el control de
/// acceso real es SO_PEERCRED (peerIsSameUser). macOS no tiene espacio
/// abstracto: archivo en runtimeDir() ($TMPDIR, por usuario, temporal).
std::string socketPath(const std::string& name) {
#if defined(__linux__)
    return std::string(1, '\0') + "broadcast-appliance-" + std::to_string(::getuid()) + "-" + name;
#else
    const std::filesystem::path dir = runtimeDir();
    if (dir.empty()) {
        return {};
    }
    std::string p = (dir / (name + ".sock")).string();
    if (p.size() >= sizeof(sockaddr_un{}.sun_path)) {
        return {};
    }
    return p;
#endif
}

bool isAbstract(const std::string& path) { return !path.empty() && path[0] == '\0'; }

bool makeAddr(const std::string& path, sockaddr_un& addr) {
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
        return false;
    }
    std::memcpy(addr.sun_path, path.data(), path.size());
    return true;
}

/// Largo de la direccion: el nombre abstracto es EXACTO (sin ceros de relleno).
socklen_t addrLen(const std::string& path) {
    return isAbstract(path) ? static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size())
                            : static_cast<socklen_t>(sizeof(sockaddr_un));
}

/// Socket stream con CLOEXEC y, en macOS, sin SIGPIPE (Linux usa MSG_NOSIGNAL).
int makeSocket() {
#if defined(__linux__)
    return ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
#else
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd >= 0) {
        ::fcntl(fd, F_SETFD, FD_CLOEXEC);
        const int one = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
    }
    return fd;
#endif
}

/// true si el proceso del otro extremo corre con nuestro UID.
bool peerIsSameUser(int fd) {
#if defined(__linux__)
    struct ucred cred {};
    socklen_t len = sizeof(cred);
    if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0) {
        return false;
    }
    return cred.uid == ::getuid();
#else
    uid_t uid = 0;
    gid_t gid = 0;
    if (::getpeereid(fd, &uid, &gid) != 0) {
        return false;
    }
    return uid == ::getuid();
#endif
}

int pollFor(int fd, short events, int timeoutMs) {
    struct pollfd p {};
    p.fd = fd;
    p.events = events;
    int r;
    do {
        r = ::poll(&p, 1, timeoutMs);
    } while (r < 0 && errno == EINTR);
    return r;
}

} // namespace

// ============================================================================
// LocalStream
// ============================================================================

LocalStream::~LocalStream() { close(); }

LocalStream::LocalStream(LocalStream&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

LocalStream& LocalStream::operator=(LocalStream&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

bool LocalStream::connect(const std::string& name, int timeoutMs, LocalStream& out, std::string* err) {
    out.close();
    if (!validName(name)) {
        setErr(err, "nombre de canal invalido: '" + name + "'");
        return false;
    }
    const std::string path = socketPath(name);
    sockaddr_un addr{};
    if (path.empty() || !makeAddr(path, addr)) {
        setErr(err, "no se pudo resolver la ruta del socket para '" + name + "'");
        return false;
    }
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs < 0 ? 0 : timeoutMs);
    for (;;) {
        const int fd = makeSocket();
        if (fd < 0) {
            setErr(err, sysErr("socket"));
            return false;
        }
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), addrLen(path)) == 0) {
            if (!peerIsSameUser(fd)) {
                ::close(fd);
                setErr(err, "el servidor de '" + name + "' no es del mismo usuario");
                return false;
            }
            out.fd_ = fd;
            return true;
        }
        const int e = errno;
        ::close(fd);
        // ENOENT/ECONNREFUSED: el servidor todavia no escucha. Se reintenta.
        if (e != ENOENT && e != ECONNREFUSED && e != EAGAIN && e != EINTR) {
            errno = e;
            setErr(err, sysErr("connect"));
            return false;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            setErr(err, "timeout conectando a '" + name + "'");
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

bool LocalStream::write(const std::uint8_t* data, std::size_t len) {
    if (fd_ < 0) {
        return false;
    }
#if defined(__linux__)
    constexpr int kFlags = MSG_NOSIGNAL;
#else
    constexpr int kFlags = 0; // SO_NOSIGPIPE ya fijado en makeSocket()
#endif
    std::size_t done = 0;
    while (done < len) {
        const ssize_t n = ::send(fd_, data + done, len - done, kFlags);
        if (n > 0) {
            done += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // Buffer del kernel lleno: esperar a que el lector drene.
            if (pollFor(fd_, POLLOUT, -1) <= 0) {
                return false;
            }
            continue;
        }
        return false; // EPIPE / ECONNRESET: el otro lado cerro
    }
    return true;
}

int LocalStream::read(std::uint8_t* buf, std::size_t cap) {
    if (fd_ < 0) {
        return -1;
    }
    for (;;) {
        const ssize_t n = ::recv(fd_, buf, cap, MSG_DONTWAIT);
        if (n > 0) {
            return static_cast<int>(n);
        }
        if (n == 0) {
            return -1; // EOF: el otro lado cerro
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0; // sin datos por ahora
        }
        return -1;
    }
}

bool LocalStream::waitReadable(int timeoutMs) {
    if (fd_ < 0) {
        return true; // read() devolveria <0 de inmediato
    }
    return pollFor(fd_, POLLIN, timeoutMs) > 0;
}

bool LocalStream::isOpen() const { return fd_ >= 0; }

void LocalStream::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

// ============================================================================
// LocalServer
// ============================================================================

LocalServer::~LocalServer() { close(); }

LocalServer::LocalServer(LocalServer&& other) noexcept
    : name_(std::move(other.name_)), fd_(other.fd_), path_(std::move(other.path_)) {
    other.fd_ = -1;
    other.path_.clear();
}

LocalServer& LocalServer::operator=(LocalServer&& other) noexcept {
    if (this != &other) {
        close();
        name_ = std::move(other.name_);
        fd_ = other.fd_;
        path_ = std::move(other.path_);
        other.fd_ = -1;
        other.path_.clear();
    }
    return *this;
}

bool LocalServer::listen(const std::string& name, LocalServer& out, std::string* err) {
    out.close();
    if (!validName(name)) {
        setErr(err, "nombre de canal invalido: '" + name + "'");
        return false;
    }
    const std::string path = socketPath(name);
    sockaddr_un addr{};
    if (path.empty() || !makeAddr(path, addr)) {
        setErr(err, "no se pudo resolver la ruta del socket para '" + name + "'");
        return false;
    }

    // Un archivo previo puede ser de un servidor vivo (error: el nombre esta
    // en uso) o un huerfano de un proceso que murio (se borra).
    if (!isAbstract(path) && ::access(path.c_str(), F_OK) == 0) {
        const int probe = makeSocket();
        if (probe >= 0) {
            const bool alive = ::connect(probe, reinterpret_cast<sockaddr*>(&addr), addrLen(path)) == 0;
            ::close(probe);
            if (alive) {
                setErr(err, "el canal '" + name + "' ya tiene un servidor activo");
                return false;
            }
        }
        ::unlink(path.c_str());
    }

    const int fd = makeSocket();
    if (fd < 0) {
        setErr(err, sysErr("socket"));
        return false;
    }
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), addrLen(path)) != 0) {
        setErr(err,
               errno == EADDRINUSE ? "el canal '" + name + "' ya tiene un servidor activo" : sysErr("bind"));
        ::close(fd);
        return false;
    }
    if (::listen(fd, 8) != 0) {
        setErr(err, sysErr("listen"));
        ::close(fd);
        if (!isAbstract(path)) ::unlink(path.c_str());
        return false;
    }
    out.name_ = name;
    out.fd_ = fd;
    out.path_ = path;
    return true;
}

bool LocalServer::accept(int timeoutMs, LocalStream& out, std::string* err) {
    out.close();
    if (fd_ < 0) {
        setErr(err, "el servidor no esta escuchando");
        return false;
    }
    const int r = pollFor(fd_, POLLIN, timeoutMs);
    if (r == 0) {
        setErr(err, "timeout");
        return false;
    }
    if (r < 0) {
        setErr(err, sysErr("poll"));
        return false;
    }
#if defined(__linux__)
    const int fd = ::accept4(fd_, nullptr, nullptr, SOCK_CLOEXEC);
#else
    const int fd = ::accept(fd_, nullptr, nullptr);
    if (fd >= 0) {
        ::fcntl(fd, F_SETFD, FD_CLOEXEC);
        const int one = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
    }
#endif
    if (fd < 0) {
        setErr(err, sysErr("accept"));
        return false;
    }
    if (!peerIsSameUser(fd)) {
        ::close(fd);
        setErr(err, "conexion rechazada: el cliente no es del mismo usuario");
        return false;
    }
    out.fd_ = fd;
    return true;
}

void LocalServer::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    if (!path_.empty()) {
        if (!isAbstract(path_)) ::unlink(path_.c_str());
        path_.clear();
    }
}

bool LocalServer::isListening() const { return fd_ >= 0; }

} // namespace ba::platform
