// ============================================================================
// src/app/mcp/McpPlataformas.cpp — ver McpPlataformas.hpp
//
// Cada herramienta se registra con una llamada a su ayudante (estado, setup, login,
// logout, videos) con el nombre, el título, la descripción y el esquema LITERALES: así
// los lee tools/docs/mcp_codigo.py (docs/mcp.json) sin compilar.
// ============================================================================

#include "mcp/McpPlataformas.hpp"

#include <QFileInfo>
#include <QJsonObject>

#include <memory>
#include <optional>

#include "i18n/I18nTexto.hpp"
#include "mcp/McpToolsCommon.hpp"

namespace ba::app::mcp {

namespace {

using Falta = PlataformaViewModel::Falta;

/// Lo que se le dice al asistente cuando falta algo, por plataforma (en el original: se
/// traduce al usarlo).
struct Textos {
    const char* plataforma;
    // Para usarla (faltaParaUsar).
    const char* sinCredencial;
    const char* sinCuenta;
    // El paso siguiente (accounts_status).
    const char* conectarApp;
    const char* loginEnCurso;
    const char* conectarCuenta;
    const char* listo;
};

const Textos kTextos[] = {
    {"youtube",
     QT_TRANSLATE_NOOP("mcp", "YouTube no esta configurado: falta el cliente OAuth de Google Cloud. Usa "
                              "youtube_setup (o la pestaña YouTube de la app, que tiene los pasos con enlaces); "
                              "help {\"topic\": \"youtube\"} los explica."),
     QT_TRANSLATE_NOOP("mcp", "No hay una cuenta de YouTube conectada: llama youtube_login (abre el navegador "
                              "para que la persona elija la cuenta)."),
     QT_TRANSLATE_NOOP("mcp", "Falta la credencial de Google: account_setup_guide + "
                              "account_credentials_request (o youtube_setup "
                              "{\"fromDownloads\": true} si ya bajo el JSON)."),
     QT_TRANSLATE_NOOP("mcp", "Login en curso: la persona tiene que elegir la cuenta en el "
                              "navegador."),
     QT_TRANSLATE_NOOP("mcp", "Falta conectar la cuenta: youtube_login."),
     QT_TRANSLATE_NOOP("mcp", "Listo. account_check {\"platform\": \"youtube\"} lo verifica.")},
    {"twitch",
     QT_TRANSLATE_NOOP("mcp", "Twitch no esta configurado: falta el ID de cliente de una app de dev.twitch.tv. "
                              "Usa twitch_setup (o la pestaña Twitch de la app, con los pasos y enlaces); "
                              "help {\"topic\": \"twitch\"} los explica."),
     QT_TRANSLATE_NOOP("mcp", "No hay una cuenta de Twitch conectada: llama twitch_login (muestra un codigo que "
                              "la persona confirma en twitch.tv/activate)."),
     QT_TRANSLATE_NOOP("mcp", "Falta el ID de cliente de una app de Twitch: account_setup_guide + "
                              "account_credentials_request."),
     QT_TRANSLATE_NOOP("mcp", "Login en curso: la persona confirma el codigo userCode en "
                              "verificationUri (twitch.tv/activate)."),
     QT_TRANSLATE_NOOP("mcp", "Falta conectar la cuenta: twitch_login."),
     QT_TRANSLATE_NOOP("mcp", "Listo. account_check {\"platform\": \"twitch\"} lo verifica.")},
    {"facebook",
     QT_TRANSLATE_NOOP("mcp", "Facebook no esta configurado: falta la app de Meta (ID y clave secreta). Usa "
                              "facebook_setup (o la pestaña Facebook de la app, con los pasos y enlaces); "
                              "help {\"topic\": \"facebook\"} los explica."),
     QT_TRANSLATE_NOOP("mcp", "No hay una cuenta de Facebook conectada: llama facebook_login (abre el navegador "
                              "para que la persona elija la cuenta y las Paginas)."),
     QT_TRANSLATE_NOOP("mcp", "Falta la app de Meta (ID + clave secreta): account_setup_guide + "
                              "account_credentials_request."),
     QT_TRANSLATE_NOOP("mcp", "Login en curso: la persona autoriza en el navegador."),
     QT_TRANSLATE_NOOP("mcp", "Falta conectar la cuenta: facebook_login."),
     QT_TRANSLATE_NOOP("mcp", "Listo. account_check {\"platform\": \"facebook\"} lo verifica.")},
};

const Textos& textos(const PlataformaViewModel& p) {
    for (const Textos& t : kTextos)
        if (p.plataforma() == QLatin1String(t.plataforma)) return t;
    return kTextos[0];
}

std::string mcp(const char* original) { return ba::i18n::traducir("mcp", original); }

// Las Páginas de Facebook: la única plataforma que la pide (cuenta + Página).
const char* kSinPagina = QT_TRANSLATE_NOOP("mcp", "No hay una Pagina elegida: facebook_pages {\"select\": \"<id>\"}. "
                                                  "Si la lista esta vacia, la cuenta no administra Paginas o no se "
                                                  "marcaron al conectar.");
const char* kElegirPagina = QT_TRANSLATE_NOOP("mcp", "Falta elegir la pagina donde transmitir: facebook_pages.");

/// Lo que no vino en los argumentos vale lo que dice el esquema (`default`).
json conDefectos(json a, const json& esquema) {
    const json props = esquema.value("properties", json::object());
    for (const auto& [clave, prop] : props.items())
        if (!a.contains(clave) && prop.contains("default")) a[clave] = prop["default"];
    return a;
}

} // namespace

PlataformaViewModel::Done responder(Done done, const PlataformaViewModel& p, const std::string& okText) {
    return [done = std::move(done), &p, okText](bool ok, const QJsonObject& d, const QString& e) {
        if (!ok) return done(ToolResult::error(p.conNombre(e).toStdString()));
        ToolResult r;
        if (!okText.empty()) r.text(okText);
        if (!d.isEmpty()) r.data(toJson(d));
        done(r);
    };
}

bool faltaParaUsar(const PlataformaViewModel& p, const Done& done) {
    switch (p.falta()) {
    case Falta::Nada:
        return false;
    case Falta::Credencial:
        done(ToolResult::error(mcp(textos(p).sinCredencial)));
        break;
    case Falta::Cuenta:
        done(ToolResult::error(mcp(textos(p).sinCuenta)));
        break;
    case Falta::Pagina:
        done(ToolResult::error(mcp(kSinPagina)));
        break;
    }
    return true;
}

json estadoDeCuenta(const PlataformaViewModel& p) {
    json s = toJson(p.estadoCuenta());
    const Textos& t = textos(p);
    const Falta f = p.falta();
    s["next"] = f == Falta::Credencial ? mcp(t.conectarApp)
                : p.loginPending()     ? mcp(t.loginEnCurso)
                : f == Falta::Cuenta   ? mcp(t.conectarCuenta)
                : f == Falta::Pagina   ? mcp(kElegirPagina)
                                       : mcp(t.listo);
    return s;
}

void registrarPlataformas(McpCore& core, const Plataformas& pl) {
    // Cada ayudante recibe la categoría (= el id de la plataforma) literal: así la lee el inventario.
    const auto de = [&pl](const char* id) -> PlataformaViewModel& {
        const std::string_view v(id);
        return v == "youtube" ? pl.youtube : v == "twitch" ? pl.twitch : pl.facebook;
    };
    // --- *_status: todo lo de la plataforma (estado()), sin secretos ---------------------
    const auto estado = [&core, &de](const char* categoria, const char* nombre, const char* titulo,
                                const char* descripcion) {
        PlataformaViewModel& p = de(categoria);
        Tool t;
        t.name = nombre;
        t.title = ba::i18n::traducir("mcp", titulo);
        t.category = categoria;
        t.clase = Clase::Lectura;
        t.description = descripcion;
        t.run = [&p](const json&, Done done) { done(ToolResult().data(toJson(p.estado()))); };
        core.addTool(std::move(t));
    };
    estado("youtube", "youtube_status", QT_TRANSLATE_NOOP("mcp", "Estado de YouTube"),
           QT_TRANSLATE_NOOP("mcp", "Si la API esta configurada, que canal esta conectado, si hay un login en curso, a "
                                    "que evento se esta transmitiendo desde la app y de cual se lee el chat."));
    estado("twitch", "twitch_status", QT_TRANSLATE_NOOP("mcp", "Estado de Twitch"),
           QT_TRANSLATE_NOOP("mcp", "Si la app de Twitch esta configurada, que cuenta esta conectada, si hay un login "
                                    "en curso (con su codigo), datos del canal, si esta en vivo y si la app transmite "
                                    "a Twitch."));
    estado("facebook", "facebook_status", QT_TRANSLATE_NOOP("mcp", "Estado de Facebook"),
           QT_TRANSLATE_NOOP("mcp", "Si la app de Meta esta configurada (y su URI de redireccion), que cuenta esta "
                                    "conectada, la Pagina elegida, el vivo al que transmite la app, de cual se leen "
                                    "comentarios y la subida en curso."));

    // --- *_setup: por la misma puerta que el formulario seguro ---------------------------
    // `alGuardar` arma la respuesta; `antes`, lo que una sola hace distinto (YouTube: importar
    // de Descargas): si contesta, devuelve true.
    using AlGuardar = std::function<ToolResult(const PlataformaViewModel&)>;
    using Antes = std::function<bool(const json&, const Done&)>;
    const auto setup = [&core, &de, guardar = pl.guardarCredencial](const char* categoria, const char* nombre,
                                                                const char* titulo, const char* descripcion,
                                                                const json& esquema, const char* borrada,
                                                                AlGuardar alGuardar, Antes antes) {
        PlataformaViewModel& p = de(categoria);
        Tool t;
        t.name = nombre;
        t.destructivaSi = "clear"; // clear=true borra
        t.title = ba::i18n::traducir("mcp", titulo);
        t.category = categoria;
        t.clase = Clase::Escritura;
        t.description = descripcion;
        t.inputSchema = esquema;
        t.run = [&p, guardar, esquema, borrada, alGuardar, antes](const json& a, Done done) {
            if (a.value("clear", false)) {
                p.clearClient();
                return done(ToolResult::ok(ba::i18n::traducir("mcp", borrada)));
            }
            if (antes && antes(a, done)) return;
            json campos = conDefectos(a, esquema);
            campos.erase("clear");
            const QString e = guardar(p.plataforma(), toQt(campos).toVariantMap());
            done(e.isEmpty() ? alGuardar(p) : ToolResult::error(e.toStdString()));
        };
        core.addTool(std::move(t));
    };
    const auto siguiente = [](const char* texto) {
        return [texto](const PlataformaViewModel&) { return ToolResult::ok(ba::i18n::traducir("mcp", texto)); };
    };
    PlataformaViewModel& yt = pl.youtube;
    setup(
        "youtube", "youtube_setup", QT_TRANSLATE_NOOP("mcp", "Configurar la credencial de Google"),
        QT_TRANSLATE_NOOP("mcp", "Guarda el cliente OAuth (tipo \"App de escritorio\") de Google Cloud. Lo mejor: "
                                 "`fromDownloads: true` importa solo el client_secret_….json mas nuevo de la carpeta de "
                                 "Descargas (el secreto no pasa por el chat). Si no, account_credentials_request "
                                 "(formulario seguro). `clientJson` / `clientId` + `clientSecret` quedan para casos "
                                 "especiales. clear=true lo borra."),
        json{{"type", "object"},
             {"properties",
              {{"fromDownloads", {{"type", "boolean"}}},
               {"clientJson", {{"type", "string"}}},
               {"clientId", {{"type", "string"}}},
               {"clientSecret", {{"type", "string"}}},
               {"clear", {{"type", "boolean"}}}}}},
        QT_TRANSLATE_NOOP("mcp", "Credencial borrada."),
        siguiente(QT_TRANSLATE_NOOP("mcp", "Credencial guardada. Siguiente paso: youtube_login.")),
        [&yt, guardar = pl.guardarCredencial, leer = pl.leerDeDescargas](const json& a, const Done& done) {
            if (!a.value("fromDownloads", false)) return false;
            // Buscar en Descargas y leer el archivo, fuera de la interfaz; guardarlo, en ella.
            leer([&yt, guardar, done](const QString& ruta, const QString& contenido, const QString& error) {
                if (!error.isEmpty()) return done(ToolResult::error(error.toStdString()));
                const QString e = guardar(yt.plataforma(), {{"clientJson", contenido}});
                if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
                done(ToolResult()
                         .text(ba::i18n::llenar(
                             ba::i18n::traducir("mcp", "Credencial importada de %1. Siguiente paso: youtube_login."),
                             {QFileInfo(ruta).fileName().toStdString()}))
                         .data(json{{"clientId", yt.estado().value("clientId").toString().toStdString()}}));
            });
            return true;
        });
    setup("twitch", "twitch_setup", QT_TRANSLATE_NOOP("mcp", "Configurar la app de Twitch"),
          QT_TRANSLATE_NOOP("mcp", "Guarda el ID de cliente de la app registrada en dev.twitch.tv (tipo Publico; "
                                   "`clientSecret` solo si es Confidencial). clear=true la borra."),
          json{{"type", "object"},
               {"properties",
                {{"clientId", {{"type", "string"}}},
                 {"clientSecret", {{"type", "string"}}},
                 {"clear", {{"type", "boolean"}}}}}},
          QT_TRANSLATE_NOOP("mcp", "App de Twitch borrada."),
          siguiente(QT_TRANSLATE_NOOP("mcp", "App guardada. Siguiente paso: twitch_login.")), Antes{});
    setup("facebook", "facebook_setup", QT_TRANSLATE_NOOP("mcp", "Configurar la app de Meta"),
          QT_TRANSLATE_NOOP("mcp", "Guarda el ID y la clave secreta de la app de developers.facebook.com, y el puerto "
                                   "de la redireccion (por defecto 8791: la app de Meta tiene que tener "
                                   "http://localhost:<puerto>/ como URI de OAuth valida). clear=true la borra."),
          json{{"type", "object"},
               {"properties",
                {{"appId", {{"type", "string"}}},
                 {"appSecret", {{"type", "string"}}},
                 {"port", {{"type", "integer"}, {"minimum", 1024}, {"maximum", 65535}, {"default", 8791}}},
                 {"clear", {{"type", "boolean"}}}}}},
          QT_TRANSLATE_NOOP("mcp", "App de Meta borrada."),
          [](const PlataformaViewModel& p) {
              return ToolResult::ok(ba::i18n::llenar(
                  ba::i18n::traducir("mcp", "App guardada (redireccion %1). Siguiente paso: facebook_login."),
                  {p.estadoCuenta().value("redirectUri").toString().toStdString()}));
          },
          Antes{});

    // --- *_login: en el navegador o con un código ------------------------------------------
    // `abierto`: lo que se contesta sin `wait` (con código: "%1" el código, "%2" la página).
    // `sinCodigo`: el error si el código no llega (solo con código).
    const auto login = [&core, &de](const char* categoria, const char* nombre, const char* titulo,
                                              const char* descripcion, const char* sinSetup, const char* abierto,
                                              const char* sinCodigo) {
        PlataformaViewModel& p = de(categoria);
        Tool t;
        t.name = nombre;
        t.title = ba::i18n::traducir("mcp", titulo);
        t.category = categoria;
        t.clase = Clase::Escritura;
        t.openWorld = true;
        t.description = descripcion;
        t.inputSchema = {
            {"type", "object"},
            {"properties", {{"wait", {{"type", "boolean"}, {"default", false}}}, {"cancel", {{"type", "boolean"}}}}}};
        t.run = [&p, sinSetup, abierto, sinCodigo](const json& a, Done done) {
            if (a.value("cancel", false)) {
                p.cancelLogin();
                return done(ToolResult::ok(ba::i18n::traducir("mcp", "Login cancelado.")));
            }
            if (!p.configured()) return done(ToolResult::error(ba::i18n::traducir("mcp", sinSetup)));
            if (a.value("wait", false)) {
                // El login del modelo (relee la cuenta y avisa a la página), con la respuesta.
                p.login([&p, done](bool ok, const QJsonObject& d, const QString& e) {
                    if (!ok) return done(ToolResult::error(e.toStdString()));
                    json r = toJson(p.alConectar());
                    r["connected"] = toJson(d);
                    done(ToolResult().data(r));
                });
                return;
            }
            if (!p.loginConCodigo()) {
                p.login();
                return done(ToolResult::ok(ba::i18n::traducir("mcp", abierto)));
            }
            // El código llega en un momento y el modelo AVISA (codigoListo): se espera esa
            // señal (máx. ~8 s), no se la sondea. Un error del login contesta antes.
            auto contestado = std::make_shared<bool>(false);
            const Done unaVez = [contestado, done](ToolResult r) {
                if (*contestado) return;
                *contestado = true;
                done(std::move(r));
            };
            p.login([unaVez, sinCodigo](bool ok, const QJsonObject&, const QString& e) {
                if (!ok) unaVez(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", sinCodigo), {e.toStdString()})));
            });
            esperarHasta(
                &p, &PlataformaViewModel::codigoListo,
                [&p, abierto]() -> std::optional<ToolResult> {
                    if (p.codigo().isEmpty()) return std::nullopt;
                    return ToolResult()
                        .text(ba::i18n::llenar(ba::i18n::traducir("mcp", abierto),
                                               {p.codigo().toStdString(), p.codigoUri().toStdString()}))
                        .data(json{{"userCode", p.codigo().toStdString()},
                                   {"verificationUri", p.codigoUri().toStdString()}});
                },
                8000,
                [&p, sinCodigo]() {
                    return ToolResult::error(
                        ba::i18n::llenar(ba::i18n::traducir("mcp", sinCodigo), {p.lastError().toStdString()}));
                },
                unaVez);
        };
        core.addTool(std::move(t));
    };
    login("youtube", "youtube_login", QT_TRANSLATE_NOOP("mcp", "Conectar una cuenta de YouTube"),
          QT_TRANSLATE_NOOP("mcp", "Abre el navegador de esta maquina en la pantalla de Google para que la PERSONA "
                                   "elija la cuenta y de permiso. Con wait=true espera hasta que termine (max. 5 "
                                   "min); si no, vuelve enseguida y youtube_status dice cuando quedo conectado. "
                                   "cancel=true cancela uno en curso."),
          QT_TRANSLATE_NOOP("mcp", "Primero youtube_setup (credencial de Google Cloud)."),
          QT_TRANSLATE_NOOP("mcp", "Se abrio el navegador: la persona tiene que elegir la cuenta y aceptar. "
                                   "Despues, youtube_status."),
          nullptr);
    login("twitch", "twitch_login", QT_TRANSLATE_NOOP("mcp", "Conectar una cuenta de Twitch"),
          QT_TRANSLATE_NOOP("mcp", "Pide un codigo a Twitch y abre twitch.tv/activate en el navegador de esta maquina: "
                                   "la PERSONA inicia sesion con la cuenta del canal y confirma. Devuelve el codigo "
                                   "(mostraselo). Con wait=true espera a que termine (max. 5 min); si no, "
                                   "twitch_status dice cuando quedo conectado. cancel=true cancela."),
          QT_TRANSLATE_NOOP("mcp", "Primero twitch_setup (ID de cliente)."),
          QT_TRANSLATE_NOOP("mcp", "Codigo: %1 — se abrio %2; la persona confirma ahi. Despues, twitch_status."),
          QT_TRANSLATE_NOOP("mcp", "Twitch no devolvio el codigo: %1"));
    login("facebook", "facebook_login", QT_TRANSLATE_NOOP("mcp", "Conectar una cuenta de Facebook"),
          QT_TRANSLATE_NOOP("mcp", "Abre el navegador de esta maquina en Facebook para que la PERSONA elija la cuenta, "
                                   "marque las Paginas y acepte los permisos. Con wait=true espera a que termine "
                                   "(max. 5 min); si no, facebook_status dice cuando quedo conectado. cancel=true "
                                   "cancela."),
          QT_TRANSLATE_NOOP("mcp", "Primero facebook_setup (app de Meta)."),
          QT_TRANSLATE_NOOP("mcp", "Se abrio el navegador: la persona elige la cuenta y las Paginas y acepta. "
                                   "Despues, facebook_status."),
          nullptr);

    // --- *_logout ----------------------------------------------------------------------------
    const auto logout = [&core, &de](const char* categoria, const char* nombre, const char* descripcion,
                                const char* listo) {
        PlataformaViewModel& p = de(categoria);
        Tool t;
        t.name = nombre;
        t.title = ba::i18n::traducir("mcp", "Desconectar la cuenta");
        t.category = categoria;
        t.clase = Clase::Escritura;
        t.destructive = true;
        t.description = descripcion;
        t.run = [&p, listo](const json&, Done done) {
            p.logout();
            done(ToolResult::ok(ba::i18n::traducir("mcp", listo)));
        };
        core.addTool(std::move(t));
    };
    logout("youtube", "youtube_logout",
           QT_TRANSLATE_NOOP("mcp", "Desconecta la cuenta de YouTube de este aparato: revoca el acceso en Google y "
                                    "borra el token guardado."),
           QT_TRANSLATE_NOOP("mcp", "Cuenta desconectada."));
    logout("twitch", "twitch_logout", QT_TRANSLATE_NOOP("mcp", "Revoca el token en Twitch y lo borra de la carpeta portable."),
           QT_TRANSLATE_NOOP("mcp", "Cuenta de Twitch desconectada."));
    logout("facebook", "facebook_logout",
           QT_TRANSLATE_NOOP("mcp", "Quita el permiso de la app en Facebook y borra los tokens guardados."),
           QT_TRANSLATE_NOOP("mcp", "Cuenta de Facebook desconectada."));

    // --- *_videos: los del canal o de la Página, los más nuevos primero -----------------------
    const auto videos = [&core, &de](const char* categoria, const char* nombre, const char* titulo,
                                const char* descripcion, const json& esquema) {
        PlataformaViewModel& p = de(categoria);
        Tool t;
        t.name = nombre;
        t.title = ba::i18n::traducir("mcp", titulo);
        t.category = categoria;
        t.clase = Clase::Lectura;
        t.description = descripcion;
        t.inputSchema = esquema;
        t.run = [&p](const json& a, Done done) {
            if (faltaParaUsar(p, done)) return;
            p.listarVideos(a.value("max", 25), responder(done, p));
        };
        core.addTool(std::move(t));
    };
    videos("youtube", "youtube_videos", QT_TRANSLATE_NOOP("mcp", "Videos del canal"),
           QT_TRANSLATE_NOOP("mcp", "Videos subidos (mas nuevos primero) con id, titulo, descripcion, privacidad, "
                                    "fecha, vistas, me gusta, comentarios, duracion y enlace."),
           json{{"type", "object"},
                {"properties", {{"max", {{"type", "integer"}, {"minimum", 1}, {"maximum", 50}, {"default", 25}}}}}});
    videos("twitch", "twitch_videos", QT_TRANSLATE_NOOP("mcp", "VODs del canal"),
           QT_TRANSLATE_NOOP("mcp", "Videos del canal (vivos guardados, destacados, subidos): id, titulo, enlace, "
                                    "vistas, fecha, duracion y tipo."),
           json{{"type", "object"},
                {"properties", {{"max", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}, {"default", 25}}}}}});
    videos("facebook", "facebook_videos", QT_TRANSLATE_NOOP("mcp", "Videos de la Página"),
           QT_TRANSLATE_NOOP("mcp", "Videos de la Pagina (mas nuevos primero): id, titulo, descripcion, enlace, "
                                    "miniatura, fecha y duracion."),
           json{{"type", "object"},
                {"properties", {{"max", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}, {"default", 25}}}}}});
}

} // namespace ba::app::mcp
