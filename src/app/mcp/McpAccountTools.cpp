// ============================================================================
// src/app/mcp/McpAccountTools.cpp — Conectar YouTube, Twitch y Facebook
//
// El asistente guia a la persona por la configuracion de cada plataforma:
//   accounts_status              que falta en cada una (y el paso siguiente)
//   account_setup_guide          pasos con enlaces y los valores exactos a pegar
//   account_open                 abre en el navegador la pagina de un paso
//   account_credentials_request  formulario SEGURO en la barra del operador:
//                                la persona pega el ID/secreto en la app y el
//                                asistente nunca los ve
//   account_check                prueba real de la conexion con diagnostico
// Crear las apps en Google/Twitch/Meta lo tiene que hacer la persona (con su
// cuenta y aceptando sus terminos); todo lo demas se automatiza.
// ============================================================================

#include <QJsonArray>
#include <QUrlQuery>

#include <algorithm>
#include <memory>

#include "net/AccountGuides.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/FacebookViewModel.hpp"
#include "ui/OperatorMessages.hpp"
#include "ui/StudioViewModel.hpp"
#include "ui/TwitchViewModel.hpp"
#include "ui/YouTubeViewModel.hpp"

namespace ba::app::mcp {

namespace {

const json kPlatformArg{{"type", "string"}, {"enum", {"youtube", "twitch", "facebook"}}};

} // namespace

void McpService::registerAccountTools() {
    core_.addCategory(
        "cuentas", "Cuentas (YouTube, Twitch, Facebook)",
        "Conectar las cuentas de las plataformas: qué falta, pasos guiados con enlaces, formulario "
        "seguro para las credenciales y prueba de la conexión. Empezar por accounts_status.");
    auto* yt = static_cast<YouTubeViewModel*>(vm_.youtube());
    auto* tw = static_cast<TwitchViewModel*>(vm_.twitch());
    auto* fb = static_cast<FacebookViewModel*>(vm_.facebook());
    auto* msgs = static_cast<OperatorMessages*>(vm_.messages());

    // Estado + paso siguiente de cada plataforma.
    const auto state = [yt, tw, fb](const std::string& p) -> json {
        if (p == "youtube") {
            const QJsonObject ch = yt->client().account();
            json s{{"configured", yt->configured()},
                   {"loggedIn", yt->loggedIn()},
                   {"loginPending", yt->loginPending()},
                   {"account", ch.value("title").toString().toStdString()}};
            s["next"] = !yt->configured()    ? "Falta la credencial de Google: account_setup_guide + "
                                               "account_credentials_request (o youtube_setup "
                                               "{\"fromDownloads\": true} si ya bajo el JSON)."
                        : yt->loginPending() ? "Login en curso: la persona tiene que elegir la cuenta en el "
                                               "navegador."
                        : !yt->loggedIn()    ? "Falta conectar la cuenta: youtube_login."
                                          : "Listo. account_check {\"platform\": \"youtube\"} lo verifica.";
            return s;
        }
        if (p == "twitch") {
            json s{{"configured", tw->configured()},
                   {"loggedIn", tw->loggedIn()},
                   {"loginPending", tw->loginPending()},
                   {"account", tw->client().user().value("display_name").toString().toStdString()}};
            if (tw->loginPending()) {
                s["userCode"] = tw->userCode().toStdString();
                s["verificationUri"] = tw->verificationUri().toStdString();
            }
            s["next"] = !tw->configured()
                            ? "Falta el ID de cliente de una app de Twitch: account_setup_guide + "
                              "account_credentials_request."
                        : tw->loginPending() ? "Login en curso: la persona confirma el codigo userCode en "
                                               "verificationUri (twitch.tv/activate)."
                        : !tw->loggedIn()    ? "Falta conectar la cuenta: twitch_login."
                                             : "Listo. account_check {\"platform\": \"twitch\"} lo verifica.";
            return s;
        }
        json s{{"configured", fb->configured()},
               {"loggedIn", fb->loggedIn()},
               {"loginPending", fb->loginPending()},
               {"account", fb->client().user().value("name").toString().toStdString()},
               {"page", fb->client().page().value("name").toString().toStdString()},
               {"redirectUri", fb->redirectUri().toStdString()}};
        s["next"] = !fb->configured()    ? "Falta la app de Meta (ID + clave secreta): account_setup_guide + "
                                           "account_credentials_request."
                    : fb->loginPending() ? "Login en curso: la persona autoriza en el navegador."
                    : !fb->loggedIn()    ? "Falta conectar la cuenta: facebook_login."
                    : fb->client().pageId().isEmpty()
                        ? "Falta elegir la pagina donde transmitir: facebook_pages."
                        : "Listo. account_check {\"platform\": \"facebook\"} lo verifica.";
        return s;
    };

    {
        Tool t;
        t.name = "accounts_status";
        t.title = "Estado de las cuentas";
        t.category = "cuentas";
        t.readOnly = true;
        t.description =
            "Para YouTube, Twitch y Facebook: si hay credencial, si hay cuenta conectada (cual) y el "
            "PASO SIGUIENTE (`next`). Nunca devuelve secretos.";
        t.run = [state](const json&, Done done) {
            done(ToolResult().data(json{{"youtube", state("youtube")},
                                        {"twitch", state("twitch")},
                                        {"facebook", state("facebook")}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "account_setup_guide";
        t.title = "Pasos para conectar una plataforma";
        t.category = "cuentas";
        t.readOnly = true;
        t.description =
            "Los pasos (con enlaces) para crear la app/credencial en Google Cloud, Twitch o Meta, los "
            "VALORES "
            "EXACTOS a pegar (`paste`: URI de redireccion, tipo de cliente...) y que pide el formulario. "
            "Guialo "
            "de a un paso: account_open abre la pagina del paso; al final, account_credentials_request.";
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"platform", kPlatformArg}}}, {"required", {"platform"}}};
        t.run = [fb](const json& a, Done done) {
            const json g = accounts::guide(a.value("platform", ""), fb->redirectUri().toStdString());
            if (g.is_null()) return done(ToolResult::error("platform: youtube, twitch o facebook."));
            done(ToolResult().data(g));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "account_open";
        t.title = "Abrir la pagina de un paso";
        t.category = "cuentas";
        t.openWorld = true;
        t.description =
            "Abre en el navegador de esta maquina el enlace del paso `step` (1, 2...) de la guia de "
            "la plataforma. Solo abre enlaces de la guia.";
        t.inputSchema = {
            {"type", "object"},
            {"properties", {{"platform", kPlatformArg}, {"step", {{"type", "integer"}, {"minimum", 1}}}}},
            {"required", {"platform", "step"}}};
        t.run = [yt, fb](const json& a, Done done) {
            const json g = accounts::guide(a.value("platform", ""), fb->redirectUri().toStdString());
            if (g.is_null()) return done(ToolResult::error("platform: youtube, twitch o facebook."));
            const int n = a.value("step", 1);
            const json& steps = g["steps"];
            if (n < 1 || n > static_cast<int>(steps.size()))
                return done(ToolResult::error("La guia tiene " + std::to_string(steps.size()) + " pasos."));
            const json& s = steps[static_cast<size_t>(n - 1)];
            if (s["links"].empty()) return done(ToolResult::error("Ese paso no tiene enlace."));
            const std::string url = s["links"][0].value("u", "");
            yt->openUrl(QString::fromStdString(url));
            done(ToolResult()
                     .text("Abri " + url + " (" + s.value("t", "") +
                           "). Decile a la persona que hacer: " + s.value("d", ""))
                     .data(json{{"opened", url}, {"step", s}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "account_credentials_request";
        t.title = "Pedir la credencial (formulario seguro)";
        t.category = "cuentas";
        t.description =
            "Muestra en la barra del operador un formulario para pegar el ID y el secreto de la plataforma "
            "(YouTube: tambien importar el JSON de Google, solo o desde Descargas). Se guarda cifrado "
            "directo "
            "desde la app: vos NUNCA ves el secreto. NO le pidas a la persona que pegue secretos en el chat. "
            "Con `timeoutSec` > 0 espera el resultado (saved / dismissed); 0 = no espera (te llega a tu "
            "chat).";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"platform", kPlatformArg},
              {"text", {{"type", "string"}, {"description", "Explicacion corta para la persona"}}},
              {"timeoutSec", {{"type", "integer"}, {"default", 600}, {"maximum", 1800}}}}},
            {"required", {"platform"}}};
        t.run = [msgs, fb, state](const json& a, Done done) {
            const std::string p = accounts::normalize(a.value("platform", ""));
            const json g = accounts::guide(p, fb->redirectUri().toStdString());
            if (g.is_null()) return done(ToolResult::error("platform: youtube, twitch o facebook."));
            json spec{
                {"kind", "credential"},
                {"platform", p},
                {"priority", "info"},
                {"title", "Conectar " + g.value("title", p)},
                {"text", a.value("text", std::string("Pegá acá los datos de la app que creaste. Se guardan "
                                                     "cifrados en este equipo; el asistente no los ve."))},
                {"fields", g["credential"]["fields"]},
                {"importFile", g["credential"].value("importFile", false)}};
            const int id = msgs->post(spec);
            const int timeout = a.value("timeoutSec", 600);
            if (timeout <= 0)
                return done(ToolResult::ok("Formulario en la barra (id " + std::to_string(id) +
                                           "): cuando lo complete te llega a tu chat."));
            msgs->waitAnswer(id, timeout * 1000, [done, id, p, state](json item) {
                if (item.is_null())
                    return done(ToolResult::ok("Todavia no lo completo (el formulario " + std::to_string(id) +
                                               " sigue en la barra)."));
                if (item.value("status", "") != "saved")
                    return done(ToolResult::ok("La persona cerro el formulario sin guardar."));
                done(ToolResult()
                         .text("Credencial de " + p + " guardada. Siguiente paso: " + p + "_login.")
                         .data(state(p)));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "account_check";
        t.title = "Probar la conexion";
        t.category = "cuentas";
        t.readOnly = true;
        t.openWorld = true;
        t.description =
            "Prueba la conexion de verdad contra la API y explica lo que falte: YouTube (canal, vivo "
            "habilitado), Twitch (canal, permiso de la clave de transmision), Facebook (permisos otorgados, "
            "paginas, pagina elegida).";
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"platform", kPlatformArg}}}, {"required", {"platform"}}};
        t.run = [yt, tw, fb, state](const json& a, Done done) {
            const std::string p = accounts::normalize(a.value("platform", ""));
            if (p.empty()) return done(ToolResult::error("platform: youtube, twitch o facebook."));
            const json st = state(p);
            if (!st.value("configured", false) || !st.value("loggedIn", false))
                return done(ToolResult().data(json{{"ok", false}, {"problem", st.value("next", "")}}));
            if (p == "youtube") {
                yt->client().loadChannel([yt, done](bool ok, const QJsonObject& ch, const QString& e) {
                    if (!ok)
                        return done(ToolResult().data(json{{"ok", false}, {"problem", e.toStdString()}}));
                    yt->client().listBroadcasts([done, ch](bool ok2, const QJsonObject&, const QString& e2) {
                        json r{{"ok", ok2},
                               {"channel", ch.value("title").toString().toStdString()},
                               {"liveEnabled", ok2}};
                        if (!ok2) r["problem"] = e2.toStdString();
                        done(ToolResult().data(r));
                    });
                });
                return;
            }
            if (p == "twitch") {
                tw->client().channel([tw, done](bool ok, const QJsonObject& ch, const QString& e) {
                    if (!ok)
                        return done(ToolResult().data(json{{"ok", false}, {"problem", e.toStdString()}}));
                    QUrlQuery q;
                    q.addQueryItem("broadcaster_id", tw->client().user().value("id").toString());
                    tw->client().api(
                        "GET", "/streams/key", q, {},
                        [done, ch](bool ok2, const QJsonObject&, const QString& e2) {
                            json r{{"ok", ok2}, {"channel", toJson(ch)}, {"streamKeyAccess", ok2}};
                            if (!ok2)
                                r["problem"] =
                                    "No se pudo leer la clave de transmision: " + e2.toStdString() +
                                    " (desconecta y volve a conectar con twitch_login).";
                            done(ToolResult().data(r));
                        });
                });
                return;
            }
            fb->client().graph(
                "GET", "/me/permissions", {}, [fb, done](bool ok, const QJsonObject& d, const QString& e) {
                    if (!ok)
                        return done(ToolResult().data(json{{"ok", false}, {"problem", e.toStdString()}}));
                    json granted = json::array();
                    json missing = json::array();
                    for (const auto& v : d.value("data").toArray()) {
                        const QJsonObject o = v.toObject();
                        (o.value("status").toString() == "granted" ? granted : missing)
                            .push_back(o.value("permission").toString().toStdString());
                    }
                    for (const char* need : {"pages_show_list", "pages_manage_posts", "publish_video"}) {
                        bool have = false;
                        for (const auto& g : granted)
                            have = have || g == need;
                        if (!have && std::find(missing.begin(), missing.end(), need) == missing.end())
                            missing.push_back(need);
                    }
                    const int pages = static_cast<int>(fb->client().pages().size());
                    const bool pageChosen = !fb->client().pageId().isEmpty();
                    json r{{"ok", missing.empty() && pages > 0 && pageChosen},
                           {"granted", granted},
                           {"missing", missing},
                           {"pages", pages},
                           {"page", fb->client().page().value("name").toString().toStdString()}};
                    if (!missing.empty())
                        r["problem"] = "Faltan permisos: al conectar, la persona tiene que aceptar todos (y "
                                       "elegir la pagina). facebook_logout + facebook_login.";
                    else if (pages == 0)
                        r["problem"] =
                            "La cuenta no administra ninguna pagina (o no la eligio al autorizar).";
                    else if (!pageChosen)
                        r["problem"] = "Falta elegir la pagina: facebook_pages.";
                    done(ToolResult().data(r));
                });
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
