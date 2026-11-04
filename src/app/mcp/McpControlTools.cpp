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

#include "agent/AgentViewModel.hpp"
#include "agent/ControlSistema.hpp"
#include "i18n/I18nTexto.hpp"
#include "arranque/AppRuntime.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"
#include "platform/Paths.hpp"

namespace ba::app::mcp {

void McpService::registerControlTools() {
    using agent::ControlSistema;
    {
        Tool t;
        t.name = "control_estado";
        t.title = ba::i18n::traducir("mcp", "Permisos del control del sistema");
        t.category = "agentes";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Si el asistente puede manejar el escritorio de esta máquina (servidor MCP `control`, las "
                        "mismas herramientas system_* en Windows, macOS y Linux: mouse, teclado, capturas, abrir "
                        "programas, buscar, ventanas, portapapeles, procesos, comandos, datos del equipo, los elementos "
                        "de las ventanas, Chrome, archivos, registro) y con qué permisos. `preguntar`: el asistente pide "
                        "permiso al operador antes de cada acción. `soportado`: si en este sistema ya anda.");
        t.run = [](const json&, Done done) { done(ToolResult().data(ControlSistema::estado(ba::platform::dataRoot()))); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "control_configurar";
        t.title = ba::i18n::traducir("mcp", "Cambiar los permisos del control del sistema");
        t.category = "agentes";
        t.clase = Clase::Escritura;
        t.destructive = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Prende o apaga el control del sistema (el escritorio de esta máquina) y sus permisos. `habilitado`: el "
                        "interruptor general. `preguntar`: que el asistente pida permiso antes de cada acción. "
                        "`permisos`: {pantalla, mouse, teclado, ejecutar, ejecutar_admin, buscar, ventanas, "
                        "portapapeles, procesos, comandos, sistema, archivos, archivos_escribir, navegador, registro, "
                        "avisos} en true/false. Lo que no se manda queda como estaba.");
        t.details = QT_TRANSLATE_NOOP("mcp", "Los permisos valen en la llamada siguiente. Prender o apagar el control entero, o cambiar "
                    "`preguntar`, vale al relanzar el asistente (es cuando se le arma la lista de servidores).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"habilitado", {{"type", "boolean"}}},
                           {"preguntar", {{"type", "boolean"}}},
                           {"permisos", {{"type", "object"}}}}}};
        t.examples = {R"({"habilitado": true})", R"({"permisos": {"ejecutar_admin": true}})"};
        t.run = [this](const json& a, Done done) {
            // Por el modelo: así Configuración se entera del cambio.
            auto* agente = qobject_cast<agent::AgentViewModel*>(vm_.agent());
            json resultado;
            const std::string e = agente != nullptr ? agente->configurarControlSistema(a, &resultado)
                                                    : ControlSistema::configurar(ba::platform::dataRoot(), a, &resultado);
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data(resultado));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
