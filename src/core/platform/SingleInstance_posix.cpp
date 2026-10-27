// ============================================================================
// src/core/platform/SingleInstance_posix.cpp — instancia unica (Linux y macOS)
//
// Linux: un socket Unix ligado a un nombre ABSTRACTO
// ("\0broadcast-appliance-<uid>-<name>.lock"): el kernel no deja que dos
// procesos liguen el mismo nombre y lo libera solo cuando el proceso muere.
// No escribe nada en disco (el appliance es portable y no deja rastros en la
// maquina) ni deja lockfiles huerfanos.
// macOS (sin espacio abstracto): flock sobre runtimeDir()/<name>.lock.
// ============================================================================

#include "platform/SingleInstance.hpp"
#include "i18n/I18n.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstddef>
#include <string>

#include <cerrno>
#include <cstring>

#include "platform/Paths.hpp"

namespace ba::platform {

SingleInstance::~SingleInstance() { release(); }

SingleInstance::SingleInstance(SingleInstance&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

SingleInstance& SingleInstance::operator=(SingleInstance&& other) noexcept {
    if (this != &other) {
        release();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

bool SingleInstance::acquire(const std::string& name, SingleInstance& out, std::string* err) {
    out.release();
#if defined(__linux__)
    if (name.empty() || name.size() > 64 || name.find('/') != std::string::npos) {
        if (err != nullptr) *err = ba::i18n::llenar(ba::i18n::traducir("ipc", "nombre de lock invalido: '%1'"), {name});
        return false;
    }
    const std::string abs =
        std::string(1, '\0') + "broadcast-appliance-" + std::to_string(::getuid()) + "-" + name + ".lock";
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, abs.data(), abs.size());
    const int sfd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (sfd < 0) {
        if (err != nullptr) *err = std::string("socket: ") + std::strerror(errno);
        return false;
    }
    if (::bind(sfd, reinterpret_cast<sockaddr*>(&addr),
               static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + abs.size())) != 0) {
        const int e = errno;
        ::close(sfd);
        if (err != nullptr) {
            *err = e == EADDRINUSE ? ba::i18n::traducir("ipc", "ya hay otra instancia en ejecucion")
                                   : std::string("bind: ") + std::strerror(e);
        }
        return false;
    }
    out.fd_ = sfd;
    return true;
#else
    const std::filesystem::path dir = runtimeDir();
    if (dir.empty() || name.empty() || name.find('/') != std::string::npos) {
        if (err != nullptr) {
            *err = ba::i18n::llenar(ba::i18n::traducir("ipc", "no se pudo resolver el lockfile para '%1'"), {name});
        }
        return false;
    }
    const std::string path = (dir / (name + ".lock")).string();
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) {
        if (err != nullptr) {
            *err = std::string("open lockfile: ") + std::strerror(errno);
        }
        return false;
    }
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        const int e = errno;
        ::close(fd);
        if (err != nullptr) {
            *err = e == EWOULDBLOCK ? ba::i18n::traducir("ipc", "ya hay otra instancia en ejecucion")
                                    : std::string("flock: ") + std::strerror(e);
        }
        return false;
    }
    // El PID adentro es solo informativo (diagnostico); el lock es el flock.
    if (::ftruncate(fd, 0) == 0) {
        const std::string pid = std::to_string(::getpid()) + "\n";
        (void)!::write(fd, pid.data(), pid.size());
    }
    out.fd_ = fd;
    return true;
#endif
}

bool SingleInstance::held() const { return fd_ >= 0; }

void SingleInstance::release() {
    if (fd_ >= 0) {
        ::close(fd_); // cerrar suelta el flock
        fd_ = -1;
    }
}

} // namespace ba::platform
