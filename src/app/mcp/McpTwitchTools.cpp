// ============================================================================
// src/app/mcp/McpTwitchTools.cpp — Herramientas de Twitch del servidor MCP
//
// Las mismas capas que la pagina Twitch (TwitchViewModel/TwitchClient):
// cuenta (login por codigo), canal (titulo, categoria, etiquetas), transmitir
// desde la app, estado del vivo, VODs y chat.
// ============================================================================

#include <QTimer>

#include <memory>

#include "i18n/I18n.hpp"
#include "arranque/AppRuntime.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"
#include "ui/TwitchViewModel.hpp"

namespace ba::app::mcp {

namespace {

TwitchClient::Done reply(Done done, const std::string& okText = {}) {
    return [done, okText](bool ok, const QJsonObject& d, const QString& e) {
        if (!ok) return done(ToolResult::error("Twitch: " + e.toStdString()));
        ToolResult r;
        if (!okText.empty()) r.text(okText);
        if (!d.isEmpty()) r.data(toJson(d));
        done(r);
    };
}

} // namespace

void McpService::registerTwitchTools() {
    core_.addCategory(
        "twitch", ba::i18n::traducir("mcp", "Twitch"),
        ba::i18n::traducir("mcp", "Canal de Twitch: título y categoría, transmitir desde la app, estado del vivo, VODs y chat. "
        "Primero twitch_status: si no está configurado o conectado, explica qué falta."));
    auto* tw = static_cast<TwitchViewModel*>(vm_.twitch());
    AppRuntime& rt = rt_;
    const auto needLogin = [tw](Done& done) {
        if (!tw->configured()) {
            done(ToolResult::error(
                ba::i18n::traducir("mcp", "Twitch no esta configurado: falta el ID de cliente de una app de dev.twitch.tv. "
                "Usa twitch_setup (o la pestaña Twitch de la app, con los pasos y enlaces); "
                "help {\"topic\": \"twitch\"} los explica.")));
            return true;
        }
        if (!tw->loggedIn()) {
            done(ToolResult::error(
                ba::i18n::traducir("mcp", "No hay una cuenta de Twitch conectada: llama twitch_login (muestra un codigo que "
                "la persona confirma en twitch.tv/activate).")));
            return true;
        }
        return false;
    };

    {
        Tool t;
        t.name = "twitch_status";
        t.title = ba::i18n::traducir("mcp", "Estado de Twitch");
        t.category = "twitch";
        t.readOnly = true;
        t.description =
            ba::i18n::traducir("mcp", "Si la app de Twitch esta configurada, que cuenta esta conectada, si hay un login en curso (con "
            "su codigo), datos del canal, si esta en vivo y si la app transmite a Twitch.");
        t.run = [tw, &rt](const json&, Done done) {
            done(ToolResult().data(json{{"configured", tw->configured()},
                                        {"loggedIn", tw->loggedIn()},
                                        {"loginPending", tw->loginPending()},
                                        {"userCode", tw->userCode().toStdString()},
                                        {"verificationUri", tw->verificationUri().toStdString()},
                                        {"clientId", tw->clientId().toStdString()},
                                        {"user", qvToJson(tw->user())},
                                        {"channel", qvToJson(tw->channel())},
                                        {"stream", qvToJson(tw->stream())},
                                        {"streamingHere", tw->streamingHere()},
                                        {"appStreaming", rt.engine().status().streaming},
                                        {"chatOn", tw->chatOn()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "twitch_setup";
        t.title = ba::i18n::traducir("mcp", "Configurar la app de Twitch");
        t.category = "twitch";
        t.description =
            ba::i18n::traducir("mcp", "Guarda el ID de cliente de la app registrada en dev.twitch.tv (tipo Publico; `clientSecret` "
            "solo si es Confidencial). clear=true la borra.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"clientId", {{"type", "string"}}},
                           {"clientSecret", {{"type", "string"}}},
                           {"clear", {{"type", "boolean"}}}}}};
        t.run = [tw](const json& a, Done done) {
            if (a.value("clear", false)) {
                tw->clearClient();
                return done(ToolResult::ok(ba::i18n::traducir("mcp", "App de Twitch borrada.")));
            }
            const QString e = tw->saveClient(QString::fromStdString(a.value("clientId", std::string())),
                                             QString::fromStdString(a.value("clientSecret", std::string())));
            done(e.isEmpty() ? ToolResult::ok(ba::i18n::traducir("mcp", "App guardada. Siguiente paso: twitch_login."))
                             : ToolResult::error(e.toStdString()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "twitch_login";
        t.title = ba::i18n::traducir("mcp", "Conectar una cuenta de Twitch");
        t.category = "twitch";
        t.openWorld = true;
        t.description =
            ba::i18n::traducir("mcp", "Pide un codigo a Twitch y abre twitch.tv/activate en el navegador de esta maquina: la PERSONA "
            "inicia sesion con la cuenta del canal y confirma. Devuelve el codigo (mostraselo). Con "
            "wait=true espera a que termine (max. 5 min); si no, twitch_status dice cuando quedo "
            "conectado. cancel=true cancela.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"wait", {{"type", "boolean"}, {"default", false}}}, {"cancel", {{"type", "boolean"}}}}}};
        t.run = [tw](const json& a, Done done) {
            if (a.value("cancel", false)) {
                tw->cancelLogin();
                return done(ToolResult::ok(ba::i18n::traducir("mcp", "Login cancelado.")));
            }
            if (!tw->configured()) return done(ToolResult::error(ba::i18n::traducir("mcp", "Primero twitch_setup (ID de cliente).")));
            if (a.value("wait", false)) {
                tw->client().login([tw, done](bool ok, const QJsonObject& u, const QString& e) {
                    if (!ok) return done(ToolResult::error(e.toStdString()));
                    tw->refresh();
                    done(ToolResult().data(json{{"connected", toJson(u)}}));
                });
                return;
            }
            tw->login();
            // El codigo llega en un momento: esperarlo (max. ~8 s) para devolverlo.
            auto* timer = new QTimer(tw);
            auto tries = std::make_shared<int>(0);
            QObject::connect(timer, &QTimer::timeout, tw, [tw, timer, tries, done]() {
                if (tw->userCode().isEmpty() && tw->lastError().isEmpty() && ++*tries < 40) return;
                timer->deleteLater();
                if (tw->userCode().isEmpty())
                    return done(
                        ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "Twitch no devolvio el codigo: %1"), {tw->lastError().toStdString()})));
                done(ToolResult()
                         .text(ba::i18n::llenar(ba::i18n::traducir("mcp", "Codigo: %1 — se abrio %2; la persona confirma ahi. Despues, twitch_status."),
                                               {tw->userCode().toStdString(), tw->verificationUri().toStdString()}))
                         .data(json{{"userCode", tw->userCode().toStdString()},
                                    {"verificationUri", tw->verificationUri().toStdString()}}));
            });
            timer->start(200);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "twitch_logout";
        t.title = ba::i18n::traducir("mcp", "Desconectar la cuenta");
        t.category = "twitch";
        t.destructive = true;
        t.description = ba::i18n::traducir("mcp", "Revoca el token en Twitch y lo borra de la carpeta portable.");
        t.run = [tw](const json&, Done done) {
            tw->logout();
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Cuenta de Twitch desconectada.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "twitch_channel";
        t.title = ba::i18n::traducir("mcp", "Canal: titulo, categoria, etiquetas");
        t.category = "twitch";
        t.description =
            ba::i18n::traducir("mcp", "Sin cambios devuelve el canal (titulo, categoria, etiquetas, idioma). Con `title`, `game` "
            "(nombre de la categoria: se busca), `tags` y/o `language` los cambia. `search` busca "
            "categorias por nombre.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"title", {{"type", "string"}, {"maxLength", 140}}},
                           {"game", {{"type", "string"}}},
                           {"tags", {{"type", "array"}, {"items", {{"type", "string"}}}, {"maxItems", 10}}},
                           {"language", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "ISO 639-1, p. ej. es")}}},
                           {"search", {{"type", "string"}}}}}};
        t.examples = {R"({"title": "Charla en vivo", "game": "Just Chatting"})"};
        t.run = [tw, needLogin](const json& a, Done done) {
            if (needLogin(done)) return;
            if (a.contains("search"))
                return tw->client().searchCategories(QString::fromStdString(a["search"]), reply(done));
            json changes = a;
            if (changes.empty()) return tw->client().channel(reply(done));
            tw->client().updateChannel(toQt(changes),
                                       [tw, done](bool ok, const QJsonObject&, const QString& e) {
                                           if (!ok)
                                               return done(ToolResult::error("Twitch: " + e.toStdString()));
                                           tw->refresh();
                                           tw->client().channel(reply(done, ba::i18n::traducir("mcp", "Canal actualizado.")));
                                       });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "twitch_live_start";
        t.title = ba::i18n::traducir("mcp", "Transmitir a Twitch");
        t.category = "twitch";
        t.openWorld = true;
        t.description =
            ba::i18n::traducir("mcp", "Toma la clave del canal (solo en memoria) y empieza a transmitir desde la app a Twitch. "
            "Opcional: `title`/`game` se aplican antes. Verifica con twitch_stream_status en ~15 s.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"title", {{"type", "string"}}}, {"game", {{"type", "string"}}}}}};
        t.run = [tw, needLogin](const json& a, Done done) {
            if (needLogin(done)) return;
            const auto go = [tw, done]() { tw->goLive(reply(done, ba::i18n::traducir("mcp", "Transmitiendo a Twitch."))); };
            if (!a.contains("title") && !a.contains("game")) return go();
            tw->client().updateChannel(toQt(a), [go, done](bool ok, const QJsonObject&, const QString& e) {
                if (!ok) return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "Twitch (canal): %1"), {e.toStdString()})));
                go();
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "twitch_live_stop";
        t.title = ba::i18n::traducir("mcp", "Cortar la transmision a Twitch");
        t.category = "twitch";
        t.destructive = true;
        t.description =
            ba::i18n::traducir("mcp", "Corta la transmision de la app si iba a Twitch (Twitch termina el vivo al dejar de recibir).");
        t.run = [tw](const json&, Done done) { tw->stopLive(reply(done, ba::i18n::traducir("mcp", "Transmision cortada."))); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "twitch_stream_status";
        t.title = ba::i18n::traducir("mcp", "Estado del vivo en Twitch");
        t.category = "twitch";
        t.readOnly = true;
        t.description =
            ba::i18n::traducir("mcp", "Lo que ve Twitch: si el canal esta en vivo, espectadores, desde cuando, titulo y categoria.");
        t.run = [tw, needLogin](const json&, Done done) {
            if (needLogin(done)) return;
            tw->client().streamStatus(reply(done));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "twitch_videos";
        t.title = ba::i18n::traducir("mcp", "VODs del canal");
        t.category = "twitch";
        t.readOnly = true;
        t.description =
            ba::i18n::traducir("mcp", "Videos del canal (vivos guardados, destacados, subidos): id, titulo, enlace, vistas, fecha, "
            "duracion y tipo.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"max", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}, {"default", 25}}}}}};
        t.run = [tw, needLogin](const json& a, Done done) {
            if (needLogin(done)) return;
            tw->client().videos(a.value("max", 25), reply(done));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "twitch_video_delete";
        t.title = ba::i18n::traducir("mcp", "Borrar VODs");
        t.category = "twitch";
        t.destructive = true;
        t.description = ba::i18n::traducir("mcp", "Borra uno o mas videos del canal (no se puede deshacer).");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"ids",
               {{"type", "array"}, {"items", {{"type", "string"}}}, {"minItems", 1}, {"maxItems", 5}}}}},
            {"required", {"ids"}}};
        t.run = [tw, needLogin](const json& a, Done done) {
            if (needLogin(done)) return;
            QStringList ids;
            for (const auto& id : a["ids"])
                ids << QString::fromStdString(id.get<std::string>());
            tw->client().deleteVideos(ids, [tw, done](bool ok, const QJsonObject& d, const QString& e) {
                if (ok) tw->refresh();
                reply(done, ba::i18n::traducir("mcp", "Videos borrados."))(ok, d, e);
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "twitch_chat";
        t.title = ba::i18n::traducir("mcp", "Chat del canal");
        t.category = "twitch";
        t.description =
            ba::i18n::traducir("mcp", "Chat de Twitch: `on` lo prende/apaga (lo usa tambien la fuente \"Chat en vivo\"), `send` "
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
            const QJsonArray& all = tw->chatMessages();
            const int n = std::min<int>(all.size(), a.value("limit", 30));
            QJsonArray last;
            for (int i = all.size() - n; i < all.size(); ++i)
                last.append(all[i]);
            done(ToolResult().data(json{{"chatOn", tw->chatOn()}, {"messages", toJson(last)}}));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
