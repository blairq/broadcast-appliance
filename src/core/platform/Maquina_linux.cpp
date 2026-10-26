// src/core/platform/Maquina_linux.cpp — el id y el modelo de la máquina en Linux

#include "platform/Maquina.hpp"

#include <fstream>

namespace ba::platform {

namespace {
std::string primeraLinea(const char* ruta) {
    std::ifstream f(ruta);
    std::string s;
    std::getline(f, s);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}
} // namespace

std::string idCrudoDeMaquina() {
    std::string s = primeraLinea("/etc/machine-id");
    if (s.empty()) s = primeraLinea("/var/lib/dbus/machine-id");
    return s;
}

std::string modeloDeMaquina() {
    // Legible sin permisos (product_uuid y el número de serie, no: piden root).
    const std::string fab = primeraLinea("/sys/class/dmi/id/sys_vendor");
    const std::string mod = primeraLinea("/sys/class/dmi/id/product_name");
    return fab.empty() ? mod : mod.empty() ? fab : fab + " " + mod;
}

} // namespace ba::platform
