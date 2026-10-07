// ============================================================================
// src/app/mcp/McpAssistantTools.cpp — La pestaña Asistente por MCP
//
// Estado, proveedor/modelo, instalacion del agente, arrancar/detener y mandar
// un pedido al agente de la pestaña (p. ej. desde otro cliente MCP).
// ============================================================================

#include <QTimer>

#include "agent/AgentViewModel.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

namespace {
/// Los ids de proveedor que existen de verdad, para el esquema.
json idsDeProveedores(agent::AgentViewModel* ag) {
    json out = json::array();
    if (ag == nullptr) return out;
    for (const QVariant& v : ag->providers())
        out.push_back(v.toMap().value("id").toString().toStdString());
    return out;
}
} // namespace

void McpService::registerAssistantTools() {
    core_.addCategory(
        "asistente", "Pestaña Asistente",
        "El agente de IA que corre en la pestaña Asistente (Claude Code, Gemini CLI u opencode): "
        "proveedor, modelo, instalación, arrancar/detener y mandarle pedidos.");
    auto* ag = static_cast<agent::AgentViewModel*>(vm_.agent());
    {
        Tool t;
        t.name = "agente_limpieza";
        t.destructive = true; // borra (la lista sola no toca nada)
        t.title = "Limpiar los asistentes";
        t.category = "asistente";
        t.description =
            "Lo que ocupan los asistentes (Claude Code, Gemini CLI, opencode) y su limpieza: los chats guardados "
            "que se acumulan, las versiones viejas del programa, o reinstalar si algo falla.";
        t.details =
            "Sin `accion` (o `ver`): por motor, las versiones instaladas (cuál se usa), lo que ocupa el "
            "programa, los chats y el perfil entero, y los espacios de trabajo.\n"
            "- `chats`: borra los chats guardados (`motor` vacío = de todos, y los espacios de trabajo). "
            "El login y los ajustes quedan.\n"
            "- `versiones`: borra las versiones del programa que no se usan.\n"
            "- `reinstalar`: borra el programa del `motor` y lo vuelve a bajar (el login y los chats quedan).\n"
            "- `de_cero`: además borra el perfil entero: chats, ajustes y el LOGIN (hay que volver a entrar). "
            "Las claves de API pegadas quedan.\n"
            "Nada de eso se puede con el asistente o un encargo de agentes corriendo.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"accion", {{"type", "string"}, {"enum", {"ver", "chats", "versiones", "reinstalar", "de_cero"}}}},
              {"motor", {{"type", "string"}, {"enum", {"", "claude", "gemini", "opencode"}}}}}}};
        t.run = [ag](const json& a, Done done) {
            const std::string accion = a.value("accion", std::string("ver"));
            const QString motor = QString::fromStdString(a.value("motor", std::string()));
            const auto mb = [](double b) { return std::to_string(static_cast<long long>(b / 1048576.0)) + " MB"; };
            if (accion == "chats" || accion == "versiones") {
                const QVariantMap r = accion == "chats" ? ag->borrarChats(motor) : ag->borrarVersionesViejas(motor);
                if (!r.value("ok").toBool()) return done(ToolResult::error(r.value("error").toString().toStdString()));
                return done(ToolResult::ok(std::string(accion == "chats" ? "Chats borrados" : "Versiones viejas borradas") +
                                           ": se liberaron " + mb(r.value("bytes").toDouble()) + "."));
            }
            if (accion == "reinstalar" || accion == "de_cero") {
                if (motor.isEmpty()) return done(ToolResult::error("Decí qué `motor` reinstalar."));
                const QString e = ag->reinstalar(motor, accion == "de_cero");
                if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
                return done(ToolResult::ok("Reinstalando " + motor.toStdString() +
                                           (accion == "de_cero" ? " desde cero (hay que volver a entrar)" : "") +
                                           ": el avance, en assistant_status."));
            }
            ag->revisarLimpieza();
            done(ToolResult().data(json{{"limpieza", qvToJson(ag->limpieza())}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "assistant_status";
        t.title = "Estado del asistente";
        t.category = "asistente";
        t.readOnly = true;
        t.description =
            "Proveedor y modelo elegidos, proveedores disponibles, si el agente esta instalado y corriendo.";
        t.run = [ag](const json&, Done done) {
            done(ToolResult().data(json{{"provider", ag->provider().toStdString()},
                                        {"current", qvToJson(ag->current())},
                                        {"model", ag->model().toStdString()},
                                        {"hasKey", ag->hasKey()},
                                        {"installed", ag->installed()},
                                        {"installing", ag->installing()},
                                        {"installText", ag->installText().toStdString()},
                                        {"running", ag->running()},
                                        {"autoStart", ag->autoStart()},
                                        {"alertsToChat", ag->alertsToChat()},
                                        {"providers", qvToJson(ag->providers())}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "assistant_config";
        t.title = "Configurar el asistente";
        t.category = "asistente";
        t.description =
            "Cambia proveedor, modelo, arranque automatico y si los avisos de disparadores van al "
            "chat. `assistant_status` lista los proveedores que hay y los modelos de cada uno. La "
            "clave de API se carga en la pestaña (no por aca).";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"provider",
                            // La lista SALE DE LA TABLA, no escrita a mano.
                            // Escrita a mano se desactualizo en cuanto se
                            // agrego un proveedor: el nuevo existia en la
                            // pestaña, aparecia en `assistant_status` y esta
                            // herramienta lo rechazaba como invalido.
                            {{"type", "string"}, {"enum", idsDeProveedores(ag)}}},
                           {"model", {{"type", "string"}}},
                           {"autoStart", {{"type", "boolean"}}},
                           {"alertsToChat", {{"type", "boolean"}}}}}};
        t.run = [ag](const json& a, Done done) {
            if (a.contains("provider")) ag->setProvider(QString::fromStdString(a["provider"]));
            if (a.contains("model")) ag->setModel(QString::fromStdString(a["model"]));
            if (a.contains("autoStart")) ag->setAutoStart(a["autoStart"].get<bool>());
            if (a.contains("alertsToChat")) ag->setAlertsToChat(a["alertsToChat"].get<bool>());
            done(ToolResult::ok("Listo: " + ag->provider().toStdString() +
                                (ag->model().isEmpty() ? "" : " / " + ag->model().toStdString())));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "assistant_install";
        t.title = "Instalar el agente";
        t.category = "asistente";
        t.openWorld = true;
        t.description =
            "Descarga el agente del proveedor elegido (version fijada, SHA-256) a la carpeta portable. "
            "Con wait=true espera a que termine (max. 15 min).";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"wait", {{"type", "boolean"}, {"default", false}}}}}};
        t.run = [ag](const json& a, Done done) {
            if (ag->installed()) return done(ToolResult::ok("Ya esta instalado."));
            ag->install();
            if (!a.value("wait", false))
                return done(ToolResult::ok("Instalando: " + ag->installText().toStdString()));
            auto* timer = new QTimer(ag);
            auto elapsed = std::make_shared<int>(0);
            QObject::connect(timer, &QTimer::timeout, ag, [ag, timer, elapsed, done]() {
                *elapsed += 500;
                if (ag->installing() && *elapsed < 15 * 60 * 1000) return;
                timer->deleteLater();
                done(ag->installed() ? ToolResult::ok("Instalado.")
                                     : ToolResult::error(ag->installText().toStdString()));
            });
            timer->start(500);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "assistant_run";
        t.title = "Arrancar / detener el asistente";
        t.category = "asistente";
        t.description = "action: start | stop | restart.";
        t.inputSchema = {
            {"type", "object"},
            {"properties", {{"action", {{"type", "string"}, {"enum", {"start", "stop", "restart"}}}}}},
            {"required", {"action"}}};
        t.run = [ag](const json& a, Done done) {
            const std::string act = a["action"];
            if (act == "start") ag->start();
            if (act == "stop") ag->stop();
            if (act == "restart") ag->restart();
            QTimer::singleShot(act == "restart" ? 800 : 300, ag, [ag, done]() {
                if (!ag->lastError().isEmpty() && !ag->running())
                    return done(ToolResult::error(ag->lastError().toStdString()));
                done(ToolResult::ok(ag->running() ? "Corriendo." : "Detenido."));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "assistant_send";
        t.title = "Mandar un pedido al asistente";
        t.category = "asistente";
        t.description = "Escribe el texto en el prompt del agente de la pestaña y lo manda (como si lo "
                        "tipeara la persona).";
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"text", {{"type", "string"}}}}}, {"required", {"text"}}};
        t.run = [ag](const json& a, Done done) {
            if (!ag->running())
                return done(ToolResult::error("El asistente no esta corriendo (assistant_run)."));
            ag->send(QString::fromStdString(a["text"]));
            done(ToolResult::ok("Enviado."));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
