// ============================================================================
// src/core/platform/SingleInstance_posix.cpp — flock (Linux y macOS)
// ============================================================================

#include "platform/SingleInstance.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

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
    const std::filesystem::path dir = runtimeDir();
    if (dir.empty() || name.empty() || name.find('/') != std::string::npos) {
        if (err != nullptr) {
            *err = "no se pudo resolver el lockfile para '" + name + "'";
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
            *err = e == EWOULDBLOCK ? "ya hay otra instancia en ejecucion"
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
}

bool SingleInstance::held() const { return fd_ >= 0; }

void SingleInstance::release() {
    if (fd_ >= 0) {
        ::close(fd_); // cerrar suelta el flock
        fd_ = -1;
    }
}

} // namespace ba::platform
