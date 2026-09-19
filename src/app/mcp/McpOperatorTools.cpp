// ============================================================================
// src/app/mcp/McpOperatorTools.cpp — Hablarle al operador y ayudarlo con el chat
//
//   operator_message       aviso en la barra de abajo (color segun prioridad)
//   operator_ask           pregunta con opciones y/o texto libre; devuelve la respuesta
//   operator_messages      lo que esta en la barra y lo que ya se contesto
//   guest_chat_pending     mensajes de invitados que el operador no contesto
//   guest_reply_suggest    respuestas sugeridas: el operador elige una y se manda
// ============================================================================

#include <QJsonDocument>

#include <algorithm>
#include <map>

#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/ListModels.hpp"
#include "ui/OperatorMessages.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

namespace {

json roomChatItems(StudioViewModel& vm) {
    auto* conf = static_cast<ConferenceViewModel*>(vm.conference());
    return json::parse(QJsonDocument(static_cast<JsonListModel*>(conf->chat())->items())
                           .toJson(QJsonDocument::Compact)
                           .toStdString(),
                       nullptr, false);
}

/// Mensajes de invitados sin respuesta del operador (despues del ultimo mensaje
/// del operador en esa conversacion: privada con esa persona, o la general).
json pendingGuestChat(StudioViewModel& vm) {
    const json chat = roomChatItems(vm);
    std::map<std::string, json> convs; // clave: id de la persona (privado) o "all"
    for (const auto& m : chat) {
        const bool mine = m.value("mine", false);
        const std::string to = m.value("to", std::string("all"));
        if (mine) {
            // El operador contesto: esa conversacion queda al dia.
            convs.erase(to == "all" ? std::string("all") : to);
            continue;
        }
        const std::string key = to == "all" ? std::string("all") : m.value("fromId", std::string());
        json& c = convs[key];
        if (c.is_null())
            c = {{"private", to != "all"},
                 {"participant", to != "all" ? m.value("fromId", "") : ""},
                 {"name", to != "all" ? m.value("fromName", "") : "Chat general"},
                 {"messages", json::array()}};
        c["messages"].push_back({{"from", m.value("fromName", "")},
                                 {"fromId", m.value("fromId", "")},
                                 {"text", m.value("text", "")},
                                 {"atMs", m.value("atMs", 0.0)}});
    }
    json out = json::array();
    for (auto& [k, c] : convs)
        out.push_back(c);
    return out;
}

} // namespace

