// src/core/platform/Segmento_posix.cpp — Segmento con shm_open + mmap (ver Segmento.hpp).
#include "platform/Segmento.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace ba::platform {

namespace {
std::string conBarra(const std::string& n) { return "/" + n; }
void poner(std::string* err, const std::string& que) {
    if (err != nullptr) *err = que + ": " + std::strerror(errno);
}
} // namespace

Segmento::~Segmento() { cerrar(); }

Segmento::Segmento(Segmento&& o) noexcept { *this = std::move(o); }

Segmento& Segmento::operator=(Segmento&& o) noexcept {
    if (this != &o) {
        cerrar();
        std::swap(datos_, o.datos_);
        std::swap(tam_, o.tam_);
        std::swap(nombre_, o.nombre_);
        std::swap(dueno_, o.dueno_);
        std::swap(handle_, o.handle_);
    }
    return *this;
}

bool Segmento::crear(const std::string& nombre, size_t bytes, std::string* err) {
    cerrar();
    const std::string n = conBarra(nombre);
    shm_unlink(n.c_str()); // uno viejo con el mismo nombre (un proceso anterior con el mismo pid)
    const int fd = shm_open(n.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) {
        poner(err, "shm_open " + n);
        return false;
    }
    if (ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
        poner(err, "ftruncate");
        close(fd);
        shm_unlink(n.c_str());
        return false;
    }
    void* p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) {
        poner(err, "mmap");
        shm_unlink(n.c_str());
        return false;
    }
    datos_ = p;
    tam_ = bytes;
    nombre_ = n;
    dueno_ = true;
    return true;
}

bool Segmento::abrir(const std::string& nombre, std::string* err) {
    cerrar();
    const std::string n = conBarra(nombre);
    const int fd = shm_open(n.c_str(), O_RDONLY, 0);
    if (fd < 0) {
        poner(err, "shm_open " + n);
        return false;
    }
    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        poner(err, "fstat");
        close(fd);
        return false;
    }
    void* p = mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) {
        poner(err, "mmap");
        return false;
    }
    // Ya está mapeado: el nombre no hace falta más (y así no queda colgado).
    shm_unlink(n.c_str());
    datos_ = p;
    tam_ = static_cast<size_t>(st.st_size);
    nombre_ = n;
    dueno_ = false;
    return true;
}

void Segmento::cerrar() {
    if (datos_ != nullptr) munmap(datos_, tam_);
    if (dueno_ && !nombre_.empty()) shm_unlink(nombre_.c_str());
    datos_ = nullptr;
    tam_ = 0;
    nombre_.clear();
    dueno_ = false;
}

} // namespace ba::platform
