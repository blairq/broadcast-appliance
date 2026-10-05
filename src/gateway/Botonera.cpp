// ============================================================================
// src/gateway/Botonera.cpp — ver Botonera.hpp
// ============================================================================

#include "Botonera.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include <mbedtls/sha256.h>

#include "HttpIo.hpp"
#include "Log.hpp"
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

const char* tipoDe(const std::string& rel) {
    const auto termina = [&](const char* e) {
        const std::string x(e);
        return rel.size() >= x.size() && rel.compare(rel.size() - x.size(), x.size(), x) == 0;
    };
    if (termina(".html")) return "text/html; charset=utf-8";
    if (termina(".js")) return "text/javascript; charset=utf-8";
    if (termina(".css")) return "text/css; charset=utf-8";
    if (termina(".json") || termina(".webmanifest")) return "application/manifest+json";
    if (termina(".svg")) return "image/svg+xml";
    if (termina(".png")) return "image/png";
    return "application/octet-stream";
}

constexpr int kMaxConexiones = 32;
constexpr int kMensajesPorSegundo = 60;
constexpr int kLatidoMs = 30000;
constexpr int kAuthMs = 10000;
constexpr std::int64_t kSolicitudMs = 5 * 60 * 1000;
const std::set<std::string> kTipos{"hola",      "presionar", "soltar", "valor",  "girar",
                                   "pagina",    "confirmar", "cancelar", "editar"};

} // namespace

BotoneraBorde::BotoneraBorde(std::string webroot, Log& log, Evento alApp)
    : webroot_(std::move(webroot)), log_(log), alApp_(std::move(alApp)) {}

std::string BotoneraBorde::sha256Hex(const std::string& s) {
    unsigned char out[32];
    mbedtls_sha256(reinterpret_cast<const unsigned char*>(s.data()), s.size(), out, 0);
    static const char* h = "0123456789abcdef";
    std::string r;
    for (unsigned char c : out) {
        r += h[c >> 4];
        r += h[c & 15];
    }
    return r;
}

bool BotoneraBorde::ipLocal(std::uint32_t ip) {
    const auto* b = reinterpret_cast<const unsigned char*>(&ip);
    return b[0] == 10 || b[0] == 127 || (b[0] == 172 && b[1] >= 16 && b[1] <= 31) || (b[0] == 192 && b[1] == 168) ||
           (b[0] == 169 && b[1] == 254);
}

bool BotoneraBorde::origenValido(const std::string& headers) {
    // Lo que viene por Cloudflare no es la red local.
    if (!http::getHeader(headers, "CF-Connecting-IP").empty() || !http::getHeader(headers, "CF-Ray").empty())
        return false;
    const std::string origin = http::getHeader(headers, "Origin");
    if (origin.empty()) return true; // no es un navegador de otra página (curl, app nativa)
    const std::string host = minus(http::getHeader(headers, "Host"));
    std::string o = minus(origin);
    const auto p = o.find("://");
    if (p == std::string::npos) return false;
    o = o.substr(p + 3);
    return !host.empty() && o == host;
}

bool BotoneraBorde::servir(long long fd, const std::string& rel) {
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
    // CSP estricta: solo lo nuestro, nada en línea, sin marcos ajenos.
    http::sendResponse(fd, 200, tipoDe(rel), ss.str(),
                       "Cache-Control: no-cache\r\n"
                       "Content-Security-Policy: default-src 'self'; connect-src 'self' ws: wss:; img-src 'self' data:; "
                       "frame-ancestors 'none'; base-uri 'none'; form-action 'none'\r\n"
                       "X-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n");
    return true;
}

bool BotoneraBorde::emparejarPermitido(std::uint32_t ip) {
    std::lock_guard<std::mutex> l(mu_);
    auto& v = intentos_[ip];
    const std::int64_t ahora = ahoraMs();
    v.erase(std::remove_if(v.begin(), v.end(), [&](std::int64_t t) { return ahora - t > 10 * 60 * 1000; }), v.end());
    if (v.size() >= 5) return false;
    v.push_back(ahora);
    return true;
}

std::string BotoneraBorde::autenticar(const std::string& token, bool soloHttp) const {
    if (token.size() != 64) return {};
    std::lock_guard<std::mutex> l(mu_);
    std::string encontrado;
    // Se recorren todas (sin cortar) para no revelar cuál coincidió por el tiempo.
    for (const auto& [id, c] : credenciales_)
        if (igualesTiempoConstante(sha256Hex(c.sal + token), c.hash) && (!soloHttp || c.http)) encontrado = id;
    return encontrado;
}

