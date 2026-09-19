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

void McpService::registerAssistantTools() {
    core_.addCategory(
        "asistente", "Pestaña Asistente",
        "El agente de IA que corre en la pestaña Asistente (Claude Code, Gemini CLI u opencode): "
        "proveedor, modelo, instalacion, arrancar/detener y mandarle pedidos.");
    auto* ag = static_cast<agent::AgentViewModel*>(vm_.agent());
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
            "Cambia proveedor (claude-cuenta, claude-api, gemini-cuenta, gemini-api, opencode-go, "
            "openrouter), modelo, arranque automatico y si los avisos de vigilancias van al chat. La "
            "clave de API se carga en la pestaña (no por aca).";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"provider",
                            {{"type", "string"},
                             {"enum",
                              {"claude-cuenta", "claude-api", "gemini-cuenta", "gemini-api", "opencode-go",
                               "openrouter"}}}},
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
