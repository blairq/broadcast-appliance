// ============================================================================
// src/app/mcp/McpAlertTools.cpp — Disparadores (alertas) por MCP
//
// El asistente programa lo que hay que vigilar (alert_create) y el servidor le
// avisa cuando pasa: en la pestaña Asistente el aviso se escribe en su chat;
// cualquier otro cliente puede esperar con alert_wait (una llamada que vuelve
// apenas hay un evento, sin temporizadores del lado del agente). Las acciones
// de un disparador (herramientas MCP) se ejecutan aca, en el acto.
// ============================================================================

#include <QPointer>
#include "agent/AlertEngine.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "plugins/Plugins.hpp"
#include "ui/StudioViewModel.hpp"
#include "agent/Notificador.hpp"

namespace ba::app::mcp {

void McpService::registerAlertTools() {
    core_.addCategory(
        "alertas", "Disparadores (alertas)",
        "Pedirle al servidor que vigile algo (saturacion, silencio, fin de un tema, tiempo de un "
        "orador, lobby, chat, cortes, disco, temporizadores) y que avise o actue solo. Evita tener "
        "que consultar cada tanto: el aviso llega al chat del asistente.");
    agent::AlertEngine& eng = vm_.alerts();

    // Acciones de los disparadores: se ejecutan como llamadas MCP normales.
    // Con el rol `disparador` (sin parar la transmisión ni la grabación, sin
    // sacar gente, sin ajustes ni plugins, salvo lo que la regla permita en
    // `permitir_aire`), y lo que hicieron queda en alert_events.
    connect(&eng, &agent::AlertEngine::fired, this, [this, &eng](const json& ev) {
        const std::uint64_t id = ev.value("id", std::uint64_t{0});
        CallContext ctx{"disparador-" + std::to_string(id), "disparador", "disparador"};
        for (const auto& p : ev.value("permitir_aire", json::array()))
            if (p.is_string()) ctx.permitidas.insert(p.get<std::string>());
        for (const auto& a : ev.value("actions", json::array())) {
            if (!a.is_object() || !a.contains("tool")) continue;
            const std::string tool = a.value("tool", std::string());
            QPointer<QObject> vivo(this);
            core_.callTool({{"name", tool}, {"arguments", a.value("arguments", json::object())}},
                           [vivo, &eng, id, tool](const json& r) {
                               if (!vivo) return;
                               std::string texto;
                               for (const auto& c : r.value("content", json::array()))
                                   if (c.value("type", "") == "text") texto += c.value("text", "");
                               const json sc = r.value("structuredContent", json::object());
                               eng.anotarResultado(id, tool, !r.value("isError", false), texto,
                                                   sc.is_object() && sc.value("bloqueada_por_rol", false));
                           },
                           ctx);
        }
    });

    {
        Tool t;
        t.name = "notificador_listar";
        t.title = "Notificadores";
        t.category = "alertas";
        t.readOnly = true;
        t.description = "Los avisos fuera del programa (webhook, Telegram, mail SMTP) que puede usar un disparador "
                        "(`notificar` en alert_create): id, tipo, nombre y si ya tiene cargado su secreto.";
        t.run = [this](const json&, Done done) { done(ToolResult().data({{"notificadores", vm_.notificador().lista()}})); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "notificador_agregar";
        t.title = "Agregar un notificador";
        t.category = "alertas";
        t.description = "Crea (o cambia, con `id`) un notificador: `tipo` webhook, telegram (`chat_id`) o smtp "
                        "(`servidor`, `puerto` 465 o 587, `usuario`, `de`, `para`). El SECRETO (URL del webhook, token del "
                        "bot, contraseña del mail) NO va por aca: lo carga una persona en la pagina Asistente, seccion "
                        "Notificaciones. No se lo pidas al operador por el chat.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"tipo"}},
                         {"properties",
                          {{"id", {{"type", "string"}}},
                           {"tipo", {{"type", "string"}, {"enum", {"webhook", "telegram", "smtp"}}}},
                           {"nombre", {{"type", "string"}}},
                           {"chat_id", {{"type", "string"}}},
                           {"servidor", {{"type", "string"}}},
                           {"puerto", {{"type", "integer"}}},
                           {"usuario", {{"type", "string"}}},
                           {"de", {{"type", "string"}}},
                           {"para", {{"type", "string"}}}}}};
        t.run = [this](const json& a, Done done) {
            const json r = vm_.notificador().agregar(a, "");
            if (r.contains("error")) return done(ToolResult::error(r.value("error", std::string())));
            done(ToolResult().text("Creado. Falta que una persona cargue el secreto en la interfaz.").data(r));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "notificador_probar";
        t.title = "Probar un notificador";
        t.category = "alertas";
        t.description = "Manda un aviso de prueba por el notificador `id` y dice si llego.";
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", {{"type", "string"}}}}}};
        t.run = [this](const json& a, Done done) {
            vm_.notificador().enviar(a.value("id", std::string()), "Prueba de Broadcast Appliance",
                                     "Si te llego esto, los avisos funcionan.", [done](bool ok, const std::string& d) {
                                         if (!ok) return done(ToolResult::error(d));
                                         done(ToolResult::ok("Llego."));
                                     });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "notificador_borrar";
        t.title = "Borrar un notificador";
        t.category = "alertas";
        t.description = "Borra el notificador `id` (y su secreto guardado).";
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", {{"type", "string"}}}}}};
        t.run = [this](const json& a, Done done) {
            if (!vm_.notificador().borrar(a.value("id", std::string()))) return done(ToolResult::error("no hay ese notificador"));
            done(ToolResult::ok("Borrado."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "alert_types";
        t.title = "Que se puede vigilar";
        t.category = "alertas";
        t.readOnly = true;
        t.description = "Referencia tecnica: que parametros admite cada tipo y con que valores vienen. "
                        "Es documentacion, no hace nada.";
        t.details = "No crea ningun disparador: para eso es `alert_create`.";
        t.run = [](const json&, Done done) { done(ToolResult().data(agent::AlertEngine::types())); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "alert_create";
        t.title = "Crear un disparador";
        t.category = "alertas";
        t.description =
            "AVISAME CUANDO: crea un disparador que avisa solo al pasar algo (entra alguien, hay silencio "
            "al aire, satura el audio, se cae un invitado). Crea un disparador que el servidor evalua solo "
            "(cada 250 ms) y dispara un aviso cuando pasa. "
            "`notify`: [\"llm\"] = te lo escribe en el chat (despierta al asistente), [\"ui\"] = aviso en "
            "pantalla (por defecto las dos). `actions`: herramientas MCP a ejecutar en el acto al dispararse "
            "(p. ej. bajar un volumen, cambiar de escena). `once`: se apaga despues del primer aviso. "
            "`cooldownSec`: minimo entre avisos. Tipos y parametros: alert_types.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"type",
               {{"type", "string"},
                // Los tipos salen del motor: un tipo nuevo no queda afuera.
                {"enum",
                 [] {
                     json e = json::array();
                     for (const auto& t : agent::AlertEngine::types())
                         e.push_back(t.value("type", ""));
                     return e;
                 }()}}},
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
              {"sonido", {{"type", "string"}, {"enum", {"ninguno", "aviso", "critico"}},
                          {"description", "suena para el operador (crítico: hasta que se reconoce)"}}},
              {"reconocer", {{"type", "boolean"}, {"description", "queda como alarma hasta que alguien la reconoce"}}},
              {"permitir_aire",
               {{"type", "array"}, {"items", {{"type", "string"}}},
                {"description", "herramientas que el disparador puede usar aunque el rol `disparador` no las tenga "
                                "(stream_stop, record_stop, settings_set…); solo fuera del aire, y en vivo igual "
                                "piden confirmación"}}},
              {"notificar",
               {{"type", "array"}, {"items", {{"type", "string"}}},
                {"description", "ids de notificadores (notificador_listar): aviso por Telegram, mail o webhook"}}},
              {"cooldownSec", {{"type", "number"}, {"minimum", 0}}}}},
            {"required", {"type"}}};
        t.examples = {
            R"({"type": "audio_peak", "params": {"source": "*", "thresholdDb": -1}})",
            R"({"type": "media_end", "params": {"source": "Musica"}, "actions": [{"tool": "scene_switch", "arguments": {"scene": "Principal"}}]})",
            R"({"type": "speaker_time", "params": {"remainingSec": 30}})",
            R"({"type": "timer", "name": "Pausa", "params": {"in": 600}, "message": "Pasaron 10 minutos: toca la pausa."})"};
        t.run = [&eng, this](const json& a, Done done) {
            if (!a.value("permitir_aire", json::array()).empty() && core_.hayAire())
                return done(ToolResult::error("`permitir_aire` no se cambia al aire: hacelo fuera de la emisión."));
            const json r = eng.create(a);
            if (r.contains("error")) return done(ToolResult::error(r["error"].get<std::string>()));
            QMetaObject::invokeMethod(vm_.agent(), "alertsChanged");
            done(ToolResult().text("Disparador " + r["id"].get<std::string>() + " activo.").data(r));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "alert_reconocer";
        t.title = "Reconocer alarmas";
        t.category = "alertas";
        t.description = "Reconoce una alarma (`id` del evento, de alert_events o alert_list) o todas (`todas`: true): "
                        "deja de sonar la crítica y sale de la lista de la tarjeta Sistema. Sin argumentos, lista "
                        "las que esperan.";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"id", {{"type", "integer"}}}, {"todas", {{"type", "boolean"}}}}}};
        t.run = [&eng](const json& a, Done done) {
            if (a.value("todas", false) || a.contains("id")) {
                const int n = eng.reconocer(a.value("todas", false) ? 0 : a.value("id", std::uint64_t{0}));
                return done(ToolResult().data(json{{"reconocidas", n}, {"quedan", eng.activas()}}));
            }
            done(ToolResult().data(json{{"activas", eng.activas()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "alert_fabrica";
        t.title = "Disparadores de fábrica";
        t.category = "alertas";
        t.description =
            "Los disparadores que vienen prendidos: silencio al aire del programa (crítico), saturación del "
            "programa, la transmisión pierde cuadros o se cae (crítico), poco disco (menos de 30 min, también sin "
            "grabar), entrada sin señal (crítico) e invitado caído. `restablecer`: true los vuelve a poner como "
            "vinieron (los propios no se tocan).";
        t.inputSchema = {{"type", "object"}, {"properties", {{"restablecer", {{"type", "boolean"}}}}}};
        t.run = [&eng, this](const json& a, Done done) {
            const json r = eng.fabrica(a.value("restablecer", false));
            if (a.value("restablecer", false)) QMetaObject::invokeMethod(vm_.agent(), "alertsChanged");
            done(ToolResult().data(json{{"fabrica", r}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "alert_list";
        t.title = "Disparadores activos";
        t.category = "alertas";
        t.readOnly = true;
        t.description = "Los disparadores con su estado (activo, cuantas veces se disparo, la ultima vez) y "
                        "los plugins que "
                        "se declaran disparadores (`plugins`: se prenden y apagan con plugin_activar).";
        auto* pl = vm_.plugins();
        t.run = [&eng, pl](const json&, Done done) {
            json dePlugins = json::array();
            if (pl != nullptr)
                for (const auto& p : pl->listar())
                    if (p.value("disparador", false))
                        dePlugins.push_back({{"id", p.value("id", "")},
                                             {"nombre", p.value("nombre", "")},
                                             {"estado", p.value("estado", "")},
                                             {"descripcion", p.value("descripcion", "")}});
            done(ToolResult().data(json{{"alerts", eng.list()}, {"plugins", dePlugins}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "alert_update";
        t.title = "Cambiar un disparador";
        t.category = "alertas";
        t.description =
            "Activa/desactiva (`enabled`) o cambia parametros, mensaje, avisos o acciones de un disparador. "
            "`silenciada`: true le saca el sonido (sus alarmas siguen apareciendo en la tarjeta Sistema).";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"id", {{"type", "string"}}},
                           {"enabled", {{"type", "boolean"}}},
                           {"params", {{"type", "object"}}},
                           {"message", {{"type", "string"}}},
                           {"notify", {{"type", "array"}}},
                           {"actions", {{"type", "array"}}},
                           {"once", {{"type", "boolean"}}},
                           {"sonido", {{"type", "string"}, {"enum", {"ninguno", "aviso", "critico"}}}},
                           {"silenciada", {{"type", "boolean"},
                                           {"description", "true: no suena (sus alarmas siguen apareciendo, con su color)"}}},
                           {"reconocer", {{"type", "boolean"}}},
                           {"permitir_aire", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                           {"notificar", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                           {"cooldownSec", {{"type", "number"}}}}},
                         {"required", {"id"}}};
        t.run = [&eng, this](const json& a, Done done) {
            if (a.contains("permitir_aire") && core_.hayAire())
                return done(ToolResult::error("`permitir_aire` no se cambia al aire: hacelo fuera de la emisión."));
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
        t.title = "Borrar disparadores";
        t.category = "alertas";
        t.destructive = true;
        t.description = "Borra un disparador (`id`) o todos (`all`: true).";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"id", {{"type", "string"}}}, {"all", {{"type", "boolean"}}}}}};
        t.run = [&eng, this](const json& a, Done done) {
            if (a.value("all", false)) {
                eng.clear();
            } else if (!a.contains("id") || !eng.remove(a["id"].get<std::string>())) {
                return done(ToolResult::error("No existe ese disparador (alert_list los muestra)."));
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
            "Vuelve apenas salta un disparador (o al vencer `timeoutSec`, max. 300) con los avisos "
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
