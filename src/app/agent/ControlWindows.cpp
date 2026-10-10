// src/app/agent/ControlWindows.cpp — ver ControlWindows.hpp
#include "agent/ControlWindows.hpp"

#include <fstream>
#include <system_error>

namespace ba::app::agent {

using json = nlohmann::json;

const std::vector<std::pair<std::string, std::string>>& ControlWindows::capacidades() {
    static const std::vector<std::pair<std::string, std::string>> c{
        {"pantalla", "Ver la pantalla (capturas, monitores)"},
        {"mouse", "Mover y hacer clic con el mouse"},
        {"teclado", "Escribir y mandar teclas"},
        {"ejecutar", "Abrir programas, archivos y enlaces"},
        {"ejecutar_admin", "Ejecutar como administrador (pide UAC)"},
        {"buscar", "Buscar archivos"},
        {"ventanas", "Listar, activar, minimizar y cerrar ventanas"},
        {"portapapeles", "Leer y escribir el portapapeles"},
        {"procesos", "Ver los procesos y cerrar uno (por su número)"},
        {"comandos", "Correr comandos (cmd o PowerShell) y leer lo que contestan"},
        {"sistema", "Ver los datos del equipo (memoria, discos, Windows)"}};
    return c;
}

bool* ControlWindows::capacidad(const std::string& k) {
    if (k == "pantalla") return &pantalla;
    if (k == "mouse") return &mouse;
    if (k == "teclado") return &teclado;
    if (k == "ejecutar") return &ejecutar;
    if (k == "ejecutar_admin") return &ejecutar_admin;
    if (k == "buscar") return &buscar;
    if (k == "ventanas") return &ventanas;
    if (k == "portapapeles") return &portapapeles;
    if (k == "procesos") return &procesos;
    if (k == "comandos") return &comandos;
    if (k == "sistema") return &sistema;
    return nullptr;
}

json ControlWindows::comoJson() const {
    json p = json::object();
    auto* yo = const_cast<ControlWindows*>(this);
    for (const auto& [k, etiqueta] : capacidades()) p[k] = *yo->capacidad(k);
    return json{{"habilitado", habilitado}, {"preguntar", preguntar}, {"permisos", p}};
}

void ControlWindows::aplicar(const json& j) {
    if (!j.is_object()) return;
    if (j.contains("habilitado") && j["habilitado"].is_boolean()) habilitado = j["habilitado"].get<bool>();
    if (j.contains("preguntar") && j["preguntar"].is_boolean()) preguntar = j["preguntar"].get<bool>();
    const json p = j.value("permisos", json::object());
    if (p.is_object())
        for (auto it = p.begin(); it != p.end(); ++it)
            if (bool* b = capacidad(it.key()); b != nullptr && it->is_boolean()) *b = it->get<bool>();
}

std::filesystem::path ControlWindows::archivo(const std::filesystem::path& datos) {
    return datos / "Config" / "control-windows.json";
}

ControlWindows ControlWindows::leer(const std::filesystem::path& datos) {
    ControlWindows c;
    std::ifstream f(archivo(datos));
    if (!f) return c;
    c.aplicar(json::parse(f, nullptr, false));
    return c;
}

bool ControlWindows::guardar(const std::filesystem::path& datos) const {
    std::error_code ec;
    std::filesystem::create_directories(archivo(datos).parent_path(), ec);
    const auto tmp = std::filesystem::path(archivo(datos)).concat(".tmp");
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return false;
        f << comoJson().dump(2) << "\n";
        if (!f) return false;
    }
    std::filesystem::rename(tmp, archivo(datos), ec);
    return !ec;
}

} // namespace ba::app::agent
