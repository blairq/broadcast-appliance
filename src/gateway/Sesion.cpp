// ============================================================================
// src/gateway/Sesion.cpp — ver Sesion.hpp
// ============================================================================

#include "Sesion.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "Botonera.hpp" // sha256Hex
#include "HttpIo.hpp"
#include "Log.hpp"

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
    if (termina(".svg")) return "image/svg+xml";
    if (termina(".png")) return "image/png";
    return "application/octet-stream";
}

constexpr int kMaxConexiones = 4096;    // el límite técnico (docs/SESION-REMOTA.md §7)
constexpr int kMensajesPorSegundo = 20;
constexpr int kLatidoMs = 30000;
constexpr int kAuthMs = 15000;
constexpr int kIntentosPorIp = 10;      // cada 10 minutos
constexpr int kFallasParaBloquear = 5;
constexpr std::int64_t kBloqueoMs = 15 * 60 * 1000;
constexpr std::size_t kMaxFoto = 96 * 1024;
// Lo que la página puede mandar; lo demás se ignora. Las de la palabra y el voto
// se agregan en sus fases (R2, R4): la app decide si las acepta.
const std::set<std::string> kTipos{"hola", "palabra", "voto"};

} // namespace

SesionBorde::SesionBorde(std::string webroot, Log& log, Evento alApp)
    : webroot_(std::move(webroot)), log_(log), alApp_(std::move(alApp)) {}

bool SesionBorde::origenValido(const std::string& headers) {
    // Puede venir por el túnel (Cloudflare): ahí también Origin tiene que ser el
    // Host que pidió. Sin Origin no es un navegador de otra página.
    const std::string origin = http::getHeader(headers, "Origin");
    if (origin.empty()) return true;
    const std::string host = minus(http::getHeader(headers, "Host"));
    std::string o = minus(origin);
    const auto p = o.find("://");
    if (p == std::string::npos) return false;
    o = o.substr(p + 3);
    return !host.empty() && o == host;
}

bool SesionBorde::servir(long long fd, const std::string& rel) {
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
    // CSP estricta; la cámara y el micrófono solo para esta página.
    http::sendResponse(fd, 200, tipoDe(rel), ss.str(),
                       "Cache-Control: no-cache\r\n"
                       "Content-Security-Policy: default-src 'self'; connect-src 'self' ws: wss:; "
                       "img-src 'self' data: blob:; media-src 'self' blob: mediastream:; "
                       "frame-ancestors 'none'; base-uri 'none'; form-action 'none'\r\n"
                       "Permissions-Policy: camera=(self), microphone=(self)\r\n"
                       "X-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n");
    return true;
}

bool SesionBorde::intentoPermitido(const std::string& ip) {
    std::lock_guard<std::mutex> l(mu_);
    auto& v = intentos_[ip];
    const std::int64_t ahora = ahoraMs();
    v.erase(std::remove_if(v.begin(), v.end(), [&](std::int64_t t) { return ahora - t > 10 * 60 * 1000; }), v.end());
    if (static_cast<int>(v.size()) >= kIntentosPorIp) return false;
    v.push_back(ahora);
    return true;
}

void SesionBorde::ponerCredenciales(const json& participantes) {
    std::lock_guard<std::mutex> l(mu_);
    std::map<std::string, Credencial> nuevas;
    for (const auto& p : participantes) {
        if (!p.is_object()) continue;
        const std::string id = p.value("id", std::string());
        if (id.empty() || p.value("hash_token", std::string()).size() != 64 ||
            p.value("hash_pin", std::string()).size() != 64)
            continue;
        Credencial c;
        c.nombre = p.value("nombre", std::string());
        c.banca = p.value("banca", std::string());
        c.sal = p.value("sal", std::string());
        c.hashToken = p.value("hash_token", std::string());
        c.hashPin = p.value("hash_pin", std::string());
        c.usuario = minus(p.value("usuario", std::string()));
        c.hashClave = p.value("hash_clave", std::string());
        // Las fallas y el bloqueo sobreviven a que la app reenvíe la lista.
        auto it = credenciales_.find(id);
        if (it != credenciales_.end() && it->second.hashToken == c.hashToken) {
            c.fallas = it->second.fallas;
            c.bloqueadoHastaMs = it->second.bloqueadoHastaMs;
        }
        nuevas[id] = std::move(c);
    }
    credenciales_ = std::move(nuevas);
}

