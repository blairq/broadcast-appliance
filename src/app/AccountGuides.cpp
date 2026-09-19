// ============================================================================
// src/app/AccountGuides.cpp — ver AccountGuides.hpp
// ============================================================================

#include "AccountGuides.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace ba::app::accounts {

using json = nlohmann::json;

namespace {

json step(const char* n, const char* t, const std::string& d, json links) {
    json s{{"n", n}, {"t", t}, {"d", d}, {"links", links}};
    // Las pestañas de Twitch/Facebook muestran un solo enlace por paso.
    if (!links.empty()) {
        s["l"] = links[0]["l"];
        s["u"] = links[0]["u"];
    }
    return s;
}

json link(const char* l, const char* u) { return json{{"l", l}, {"u", u}}; }

} // namespace

std::string normalize(const std::string& platform) {
    std::string p;
    for (char c : platform)
        p += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (p == "youtube" || p == "yt" || p == "google") return "youtube";
    if (p == "twitch") return "twitch";
    if (p == "facebook" || p == "fb" || p == "meta") return "facebook";
    return {};
}

json guide(const std::string& platform, const std::string& fbRedirect) {
    const std::string p = normalize(platform);
    if (p == "youtube") {
        return json{
            {"platform", p},
            {"title", "YouTube (Google Cloud)"},
            {"minutes", 5},
            {"intro",
             "Para que la app (y el asistente por MCP) pueda crear transmisiones en vivo y administrar tus "
             "videos, Google pide que cada instalación tenga su propia credencial. Se hace una sola vez, en "
             "unos 5 minutos:"},
            {"steps",
             {step("1", "Creá un proyecto en Google Cloud",
                   "Cualquier nombre (por ejemplo «Broadcast Appliance»). Si ya tenés uno, podés usarlo.",
                   {link("Crear proyecto", "https://console.cloud.google.com/projectcreate")}),
              step("2", "Habilitá la YouTube Data API v3",
                   "Con el proyecto elegido arriba, tocá «Habilitar».",
                   {link("YouTube Data API v3",
                         "https://console.cloud.google.com/apis/library/youtube.googleapis.com")}),
              step("3", "Configurá la pantalla de consentimiento",
                   "Google Auth Platform → «Comenzar»: nombre de la app, tu mail de soporte, público "
                   "«Externo» y tu mail de contacto.",
                   {link("Pantalla de consentimiento", "https://console.cloud.google.com/auth/branding")}),
              step("4", "Agregate como usuario de prueba",
                   "En «Público», agregá el mail de la cuenta de YouTube que vas a usar. En modo prueba el "
                   "acceso vence cada 7 días; si tocás «Publicar app» no vence (al conectar vas a ver un "
                   "aviso "
                   "de «app no verificada»: es normal, la app sos vos).",
                   {link("Público / usuarios de prueba", "https://console.cloud.google.com/auth/audience")}),
              step("5", "Creá el cliente OAuth",
                   "«Clientes» → «Crear cliente» → tipo de aplicación «App de escritorio» → «Crear» → "
                   "«Descargar JSON».",
                   {link("Clientes OAuth", "https://console.cloud.google.com/auth/clients")})}},
            {"paste",
             {{"tipoDeCliente", "App de escritorio"},
              {"api", "YouTube Data API v3"},
              {"publico", "Externo (con tu mail como usuario de prueba)"}}},
            {"credential",
             {{"fields",
               {{{"key", "clientId"},
                 {"label", "ID de cliente"},
                 {"secret", false},
                 {"placeholder", "…apps.googleusercontent.com"}},
                {{"key", "clientSecret"},
                 {"label", "Secreto del cliente"},
                 {"secret", true},
                 {"placeholder", "GOCSPX-…"}}}},
              {"importFile", true}}},
            {"notes",
             {"El JSON que descarga Google (client_secret_….json) trae el ID y el secreto: la app lo puede "
              "importar sola desde Descargas.",
              "Después de guardar la credencial: youtube_login abre el navegador para elegir la cuenta.",
              "Para transmitir en vivo el canal tiene que tener habilitado el vivo en youtube.com/features "
              "(la primera vez tarda hasta 24 h)."}}};
    }
    if (p == "twitch") {
        return json{
            {"platform", p},
            {"title", "Twitch (dev.twitch.tv)"},
            {"minutes", 3},
            {"intro",
             "Twitch pide registrar una app (gratis) para usar su API. Se hace una sola vez, en unos 3 "
             "minutos:"},
            {"steps",
             {step("1", "Activá la verificación en dos pasos", "Twitch la exige para registrar apps.",
                   {link("Seguridad de la cuenta", "https://www.twitch.tv/settings/security")}),
              step("2", "Registrá la app",
                   "Nombre: el que quieras (por ejemplo «Broadcast Appliance de <tu canal>»). URL de "
                   "redireccionamiento de OAuth: http://localhost (no se usa, pero el campo es obligatorio). "
                   "Categoría: «Broadcaster Suite». Tipo de cliente: «Público». Crear.",
                   {link("Registrar app en Twitch", "https://dev.twitch.tv/console/apps/create")}),
              step(
                  "3", "Copiá el ID de cliente",
                  "En la lista de apps → «Administrar» → «ID de cliente». Si elegiste «Confidencial», generá "
                  "también un secreto.",
                  {link("Mis apps de Twitch", "https://dev.twitch.tv/console/apps")})}},
            {"paste",
             {{"urlDeRedireccion", "http://localhost"},
              {"categoria", "Broadcaster Suite"},
              {"tipoDeCliente", "Público"}}},
            {"credential",
             {{"fields",
               {{{"key", "clientId"}, {"label", "ID de cliente"}, {"secret", false}, {"placeholder", ""}},
                {{"key", "clientSecret"},
                 {"label", "Secreto (solo si la app es «Confidencial»)"},
                 {"secret", true},
                 {"placeholder", "vacío si es «Público»"}}}},
              {"importFile", false}}},
            {"notes",
             {"Con un cliente «Público» alcanza el ID (no hay secreto).",
              "Después de guardar: twitch_login muestra un código que la persona confirma en "
              "twitch.tv/activate."}}};
    }
    if (p == "facebook") {
        const std::string redirect = fbRedirect.empty() ? "http://localhost:8791/" : fbRedirect;
        return json{
            {"platform", p},
            {"title", "Facebook (Meta for Developers)"},
            {"minutes", 5},
            {"intro",
             "La API de Facebook permite transmitir y administrar videos en PÁGINAS (no en el perfil "
             "personal). Para usarla, creá una app de Meta propia (queda en modo desarrollo: solo vos la "
             "usás) — unos 5 minutos:"},
            {"steps",
             {step("1", "Creá la app de Meta",
                   "«Crear app» → caso de uso «Otro» → tipo «Empresa» → un nombre (por ejemplo «Broadcast "
                   "Appliance») → Crear. Si te pide cuenta de desarrollador, registrate con tu cuenta de "
                   "Facebook.",
                   {link("Mis apps de Meta", "https://developers.facebook.com/apps/")}),
              step("2", "Agregá «Inicio de sesión con Facebook»",
                   "En el panel de la app: «Agregar producto» → «Inicio de sesión con Facebook» → "
                   "Configuración → en «URI de redireccionamiento de OAuth válidos» agregá exactamente: " +
                       redirect + " y guardá.",
                   {link("Documentación del inicio de sesión",
                         "https://developers.facebook.com/docs/facebook-login/")}),
              step("3", "Copiá el ID y la clave secreta",
                   "Configuración de la app → «Básica»: «Identificador de la app» y «Clave secreta de la "
                   "app» (tocá «Mostrar»).",
                   {link("Panel de apps", "https://developers.facebook.com/apps/")})}},
            {"paste", {{"uriDeRedireccion", redirect}, {"tipoDeApp", "Empresa"}}},
            {"credential",
             {{"fields",
               {{{"key", "appId"},
                 {"label", "Identificador de la app"},
                 {"secret", false},
                 {"placeholder", ""}},
                {{"key", "appSecret"},
                 {"label", "Clave secreta de la app"},
                 {"secret", true},
                 {"placeholder", ""}}}},
              {"importFile", false}}},
            {"notes",
             {"Se transmite a una PÁGINA de la que seas administrador: después de conectar, facebook_pages "
              "para elegirla.",
              "En modo desarrollo la app solo la pueden usar sus administradores: no hace falta revisión "
              "de Meta.",
              "Después de guardar: facebook_login abre el navegador para autorizar."}}};
    }
    return {};
}

QString findGoogleClientJson(QString* why) {
    QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::DownloadLocation);
    for (const char* name : {"Downloads", "Descargas"})
        dirs << QDir::home().filePath(QString::fromLatin1(name));
    dirs.removeDuplicates();
    QFileInfo best;
    for (const QString& d : dirs) {
        const QFileInfoList files = QDir(d).entryInfoList({"client_secret*.json"}, QDir::Files, QDir::Time);
        for (const QFileInfo& f : files) {
            if (f.size() > 64 * 1024) continue;
            QFile in(f.absoluteFilePath());
            if (!in.open(QIODevice::ReadOnly)) continue;
            const QJsonObject o = QJsonDocument::fromJson(in.readAll()).object();
            // Solo un cliente "App de escritorio" sirve (el que tiene "installed").
            if (!o.value("installed").toObject().contains("client_id")) continue;
            if (!best.exists() || f.lastModified() > best.lastModified()) best = f;
        }
    }
    if (!best.exists()) {
        if (why != nullptr)
            *why = "No hay ningun client_secret_….json de tipo «App de escritorio» en Descargas (" +
                   dirs.join(", ") + ").";
        return {};
    }
    return best.absoluteFilePath();
}

} // namespace ba::app::accounts
