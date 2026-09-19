// ============================================================================
// src/app/mcp/McpAlertTools.cpp — Vigilancias (alertas) por MCP
//
// El asistente programa lo que hay que vigilar (alert_create) y el servidor le
// avisa cuando pasa: en la pestaña Asistente el aviso se escribe en su chat;
// cualquier otro cliente puede esperar con alert_wait (una llamada que vuelve
// apenas hay un evento, sin temporizadores del lado del agente). Las acciones
// de una vigilancia (herramientas MCP) se ejecutan aca, en el acto.
// ============================================================================

#include "agent/AlertEngine.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

void McpService::registerAlertTools() {
    core_.addCategory(
        "alertas", "Vigilancias (alertas)",
        "Pedirle al servidor que vigile algo (saturacion, silencio, fin de un tema, tiempo de un "
        "orador, lobby, chat, cortes, disco, temporizadores) y que avise o actue solo. Evita tener "
        "que consultar cada tanto: el aviso llega al chat del asistente.");
    agent::AlertEngine& eng = vm_.alerts();

    // Acciones de las vigilancias: se ejecutan como llamadas MCP normales.
    connect(&eng, &agent::AlertEngine::fired, this, [this](const json& ev) {
        for (const auto& a : ev.value("actions", json::array())) {
            if (!a.is_object() || !a.contains("tool")) continue;
            const json call{
                {"jsonrpc", "2.0"},
                {"id", "alerta-" + std::to_string(ev.value("id", 0))},
                {"method", "tools/call"},
                {"params", {{"name", a["tool"]}, {"arguments", a.value("arguments", json::object())}}}};
            core_.handle(call, [](const json&) {});
        }
    });

    {
        Tool t;
        t.name = "alert_types";
        t.title = "Que se puede vigilar";
        t.category = "alertas";
        t.readOnly = true;
        t.description = "Tipos de vigilancia con sus parametros y valores por defecto.";
        t.run = [](const json&, Done done) { done(ToolResult().data(agent::AlertEngine::types())); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "alert_create";
        t.title = "Crear una vigilancia";
        t.category = "alertas";
        t.description =
            "Crea una vigilancia que el servidor evalua sola (cada 250 ms) y dispara un aviso cuando pasa. "
            "`notify`: [\"llm\"] = te lo escribe en el chat (despierta al asistente), [\"ui\"] = aviso en "
            "pantalla (por defecto las dos). `actions`: herramientas MCP a ejecutar en el acto al dispararse "
            "(p. ej. bajar un volumen, cambiar de escena). `once`: se apaga despues del primer aviso. "
            "`cooldownSec`: minimo entre avisos. Tipos y parametros: alert_types.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"type",
               {{"type", "string"},
                {"enum",
                 {"audio_peak", "audio_silence", "media_end", "speaker_time", "lobby", "hand", "guest_drop",
                  "chat", "guest_message", "stream_health", "disk", "timer"}}}},
              {"name", {{"type", "string"}}},
              {"params", {{"type", "object"}}},
              {"message", {{"type", "string"}, {"description", "Texto propio que acompaña al aviso."}}},
              {"notify", {{"type", "array"}, {"items", {{"type", "string"}, {"enum", {"llm", "ui"}}}}}},
              {"actions",
               {{"type", "array"},
                {"items",
                 {{"type", "object"},
                  {"properties", {{"tool", {{"type", "string"}}}, {"arguments", {{"type", "object"}}}}},
                  {"required", {"tool"}}}}}},
              {"once", {{"type", "boolean"}}},
              {"cooldownSec", {{"type", "number"}, {"minimum", 0}}}}},
            {"required", {"type"}}};
        t.examples = {
            R"({"type": "audio_peak", "params": {"source": "*", "thresholdDb": -1}})",
            R"({"type": "media_end", "params": {"source": "Musica"}, "actions": [{"tool": "scene_switch", "arguments": {"scene": "Principal"}}]})",
            R"({"type": "speaker_time", "params": {"remainingSec": 30}})",
            R"({"type": "timer", "name": "Pausa", "params": {"in": 600}, "message": "Pasaron 10 minutos: toca la pausa."})"};
        t.run = [&eng, this](const json& a, Done done) {
            const json r = eng.create(a);
            if (r.contains("error")) return done(ToolResult::error(r["error"].get<std::string>()));
            QMetaObject::invokeMethod(vm_.agent(), "alertsChanged");
            done(ToolResult().text("Vigilancia " + r["id"].get<std::string>() + " activa.").data(r));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "alert_list";
        t.title = "Vigilancias activas";
        t.category = "alertas";
        t.readOnly = true;
        t.description = "Las vigilancias con su estado (activa, cuantas veces se disparo, la ultima vez).";
        t.run = [&eng](const json&, Done done) { done(ToolResult().data(json{{"alerts", eng.list()}})); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "alert_update";
        t.title = "Cambiar una vigilancia";
        t.category = "alertas";
        t.description =
            "Activa/desactiva (`enabled`) o cambia parametros, mensaje, avisos o acciones de una vigilancia.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"id", {{"type", "string"}}},
                           {"enabled", {{"type", "boolean"}}},
                           {"params", {{"type", "object"}}},
                           {"message", {{"type", "string"}}},
                           {"notify", {{"type", "array"}}},
                           {"actions", {{"type", "array"}}},
                           {"once", {{"type", "boolean"}}},
                           {"cooldownSec", {{"type", "number"}}}}},
                         {"required", {"id"}}};
        t.run = [&eng, this](const json& a, Done done) {
            const json r = eng.update(a["id"].get<std::string>(), a);
            if (r.contains("error")) return done(ToolResult::error(r["error"].get<std::string>()));
            QMetaObject::invokeMethod(vm_.agent(), "alertsChanged");
            done(ToolResult().data(r));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "alert_delete";
        t.title = "Borrar vigilancias";
        t.category = "alertas";
        t.destructive = true;
        t.description = "Borra una vigilancia (`id`) o todas (`all`: true).";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"id", {{"type", "string"}}}, {"all", {{"type", "boolean"}}}}}};
        t.run = [&eng, this](const json& a, Done done) {
            if (a.value("all", false)) {
                eng.clear();
            } else if (!a.contains("id") || !eng.remove(a["id"].get<std::string>())) {
                return done(ToolResult::error("No existe esa vigilancia (alert_list las muestra)."));
            }
            QMetaObject::invokeMethod(vm_.agent(), "alertsChanged");
            done(ToolResult::ok("Listo."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "alert_events";
        t.title = "Avisos recientes";
        t.category = "alertas";
        t.readOnly = true;
        t.description =
            "Los avisos disparados (los mas nuevos al final). `sinceId` para leer solo los nuevos.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"sinceId", {{"type", "integer"}, {"minimum", 0}}},
              {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 200}, {"default", 30}}}}}};
        t.run = [&eng](const json& a, Done done) {
            const json evs = eng.events(a.value("sinceId", std::uint64_t{0}), a.value("limit", 30));
            ToolResult r;
            r.data(json{{"events", evs}, {"lastId", eng.lastEventId()}});
            for (const auto& e : evs)
                if (e.value("untrusted", false)) r.untrusted = true;
            done(r);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "alert_wait";
        t.title = "Esperar el proximo aviso";
        t.category = "alertas";
        t.readOnly = true;
        t.description =
            "Vuelve apenas se dispara una vigilancia (o al vencer `timeoutSec`, max. 300) con los avisos "
            "nuevos "
            "desde `sinceId`. Para clientes que no reciben los avisos en su chat: una sola llamada en vez de "
            "consultar cada tanto.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"sinceId", {{"type", "integer"}, {"minimum", 0}}},
              {"timeoutSec", {{"type", "integer"}, {"minimum", 1}, {"maximum", 300}, {"default", 120}}}}}};
        t.run = [&eng](const json& a, Done done) {
            const std::uint64_t since = a.value("sinceId", eng.lastEventId());
            eng.wait(since, a.value("timeoutSec", 120) * 1000, [done, &eng](json evs) {
                ToolResult r;
                r.data(json{{"events", evs}, {"lastId", eng.lastEventId()}, {"timedOut", evs.empty()}});
                for (const auto& e : evs)
                    if (e.value("untrusted", false)) r.untrusted = true;
                done(r);
            });
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