bool SesionBorde::bloqueado(const std::string& id) const {
    std::lock_guard<std::mutex> l(mu_);
    auto it = credenciales_.find(id);
    return it != credenciales_.end() && it->second.bloqueadoHastaMs > ahoraMs();
}

void SesionBorde::anotarPinMal(const std::string& id) {
    bool seBloqueo = false;
    {
        std::lock_guard<std::mutex> l(mu_);
        auto it = credenciales_.find(id);
        if (it == credenciales_.end()) return;
        if (++it->second.fallas >= kFallasParaBloquear) {
            it->second.fallas = 0;
            it->second.bloqueadoHastaMs = ahoraMs() + kBloqueoMs;
            seBloqueo = true;
        }
    }
    if (seBloqueo) {
        log_.warn("sesion: " + id + " bloqueado 15 min por PIN errado", -1);
        alApp_({{"tipo", "bloqueo"}, {"id", id}, {"minutos", kBloqueoMs / 60000}});
    }
}

std::string SesionBorde::autenticar(const std::string& id, const std::string& token, const std::string& pin,
                                    bool& pinMal) {
    pinMal = false;
    if (token.size() != 64 || pin.empty() || pin.size() > 12) return {};
    std::lock_guard<std::mutex> l(mu_);
    auto it = credenciales_.find(id);
    if (it == credenciales_.end()) return {};
    const Credencial& c = it->second;
    // El token primero: sin él no se cuenta como PIN errado (no se puede
    // bloquear a alguien solo sabiendo su id).
    if (!igualesTiempoConstante(BotoneraBorde::sha256Hex(c.sal + token), c.hashToken)) return {};
    if (!igualesTiempoConstante(BotoneraBorde::sha256Hex(c.sal + ":" + pin + ":" + token), c.hashPin)) {
        pinMal = true;
        return {};
    }
    return id;
}

std::string SesionBorde::autenticarUsuario(const std::string& usuario, const std::string& clave, std::string& id,
                                           bool& claveMal) {
    claveMal = false;
    id.clear();
    if (usuario.empty() || usuario.size() > 40 || clave.empty() || clave.size() > 64) return {};
    const std::string u = minus(usuario);
    std::lock_guard<std::mutex> l(mu_);
    for (const auto& [cid, c] : credenciales_)
        if (!c.usuario.empty() && c.usuario == u && c.hashClave.size() == 64) {
            id = cid;
            if (igualesTiempoConstante(BotoneraBorde::sha256Hex(c.sal + ":clave:" + clave), c.hashClave)) return cid;
            claveMal = true;
            return {};
        }
    return {};
}

