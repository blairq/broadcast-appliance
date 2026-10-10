// ============================================================================
// src/app/agent/ControlWindows.hpp — los permisos del control de Windows
//
// El archivo <datos de la máquina>/Config/control-windows.json (la raíz de
// datos, no la carpeta de una configuración: es la misma PC) que lee ba-control (el
// servidor MCP del escritorio, src/app/escritorio) en cada llamada. Lo escriben
// Configuración → Control de Windows y el MCP interno
// (control_estado / control_configurar); lo lee el lanzador de los asistentes
// para saber si les agrega ese servidor y si lo deja usar sin preguntar.
//
// De fábrica: APAGADO. Prendido, cada capacidad viene habilitada salvo
// «ejecutar como administrador», que se prende aparte. Y el asistente le
// pregunta al operador antes de cada acción (`preguntar`) salvo que lo apaguen.
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace ba::app::agent {

struct ControlWindows {
    bool habilitado = false;
    /// El cliente (Claude, Gemini) le pregunta al operador antes de cada herramienta.
    bool preguntar = true;
    bool pantalla = true, mouse = true, teclado = true, ejecutar = true, ejecutar_admin = false, buscar = true,
         ventanas = true, portapapeles = true, procesos = true, comandos = true, sistema = true;

    /// Las capacidades, en orden, con su etiqueta (para Configuración y el MCP).
    static const std::vector<std::pair<std::string, std::string>>& capacidades();
    bool* capacidad(const std::string& clave);

    nlohmann::json comoJson() const;
    /// Aplica lo que venga en `j` (habilitado, preguntar, permisos{...}); lo demás queda.
    void aplicar(const nlohmann::json& j);

    static std::filesystem::path archivo(const std::filesystem::path& datos);
    static ControlWindows leer(const std::filesystem::path& datos);
    bool guardar(const std::filesystem::path& datos) const;
};

} // namespace ba::app::agent
