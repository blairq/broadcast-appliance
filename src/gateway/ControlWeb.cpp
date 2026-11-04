// ============================================================================
// src/gateway/ControlWeb.cpp — ver ControlWeb.hpp
// ============================================================================

#include "ControlWeb.hpp"
#include "Canal.hpp"
#include "Borde.hpp"
#include "security/Comparar.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "HttpIo.hpp"
#include "Log.hpp"
#include "ajustes/Ajustes.hpp"
#include "security/SecretsStore.hpp"
#include "i18n/I18nTexto.hpp"

// Lo que se le manda a la página del control (error, motivo) no se traduce acá:
// se marca para el catálogo de la web y la página lo traduce (el valor que viaja
// es el original).
#ifndef QT_TRANSLATE_NOOP
#define QT_TRANSLATE_NOOP(scope, x) x
#endif

namespace ba::gateway {

using ba::i18n::llenar;
using ba::i18n::traducir;
using json = nlohmann::json;

namespace {

std::int64_t ahoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string volcar(const json& j) { return j.dump(-1, ' ', false, json::error_handler_t::replace); }

inline int kMaxConexiones() { return ba::ajustes::entero("controlWeb.maxConexiones", 32); }
inline int kMensajesPorSegundo() { return ba::ajustes::entero("controlWeb.mensajesPorSegundo", 60); }
inline int kLatidoMs() { return ba::ajustes::sAMs("controlWeb.latidoS", 30); }
inline int kAuthMs() { return ba::ajustes::sAMs("controlWeb.authS", 10); }
inline std::int64_t kSolicitudMs() { return ba::ajustes::entero64("controlWeb.solicitudMin", 5) * 60 * 1000; }
constexpr std::size_t kMaxMensaje = 64 * 1024;
/// Lo único que se le pasa a la app; el resto se descarta acá.
const std::set<std::string> kTipos{"hola", "llamar", "mirar", "preferencias", "replica"};

/// ¿El cliente acepta esa codificación? (Accept-Encoding, sin distinguir mayúsculas)
bool acepta(const std::string& headers, const char* codificacion) {
    std::string h = headers;
    std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto i = h.find("accept-encoding:");
    if (i == std::string::npos) return false;
    const auto fin = h.find('\n', i);
    return h.substr(i, fin == std::string::npos ? std::string::npos : fin - i).find(codificacion) != std::string::npos;
}

} // namespace

ControlWebBorde::ControlWebBorde(std::string webroot, Log& log, Evento alApp)
    : webroot_(std::move(webroot)), log_(log), alApp_(std::move(alApp)) {}

bool ControlWebBorde::deAfuera(std::uint32_t ip, bool externo) {
    // Cloudflare (aunque llegue desde 127.0.0.1) ya viene como `externo`: ipcliente::resolver.
    return externo || !borde::ipLocal(ip);
}

bool ControlWebBorde::mismoOrigen(const std::string& headers) { return borde::mismoOrigen(headers); }

bool ControlWebBorde::tunelHabilitado() const {
    std::lock_guard<std::mutex> l(mu_);
    return tunel_ && tunelHashPin_.size() == 64;
}

int ControlWebBorde::conectados() const {
    std::lock_guard<std::mutex> l(mu_);
    return static_cast<int>(conns_.size());
}

bool ControlWebBorde::servir(long long fd, const std::string& rel) {
    // CSP estricta: solo lo nuestro (más /comun/, que es el mismo origen), nada en
    // línea, sin marcos ajenos. Las tarjetas que se abren en una pestaña nueva son
    // del mismo origen; el video va por WebRTC (connect-src ws/wss para la señalización).
    return borde::servir(fd, webroot_, rel,
                         "Cache-Control: no-cache\r\n"
                         "Content-Security-Policy: default-src 'self'; connect-src 'self' ws: wss:; img-src 'self' data: blob:; "
                         "media-src 'self' blob:; frame-ancestors 'self'; base-uri 'none'; form-action 'none'\r\n"
                         "X-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n");
}

bool ControlWebBorde::servirTablero(long long fd, const std::string& rel, const std::string& query,
                                    const std::string& headers) {
    // El tablero web (Qt para WebAssembly, src/tablero-web): el .wasm necesita
    // 'wasm-unsafe-eval' para compilarse (nada de eval de JS). Lo grande (.wasm, .js) viaja
    // comprimido de antemano (.br por HTTPS, .gz por HTTP: el navegador no pide br sin
    // TLS) y con la versión en la consulta (?v=): se guarda para siempre en el navegador y
    // una versión nueva es otra URL. La página misma, sin caché.
    const bool versionado = query.find("v=") != std::string::npos;
    const bool grande = rel.size() > 5 && (rel.compare(rel.size() - 5, 5, ".wasm") == 0 || rel.compare(rel.size() - 3, 3, ".js") == 0);
    std::string cab = std::string(versionado ? "Cache-Control: public, max-age=31536000, immutable\r\n" : "Cache-Control: no-cache\r\n") +
                      "Content-Security-Policy: default-src 'self'; script-src 'self' 'wasm-unsafe-eval'; connect-src 'self' ws: "
                      "wss:; img-src 'self' data: blob:; style-src 'self' 'unsafe-inline'; font-src 'self' data:; media-src "
                      "'self' blob:; frame-ancestors 'self'; base-uri 'none'; form-action 'none'\r\n"
                      "X-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nVary: Accept-Encoding\r\n";
    if (grande) {
        std::error_code ec;
        for (const auto& [ext, nombre] : {std::pair<const char*, const char*>{".br", "br"}, {".gz", "gzip"}}) {
            if (!acepta(headers, nombre) || !std::filesystem::exists(std::filesystem::path(webroot_) / (rel + ext), ec)) continue;
            std::ifstream f(std::filesystem::path(webroot_) / (rel + ext), std::ios::binary);
            if (!f) continue;
            std::ostringstream ss;
            ss << f.rdbuf();
            http::sendResponse(fd, 200, borde::tipoDe(rel), ss.str(), cab + "Content-Encoding: " + nombre + "\r\n");
            return true;
        }
    }
    return borde::servir(fd, webroot_, rel, cab.c_str());
}

bool ControlWebBorde::intentoPermitido(std::uint32_t ip) {
    std::lock_guard<std::mutex> l(mu_);
    auto& v = intentos_[ip];
    const std::int64_t ahora = ahoraMs();
    v.erase(std::remove_if(v.begin(), v.end(), [&](std::int64_t t) { return ahora - t > 10 * 60 * 1000; }), v.end());
    if (v.size() >= 5) return false;
    v.push_back(ahora);
    return true;
}

bool ControlWebBorde::bloqueada(std::uint32_t ip) const {
    std::lock_guard<std::mutex> l(mu_);
    const auto it = intentos_.find(ip);
    if (it == intentos_.end()) return false;
    const std::int64_t ahora = ahoraMs();
    return std::count_if(it->second.begin(), it->second.end(), [&](std::int64_t t) { return ahora - t <= 10 * 60 * 1000; }) >= 5;
}

std::string ControlWebBorde::autenticar(const std::string& token, const std::string& pin, bool remoto) const {
    if (token.size() != 64) return {};
    std::lock_guard<std::mutex> l(mu_);
    std::string encontrado;
    // Se recorren todas (sin cortar) para no revelar cuál coincidió por el tiempo.
    for (const auto& [id, c] : credenciales_)
        if (ba::security::igualesEnTiempoConstante(borde::hashConSal(c.sal + token), c.hash)) encontrado = id;
    if (encontrado.empty() || !remoto) return encontrado;
    // Desde afuera: el túnel habilitado a propósito y el PIN, además del token.
    if (!tunel_ || tunelHashPin_.size() != 64 || pin.empty()) return {};
    return ba::security::igualesEnTiempoConstante(borde::hashConSal(tunelSal_ + pin), tunelHashPin_) ? encontrado
                                                                                            : std::string();
}

void ControlWebBorde::ponerCredenciales(const json& r) {
    std::vector<std::shared_ptr<Conn>> afuera;
    {
        std::lock_guard<std::mutex> l(mu_);
        conCredenciales_ = true;
        credenciales_.clear();
        for (const auto& d : r.value("aparatos", json::array()))
            if (d.is_object() && d.value("hash", std::string()).size() == 64 && !d.value("id", std::string()).empty())
                credenciales_[d.value("id", std::string())] = {d.value("hash", std::string()),
                                                               d.value("sal", std::string())};
        const json t = r.value("tunel", json::object());
        tunel_ = t.is_object() && t.value("habilitado", false);
        tunelSal_ = t.is_object() ? t.value("sal", std::string()) : std::string();
        tunelHashPin_ = t.is_object() ? t.value("hash_pin", std::string()) : std::string();
        // Lo revocado se corta ya; y lo de afuera, si se apagó el túnel.
        for (const auto& c : conns_)
            if (!credenciales_.count(c->aparato) || (c->remoto && !tunel_)) afuera.push_back(c);
    }
    for (const auto& c : afuera)
        cortar(c, QT_TRANSLATE_NOOP("web/comun", "Este aparato fue revocado (o se apagó el acceso desde afuera)."));
}

bool ControlWebBorde::atender(long long fd, const std::string& method, const std::string& path, const std::string& query,
                              const std::string& headers, const std::string& body, std::uint32_t clientIp,
                              bool externo) {
    const bool ws = path == "/ws" && http::parseQuery(query).count("control") > 0;
    const bool pagina = path == "/control" || path.rfind("/control/", 0) == 0;
    const bool api = path == "/api/control/emparejar";
    if (!ws && !pagina && !api) return false;
    const bool remoto = deAfuera(clientIp, externo);
    // De afuera: solo si la app habilitó el túnel (con PIN); emparejar, nunca.
    if (!mismoOrigen(headers) || (remoto && (api || !tunelHabilitado()))) {
        log_.warn(remoto ? llenar(traducir("gateway", "control web: rechazado %1 %2 (de afuera, sin el acceso por el túnel "
                                                      "habilitado)"),
                                  {borde::ipTexto(clientIp), path})
                         : llenar(traducir("gateway", "control web: rechazado %1 %2 (origen ajeno)"), {borde::ipTexto(clientIp), path}),
                  -1);
        http::sendResponse(fd, 403, "text/plain", traducir("gateway", "solo desde la red local") + "\n");
        return true;
    }
    if (pagina) {
        if (method != "GET" && method != "HEAD") return http::sendResponse(fd, 405, "text/plain", "\n"), true;
        if (path == "/control") return http::sendResponse(fd, 301, "text/plain", "", "Location: /control/\r\n"), true;
        std::string rel = path.substr(std::string("/control/").size());
        // Las superficies de una página (/control/escaleta, /control/presentador,
        // /control/camara) son carpetas: sin la barra, a la carpeta (con la consulta,
        // que dice la salida o la cámara), así sus rutas relativas andan.
        // Solo un nombre simple (letras, números, guiones) y una consulta sin cortes de línea.
        const auto simple = [](const std::string& s) {
            return !s.empty() && s.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") == std::string::npos;
        };
        if (simple(rel) && query.find_first_of("\r\n") == std::string::npos)
            return http::sendResponse(fd, 301, "text/plain", "",
                                      "Location: /control/" + rel + "/" + (query.empty() ? std::string() : "?" + query) + "\r\n"),
                   true;
        if (rel.empty() || rel.back() == '/') rel += "index.html";
        if (rel.rfind("app/", 0) == 0) return servirTablero(fd, rel, query, headers);
        return servir(fd, rel);
    }
    if (api && method == "POST") {
        if (!intentoPermitido(clientIp)) {
            http::sendResponse(fd, 429, "application/json",
                               volcar({{"error", QT_TRANSLATE_NOOP("web/comun", "demasiados intentos; esperá 10 minutos")}}));
            return true;
        }
        const json r = json::parse(body, nullptr, false);
        const std::string codigo = r.is_object() ? r.value("codigo", std::string()) : std::string();
        if (codigo.size() != 32) {
            http::sendResponse(fd, 400, "application/json", volcar({{"error", QT_TRANSLATE_NOOP("web/comun", "código inválido")}}));
            return true;
        }
        // La app valida el código (lo emitió ella) y le pregunta a una persona.
        std::string nombre = r.value("nombre", std::string("Navegador"));
        if (nombre.size() > 60) nombre.resize(60);
        std::string sol;
        if (!ba::security::randomSecretHex(16, sol)) return http::sendResponse(fd, 500, "text/plain", "\n"), true;
        {
            std::lock_guard<std::mutex> l(mu_);
            const std::int64_t ahora = ahoraMs();
            for (auto it = solicitudes_.begin(); it != solicitudes_.end();)
                it = ahora - it->second.ms > kSolicitudMs() ? solicitudes_.erase(it) : std::next(it);
            Solicitud s;
            s.ip = borde::ipTexto(clientIp);
            s.ms = ahora;
            s.codigo = codigo;
            s.nombre = nombre;
            solicitudes_[sol] = s;
        }
        alApp_({{"tipo", "solicitud"}, {"solicitud", sol}, {"codigo", codigo}, {"nombre", nombre}, {"ip", borde::ipTexto(clientIp)}});
        http::sendResponse(fd, 200, "application/json", volcar({{"solicitud", sol}}), "Cache-Control: no-store\r\n");
        return true;
    }
    if (api && method == "GET") {
        const auto q = http::parseQuery(query);
        const std::string sol = q.count("solicitud") ? http::urlDecode(q.at("solicitud")) : std::string();
        json out{{"estado", "desconocida"}};
        {
            std::lock_guard<std::mutex> l(mu_);
            auto it = solicitudes_.find(sol);
            if (it != solicitudes_.end() && it->second.ip == borde::ipTexto(clientIp)) {
                out = {{"estado", it->second.estado}};
                if (!it->second.error.empty()) out["error"] = it->second.error;
                if (it->second.estado == "aprobada") {
                    out["aparato"] = it->second.aparato;
                    out["token"] = it->second.token;
                    solicitudes_.erase(it); // el token se entrega una sola vez
                } else if (it->second.estado != "pendiente") {
                    solicitudes_.erase(it);
                }
            }
        }
        http::sendResponse(fd, 200, "application/json", volcar(out), "Cache-Control: no-store\r\n");
        return true;
    }
    if (ws) {
        wsLoop(fd, headers, clientIp, remoto);
        return true;
    }
    http::sendResponse(fd, 405, "text/plain", "\n");
    return true;
}

void ControlWebBorde::cortar(const std::shared_ptr<Conn>& c, const std::string& motivo) {
    if (!motivo.empty()) c->enviar({{"type", "auth"}, {"ok", false}, {"error", motivo}, {"error_codigo", "token"}});
    c->cortar();
}

void ControlWebBorde::wsLoop(long long fd, const std::string& headers, std::uint32_t clientIp, bool remoto) {
    {
        std::lock_guard<std::mutex> l(mu_);
        if (static_cast<int>(conns_.size()) >= kMaxConexiones()) {
            http::sendResponse(fd, 503, "text/plain", traducir("gateway", "demasiados aparatos conectados") + "\n");
            return;
        }
    }
    if (!canal::aceptar(fd, headers)) return;
    auto c = std::make_shared<Conn>();
    c->ip = borde::ipTexto(clientIp);
    c->remoto = remoto;
    // 1. Autenticación: el primer mensaje (docs/arquitectura/06-canal-web.md), a los 10 s como mucho.
    {
        const json m = canal::primerMensaje(fd, kAuthMs());
        if (m.is_null()) return;
        const std::string token = m.contains("token") && m["token"].is_string() ? m["token"].get<std::string>() : "";
        const std::string pin = m.contains("pin") && m["pin"].is_string() ? m["pin"].get<std::string>() : "";
        // Recién arrancado (o la app reiniciándose) todavía no hay credenciales: no se puede
        // saber si el token vale. «esperar»: la página vuelve a probar sola y NO olvida su token
        // (antes se le decía «no emparejado», lo borraba y no se reconectaba nunca más).
        {
            std::lock_guard<std::mutex> l(mu_);
            if (!conCredenciales_)
                return canal::rechazar(fd, QT_TRANSLATE_NOOP("web/comun", "La app todavía está arrancando."), "esperar");
        }
        // Con 5 errores seguidos desde esa IP no se prueba nada más (el PIN es corto).
        const std::string ap = bloqueada(clientIp) ? std::string() : autenticar(token, pin, remoto);
        if (ap.empty()) {
            // Un token (o un PIN) errado cuenta como intento: 5 cada 10 min por IP.
            const bool otra = intentoPermitido(clientIp);
            log_.warn(remoto ? llenar(traducir("gateway", "control web: token o PIN inválido desde %1"), {c->ip})
                             : llenar(traducir("gateway", "control web: token inválido desde %1"), {c->ip}),
                      -1);
            if (!otra) return canal::rechazar(fd, QT_TRANSLATE_NOOP("web/comun", "Demasiados intentos: esperá 10 minutos."), "intentos");
            return canal::rechazar(fd,
                                   remoto ? QT_TRANSLATE_NOOP("web/comun", "Este aparato no está emparejado, o falta el PIN "
                                                                           "para entrar desde afuera.")
                                          : QT_TRANSLATE_NOOP("web/comun", "Este aparato no está emparejado (o fue quitado)."),
                                   "token");
        }
        c->aparato = ap;
    }
    c->fd = fd;
    canal::anotar(c);
    {
        std::string id;
        if (!ba::security::randomSecretHex(8, id)) return;
        c->id = "c" + id;
        std::lock_guard<std::mutex> l(mu_);
        conns_.push_back(c);
    }
    log_.info(remoto ? llenar(traducir("gateway", "control web: %1 conectado desde %2 (afuera) [%3]"), {c->aparato, c->ip, c->id})
                     : llenar(traducir("gateway", "control web: %1 conectado desde %2 [%3]"), {c->aparato, c->ip, c->id}));
    // La app contesta el auth (con el perfil y las herramientas) cuando se entera.
    alApp_({{"tipo", "conexion"},
            {"conexion", c->id},
            {"aparato", c->aparato},
            {"conectado", true},
            {"ip", c->ip},
            {"lugar", remoto ? "afuera" : "sala"}});
    // 2. El lazo.
    const canal::Fin fin = canal::leer(*c, {kLatidoMs(), kMaxMensaje, kMensajesPorSegundo()}, [&](const json& m) {
        const std::string t = m.value("type", std::string());
        if (!kTipos.count(t)) return true;
        // Se reenvía solo lo que la app entiende, con los campos que espera.
        json limpio{{"type", t}};
        if (m.contains("id") && (m["id"].is_number() || m["id"].is_string())) limpio["id"] = m["id"];
        if (t == "llamar") {
            if (!m.contains("herramienta") || !m["herramienta"].is_string()) return true;
            limpio["herramienta"] = m["herramienta"];
            limpio["args"] = m.contains("args") && m["args"].is_object() ? m["args"] : json::object();
            if (m.contains("confirmado") && m["confirmado"].is_boolean()) limpio["confirmado"] = m["confirmado"];
        } else if (t == "mirar") {
            json temas = json::array();
            if (m.contains("temas") && m["temas"].is_array())
                for (const auto& x : m["temas"])
                    if (x.is_string() && temas.size() < 32) temas.push_back(x);
            limpio["temas"] = temas;
        } else if (t == "replica") {
            // El tablero web (/control/app/): lo valida la app (control/ReplicaPublicador).
            // Acá solo que sea un objeto con su `op` (el tamaño ya lo cortó el canal).
            if (!m.contains("op") || !m["op"].is_string()) return true;
            limpio = m;
        } else if (t == "preferencias") {
            if (!m.contains("datos") || !m["datos"].is_object() || m["datos"].dump().size() > 8192) return true;
            limpio["datos"] = m["datos"];
        }
        alApp_({{"tipo", "entrada"}, {"conexion", c->id}, {"aparato", c->aparato}, {"msg", limpio}});
        return true;
    });
    if (fin == canal::Fin::Exceso)
        log_.warn(llenar(traducir("gateway", "control web: %1 manda demasiado; se corta"), {c->aparato}), -1);
    {
        std::lock_guard<std::mutex> l(mu_);
        conns_.erase(std::remove(conns_.begin(), conns_.end(), c), conns_.end());
    }
    c->soltar();
    alApp_({{"tipo", "conexion"}, {"conexion", c->id}, {"aparato", c->aparato}, {"conectado", false}, {"ip", c->ip}});
    log_.info(llenar(traducir("gateway", "control web: %1 desconectado [%2]"), {c->aparato, c->id}));
}

bool ControlWebBorde::admin(long long fd, const std::string& method, const std::string& path, const std::string& body) {
    if (path.rfind("/api/v1/control/", 0) != 0) return false;
    const std::string que = path.substr(std::string("/api/v1/control/").size());
    const json r = json::parse(body.empty() ? "{}" : body, nullptr, false);
    // Las solicitudes que la app todavía no resolvió (una app que no estaba cuando llegaron,
    // o que no recibió el evento: las va a buscar al conectar).
    if (method == "GET" && que == "solicitudes") {
        json ps = json::array();
        {
            std::lock_guard<std::mutex> l(mu_);
            const std::int64_t ahora = ahoraMs();
            for (const auto& [sol, s] : solicitudes_)
                if (s.estado == "pendiente" && ahora - s.ms <= kSolicitudMs())
                    ps.push_back({{"solicitud", sol}, {"codigo", s.codigo}, {"nombre", s.nombre}, {"ip", s.ip}});
        }
        http::sendResponse(fd, 200, "application/json", json{{"solicitudes", ps}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        return true;
    }
    if (method == "GET" && que == "estado") {
        json cs = json::array();
        std::lock_guard<std::mutex> l(mu_);
        for (const auto& c : conns_)
            cs.push_back({{"conexion", c->id}, {"aparato", c->aparato}, {"ip", c->ip}, {"lugar", c->remoto ? "afuera" : "sala"}});
        http::sendResponse(fd, 200, "application/json",
                           volcar({{"conexiones", cs},
                                   {"credenciales", credenciales_.size()},
                                   {"solicitudes", solicitudes_.size()},
                                   {"tunel", tunel_}}));
        return true;
    }
    if (method != "POST" || !r.is_object()) {
        http::sendResponse(fd, 400, "application/json", volcar({{"error", "bad_request"}}));
        return true;
    }
    if (que == "credenciales") {
        ponerCredenciales(r);
        http::sendResponse(fd, 200, "application/json", volcar({{"ok", true}}));
        return true;
    }
    if (que == "resolver") {
        std::lock_guard<std::mutex> l(mu_);
        auto it = solicitudes_.find(r.value("solicitud", std::string()));
        if (it == solicitudes_.end()) {
            http::sendResponse(fd, 404, "application/json", volcar({{"error", "not_found"}}));
            return true;
        }
        it->second.estado = r.value("estado", std::string("rechazada"));
        it->second.aparato = r.value("aparato", std::string());
        it->second.token = r.value("token", std::string());
        it->second.error = r.value("error", std::string());
        http::sendResponse(fd, 200, "application/json", volcar({{"ok", true}}));
        return true;
    }
    // A quiénes va: {conexion | conexiones:[…] | aparato | todas:true}. `todas`: a cada aparato
    // conectado (una alarma que la app manda desde fuera del hilo de su interfaz y no sabe
    // quién está). Solo para enviar: cortar a todos, no.
    const auto destinatarios = [this](const json& d, bool cortando) {
        std::set<std::string> ids;
        if (d.contains("conexion") && d["conexion"].is_string()) ids.insert(d["conexion"].get<std::string>());
        if (d.contains("conexiones") && d["conexiones"].is_array())
            for (const auto& x : d["conexiones"])
                if (x.is_string()) ids.insert(x.get<std::string>());
        const std::string ap = d.value("aparato", std::string());
        const bool todas = !cortando && d.value("todas", false);
        std::vector<std::shared_ptr<Conn>> a;
        std::lock_guard<std::mutex> l(mu_);
        for (const auto& c : conns_)
            if (todas || ids.count(c->id) || (!ap.empty() && c->aparato == ap)) a.push_back(c);
        return a;
    };
    if (que == "enviar" && r.contains("envios") && r["envios"].is_array()) {
        // Varios, EN ORDEN (la app junta lo que se le acumuló mientras esperaba la respuesta
        // del anterior): cada uno con sus destinatarios, como un «enviar» suelto.
        std::size_t entregado = 0;
        for (const auto& e : r["envios"]) {
            if (!e.is_object()) continue;
            const auto a = destinatarios(e, false);
            const json msg = e.value("msg", json::object());
            for (const auto& c : a) c->enviar(msg);
            entregado += a.size();
        }
        http::sendResponse(fd, 200, "application/json", volcar({{"ok", true}, {"entregado", entregado}}));
        return true;
    }
    if (que == "enviar" || que == "cortar") {
        const auto a = destinatarios(r, que == "cortar");
        if (que == "enviar") {
            const json msg = r.value("msg", json::object());
            for (const auto& c : a) c->enviar(msg);
        } else {
            for (const auto& c : a) cortar(c, r.value("motivo", std::string()));
        }
        http::sendResponse(fd, 200, "application/json", volcar({{"ok", true}, {"entregado", a.size()}}));
        return true;
    }
    http::sendResponse(fd, 404, "application/json", volcar({{"error", "not_found"}}));
    return true;
}

void ControlWebBorde::cerrar() {
    std::vector<std::shared_ptr<Conn>> a;
    {
        std::lock_guard<std::mutex> l(mu_);
        a = conns_;
    }
    for (const auto& c : a) cortar(c, "");
}

} // namespace ba::gateway
