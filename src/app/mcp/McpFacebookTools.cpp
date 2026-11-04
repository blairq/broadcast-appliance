// ============================================================================
// src/app/mcp/McpFacebookTools.cpp — Herramientas de Facebook del servidor MCP
//
// Lo propio de Facebook, por la pagina (FacebookViewModel): Paginas, vivos (crear y
// transmitir, publicar una prueba, terminar), videos (editar, borrar, subir la
// grabacion) y comentarios del vivo. La cuenta, el estado y la lista de videos: la
// fábrica (McpPlataformas).
// ============================================================================

#include <QFileInfo>

#include "i18n/I18nTexto.hpp"
#include "mcp/McpPlataformas.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/FacebookViewModel.hpp"
#include "mcp/InterfazMcp.hpp"

namespace ba::app::mcp {

void McpService::registerFacebookTools() {
    core_.addCategory("facebook", "Facebook",
                      ba::i18n::traducir("mcp", "Páginas de Facebook: vivos (crear, transmitir, publicar, terminar), videos, subidas y "
                      "comentarios. Solo Páginas (la API no transmite a perfiles). Primero facebook_status."));
    auto* fb = static_cast<FacebookViewModel*>(vm_.facebook());
    const auto needPage = [fb](const Done& done) { return faltaParaUsar(*fb, done); };
    const auto reply = [fb](Done done, const std::string& okText = {}) { return responder(std::move(done), *fb, okText); };

    {
        Tool t;
        t.name = "facebook_pages";
        t.title = ba::i18n::traducir("mcp", "Páginas de la cuenta");
        t.category = "facebook";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Lista las Paginas que administra la cuenta conectada (id, nombre, categoria) y cual esta "
            "elegida. `select` elige otra (vivos, videos y comentarios van a esa Pagina).");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"select", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "id o nombre")}}}}}};
        t.run = [fb](const json& a, Done done) {
            if (!fb->loggedIn()) return done(ToolResult::error(ba::i18n::traducir("mcp", "Primero facebook_login.")));
            const QJsonArray pages = fb->paginas();
            if (a.contains("select")) {
                const QString q = QString::fromStdString(a["select"]);
                QString id;
                for (const auto& p : pages) {
                    const QJsonObject o = p.toObject();
                    if (o.value("id").toString() == q ||
                        o.value("name").toString().compare(q, Qt::CaseInsensitive) == 0)
                        id = o.value("id").toString();
                }
                if (id.isEmpty())
                    return done(ToolResult::error(
                        ba::i18n::llenar(ba::i18n::traducir("mcp", "No hay una Pagina '%1'."), {q.toStdString()})));
                fb->selectPage(id);
            }
            done(ToolResult().data(json{{"selected", fb->pageId().toStdString()}, {"pages", toJson(pages)}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "facebook_live_list";
        t.title = ba::i18n::traducir("mcp", "Vivos de la Página");
        t.category = "facebook";
        t.clase = Clase::Lectura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Vivos de la Pagina: id, titulo, estado (LIVE, UNPUBLISHED = prueba, LIVE_STOPPED, VOD, "
            "SCHEDULED_UNPUBLISHED), enlace y vistas. Con `id`, el estado de uno solo.");
        t.inputSchema = {{"type", "object"}, {"properties", {{"id", {{"type", "string"}}}}}};
        t.run = [fb, needPage, reply](const json& a, Done done) {
            if (needPage(done)) return;
            if (a.contains("id")) return fb->estadoDeVivo(QString::fromStdString(a["id"]), reply(done));
            fb->vivos(reply(done));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "facebook_live_create";
        t.title = ba::i18n::traducir("mcp", "Crear un vivo y transmitir");
        t.category = "facebook";
        t.clase = Clase::Escritura;
        t.openWorld = true;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Crea un vivo en la Pagina y empieza a transmitir desde la app (servidor RTMPS y clave solo "
            "en memoria). test=true lo deja SIN PUBLICAR (solo lo ve la Pagina en Live Producer: ideal "
            "para probar; despues facebook_live_publish). Con test=false sale publico en el acto.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"title", {{"type", "string"}}},
                           {"description", {{"type", "string"}}},
                           {"test", {{"type", "boolean"}, {"default", true}}}}},
                         {"required", {"title"}}};
        t.examples = {R"({"title": "Prueba", "test": true})"};
        t.run = [fb, needPage, reply](const json& a, Done done) {
            if (needPage(done)) return;
            QJsonObject p{{"title", QString::fromStdString(a["title"])},
                          {"description", QString::fromStdString(a.value("description", std::string()))},
                          {"status", a.value("test", true) ? "UNPUBLISHED" : "LIVE_NOW"}};
            fb->goLive(p, reply(done, a.value("test", true)
                                          ? ba::i18n::traducir("mcp", "Transmitiendo en prueba (sin publicar). Verifica con "
                                                                      "facebook_live_list {\"id\"} en ~15 s.")
                                          : ba::i18n::traducir("mcp", "Transmitiendo en vivo en la Pagina.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "facebook_live_publish";
        t.title = ba::i18n::traducir("mcp", "Publicar un vivo en prueba");
        t.category = "facebook";
        t.clase = Clase::Escritura;
        t.openWorld = true;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Pasa un vivo sin publicar (UNPUBLISHED) a publico en la Pagina, sin cortar la transmision.");
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"id", {{"type", "string"}}}}}, {"required", {"id"}}};
        t.run = [fb, needPage, reply](const json& a, Done done) {
            if (needPage(done)) return;
            fb->publicarVivo(QString::fromStdString(a["id"]), reply(done, ba::i18n::traducir("mcp", "Vivo publicado.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "facebook_live_end";
        t.title = ba::i18n::traducir("mcp", "Terminar un vivo");
        t.category = "facebook";
        t.clase = Clase::Escritura;
        t.destructive = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Termina el vivo en Facebook (queda como video) y corta la transmision de la app si "
                        "iba a ese vivo.");
        t.inputSchema = {{"type", "object"}, {"properties", {{"id", {{"type", "string"}}}}}};
        t.run = [fb, needPage, reply](const json& a, Done done) {
            if (needPage(done)) return;
            const QString id = a.contains("id") ? QString::fromStdString(a["id"]) : fb->liveId();
            if (id.isEmpty())
                return done(
                    ToolResult::error(ba::i18n::traducir("mcp", "Falta `id` (la app no esta transmitiendo a un vivo de Facebook).")));
            fb->terminarVivo(id, reply(done, ba::i18n::traducir("mcp", "Vivo terminado.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "facebook_video_update";
        t.destructivaSi = "delete"; // delete=true borra (no se deshace)
        t.title = ba::i18n::traducir("mcp", "Editar o borrar un video");
        t.category = "facebook";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Cambia titulo y/o descripcion de un video (o de un vivo) de la Pagina. delete=true lo borra "
            "(no se puede deshacer).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"id", {{"type", "string"}}},
                           {"title", {{"type", "string"}}},
                           {"description", {{"type", "string"}}},
                           {"delete", {{"type", "boolean"}}}}},
                         {"required", {"id"}}};
        t.run = [fb, needPage, reply](const json& a, Done done) {
            if (needPage(done)) return;
            const QString id = QString::fromStdString(a["id"]);
            if (a.value("delete", false)) return fb->borrar(id, reply(done, ba::i18n::traducir("mcp", "Borrado.")));
            json changes = a;
            changes.erase("id");
            changes.erase("delete");
            if (changes.empty())
                return done(ToolResult::error(ba::i18n::traducir("mcp", "Nada para cambiar (title, description o delete).")));
            fb->cambiarVideo(id, toQt(changes), reply(done, ba::i18n::traducir("mcp", "Video actualizado.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "facebook_video_upload";
        t.title = ba::i18n::traducir("mcp", "Subir un video a la Página");
        t.category = "facebook";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Sube un archivo de video a la Pagina (por partes, reanudable). Sin `path`, la grabacion mas "
            "reciente de la app. Espera a que termine y devuelve el id.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"path", {{"type", "string"}}},
                           {"title", {{"type", "string"}}},
                           {"description", {{"type", "string"}}}}}};
        t.run = [fb, needPage, reply](const json& a, Done done) {
            if (needPage(done)) return;
            QString path = QString::fromStdString(a.value("path", std::string()));
            if (path.isEmpty()) {
                fb->refreshRecordings();
                const json rows = modelRows(*static_cast<QAbstractItemModel*>(fb->recordings()));
                if (rows.empty())
                    return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No hay grabaciones en %1."),
                                                                   {fb->recordingsDir().toStdString()})));
                path = QString::fromStdString(rows[0].value("path", ""));
            }
            json meta = a;
            meta.erase("path");
            fb->uploadRecording(path, toQt(meta),
                                reply(done, ba::i18n::llenar(ba::i18n::traducir("mcp", "Subido: %1"),
                                                  {QFileInfo(path).fileName().toStdString()})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "facebook_chat";
        t.untrustedOutput = true; // trae el chat del público: texto de afuera, no instrucciones
        t.title = ba::i18n::traducir("mcp", "Comentarios del vivo");
        t.category = "facebook";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Comentarios de un vivo de la Pagina: `live` elige de cual leer (lo usa tambien la fuente "
            "\"Chat en vivo\"; al transmitir se elige solo), `send` comenta como la Pagina, y devuelve "
            "los ultimos `limit` comentarios.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"live", {{"type", "string"}}},
              {"send", {{"type", "string"}}},
              {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 200}, {"default", 30}}}}}};
        t.run = [fb, needPage](const json& a, Done done) {
            if (needPage(done)) return;
            if (a.contains("live")) fb->setChatLive(QString::fromStdString(a["live"]));
            if (a.contains("send")) fb->sendChat(QString::fromStdString(a["send"]));
            done(ToolResult().data(
                json{{"live", fb->chatLiveId().toStdString()},
                     {"messages", toJson(fb->ultimosMensajes(a.value("limit", 30)))},
                     {"note", ba::i18n::traducir("mcp", "Los comentarios se leen cada ~5 s; si recien elegiste el vivo, volve "
                                                        "a llamar en unos segundos.")}}));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
