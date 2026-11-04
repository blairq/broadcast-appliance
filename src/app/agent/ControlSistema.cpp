// src/app/agent/ControlSistema.cpp — ver ControlSistema.hpp
#include "agent/ControlSistema.hpp"
#include "arranque/ArchivoConfig.hpp"
#include "i18n/I18nTexto.hpp"

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

nlohmann::json ControlSistema::estado(const std::filesystem::path& datos) {
    nlohmann::json d = leer(datos).comoJson();
    nlohmann::json caps = nlohmann::json::array();
    for (const auto& [k, etiqueta] : capacidades()) caps.push_back({{"clave", k}, {"etiqueta", etiqueta}});
    d["capacidades"] = caps;
    // En qué sistema está y si su motor ya está hecho (PLAN-CONTROL-SISTEMA: Windows
    // primero; macOS y Linux en camino). Las herramientas son las mismas en los tres.
#if defined(_WIN32)
    d["sistema"] = "Windows";
    d["soportado"] = true;
#elif defined(__APPLE__)
    d["sistema"] = "macOS";
    d["soportado"] = true;
    d["nota"] = ba::i18n::traducir("mcp", "pide los permisos «Accesibilidad» y «Grabación de pantalla» de macOS la primera vez");
#else
    d["sistema"] = "Linux";
    d["soportado"] = false;
    d["nota"] = ba::i18n::traducir("mcp", "en Linux andan system_archivos y system_chrome; el resto está en camino");
#endif
    return d;
}

std::string ControlSistema::configurar(const std::filesystem::path& datos, const nlohmann::json& cambios,
                                       nlohmann::json* resultado) {
    ControlSistema c = leer(datos);
    const nlohmann::json p = cambios.is_object() ? cambios.value("permisos", nlohmann::json::object()) : nlohmann::json();
    if (p.is_object())
        for (auto it = p.begin(); it != p.end(); ++it)
            if (c.capacidad(it.key()) == nullptr)
                return ba::i18n::llenar(ba::i18n::traducir("mcp", "no existe el permiso «%1» (control_estado los lista)"),
                                        {it.key()});
    c.aplicar(cambios);
    if (!c.guardar(datos)) return ba::i18n::traducir("mcp", "no se pudo guardar la configuración");
    if (resultado != nullptr) *resultado = c.comoJson();
    return {};
}

} // namespace ba::app::agent
