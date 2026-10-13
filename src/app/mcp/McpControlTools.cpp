// ============================================================================
// src/app/mcp/McpControlTools.cpp — los permisos del control del sistema, por MCP
//
// ba-control (src/app/escritorio) es otro servidor MCP: el que maneja el
// escritorio de la máquina (mouse, teclado, capturas, programas…). Sus
// permisos viven en Config/control-sistema.json (agent/ControlSistema); acá se
// leen y se cambian, igual que en Configuración → Control del sistema. Un
// permiso nuevo vale en la llamada siguiente de ba-control; prender o apagar
// el control entero, o `preguntar`, vale al relanzar el asistente.
//
// Es de la MÁQUINA (la raíz de datos), no de una configuración: se maneja la
// misma PC sea cual sea la configuración abierta, y ba-control recibe esa raíz.
// ============================================================================

#include "agent/ControlSistema.hpp"
#include "arranque/AppRuntime.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "platform/Paths.hpp"

namespace ba::app::mcp {

void McpService::registerControlTools() {
    using agent::ControlSistema;
    {
        Tool t;
        t.name = "control_estado";
        t.title = "Permisos del control del sistema";
        t.category = "agentes";
        t.readOnly = true;
        t.description = "Si el asistente puede manejar el escritorio de esta máquina (servidor MCP `control`, las "
                        "mismas herramientas system_* en Windows, macOS y Linux: mouse, teclado, capturas, abrir "
                        "programas, buscar, ventanas, portapapeles, procesos, comandos, datos del equipo, los elementos "
                        "de las ventanas, Chrome, archivos, registro) y con qué permisos. `preguntar`: el asistente pide "
                        "permiso al operador antes de cada acción. `soportado`: si en este sistema ya anda.";
        t.run = [this](const json&, Done done) {
            json d = ControlSistema::leer(ba::platform::dataRoot()).comoJson();
            json caps = json::array();
            for (const auto& [k, etiqueta] : ControlSistema::capacidades())
                caps.push_back({{"clave", k}, {"etiqueta", etiqueta}});
            d["capacidades"] = caps;
#if defined(_WIN32)
            d["sistema"] = "Windows";
            d["soportado"] = true;
#elif defined(__APPLE__)
            d["sistema"] = "macOS";
            d["soportado"] = true;
            d["nota"] = "pide los permisos «Accesibilidad» y «Grabación de pantalla» de macOS la primera vez";
#else
            d["sistema"] = "Linux";
            d["soportado"] = false;
            d["nota"] = "en Linux andan system_archivos y system_chrome; el resto está en camino";
#endif
            done(ToolResult().data(d));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "control_configurar";
        t.title = "Cambiar los permisos del control del sistema";
        t.category = "agentes";
        t.destructive = true;
        t.description = "Prende o apaga el control del sistema (el escritorio de esta máquina) y sus permisos. `habilitado`: el "
                        "interruptor general. `preguntar`: que el asistente pida permiso antes de cada acción. "
                        "`permisos`: {pantalla, mouse, teclado, ejecutar, ejecutar_admin, buscar, ventanas, "
                        "portapapeles, procesos, comandos, sistema, archivos, archivos_escribir, navegador, registro, "
                        "avisos} en true/false. Lo que no se manda queda como estaba.";
        t.details = "Los permisos valen en la llamada siguiente. Prender o apagar el control entero, o cambiar "
                    "`preguntar`, vale al relanzar el asistente (es cuando se le arma la lista de servidores).";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"habilitado", {{"type", "boolean"}}},
                           {"preguntar", {{"type", "boolean"}}},
                           {"permisos", {{"type", "object"}}}}}};
        t.examples = {R"({"habilitado": true})", R"({"permisos": {"ejecutar_admin": true}})"};
        t.run = [this](const json& a, Done done) {
            ControlSistema c = ControlSistema::leer(ba::platform::dataRoot());
            const json p = a.value("permisos", json::object());
            if (p.is_object())
                for (auto it = p.begin(); it != p.end(); ++it)
                    if (c.capacidad(it.key()) == nullptr)
                        return done(ToolResult::error("no existe el permiso «" + it.key() + "» (control_estado los lista)"));
            c.aplicar(a);
            if (!c.guardar(ba::platform::dataRoot())) return done(ToolResult::error("no se pudo guardar la configuración"));
            done(ToolResult().data(c.comoJson()));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
