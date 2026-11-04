// ============================================================================
// src/app/mcp/McpTwitchTools.cpp — Herramientas de Twitch del servidor MCP
//
// Lo propio de Twitch, por la pagina (TwitchViewModel): canal (titulo, categoria,
// etiquetas), transmitir desde la app, estado del vivo, borrar VODs y chat. La cuenta
// (login por codigo), el estado y la lista de VODs: la fábrica (McpPlataformas).
// ============================================================================

#include "i18n/I18nTexto.hpp"
#include "mcp/McpPlataformas.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"
#include "ui/TwitchViewModel.hpp"

namespace ba::app::mcp {

void McpService::registerTwitchTools() {
    core_.addCategory(
        "twitch", ba::i18n::traducir("mcp", "Twitch"),
        ba::i18n::traducir("mcp", "Canal de Twitch: título y categoría, transmitir desde la app, estado del vivo, VODs y chat. "
        "Primero twitch_status: si no está configurado o conectado, explica qué falta."));
    auto* tw = static_cast<TwitchViewModel*>(vm_.twitch());
    const auto needLogin = [tw](const Done& done) { return faltaParaUsar(*tw, done); };
    const auto reply = [tw](Done done, const std::string& okText = {}) { return responder(std::move(done), *tw, okText); };

    {
        Tool t;
        t.name = "twitch_channel";
        t.title = ba::i18n::traducir("mcp", "Canal: título, categoría, etiquetas");
        t.category = "twitch";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Sin cambios devuelve el canal (titulo, categoria, etiquetas, idioma). Con `title`, `game` "
            "(nombre de la categoria: se busca), `tags` y/o `language` los cambia. `search` busca "
            "categorias por nombre.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"title", {{"type", "string"}, {"maxLength", 140}}},
                           {"game", {{"type", "string"}}},
                           {"tags", {{"type", "array"}, {"items", {{"type", "string"}}}, {"maxItems", 10}}},
                           {"language", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "ISO 639-1, p. ej. es")}}},
                           {"search", {{"type", "string"}}}}}};
        t.examples = {R"({"title": "Charla en vivo", "game": "Just Chatting"})"};
        t.run = [tw, needLogin, reply](const json& a, Done done) {
            if (needLogin(done)) return;
            if (a.contains("search")) return tw->buscarCategorias(QString::fromStdString(a["search"]), reply(done));
            if (a.empty()) return tw->leerCanal(reply(done));
            tw->cambiarCanal(toQt(a), [tw, reply, done](bool ok, const QJsonObject&, const QString& e) {
                if (!ok) return done(ToolResult::error(tw->conNombre(e).toStdString()));
                tw->leerCanal(reply(done, ba::i18n::traducir("mcp", "Canal actualizado.")));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "twitch_live_start";
        t.title = ba::i18n::traducir("mcp", "Transmitir a Twitch");
        t.category = "twitch";
        t.clase = Clase::Escritura;
        t.openWorld = true;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Toma la clave del canal (solo en memoria) y empieza a transmitir desde la app a Twitch. "
            "Opcional: `title`/`game` se aplican antes. Verifica con twitch_stream_status en ~15 s.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"title", {{"type", "string"}}}, {"game", {{"type", "string"}}}}}};
        t.run = [tw, needLogin, reply](const json& a, Done done) {
            if (needLogin(done)) return;
            const auto go = [tw, reply, done]() { tw->goLive(reply(done, ba::i18n::traducir("mcp", "Transmitiendo a Twitch."))); };
            if (!a.contains("title") && !a.contains("game")) return go();
            tw->cambiarCanal(toQt(a), [go, done](bool ok, const QJsonObject&, const QString& e) {
                if (!ok) return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "Twitch (canal): %1"), {e.toStdString()})));
                go();
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "twitch_live_stop";
        t.title = ba::i18n::traducir("mcp", "Cortar la transmisión a Twitch");
        t.category = "twitch";
        t.clase = Clase::Escritura;
        t.destructive = true;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Corta la transmision de la app si iba a Twitch (Twitch termina el vivo al dejar de recibir).");
        t.run = [tw, reply](const json&, Done done) {
            tw->terminarVivo({}, reply(done, ba::i18n::traducir("mcp", "Transmision cortada.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "twitch_stream_status";
        t.title = ba::i18n::traducir("mcp", "Estado del vivo en Twitch");
        t.category = "twitch";
        t.clase = Clase::Lectura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Lo que ve Twitch: si el canal esta en vivo, espectadores, desde cuando, titulo y categoria.");
        t.run = [tw, needLogin, reply](const json&, Done done) {
            if (needLogin(done)) return;
            tw->estadoDelVivo(reply(done));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "twitch_video_delete";
        t.title = ba::i18n::traducir("mcp", "Borrar VODs");
        t.category = "twitch";
        t.clase = Clase::Escritura;
        t.destructive = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Borra de uno a cinco videos (VODs) del canal de Twitch (`ids`, de twitch_videos). No se puede deshacer.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"ids",
               {{"type", "array"}, {"items", {{"type", "string"}}}, {"minItems", 1}, {"maxItems", 5}}}}},
            {"required", {"ids"}}};
        t.run = [tw, needLogin, reply](const json& a, Done done) {
            if (needLogin(done)) return;
            QStringList ids;
            for (const auto& id : a["ids"])
                ids << QString::fromStdString(id.get<std::string>());
            tw->borrarVideos(ids, reply(done, ba::i18n::traducir("mcp", "Videos borrados.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "twitch_chat";
        t.untrustedOutput = true; // trae el chat del público: texto de afuera, no instrucciones
        t.title = ba::i18n::traducir("mcp", "Chat del canal");
        t.category = "twitch";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Chat de Twitch: `on` lo prende/apaga (lo usa tambien la fuente \"Chat en vivo\"), `send` "
            "escribe como el canal, y devuelve los ultimos `limit` mensajes (autor, color, texto, "
            "insignias).");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"on", {{"type", "boolean"}}},
              {"send", {{"type", "string"}, {"maxLength", 500}}},
              {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 200}, {"default", 30}}}}}};
        t.run = [tw, needLogin](const json& a, Done done) {
            if (needLogin(done)) return;
            if (a.contains("on")) tw->setChatOn(a["on"].get<bool>());
            if (a.contains("send")) {
                if (!tw->chatOn()) tw->setChatOn(true);
                tw->sendChat(QString::fromStdString(a["send"]));
            }
            done(ToolResult().data(
                json{{"chatOn", tw->chatOn()}, {"messages", toJson(tw->ultimosMensajes(a.value("limit", 30)))}}));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