// La API remota HTTP: una celda por URL, para que el «Generic HTTP» de Companion
// (o cualquier cosa que haga un POST) la apriete sin código. El borde solo
// autentica y reenvía; qué hace la celda y si ese perfil se puede usar lo decide la app.
bool BotoneraBorde::apiRemota(long long fd, const std::string& method, const std::string& path,
                              const std::string& headers, std::uint32_t clientIp) {
    // /api/botonera/<perfil>/pagina/<pagina>/celda/<x>/<y>/<accion>
    std::vector<std::string> partes;
    for (std::size_t i = std::string("/api/botonera/").size(); i <= path.size();) {
        const std::size_t j = std::min(path.find('/', i), path.size());
        partes.push_back(http::urlDecode(path.substr(i, j - i)));
        i = j + 1;
    }
    if (partes.size() != 7 || partes[1] != "pagina" || partes[3] != "celda") return false;
    if (method != "POST" && method != "PUT") {
        http::sendResponse(fd, 405, "application/json", json{{"error", "usá POST"}}.dump(), "Allow: POST\r\n");
        return true;
    }
    const std::string accion = partes[6];
    if (accion != "presionar" && accion != "soltar" && accion != "tocar") {
        http::sendResponse(fd, 400, "application/json", json{{"error", "acción: presionar, soltar o tocar"}}.dump());
        return true;
    }
    int x = -1, y = -1;
    try {
        x = std::stoi(partes[4]);
        y = std::stoi(partes[5]);
    } catch (...) {
    }
    if (x < 0 || y < 0 || x > 63 || y > 63) {
        http::sendResponse(fd, 400, "application/json", json{{"error", "celda inválida"}}.dump());
        return true;
    }
    std::string auth = http::getHeader(headers, "Authorization");
    const std::string pre = "Bearer ";
    const std::string token = auth.rfind(pre, 0) == 0 ? auth.substr(pre.size()) : std::string();
    const std::string dispositivo = autenticar(token, true);
    if (dispositivo.empty()) {
        // Un token mal puesto cuenta como intento (5 cada 10 min por IP).
        if (!emparejarPermitido(clientIp)) {
            http::sendResponse(fd, 429, "application/json", json{{"error", "demasiados intentos; esperá 10 minutos"}}.dump());
            return true;
        }
        log_.warn("botonera: API remota sin token válido desde " + ipTexto(clientIp), -1);
        http::sendResponse(fd, 401, "application/json", json{{"error", "token inválido o API remota apagada"}}.dump(),
                           "WWW-Authenticate: Bearer\r\n");
        return true;
    }
    alApp_({{"tipo", "remota"},
            {"dispositivo", dispositivo},
            {"perfil", partes[0]},
            {"pagina", partes[2]},
            {"x", x},
            {"y", y},
            {"accion", accion},
            {"ip", ipTexto(clientIp)}});
    http::sendResponse(fd, 202, "application/json", json{{"ok", true}}.dump());
    return true;
}

