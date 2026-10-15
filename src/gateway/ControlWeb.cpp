// ============================================================================
// src/gateway/ControlWeb.cpp — ver ControlWeb.hpp
// ============================================================================

#include "ControlWeb.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "Botonera.hpp" // ipLocal y sha256Hex: los mismos criterios que la botonera
#include "HttpIo.hpp"
#include "Log.hpp"
#include "ajustes/Ajustes.hpp"
#include "security/SecretsStore.hpp"

namespace ba::gateway {

using json = nlohmann::json;

namespace {

std::int64_t ahoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string ipTexto(std::uint32_t ip) {
    const auto* b = reinterpret_cast<const unsigned char*>(&ip);
    return std::to_string(b[0]) + "." + std::to_string(b[1]) + "." + std::to_string(b[2]) + "." + std::to_string(b[3]);
}

bool igualesTiempoConstante(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char d = 0;
    for (std::size_t i = 0; i < a.size(); ++i) d |= static_cast<unsigned char>(a[i] ^ b[i]);
    return d == 0;
}

std::string minus(std::string s) {
    for (auto& c : s) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string volcar(const json& j) { return j.dump(-1, ' ', false, json::error_handler_t::replace); }

const char* tipoDe(const std::string& rel) {
    const auto termina = [&](const char* e) {
        const std::string x(e);
        return rel.size() >= x.size() && rel.compare(rel.size() - x.size(), x.size(), x) == 0;
    };
    if (termina(".html")) return "text/html; charset=utf-8";
    if (termina(".js") || termina(".mjs")) return "text/javascript; charset=utf-8";
    if (termina(".css")) return "text/css; charset=utf-8";
    if (termina(".json")) return "application/json";
    if (termina(".webmanifest")) return "application/manifest+json";
    if (termina(".svg")) return "image/svg+xml";
    if (termina(".png")) return "image/png";
    if (termina(".jpg") || termina(".jpeg")) return "image/jpeg";
    if (termina(".woff2")) return "font/woff2";
    if (termina(".ico")) return "image/x-icon";
    return "application/octet-stream";
}

inline int kMaxConexiones() { return ba::ajustes::entero("controlWeb.maxConexiones", 32); }
inline int kMensajesPorSegundo() { return ba::ajustes::entero("controlWeb.mensajesPorSegundo", 60); }
inline int kLatidoMs() { return ba::ajustes::sAMs("controlWeb.latidoS", 30); }
inline int kAuthMs() { return ba::ajustes::sAMs("controlWeb.authS", 10); }
inline std::int64_t kSolicitudMs() { return ba::ajustes::entero64("controlWeb.solicitudMin", 5) * 60 * 1000; }
constexpr std::size_t kMaxMensaje = 64 * 1024;
/// Lo único que se le pasa a la app; el resto se descarta acá.
const std::set<std::string> kTipos{"hola", "llamar", "mirar", "preferencias"};

} // namespace

ControlWebBorde::ControlWebBorde(std::string webroot, Log& log, Evento alApp)
    : webroot_(std::move(webroot)), log_(log), alApp_(std::move(alApp)) {}

bool ControlWebBorde::deAfuera(std::uint32_t ip, const std::string& headers, bool externo) {
    if (externo || !BotoneraBorde::ipLocal(ip)) return true;
    // Lo que viene por Cloudflare no es la red local aunque llegue desde 127.0.0.1.
    return !http::getHeader(headers, "CF-Connecting-IP").empty() || !http::getHeader(headers, "CF-Ray").empty();
}

bool ControlWebBorde::mismoOrigen(const std::string& headers) {
    const std::string origin = http::getHeader(headers, "Origin");
    if (origin.empty()) return true; // no es un navegador de otra página (curl, app nativa)
    const std::string host = minus(http::getHeader(headers, "Host"));
    std::string o = minus(origin);
    const auto p = o.find("://");
    if (p == std::string::npos) return false;
    o = o.substr(p + 3);
    return !host.empty() && o == host;
}

bool ControlWebBorde::tunelHabilitado() const {
    std::lock_guard<std::mutex> l(mu_);
    return tunel_ && tunelHashPin_.size() == 64;
}

int ControlWebBorde::conectados() const {
    std::lock_guard<std::mutex> l(mu_);
    return static_cast<int>(conns_.size());
}

bool ControlWebBorde::servir(long long fd, const std::string& rel) {
    std::error_code ec;
    const auto raiz = std::filesystem::weakly_canonical(webroot_, ec);
    if (ec || webroot_.empty()) return http::sendResponse(fd, 404, "text/plain", "no encontrado\n"), true;
    const auto destino = std::filesystem::weakly_canonical(std::filesystem::path(webroot_) / http::urlDecode(rel), ec);
    const std::string r = raiz.generic_string(), d = destino.generic_string();
    if (ec || d.size() < r.size() || d.compare(0, r.size(), r) != 0 || (d.size() > r.size() && d[r.size()] != '/'))
        return http::sendResponse(fd, 400, "text/plain", "ruta invalida\n"), true;
    std::ifstream f(destino, std::ios::binary);
    if (!f || std::filesystem::is_directory(destino, ec))
        return http::sendResponse(fd, 404, "text/plain", "no encontrado\n"), true;
    std::ostringstream ss;
    ss << f.rdbuf();
    // CSP estricta: solo lo nuestro (más /comun/, que es el mismo origen), nada en
    // línea, sin marcos ajenos. Las tarjetas que se abren en una pestaña nueva son
    // del mismo origen; el video va por WebRTC (connect-src ws/wss para la señalización).
    http::sendResponse(fd, 200, tipoDe(rel), ss.str(),
                       "Cache-Control: no-cache\r\n"
                       "Content-Security-Policy: default-src 'self'; connect-src 'self' ws: wss:; img-src 'self' data: blob:; "
                       "media-src 'self' blob:; frame-ancestors 'self'; base-uri 'none'; form-action 'none'\r\n"
                       "X-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n");
    return true;
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
        if (igualesTiempoConstante(BotoneraBorde::sha256Hex(c.sal + token), c.hash)) encontrado = id;
    if (encontrado.empty() || !remoto) return encontrado;
    // Desde afuera: el túnel habilitado a propósito y el PIN, además del token.
    if (!tunel_ || tunelHashPin_.size() != 64 || pin.empty()) return {};
    return igualesTiempoConstante(BotoneraBorde::sha256Hex(tunelSal_ + pin), tunelHashPin_) ? encontrado
                                                                                            : std::string();
}

void ControlWebBorde::ponerCredenciales(const json& r) {
    std::vector<std::shared_ptr<Conn>> afuera;
    {
        std::lock_guard<std::mutex> l(mu_);
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
    for (const auto& c : afuera) cortar(c, "Este aparato fue revocado (o se apagó el acceso desde afuera).");
}

bool ControlWebBorde::atender(long long fd, const std::string& method, const std::string& path, const std::string& query,
                              const std::string& headers, const std::string& body, std::uint32_t clientIp,
                              bool externo) {
    const bool ws = path == "/ws" && http::parseQuery(query).count("control") > 0;
    const bool pagina = path == "/control" || path.rfind("/control/", 0) == 0;
    const bool api = path == "/api/control/emparejar";
    if (!ws && !pagina && !api) return false;
    const bool remoto = deAfuera(clientIp, headers, externo);
    // De afuera: solo si la app habilitó el túnel (con PIN); emparejar, nunca.
    if (!mismoOrigen(headers) || (remoto && (api || !tunelHabilitado()))) {
        log_.warn("control web: rechazado " + ipTexto(clientIp) + " " + path +
                      (remoto ? " (de afuera, sin el acceso por el túnel habilitado)" : " (origen ajeno)"),
                  -1);
        http::sendResponse(fd, 403, "text/plain", "solo desde la red local\n");
        return true;
    }
    if (pagina) {
        if (method != "GET" && method != "HEAD") return http::sendResponse(fd, 405, "text/plain", "\n"), true;
        if (path == "/control") return http::sendResponse(fd, 301, "text/plain", "", "Location: /control/\r\n"), true;
        std::string rel = path.substr(std::string("/control/").size());
        if (rel.empty() || rel.back() == '/') rel += "index.html";
        return servir(fd, rel);
    }
    if (api && method == "POST") {
        if (!intentoPermitido(clientIp)) {
            http::sendResponse(fd, 429, "application/json", volcar({{"error", "demasiados intentos; esperá 10 minutos"}}));
            return true;
        }
        const json r = json::parse(body, nullptr, false);
        const std::string codigo = r.is_object() ? r.value("codigo", std::string()) : std::string();
        if (codigo.size() != 32) {
            http::sendResponse(fd, 400, "application/json", volcar({{"error", "código inválido"}}));
            return true;
        }
        std::string sol;
        if (!ba::security::randomSecretHex(16, sol)) return http::sendResponse(fd, 500, "text/plain", "\n"), true;
        {
            std::lock_guard<std::mutex> l(mu_);
            const std::int64_t ahora = ahoraMs();
            for (auto it = solicitudes_.begin(); it != solicitudes_.end();)
                it = ahora - it->second.ms > kSolicitudMs() ? solicitudes_.erase(it) : std::next(it);
            Solicitud s;
            s.ip = ipTexto(clientIp);
            s.ms = ahora;
            solicitudes_[sol] = s;
        }
        // La app valida el código (lo emitió ella) y le pregunta a una persona.
        std::string nombre = r.value("nombre", std::string("Navegador"));
        if (nombre.size() > 60) nombre.resize(60);
        alApp_({{"tipo", "solicitud"}, {"solicitud", sol}, {"codigo", codigo}, {"nombre", nombre}, {"ip", ipTexto(clientIp)}});
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
            if (it != solicitudes_.end() && it->second.ip == ipTexto(clientIp)) {
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

void ControlWebBorde::enviar(const std::shared_ptr<Conn>& c, const json& msg) {
    std::lock_guard<std::mutex> l(c->sendLock);
    if (c->fd >= 0) http::writeWsFrame(c->fd, 0x1, volcar(msg));
}

void ControlWebBorde::cortar(const std::shared_ptr<Conn>& c, const std::string& motivo) {
    if (!motivo.empty()) enviar(c, {{"type", "auth"}, {"ok", false}, {"error", motivo}});
    std::lock_guard<std::mutex> l(c->sendLock);
    if (c->fd >= 0) http::shutdownFd(c->fd);
}

void ControlWebBorde::wsLoop(long long fd, const std::string& headers, std::uint32_t clientIp, bool remoto) {
    const std::string key = http::getHeader(headers, "Sec-WebSocket-Key");
    if (key.empty()) {
        http::sendResponse(fd, 400, "text/plain", "falta Sec-WebSocket-Key\n");
        return;
    }
    {
        std::lock_guard<std::mutex> l(mu_);
        if (static_cast<int>(conns_.size()) >= kMaxConexiones()) {
            http::sendResponse(fd, 503, "text/plain", "demasiados aparatos conectados\n");
            return;
        }
    }
    const std::string accept = http::base64(http::sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").data(), 20);
    const std::string ok = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                           "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
    if (!http::sendAll(fd, ok.data(), ok.size())) return;
    http::setSocketTimeout(fd, 0);
    auto c = std::make_shared<Conn>();
    c->fd = fd;
    c->ip = ipTexto(clientIp);
    c->remoto = remoto;
    // 1. Autenticación: el primer mensaje, a los 10 s como mucho.
    {
        if (http::waitReadable(fd, kAuthMs()) <= 0) return;
        std::string payload;
        int op = 0;
        if (!http::readWsFrame(fd, payload, op) || op != 0x1 || payload.size() > 4096) return;
        const json m = json::parse(payload, nullptr, false);
        const bool esAuth = m.is_object() && m.value("type", std::string()) == "auth";
        const std::string token = esAuth && m.contains("token") && m["token"].is_string() ? m["token"].get<std::string>() : "";
        const std::string pin = esAuth && m.contains("pin") && m["pin"].is_string() ? m["pin"].get<std::string>() : "";
        // Con 5 errores seguidos desde esa IP no se prueba nada más (el PIN es corto).
        const std::string ap = bloqueada(clientIp) ? std::string() : autenticar(token, pin, remoto);
        if (ap.empty()) {
            // Un token (o un PIN) errado cuenta como intento: 5 cada 10 min por IP.
            const bool otra = intentoPermitido(clientIp);
            log_.warn("control web: token" + std::string(remoto ? " o PIN" : "") + " inválido desde " + c->ip, -1);
            enviar(c, {{"type", "auth"},
                       {"ok", false},
                       {"error", !otra    ? "Demasiados intentos: esperá 10 minutos."
                                 : remoto ? "Este aparato no está emparejado, o falta el PIN para entrar desde afuera."
                                          : "Este aparato no está emparejado (o fue quitado)."}});
            http::writeWsFrame(fd, 0x8, "");
            return;
        }
        c->aparato = ap;
    }
    {
        std::string id;
        if (!ba::security::randomSecretHex(8, id)) return;
        c->id = "c" + id;
        std::lock_guard<std::mutex> l(mu_);
        conns_.push_back(c);
    }
    log_.info("control web: " + c->aparato + " conectado desde " + c->ip + (remoto ? " (afuera)" : "") + " [" + c->id + "]");
    // La app contesta el auth (con el perfil y las herramientas) cuando se entera.
    alApp_({{"tipo", "conexion"},
            {"conexion", c->id},
            {"aparato", c->aparato},
            {"conectado", true},
            {"ip", c->ip},
            {"lugar", remoto ? "afuera" : "sala"}});
    // 2. El lazo.
    std::vector<std::int64_t> ventana;
    while (true) {
        const int r = http::waitReadable(fd, kLatidoMs());
        if (r <= 0) break; // sin latido: se da por caída
        std::string payload;
        int op = 0;
        if (!http::readWsFrame(fd, payload, op)) break;
        if (op == 0x8) break;
        if (op == 0x9) {
            std::lock_guard<std::mutex> l(c->sendLock);
            http::writeWsFrame(fd, 0xA, payload);
            continue;
        }
        if (op != 0x1 || payload.size() > kMaxMensaje) continue;
        const std::int64_t ahora = ahoraMs();
        ventana.erase(std::remove_if(ventana.begin(), ventana.end(), [&](std::int64_t t) { return ahora - t > 1000; }),
                      ventana.end());
        if (static_cast<int>(ventana.size()) >= kMensajesPorSegundo()) {
            log_.warn("control web: " + c->aparato + " manda demasiado; se corta", -1);
            break;
        }
        ventana.push_back(ahora);
        const json m = json::parse(payload, nullptr, false);
        if (!m.is_object()) continue;
        const std::string t = m.value("type", std::string());
        if (t == "ping") {
            enviar(c, {{"type", "pong"}});
            continue;
        }
        if (!kTipos.count(t)) continue;
        // Se reenvía solo lo que la app entiende, con los campos que espera.
        json limpio{{"type", t}};
        if (m.contains("id") && (m["id"].is_number() || m["id"].is_string())) limpio["id"] = m["id"];
        if (t == "llamar") {
            if (!m.contains("herramienta") || !m["herramienta"].is_string()) continue;
            limpio["herramienta"] = m["herramienta"];
            limpio["args"] = m.contains("args") && m["args"].is_object() ? m["args"] : json::object();
            if (m.contains("confirmado") && m["confirmado"].is_boolean()) limpio["confirmado"] = m["confirmado"];
        } else if (t == "mirar") {
            json temas = json::array();
            if (m.contains("temas") && m["temas"].is_array())
                for (const auto& x : m["temas"])
                    if (x.is_string() && temas.size() < 32) temas.push_back(x);
            limpio["temas"] = temas;
        } else if (t == "preferencias") {
            if (!m.contains("datos") || !m["datos"].is_object() || m["datos"].dump().size() > 8192) continue;
            limpio["datos"] = m["datos"];
        }
        alApp_({{"tipo", "entrada"}, {"conexion", c->id}, {"aparato", c->aparato}, {"msg", limpio}});
    }
    {
        std::lock_guard<std::mutex> l(mu_);
        conns_.erase(std::remove(conns_.begin(), conns_.end(), c), conns_.end());
    }
    {
        std::lock_guard<std::mutex> l(c->sendLock);
        c->fd = -1;
    }
    alApp_({{"tipo", "conexion"}, {"conexion", c->id}, {"aparato", c->aparato}, {"conectado", false}, {"ip", c->ip}});
    log_.info("control web: " + c->aparato + " desconectado [" + c->id + "]");
}

bool ControlWebBorde::admin(long long fd, const std::string& method, const std::string& path, const std::string& body) {
    if (path.rfind("/api/v1/control/", 0) != 0) return false;
    const std::string que = path.substr(std::string("/api/v1/control/").size());
    const json r = json::parse(body.empty() ? "{}" : body, nullptr, false);
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
    if (que == "enviar" || que == "cortar") {
        std::set<std::string> ids;
        if (r.contains("conexion") && r["conexion"].is_string()) ids.insert(r["conexion"].get<std::string>());
        if (r.contains("conexiones") && r["conexiones"].is_array())
            for (const auto& x : r["conexiones"])
                if (x.is_string()) ids.insert(x.get<std::string>());
        const std::string ap = r.value("aparato", std::string());
        std::vector<std::shared_ptr<Conn>> a;
        {
            std::lock_guard<std::mutex> l(mu_);
            for (const auto& c : conns_)
                if (ids.count(c->id) || (!ap.empty() && c->aparato == ap)) a.push_back(c);
        }
        if (que == "enviar") {
            const json msg = r.value("msg", json::object());
            for (const auto& c : a) enviar(c, msg);
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
