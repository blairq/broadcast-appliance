// src/app/agent/ControlSistema.cpp — ver ControlSistema.hpp
#include "agent/ControlSistema.hpp"
#include "arranque/ArchivoConfig.hpp"

#include <QtGlobal>

#include <fstream>
#include <system_error>

namespace ba::app::agent {

using json = nlohmann::json;

// Las etiquetas se traducen al mostrarlas: ba::i18n::traducir("controlsistema", etiqueta).
const std::vector<std::pair<std::string, std::string>>& ControlSistema::capacidades() {
    static const std::vector<std::pair<std::string, std::string>> c{
        {"pantalla", QT_TRANSLATE_NOOP("controlsistema", "Ver la pantalla (capturas, monitores)")},
        {"mouse", QT_TRANSLATE_NOOP("controlsistema", "Mover y hacer clic con el mouse")},
        {"teclado", QT_TRANSLATE_NOOP("controlsistema", "Escribir y mandar teclas")},
        {"ejecutar", QT_TRANSLATE_NOOP("controlsistema", "Abrir programas, archivos y enlaces")},
        {"ejecutar_admin", QT_TRANSLATE_NOOP("controlsistema", "Ejecutar como administrador (pide UAC)")},
        {"buscar", QT_TRANSLATE_NOOP("controlsistema", "Buscar archivos")},
        {"ventanas", QT_TRANSLATE_NOOP("controlsistema", "Listar, activar, minimizar y cerrar ventanas")},
        {"portapapeles", QT_TRANSLATE_NOOP("controlsistema", "Leer y escribir el portapapeles")},
        {"procesos", QT_TRANSLATE_NOOP("controlsistema", "Ver los procesos y cerrar uno (por su número)")},
        {"comandos", QT_TRANSLATE_NOOP("controlsistema", "Correr comandos (cmd o PowerShell) y leer lo que contestan")},
        {"sistema", QT_TRANSLATE_NOOP("controlsistema", "Ver los datos del equipo (memoria, discos, Windows) y leer el registro")},
        {"archivos", QT_TRANSLATE_NOOP("controlsistema", "Leer y listar archivos")},
        {"archivos_escribir", QT_TRANSLATE_NOOP("controlsistema", "Escribir, copiar, mover y borrar archivos (a la papelera)")},
        {"navegador", QT_TRANSLATE_NOOP("controlsistema", "Chrome y Edge: la consola, JavaScript, navegar (un navegador con perfil propio)")},
        {"registro", QT_TRANSLATE_NOOP("controlsistema", "Escribir y borrar valores del registro de Windows")},
        {"avisos", QT_TRANSLATE_NOOP("controlsistema", "Mostrar avisos de Windows")}};
    return c;
}

bool* ControlSistema::capacidad(const std::string& k) {
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
    if (k == "archivos") return &archivos;
    if (k == "archivos_escribir") return &archivos_escribir;
    if (k == "navegador") return &navegador;
    if (k == "registro") return &registro;
    if (k == "avisos") return &avisos;
    return nullptr;
}

json ControlSistema::comoJson() const {
    json p = json::object();
    auto* yo = const_cast<ControlSistema*>(this);
    for (const auto& [k, etiqueta] : capacidades()) p[k] = *yo->capacidad(k);
    return json{{"habilitado", habilitado}, {"preguntar", preguntar}, {"permisos", p}};
}

void ControlSistema::aplicar(const json& j) {
    if (!j.is_object()) return;
    if (j.contains("habilitado") && j["habilitado"].is_boolean()) habilitado = j["habilitado"].get<bool>();
    if (j.contains("preguntar") && j["preguntar"].is_boolean()) preguntar = j["preguntar"].get<bool>();
    const json p = j.value("permisos", json::object());
    if (p.is_object())
        for (auto it = p.begin(); it != p.end(); ++it)
            if (bool* b = capacidad(it.key()); b != nullptr && it->is_boolean()) *b = it->get<bool>();
}

std::filesystem::path ControlSistema::archivo(const std::filesystem::path& datos) {
    return datos / "Config" / "control-sistema.json";
}

ControlSistema ControlSistema::leer(const std::filesystem::path& datos) {
    ControlSistema c;
    json j = config::leerObjeto(archivo(datos));
    // El de antes (cuando era «Control de Windows»): se lee hasta que se guarde el nuevo.
    if (j.empty()) j = config::leerObjeto(datos / "Config" / "control-windows.json");
    c.aplicar(j);
    return c;
}

bool ControlSistema::guardar(const std::filesystem::path& datos) const {
    return config::guardar(archivo(datos), comoJson());
}

} // namespace ba::app::agent