bool BotoneraBorde::atender(long long fd, const std::string& method, const std::string& path, const std::string& query,
                            const std::string& headers, const std::string& body, std::uint32_t clientIp) {
    const bool ws = path == "/ws" && http::parseQuery(query).count("botonera") > 0;
    const bool pagina = path == "/botonera" || path.rfind("/botonera/", 0) == 0;
    const bool api = path == "/api/botonera/emparejar";
    const bool remota = !api && path.rfind("/api/botonera/", 0) == 0;
    if (!ws && !pagina && !api && !remota) return false;
    if (!ipLocal(clientIp) || !origenValido(headers)) {
        log_.warn("botonera: rechazado " + ipTexto(clientIp) + " " + path + " (fuera de la red local u origen ajeno)", -1);
        http::sendResponse(fd, 403, "text/plain", "solo desde la red local\n");
        return true;
    }
    if (remota) {
        if (!apiRemota(fd, method, path, headers, clientIp))
            http::sendResponse(fd, 404, "application/json", json{{"error", "ruta desconocida"}}.dump());
        return true;
    }
    if (pagina) {
        if (method != "GET") return http::sendResponse(fd, 405, "text/plain", "\n"), true;
        if (path == "/botonera") return http::sendResponse(fd, 301, "text/plain", "", "Location: /botonera/\r\n"), true;
        std::string rel = path.substr(std::string("/botonera/").size());
        if (rel.empty()) rel = "index.html";
        return servir(fd, rel);
    }
    if (api && method == "POST") {
        if (!emparejarPermitido(clientIp)) {
            http::sendResponse(fd, 429, "application/json", json{{"error", "demasiados intentos; esperá 10 minutos"}}.dump());
            return true;
        }
        const json r = json::parse(body, nullptr, false);
        const std::string codigo = r.is_object() ? r.value("codigo", std::string()) : std::string();
        if (codigo.size() != 32) {
            http::sendResponse(fd, 400, "application/json", json{{"error", "código inválido"}}.dump());
            return true;
        }
        std::string sol;
        if (!ba::security::randomSecretHex(16, sol)) return http::sendResponse(fd, 500, "text/plain", "\n"), true;
        {
            std::lock_guard<std::mutex> l(mu_);
            const std::int64_t ahora = ahoraMs();
            for (auto it = solicitudes_.begin(); it != solicitudes_.end();)
                it = ahora - it->second.ms > kSolicitudMs ? solicitudes_.erase(it) : std::next(it);
            Solicitud s;
            s.ip = ipTexto(clientIp);
            s.ms = ahora;
            solicitudes_[sol] = s;
        }
        // La app valida el código (lo emitió ella) y le pregunta a una persona.
        std::string nombre = r.value("nombre", std::string("Tablet"));
        if (nombre.size() > 60) nombre.resize(60);
        alApp_({{"tipo", "solicitud"}, {"solicitud", sol}, {"codigo", codigo}, {"nombre", nombre}, {"ip", ipTexto(clientIp)}});
        http::sendResponse(fd, 200, "application/json", json{{"solicitud", sol}}.dump());
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
                    out["dispositivo"] = it->second.dispositivo;
                    out["token"] = it->second.token;
                    solicitudes_.erase(it); // el token se entrega una sola vez
                } else if (it->second.estado != "pendiente") {
                    solicitudes_.erase(it);
                }
            }
        }
        http::sendResponse(fd, 200, "application/json", out.dump(), "Cache-Control: no-store\r\n");
        return true;
    }
    if (ws) {
        wsLoop(fd, headers, clientIp);
        return true;
    }
    http::sendResponse(fd, 405, "text/plain", "\n");
    return true;
}

void BotoneraBorde::enviar(const std::shared_ptr<Conn>& c, const json& msg) {
    std::lock_guard<std::mutex> l(c->sendLock);
    if (c->fd >= 0) http::writeWsFrame(c->fd, 0x1, msg.dump());
}

