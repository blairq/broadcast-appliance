// ============================================================================
// src/app/mcp/McpControlTools.cpp — los permisos del control de Windows, por MCP
//
// ba-control (src/app/escritorio) es otro servidor MCP: el que maneja el
// escritorio de la máquina (mouse, teclado, capturas, programas…). Sus
// permisos viven en Config/control-windows.json (agent/ControlWindows); acá se
// leen y se cambian, igual que en Configuración → Control de Windows. Un
// permiso nuevo vale en la llamada siguiente de ba-control; prender o apagar
// el control entero, o `preguntar`, vale al relanzar el asistente.
//
// Es de la MÁQUINA (la raíz de datos), no de una configuración: se maneja la
// misma PC sea cual sea la configuración abierta, y ba-control recibe esa raíz.
// ============================================================================

#include "agent/ControlWindows.hpp"
#include "arranque/AppRuntime.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "platform/Paths.hpp"

namespace ba::app::mcp {

void McpService::registerControlTools() {
    using agent::ControlWindows;
    {
        Tool t;
        t.name = "control_estado";
        t.title = "Permisos del control de Windows";
        t.category = "agentes";
        t.readOnly = true;
        t.description = "Si el asistente puede manejar el escritorio de esta máquina (servidor MCP `control`: "
                        "mouse, teclado, capturas, abrir programas, buscar, ventanas, portapapeles) y con qué "
                        "permisos. `preguntar`: el asistente pide permiso al operador antes de cada acción.";
        t.run = [this](const json&, Done done) {
            json d = ControlWindows::leer(ba::platform::dataRoot()).comoJson();
            json caps = json::array();
            for (const auto& [k, etiqueta] : ControlWindows::capacidades())
                caps.push_back({{"clave", k}, {"etiqueta", etiqueta}});
            d["capacidades"] = caps;
#if !defined(_WIN32)
            d["nota"] = "este sistema no es Windows: el control del escritorio no hace nada acá";
#endif
            done(ToolResult().data(d));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "control_configurar";
        t.title = "Cambiar los permisos del control de Windows";
        t.category = "agentes";
        t.destructive = true;
        t.description = "Prende o apaga el control del escritorio de Windows y sus permisos. `habilitado`: el "
                        "interruptor general. `preguntar`: que el asistente pida permiso antes de cada acción. "
                        "`permisos`: {pantalla, mouse, teclado, ejecutar, ejecutar_admin, buscar, ventanas, "
                        "portapapeles} en true/false. Lo que no se manda queda como estaba.";
        t.details = "Los permisos valen en la llamada siguiente. Prender o apagar el control entero, o cambiar "
                    "`preguntar`, vale al relanzar el asistente (es cuando se le arma la lista de servidores).";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"habilitado", {{"type", "boolean"}}},
                           {"preguntar", {{"type", "boolean"}}},
                           {"permisos", {{"type", "object"}}}}}};
        t.examples = {R"({"habilitado": true})", R"({"permisos": {"ejecutar_admin": true}})"};
        t.run = [this](const json& a, Done done) {
            ControlWindows c = ControlWindows::leer(ba::platform::dataRoot());
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