void McpService::registerOperatorTools() {
    core_.addCategory(
        "operador", "Hablar con el operador",
        "Mensajes y preguntas al operador en la barra de abajo de la app (con color segun prioridad) y "
        "ayuda con el chat de los invitados: ver mensajes sin responder y sugerir respuestas que el "
        "operador manda con un click.");
    StudioViewModel& vm = vm_;
    auto* msgs = static_cast<OperatorMessages*>(vm.messages());
    const json prioProp{
        {"type", "string"},
        {"enum", {"info", "success", "warning", "critical"}},
        {"default", "info"},
        {"description", "info = azul, success = verde, warning = ambar, critical = rojo (queda fijo)."}};
    {
        Tool t;
        t.name = "operator_message";
        t.title = "Mensaje al operador";
        t.category = "operador";
        t.description =
            "Muestra un mensaje en la barra de abajo de la app, con el color de la prioridad. Los "
            "info/success/warning se van solos a los `ttlSec` (30 por defecto); critical queda hasta que "
            "lo cierren. Corto y claro: el operador esta en vivo.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"text", {{"type", "string"}, {"maxLength", 500}}},
                           {"title", {{"type", "string"}, {"maxLength", 60}}},
                           {"priority", prioProp},
                           {"ttlSec", {{"type", "integer"}, {"minimum", 0}, {"maximum", 3600}}}}},
                         {"required", {"text"}}};
        t.run = [msgs](const json& a, Done done) {
            json spec = a;
            spec["kind"] = "message";
            const int id = msgs->post(spec);
            done(ToolResult::ok("Mostrado en la barra (id " + std::to_string(id) + ")."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "operator_ask";
        t.title = "Preguntarle al operador";
        t.category = "operador";
        t.description =
            "Le hace una pregunta al operador en la barra de abajo: con `options` (botones) y/o texto libre "
            "(`allowText`). Espera la respuesta hasta `timeoutSec` (60 por defecto, max. 600) y la devuelve; "
            "si no "
            "contesta a tiempo, la pregunta queda en la barra y la respuesta te llega despues a tu chat como "
            "[RESPUESTA DEL OPERADOR]. Para decisiones que son del operador (salir al aire, responder a un "
            "invitado "
            "por el, cambios grandes).";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"text", {{"type", "string"}, {"maxLength", 500}}},
              {"title", {{"type", "string"}, {"maxLength", 60}}},
              {"priority", prioProp},
              {"options",
               {{"type", "array"}, {"items", {{"type", "string"}, {"maxLength", 60}}}, {"maxItems", 6}}},
              {"allowText", {{"type", "boolean"}}},
              {"timeoutSec", {{"type", "integer"}, {"minimum", 0}, {"maximum", 600}, {"default", 60}}}}},
            {"required", {"text"}}};
        t.examples = {
            R"({"text": "Ana escribio preguntando cuando sale. ¿Como queres manejar los mensajes de los invitados?", "options": ["Responde vos por mi", "Sugerime respuestas", "Los contesto yo"]})"};
        t.run = [msgs](const json& a, Done done) {
            json spec = a;
            spec["kind"] = "question";
            if (!a.contains("allowText")) spec["allowText"] = !a.contains("options");
            const int id = msgs->post(spec);
            const int timeout = a.value("timeoutSec", 60);
            if (timeout <= 0)
                return done(
                    ToolResult::ok("Pregunta en la barra (id " + std::to_string(id) +
                                   "): la respuesta te llega a tu chat como [RESPUESTA DEL OPERADOR]."));
            msgs->waitAnswer(id, timeout * 1000, [done, id](json item) {
                if (item.is_null())
                    return done(ToolResult::ok("Todavia no contesto (la pregunta " + std::to_string(id) +
                                               " sigue en la barra; la respuesta te llega a tu chat)."));
                if (item.value("status", "") == "dismissed")
                    return done(ToolResult::ok("El operador cerro la pregunta sin contestar."));
                done(ToolResult()
                         .text("Respuesta del operador: " + item.value("answer", ""))
                         .data(json{{"id", id}, {"answer", item.value("answer", "")}}));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "operator_messages";
        t.title = "Barra del operador";
        t.category = "operador";
        t.readOnly = true;
        t.description =
            "Lo que esta en la barra ahora (y con `history`, lo ya contestado/cerrado con sus respuestas).";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"history", {{"type", "boolean"}, {"default", false}}}}}};
        t.run = [msgs](const json& a, Done done) {
            const json items = msgs->list(a.value("history", false));
            ToolResult r;
            r.data(json{{"items", items}});
            for (const auto& it : items)
                if (it.value("kind", "") == "reply") r.untrusted = true; // trae lo que escribio el invitado
            done(r);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "guest_chat_pending";
        t.title = "Mensajes de invitados sin responder";
        t.category = "operador";
        t.readOnly = true;
        t.description =
            "Conversaciones del chat de la sala donde un invitado escribio y el operador todavia no "
            "contesto (privadas al operador y la general), con los mensajes pendientes.";
        t.run = [&vm](const json&, Done done) {
            const json p = pendingGuestChat(vm);
            if (p.empty()) return done(ToolResult::ok("No hay mensajes de invitados sin responder."));
            ToolResult r;
            r.data(json{{"pending", p}});
            r.untrusted = true; // texto de invitados
            done(r);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "guest_reply_suggest";
        t.title = "Sugerir respuestas a un invitado";
        t.category = "operador";
        t.description = "Muestra en la barra del operador el mensaje de un invitado con 1 a 4 respuestas "
                        "sugeridas: el operador "
                        "toca una (o la edita) y se le manda al invitado (en privado si el invitado escribio "
                        "en privado; si no, "
                        "al chat general). Vuelve enseguida; lo que el operador haga te llega a tu chat. "
                        "Para responder vos "
                        "directamente (si el operador te autorizo): chat_send.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"participant", {{"type", "string"}, {"description", "Nombre o id del invitado."}}},
              {"guestText",
               {{"type", "string"}, {"description", "Lo que escribio (por defecto su ultimo mensaje)."}}},
              {"suggestions",
               {{"type", "array"},
                {"items", {{"type", "string"}, {"maxLength", 300}}},
                {"minItems", 1},
                {"maxItems", 4}}},
              {"public",
               {{"type", "boolean"}, {"description", "Responder en el chat general en vez de en privado."}}},
              {"priority", prioProp}}},
            {"required", {"participant", "suggestions"}}};
        t.run = [&vm, msgs](const json& a, Done done) {
            json p;
            std::string err;
            if (!resolveParticipant(roomSnapshot(vm), a["participant"].get<std::string>(), p, err))
                return done(ToolResult::error(err));
            const std::string pid = p.value("id", "");
            std::string guestText = a.value("guestText", std::string());
            bool wasPrivate = true;
            if (guestText.empty()) {
                const json chat = roomChatItems(vm);
                for (auto it = chat.rbegin(); it != chat.rend(); ++it)
                    if (!it->value("mine", false) && it->value("fromId", "") == pid) {
                        guestText = it->value("text", "");
                        wasPrivate = it->value("to", std::string("all")) != "all";
                        break;
                    }
            }
            const bool pub = a.value("public", !wasPrivate);
            json spec{{"kind", "reply"},
                      {"text", p.value("name", "") + " escribio"},
                      {"priority", a.value("priority", std::string("info"))},
                      {"participantId", pid},
                      {"participantName", p.value("name", "")},
                      {"to", pub ? std::string("all") : pid},
                      {"guestText", guestText},
                      {"suggestions", a["suggestions"]}};
            const int id = msgs->post(spec);
            done(ToolResult::ok("Sugerencias en la barra del operador (id " + std::to_string(id) +
                                "). Lo que mande (o si lo ignora) te llega a tu chat."));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