bool SesionBorde::atender(long long fd, const std::string& method, const std::string& path, const std::string& query,
                          const std::string& headers, std::uint32_t clientIp) {
    const bool ws = path == "/ws" && http::parseQuery(query).count("sesion") > 0;
    const bool pagina = path == "/sesion" || path.rfind("/sesion/", 0) == 0;
    if (!ws && !pagina) return false;
    // La IP real si viene por el túnel (para el registro y los límites).
    std::string ip = http::getHeader(headers, "CF-Connecting-IP");
    // En la sala (red local) o afuera (por el túnel o una IP pública): la página lo
    // usa para no reproducir la sala en un celular que está adentro (acoplaría).
    const bool tunel = !ip.empty();
    if (ip.empty() || ip.size() > 45) ip = ipTexto(clientIp);
    const std::string lugar = !tunel && BotoneraBorde::ipLocal(clientIp) ? "sala" : "afuera";
    if (!origenValido(headers)) {
        log_.warn("sesion: origen ajeno desde " + ip, -1);
        http::sendResponse(fd, 403, "text/plain", "origen invalido\n");
        return true;
    }
    if (pagina) {
        if (method != "GET") return http::sendResponse(fd, 405, "text/plain", "\n"), true;
        if (path == "/sesion") return http::sendResponse(fd, 301, "text/plain", "", "Location: /sesion/\r\n"), true;
        std::string rel = path.substr(std::string("/sesion/").size());
        if (rel.empty()) rel = "index.html";
        return servir(fd, rel);
    }
    const std::string key = http::getHeader(headers, "Sec-WebSocket-Key");
    if (key.empty()) return http::sendResponse(fd, 400, "text/plain", "falta Sec-WebSocket-Key\n"), true;
    {
        std::lock_guard<std::mutex> l(mu_);
        if (static_cast<int>(conns_.size()) >= kMaxConexiones)
            return http::sendResponse(fd, 503, "text/plain", "sesion llena\n"), true;
    }
    const std::string accept = http::base64(http::sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").data(), 20);
    const std::string ok = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                           "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
    if (!http::sendAll(fd, ok.data(), ok.size())) return true;
    http::setSocketTimeout(fd, 0);
    wsLoop(fd, ip, lugar);
    return true;
}

void SesionBorde::enviar(const std::shared_ptr<Conn>& c, const json& msg) {
    std::lock_guard<std::mutex> l(c->sendLock);
    if (c->fd >= 0) http::writeWsFrame(c->fd, 0x1, msg.dump());
}

void SesionBorde::wsLoop(long long fd, const std::string& ip, const std::string& lugar) {
    auto c = std::make_shared<Conn>();
    c->fd = fd;
    c->ip = ip;
    c->lugar = lugar;
    // 1. Autenticación: el primer mensaje, a los 15 s como mucho.
    {
        if (http::waitReadable(fd, kAuthMs) <= 0) return;
        std::string payload;
        int op = 0;
        if (!http::readWsFrame(fd, payload, op) || op != 0x1) return;
        const json m = json::parse(payload, nullptr, false);
        const auto rechazar = [&](const std::string& error) {
            enviar(c, {{"type", "auth"}, {"ok", false}, {"error", error}});
            http::writeWsFrame(fd, 0x8, "");
        };
        if (!m.is_object() || m.value("type", std::string()) != "auth") return rechazar("Mensaje inválido.");
        const bool conUsuario = m.contains("u");
        std::string id = m.value("i", std::string());
        if (!intentoPermitido(ip)) {
            log_.warn("sesion: demasiados intentos desde " + ip, -1);
            return rechazar("Demasiados intentos desde esta conexión. Esperá 10 minutos.");
        }
        bool pinMal = false;
        std::string quien;
        if (conUsuario) {
            quien = autenticarUsuario(m.value("u", std::string()), m.value("clave", std::string()), id, pinMal);
            if (!id.empty() && bloqueado(id))
                return rechazar("Por contraseña errada varias veces, tu acceso está bloqueado 15 minutos.");
            if (quien.empty() && !pinMal) {
                alApp_({{"tipo", "intento"}, {"id", ""}, {"ip", ip}, {"motivo", "usuario"}});
                return rechazar("Usuario o contraseña incorrectos.");
            }
        } else {
            if (bloqueado(id)) return rechazar("Por PIN errado varias veces, tu acceso está bloqueado 15 minutos.");
            quien = autenticar(id, m.value("t", std::string()), m.value("pin", std::string()), pinMal);
        }
        if (quien.empty()) {
            if (pinMal) {
                anotarPinMal(id);
                alApp_({{"tipo", "intento"}, {"id", id}, {"ip", ip}, {"motivo", "pin"}});
                return rechazar(conUsuario ? "Usuario o contraseña incorrectos." : "El PIN no es correcto.");
            }
            alApp_({{"tipo", "intento"}, {"id", id}, {"ip", ip}, {"motivo", "link"}});
            return rechazar("Este link no es válido (o fue revocado). Pedile uno nuevo a la secretaría.");
        }
        c->id = quien;
    }
    std::shared_ptr<Conn> vieja;
    json datos;
    {
        std::lock_guard<std::mutex> l(mu_);
        auto it = conns_.find(c->id);
        if (it != conns_.end()) vieja = it->second;
        conns_[c->id] = c;
        auto cr = credenciales_.find(c->id);
        if (cr != credenciales_.end()) {
            cr->second.fallas = 0;
            datos = {{"nombre", cr->second.nombre}, {"banca", cr->second.banca}};
        }
    }
    if (vieja) { // un integrante, una conexión: la nueva corta la vieja
        enviar(vieja, {{"type", "cortado"}, {"motivo", "Entraste desde otro lado: esta ventana se cerró."}});
        std::lock_guard<std::mutex> l(vieja->sendLock);
        if (vieja->fd >= 0) http::shutdownFd(vieja->fd);
    }
    enviar(c, {{"type", "auth"}, {"ok", true}, {"id", c->id}, {"nombre", datos.value("nombre", std::string())},
               {"banca", datos.value("banca", std::string())}, {"lugar", c->lugar}});
    log_.info("sesion: " + c->id + " conectado desde " + c->ip);
    alApp_({{"tipo", "conexion"}, {"id", c->id}, {"conectado", true}, {"ip", c->ip}, {"lugar", c->lugar}});
    // 2. El lazo.
    std::vector<std::int64_t> ventana;
    while (true) {
        const int r = http::waitReadable(fd, kLatidoMs);
        if (r <= 0) break; // sin latido: se da por caído
        std::string payload;
        int op = 0;
        if (!http::readWsFrame(fd, payload, op)) break;
        if (op == 0x8) break;
        if (op == 0x9) {
            std::lock_guard<std::mutex> l(c->sendLock);
            http::writeWsFrame(fd, 0xA, payload);
            continue;
        }
        if (op != 0x1 || payload.size() > kMaxFoto * 2) continue;
        const std::int64_t ahora = ahoraMs();
        ventana.erase(std::remove_if(ventana.begin(), ventana.end(), [&](std::int64_t t) { return ahora - t > 1000; }),
                      ventana.end());
        if (static_cast<int>(ventana.size()) >= kMensajesPorSegundo) {
            log_.warn("sesion: " + c->id + " manda demasiado; se corta", -1);
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
        if (t == "foto") {
            // La foto de su cámara: queda acá; la app la pide cuando la muestra.
            std::string jpeg = http::deBase64(m.value("jpeg", std::string()));
            if (jpeg.size() >= 4 && jpeg.size() <= kMaxFoto && static_cast<unsigned char>(jpeg[0]) == 0xFF &&
                static_cast<unsigned char>(jpeg[1]) == 0xD8) {
                std::lock_guard<std::mutex> l(mu_);
                fotos_[c->id] = std::move(jpeg);
            }
            continue;
        }
        if (!kTipos.count(t)) continue;
        // Se reenvía solo lo que la app entiende, con los campos que espera.
        json limpio{{"type", t}};
        for (const char* k : {"accion", "opcion", "votacion"})
            if (m.contains(k) && (m[k].is_number() || m[k].is_string() || m[k].is_boolean())) limpio[k] = m[k];
        alApp_({{"tipo", "entrada"}, {"id", c->id}, {"msg", limpio}});
    }
    bool eraLaActual = false;
    {
        std::lock_guard<std::mutex> l(mu_);
        auto it = conns_.find(c->id);
        if (it != conns_.end() && it->second == c) {
            conns_.erase(it);
            eraLaActual = true;
        }
    }
    {
        std::lock_guard<std::mutex> l(c->sendLock);
        c->fd = -1;
    }
    // Si lo reemplazó otra conexión suya, no se avisa que se fue.
    if (eraLaActual) {
        alApp_({{"tipo", "conexion"}, {"id", c->id}, {"conectado", false}, {"ip", c->ip}});
        log_.info("sesion: " + c->id + " desconectado");
    }
}

bool SesionBorde::admin(long long fd, const std::string& method, const std::string& path, const std::string& query,
                        const std::string& body) {
    if (path.rfind("/api/v1/sesion/", 0) != 0) return false;
    const std::string que = path.substr(std::string("/api/v1/sesion/").size());
    if (method == "GET" && que == "estado") {
        json cs = json::array();
        std::lock_guard<std::mutex> l(mu_);
        for (const auto& [id, c] : conns_)
            cs.push_back({{"id", id}, {"ip", c->ip}, {"lugar", c->lugar}, {"foto", fotos_.count(id) > 0}});
        http::sendResponse(fd, 200, "application/json", json{{"conexiones", cs}, {"credenciales", credenciales_.size()}}.dump());
        return true;
    }
    if (method == "GET" && que == "foto") {
        const auto q = http::parseQuery(query);
        const std::string id = q.count("id") ? http::urlDecode(q.at("id")) : std::string();
        std::string jpeg;
        {
            std::lock_guard<std::mutex> l(mu_);
            auto it = fotos_.find(id);
            if (it != fotos_.end()) jpeg = it->second;
        }
        if (jpeg.empty()) return http::sendResponse(fd, 404, "text/plain", "sin foto\n"), true;
        http::sendResponse(fd, 200, "image/jpeg", jpeg, "Cache-Control: no-store\r\n");
        return true;
    }
    const json r = json::parse(body.empty() ? "{}" : body, nullptr, false);
    if (method != "POST" || !r.is_object()) {
        http::sendResponse(fd, 400, "application/json", json{{"error", "bad_request"}}.dump());
        return true;
    }
    if (que == "credenciales") {
        ponerCredenciales(r.value("participantes", json::array()));
        // Lo revocado se corta ya.
        std::vector<std::shared_ptr<Conn>> cortar;
        {
            std::lock_guard<std::mutex> l(mu_);
            for (const auto& [id, c] : conns_)
                if (!credenciales_.count(id)) cortar.push_back(c);
        }
        for (const auto& c : cortar) {
            enviar(c, {{"type", "cortado"}, {"motivo", "Tu acceso fue revocado."}});
            std::lock_guard<std::mutex> l(c->sendLock);
            if (c->fd >= 0) http::shutdownFd(c->fd);
        }
        http::sendResponse(fd, 200, "application/json", json{{"ok", true}}.dump());
        return true;
    }
    if (que == "enviar" || que == "cortar") {
        const std::string id = r.value("id", std::string());
        std::vector<std::shared_ptr<Conn>> a;
        {
            std::lock_guard<std::mutex> l(mu_);
            for (const auto& [cid, c] : conns_)
                if (id == "*" || cid == id) a.push_back(c);
            if (que == "cortar") fotos_.erase(id);
        }
        for (const auto& c : a) {
            if (que == "enviar") {
                enviar(c, r.value("msg", json::object()));
            } else {
                enviar(c, {{"type", "cortado"}, {"motivo", r.value("motivo", std::string("La presidencia te desconectó."))}});
                std::lock_guard<std::mutex> l(c->sendLock);
                if (c->fd >= 0) http::shutdownFd(c->fd);
            }
        }
        http::sendResponse(fd, 200, "application/json", json{{"ok", true}, {"entregado", a.size()}}.dump());
        return true;
    }
    http::sendResponse(fd, 404, "application/json", json{{"error", "not_found"}}.dump());
    return true;
}

void SesionBorde::cerrar() {
    std::vector<std::shared_ptr<Conn>> a;
    {
        std::lock_guard<std::mutex> l(mu_);
        for (const auto& [id, c] : conns_) a.push_back(c);
    }
    for (const auto& c : a) {
        std::lock_guard<std::mutex> l(c->sendLock);
        if (c->fd >= 0) http::shutdownFd(c->fd);
    }
}

} // namespace ba::gateway