void BotoneraBorde::wsLoop(long long fd, const std::string& headers, std::uint32_t clientIp) {
    const std::string key = http::getHeader(headers, "Sec-WebSocket-Key");
    if (key.empty()) {
        http::sendResponse(fd, 400, "text/plain", "falta Sec-WebSocket-Key\n");
        return;
    }
    {
        std::lock_guard<std::mutex> l(mu_);
        if (static_cast<int>(conns_.size()) >= kMaxConexiones) {
            http::sendResponse(fd, 503, "text/plain", "demasiadas botoneras\n");
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
    // 1. Autenticación: el primer mensaje, a los 10 s como mucho.
    {
        if (http::waitReadable(fd, kAuthMs) <= 0) return;
        std::string payload;
        int op = 0;
        if (!http::readWsFrame(fd, payload, op) || op != 0x1) return;
        const json m = json::parse(payload, nullptr, false);
        const std::string dev = m.is_object() && m.value("type", std::string()) == "auth"
                                    ? autenticar(m.value("token", std::string()))
                                    : std::string();
        if (dev.empty()) {
            log_.warn("botonera: token inválido desde " + c->ip, -1);
            enviar(c, {{"type", "auth"}, {"ok", false}, {"error", "Este aparato no está emparejado (o fue revocado)."}});
            http::writeWsFrame(fd, 0x8, "");
            return;
        }
        c->dispositivo = dev;
    }
    {
        std::lock_guard<std::mutex> l(mu_);
        conns_.push_back(c);
    }
    enviar(c, {{"type", "auth"}, {"ok", true}, {"dispositivo", c->dispositivo}});
    log_.info("botonera: " + c->dispositivo + " conectada desde " + c->ip);
    alApp_({{"tipo", "conexion"}, {"dispositivo", c->dispositivo}, {"conectado", true}, {"ip", c->ip}});
    alApp_({{"tipo", "entrada"}, {"dispositivo", c->dispositivo}, {"msg", {{"type", "hola"}}}});
    // 2. El lazo.
    std::vector<std::int64_t> ventana;
    while (true) {
        const int r = http::waitReadable(fd, kLatidoMs);
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
        if (op != 0x1 || payload.size() > 16384) continue;
        const std::int64_t ahora = ahoraMs();
        ventana.erase(std::remove_if(ventana.begin(), ventana.end(), [&](std::int64_t t) { return ahora - t > 1000; }),
                      ventana.end());
        if (static_cast<int>(ventana.size()) >= kMensajesPorSegundo) {
            log_.warn("botonera: " + c->dispositivo + " manda demasiado; se corta", -1);
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
        for (const char* k : {"x", "y", "v", "delta", "id"})
            if (m.contains(k) && (m[k].is_number() || m[k].is_string())) limpio[k] = m[k];
        if (t == "editar" && m.contains("celda") && m["celda"].is_object()) limpio["celda"] = m["celda"];
        alApp_({{"tipo", "entrada"}, {"dispositivo", c->dispositivo}, {"msg", limpio}});
    }
    {
        std::lock_guard<std::mutex> l(mu_);
        conns_.erase(std::remove(conns_.begin(), conns_.end(), c), conns_.end());
    }
    {
        std::lock_guard<std::mutex> l(c->sendLock);
        c->fd = -1;
    }
    alApp_({{"tipo", "conexion"}, {"dispositivo", c->dispositivo}, {"conectado", false}, {"ip", c->ip}});
    log_.info("botonera: " + c->dispositivo + " desconectada");
}

bool BotoneraBorde::admin(long long fd, const std::string& method, const std::string& path, const std::string& body) {
    if (path.rfind("/api/v1/botonera/", 0) != 0) return false;
    const std::string que = path.substr(std::string("/api/v1/botonera/").size());
    const json r = json::parse(body.empty() ? "{}" : body, nullptr, false);
    if (method == "GET" && que == "estado") {
        json cs = json::array();
        std::lock_guard<std::mutex> l(mu_);
        for (const auto& c : conns_) cs.push_back({{"dispositivo", c->dispositivo}, {"ip", c->ip}});
        http::sendResponse(fd, 200, "application/json",
                           json{{"conexiones", cs}, {"credenciales", credenciales_.size()}, {"solicitudes", solicitudes_.size()}}.dump());
        return true;
    }
    if (method != "POST" || !r.is_object()) {
        http::sendResponse(fd, 400, "application/json", json{{"error", "bad_request"}}.dump());
        return true;
    }
    if (que == "credenciales") {
        std::vector<std::shared_ptr<Conn>> cortar;
        {
            std::lock_guard<std::mutex> l(mu_);
            credenciales_.clear();
            for (const auto& d : r.value("dispositivos", json::array()))
                if (d.is_object() && d.value("hash", std::string()).size() == 64)
                    credenciales_[d.value("id", std::string())] = {d.value("hash", std::string()), d.value("sal", std::string()),
                                                                   d.value("http", false)};
            // Lo revocado se corta ya.
            for (const auto& c : conns_)
                if (!credenciales_.count(c->dispositivo)) cortar.push_back(c);
        }
        for (const auto& c : cortar) {
            enviar(c, {{"type", "auth"}, {"ok", false}, {"error", "Este aparato fue revocado."}});
            std::lock_guard<std::mutex> l(c->sendLock);
            if (c->fd >= 0) http::shutdownFd(c->fd);
        }
        http::sendResponse(fd, 200, "application/json", json{{"ok", true}}.dump());
        return true;
    }
    if (que == "resolver") {
        std::lock_guard<std::mutex> l(mu_);
        auto it = solicitudes_.find(r.value("solicitud", std::string()));
        if (it == solicitudes_.end()) {
            http::sendResponse(fd, 404, "application/json", json{{"error", "not_found"}}.dump());
            return true;
        }
        it->second.estado = r.value("estado", std::string("rechazada"));
        it->second.dispositivo = r.value("dispositivo", std::string());
        it->second.token = r.value("token", std::string());
        it->second.error = r.value("error", std::string());
        http::sendResponse(fd, 200, "application/json", json{{"ok", true}}.dump());
        return true;
    }
    if (que == "enviar") {
        const std::string dev = r.value("dispositivo", std::string());
        std::vector<std::shared_ptr<Conn>> a;
        {
            std::lock_guard<std::mutex> l(mu_);
            for (const auto& c : conns_)
                if (c->dispositivo == dev) a.push_back(c);
        }
        for (const auto& c : a) enviar(c, r.value("msg", json::object()));
        http::sendResponse(fd, 200, "application/json", json{{"ok", true}, {"entregado", a.size()}}.dump());
        return true;
    }
    http::sendResponse(fd, 404, "application/json", json{{"error", "not_found"}}.dump());
    return true;
}

void BotoneraBorde::cerrar() {
    std::vector<std::shared_ptr<Conn>> a;
    {
        std::lock_guard<std::mutex> l(mu_);
        a = conns_;
    }
    for (const auto& c : a) {
        std::lock_guard<std::mutex> l(c->sendLock);
        if (c->fd >= 0) http::shutdownFd(c->fd);
    }
}

} // namespace ba::gateway
