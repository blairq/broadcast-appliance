// PerroGuardian.cpp
//
// Implementacion del watchdog de hardware (ver PerroGuardian.hpp).

#include "PerroGuardian.hpp"

#include "Log.hpp"
#include "i18n/I18n.hpp"

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#endif
#if defined(__linux__)
#include <linux/watchdog.h>
#endif

namespace ba::launcher {

using ba::i18n::llenar;
using ba::i18n::traducir;

PerroGuardian::~PerroGuardian() {
    // Si se va sin desarmar (el proceso termina en orden pero nadie llamó a
    // desarmar), se desarma igual: un destructor no es un cuelgue.
    desarmar();
}

bool PerroGuardian::abrir(const std::string& dispositivo, int segundos, std::string* error) {
#if !defined(_WIN32)
    desarmar();
    fd_ = ::open(dispositivo.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd_ < 0) {
        const std::string e = llenar(traducir("perro", "no se pudo abrir %1: %2 (¿hay una regla de udev que se lo dé al usuario?)"),
                                     {dispositivo, std::strerror(errno)});
        if (error != nullptr) *error = e;
        BA_LOG_WARN("perro", 0, e);
        return false;
    }
    int plazo = segundos;
#if defined(__linux__)
    const bool tomo = ::ioctl(fd_, WDIOC_SETTIMEOUT, &plazo) == 0;
#else
    const bool tomo = false; // fuera de Linux solo se prueba con un archivo
#endif
    if (!tomo)
        BA_LOG_WARN("perro", 0,
                    llenar(traducir("perro", "%1 no acepta el plazo de %2 s: queda el de la placa"), {dispositivo, std::to_string(segundos)}));
    else
        segundos = plazo; // la placa puede redondear
    BA_LOG_INFO("perro", 0,
                llenar(traducir("perro", "watchdog de hardware armado en %1: si el launcher se traba %2 s, la máquina se reinicia"),
                       {dispositivo, std::to_string(segundos)}));
    ultimoMs_ = 0;
    return true;
#else
    (void)dispositivo;
    (void)segundos;
    const std::string e = traducir("perro", "el watchdog de hardware es solo de Linux");
    if (error != nullptr) *error = e;
    BA_LOG_WARN("perro", 0, e);
    return false;
#endif
}

void PerroGuardian::latir(std::uint64_t ahoraMs) {
#if !defined(_WIN32)
    if (fd_ < 0 || (ultimoMs_ != 0 && ahoraMs - ultimoMs_ < 1000)) return;
    ultimoMs_ = ahoraMs;
    // Cualquier escritura le da de comer (el ioctl WDIOC_KEEPALIVE es lo mismo, y un
    // archivo de prueba lo entiende).
    if (::write(fd_, "k", 1) == 1) ++latidos_;
#else
    (void)ahoraMs;
#endif
}

void PerroGuardian::desarmar() {
#if !defined(_WIN32)
    if (fd_ < 0) return;
    // La «V» mágica: con ella, cerrar el dispositivo detiene el perro en vez de dejarlo
    // contando (salvo que el núcleo tenga nowayout: ahí no hay cómo y la máquina se
    // reinicia; se avisa).
    if (::write(fd_, "V", 1) != 1)
        BA_LOG_WARN("perro", 0, traducir("perro", "no se pudo desarmar el watchdog: la máquina puede reiniciarse"));
    ::close(fd_);
    fd_ = -1;
    BA_LOG_INFO("perro", 0, traducir("perro", "watchdog de hardware desarmado"));
#endif
}

} // namespace ba::launcher
