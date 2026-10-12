// ============================================================================
// src/app/mcp/McpFacebookTools.cpp — Herramientas de Facebook del servidor MCP
//
// Las mismas capas que la pagina Facebook (FacebookViewModel/FacebookClient):
// cuenta, Paginas, vivos (crear y transmitir, publicar una prueba, terminar),
// videos (editar, borrar, subir la grabacion) y comentarios del vivo.
// ============================================================================

#include <QFileInfo>

#include "arranque/AppRuntime.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/FacebookViewModel.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

namespace {

FacebookClient::Done reply(Done done, const std::string& okText = {}) {
    return [done, okText](bool ok, const QJsonObject& d, const QString& e) {
        if (!ok) return done(ToolResult::error("Facebook: " + e.toStdString()));
        ToolResult r;
        if (!okText.empty()) r.text(okText);
        if (!d.isEmpty()) r.data(toJson(d));
        done(r);
    };
}

} // namespace

void McpService::registerFacebookTools() {
    core_.addCategory("facebook", "Facebook",
                      "Páginas de Facebook: vivos (crear, transmitir, publicar, terminar), videos, subidas y "
                      "comentarios. Solo Páginas (la API no transmite a perfiles). Primero facebook_status.");
    auto* fb = static_cast<FacebookViewModel*>(vm_.facebook());
    AppRuntime& rt = rt_;
    const auto needPage = [fb](Done& done) {
        if (!fb->configured()) {
            done(ToolResult::error(
                "Facebook no esta configurado: falta la app de Meta (ID y clave secreta). Usa "
                "facebook_setup (o la pestaña Facebook de la app, con los pasos y enlaces); "
                "help {\"topic\": \"facebook\"} los explica."));
            return true;
        }
        if (!fb->loggedIn()) {
            done(ToolResult::error(
                "No hay una cuenta de Facebook conectada: llama facebook_login (abre el navegador "
                "para que la persona elija la cuenta y las Paginas)."));
            return true;
        }
        if (fb->pageId().isEmpty()) {
            done(ToolResult::error(
                "No hay una Pagina elegida: facebook_pages {\"select\": \"<id>\"}. Si la lista esta "
                "vacia, la cuenta no administra Paginas o no se marcaron al conectar."));
            return true;
        }
        return false;
    };

    {
        Tool t;
        t.name = "facebook_status";
        t.title = "Estado de Facebook";
        t.category = "facebook";
        t.readOnly = true;
        t.description =
            "Si la app de Meta esta configurada (y su URI de redireccion), que cuenta esta conectada, la "
            "Pagina elegida, el vivo al que transmite la app, de cual se leen comentarios y la subida en "
            "curso.";
        t.run = [fb, &rt](const json&, Done done) {
            done(ToolResult().data(json{{"configured", fb->configured()},
                                        {"loggedIn", fb->loggedIn()},
                                        {"loginPending", fb->loginPending()},
                                        {"appId", fb->appId().toStdString()},
                                        {"redirectUri", fb->redirectUri().toStdString()},
                                        {"user", qvToJson(fb->user())},
                                        {"page", qvToJson(fb->page())},
                                        {"liveId", fb->liveId().toStdString()},
                                        {"appStreaming", rt.engine().status().streaming},
                                        {"chatLiveId", fb->chatLiveId().toStdString()},
                                        {"upload", qvToJson(fb->upload())}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "facebook_setup";
        t.title = "Configurar la app de Meta";
        t.category = "facebook";
        t.description =
            "Guarda el ID y la clave secreta de la app de developers.facebook.com, y el puerto de la "
            "redireccion (por defecto 8791: la app de Meta tiene que tener http://localhost:<puerto>/ "
            "como URI de OAuth valida). clear=true la borra.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"appId", {{"type", "string"}}},
              {"appSecret", {{"type", "string"}}},
              {"port", {{"type", "integer"}, {"minimum", 1024}, {"maximum", 65535}, {"default", 8791}}},
              {"clear", {{"type", "boolean"}}}}}};
        t.run = [fb](const json& a, Done done) {
            if (a.value("clear", false)) {
                fb->clearClient();
                return done(ToolResult::ok("App de Meta borrada."));
            }
            const QString e = fb->saveClient(QString::fromStdString(a.value("appId", std::string())),
                                             QString::fromStdString(a.value("appSecret", std::string())),
                                             a.value("port", 8791));
            done(e.isEmpty() ? ToolResult::ok("App guardada (redireccion " + fb->redirectUri().toStdString() +
                                              "). Siguiente paso: facebook_login.")
                             : ToolResult::error(e.toStdString()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "facebook_login";
        t.title = "Conectar una cuenta de Facebook";
        t.category = "facebook";
        t.openWorld = true;
        t.description =
            "Abre el navegador de esta maquina en Facebook para que la PERSONA elija la cuenta, marque "
            "las Paginas y acepte los permisos. Con wait=true espera a que termine (max. 5 min); si no, "
            "facebook_status dice cuando quedo conectado. cancel=true cancela.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"wait", {{"type", "boolean"}, {"default", false}}}, {"cancel", {{"type", "boolean"}}}}}};
        t.run = [fb](const json& a, Done done) {
            if (a.value("cancel", false)) {
                fb->cancelLogin();
                return done(ToolResult::ok("Login cancelado."));
            }
            if (!fb->configured()) return done(ToolResult::error("Primero facebook_setup (app de Meta)."));
            if (!a.value("wait", false)) {
                fb->login();
                return done(ToolResult::ok(
                    "Se abrio el navegador: la persona elige la cuenta y las Paginas y acepta. "
                    "Despues, facebook_status."));
            }
            fb->client().login([fb, done](bool ok, const QJsonObject& d, const QString& e) {
                if (!ok) return done(ToolResult::error(e.toStdString()));
                fb->refresh();
                done(ToolResult().data(
                    json{{"connected", toJson(d)}, {"pages", toJson(fb->client().pages())}}));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "facebook_logout";
        t.title = "Desconectar la cuenta";
        t.category = "facebook";
        t.destructive = true;
        t.description = "Quita el permiso de la app en Facebook y borra los tokens guardados.";
        t.run = [fb](const json&, Done done) {
            fb->logout();
            done(ToolResult::ok("Cuenta de Facebook desconectada."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "facebook_pages";
        t.title = "Paginas de la cuenta";
        t.category = "facebook";
        t.description =
            "Lista las Paginas que administra la cuenta conectada (id, nombre, categoria) y cual esta "
            "elegida. `select` elige otra (vivos, videos y comentarios van a esa Pagina).";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"select", {{"type", "string"}, {"description", "id o nombre"}}}}}};
        t.run = [fb](const json& a, Done done) {
            if (!fb->loggedIn()) return done(ToolResult::error("Primero facebook_login."));
            const QJsonArray pages = fb->client().pages();
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
                    return done(ToolResult::error("No hay una Pagina '" + q.toStdString() + "'."));
                fb->selectPage(id);
            }
            done(ToolResult().data(json{{"selected", fb->pageId().toStdString()}, {"pages", toJson(pages)}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "facebook_live_list";
        t.title = "Vivos de la Pagina";
        t.category = "facebook";
        t.readOnly = true;
        t.description =
            "Vivos de la Pagina: id, titulo, estado (LIVE, UNPUBLISHED = prueba, LIVE_STOPPED, VOD, "
            "SCHEDULED_UNPUBLISHED), enlace y vistas. Con `id`, el estado de uno solo.";
        t.inputSchema = {{"type", "object"}, {"properties", {{"id", {{"type", "string"}}}}}};
        t.run = [fb, needPage](const json& a, Done done) {
            if (needPage(done)) return;
            if (a.contains("id"))
                return fb->client().liveStatus(QString::fromStdString(a["id"]), reply(done));
            fb->client().liveVideos(reply(done));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "facebook_live_create";
        t.title = "Crear un vivo y transmitir";
        t.category = "facebook";
        t.openWorld = true;
        t.description =
            "Crea un vivo en la Pagina y empieza a transmitir desde la app (servidor RTMPS y clave solo "
            "en memoria). test=true lo deja SIN PUBLICAR (solo lo ve la Pagina en Live Producer: ideal "
            "para probar; despues facebook_live_publish). Con test=false sale publico en el acto.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"title", {{"type", "string"}}},
                           {"description", {{"type", "string"}}},
                           {"test", {{"type", "boolean"}, {"default", true}}}}},
                         {"required", {"title"}}};
        t.examples = {R"({"title": "Prueba", "test": true})"};
        t.run = [fb, needPage](const json& a, Done done) {
            if (needPage(done)) return;
            QJsonObject p{{"title", QString::fromStdString(a["title"])},
                          {"description", QString::fromStdString(a.value("description", std::string()))},
                          {"status", a.value("test", true) ? "UNPUBLISHED" : "LIVE_NOW"}};
            fb->goLive(p, reply(done, a.value("test", true)
                                          ? "Transmitiendo en prueba (sin publicar). Verifica con "
                                            "facebook_live_list {\"id\"} en ~15 s."
                                          : "Transmitiendo en vivo en la Pagina."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "facebook_live_publish";
        t.title = "Publicar un vivo en prueba";
        t.category = "facebook";
        t.openWorld = true;
        t.description =
            "Pasa un vivo sin publicar (UNPUBLISHED) a publico en la Pagina, sin cortar la transmision.";
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"id", {{"type", "string"}}}}}, {"required", {"id"}}};
        t.run = [fb, needPage](const json& a, Done done) {
            if (needPage(done)) return;
            fb->client().publishLive(QString::fromStdString(a["id"]),
                                     [fb, done](bool ok, const QJsonObject& d, const QString& e) {
                                         if (ok) fb->refresh();
                                         reply(done, "Vivo publicado.")(ok, d, e);
                                     });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "facebook_live_end";
        t.title = "Terminar un vivo";
        t.category = "facebook";
        t.destructive = true;
        t.description = "Termina el vivo en Facebook (queda como video) y corta la transmision de la app si "
                        "iba a ese vivo.";
        t.inputSchema = {{"type", "object"}, {"properties", {{"id", {{"type", "string"}}}}}};
        t.run = [fb, needPage](const json& a, Done done) {
            if (needPage(done)) return;
            const QString id = a.contains("id") ? QString::fromStdString(a["id"]) : fb->liveId();
            if (id.isEmpty())
                return done(
                    ToolResult::error("Falta `id` (la app no esta transmitiendo a un vivo de Facebook)."));
            fb->endLive(id, reply(done, "Vivo terminado."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "facebook_videos";
        t.title = "Videos de la Pagina";
        t.category = "facebook";
        t.readOnly = true;
        t.description = "Videos de la Pagina (mas nuevos primero): id, titulo, descripcion, enlace, "
                        "miniatura, fecha y duracion.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"max", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}, {"default", 25}}}}}};
        t.run = [fb, needPage](const json& a, Done done) {
            if (needPage(done)) return;
            fb->client().videos(a.value("max", 25), reply(done));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "facebook_video_update";
        t.title = "Editar o borrar un video";
        t.category = "facebook";
        t.description =
            "Cambia titulo y/o descripcion de un video (o de un vivo) de la Pagina. delete=true lo borra "
            "(no se puede deshacer).";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"id", {{"type", "string"}}},
                           {"title", {{"type", "string"}}},
                           {"description", {{"type", "string"}}},
                           {"delete", {{"type", "boolean"}}}}},
                         {"required", {"id"}}};
        t.run = [fb, needPage](const json& a, Done done) {
            if (needPage(done)) return;
            const QString id = QString::fromStdString(a["id"]);
            const auto after = [fb, done](const std::string& text) {
                return [fb, done, text](bool ok, const QJsonObject& d, const QString& e) {
                    if (ok) fb->refresh();
                    reply(done, text)(ok, d, e);
                };
            };
            if (a.value("delete", false)) return fb->client().deleteObject(id, after("Borrado."));
            json changes = a;
            changes.erase("id");
            changes.erase("delete");
            if (changes.empty())
                return done(ToolResult::error("Nada para cambiar (title, description o delete)."));
            fb->client().updateVideo(id, toQt(changes), after("Video actualizado."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "facebook_video_upload";
        t.title = "Subir un video a la Pagina";
        t.category = "facebook";
        t.description =
            "Sube un archivo de video a la Pagina (por partes, reanudable). Sin `path`, la grabacion mas "
            "reciente de la app. Espera a que termine y devuelve el id.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"path", {{"type", "string"}}},
                           {"title", {{"type", "string"}}},
                           {"description", {{"type", "string"}}}}}};
        t.run = [fb, needPage](const json& a, Done done) {
            if (needPage(done)) return;
            QString path = QString::fromStdString(a.value("path", std::string()));
            if (path.isEmpty()) {
                fb->refreshRecordings();
                const json rows = modelRows(*static_cast<QAbstractItemModel*>(fb->recordings()));
                if (rows.empty())
                    return done(ToolResult::error("No hay grabaciones en " +
                                                  fb->recordingsDir().toStdString() + "."));
                path = QString::fromStdString(rows[0].value("path", ""));
            }
            json meta = a;
            meta.erase("path");
            fb->uploadRecording(path, toQt(meta),
                                reply(done, "Subido: " + QFileInfo(path).fileName().toStdString()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "facebook_chat";
        t.title = "Comentarios del vivo";
        t.category = "facebook";
        t.description =
            "Comentarios de un vivo de la Pagina: `live` elige de cual leer (lo usa tambien la fuente "
            "\"Chat en vivo\"; al transmitir se elige solo), `send` comenta como la Pagina, y devuelve "
            "los ultimos `limit` comentarios.";
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
            const QJsonArray& all = fb->chatMessages();
            const int n = std::min<int>(all.size(), a.value("limit", 30));
            QJsonArray last;
            for (int i = all.size() - n; i < all.size(); ++i)
                last.append(all[i]);
            done(ToolResult().data(
                json{{"live", fb->chatLiveId().toStdString()},
                     {"messages", toJson(last)},
                     {"note", "Los comentarios se leen cada ~5 s; si recien elegiste el vivo, volve "
                              "a llamar en unos segundos."}}));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
