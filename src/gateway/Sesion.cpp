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
#include "ajustes/Ajustes.hpp"

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

inline int kMaxConexiones() { return ba::ajustes::entero("sesion.maxConexiones", 4096); }    // el límite técnico (docs/funciones/SESION-REMOTA.md §7)
inline int kMensajesPorSegundo() { return ba::ajustes::entero("sesion.mensajesPorSegundo", 20); }
inline int kLatidoMs() { return ba::ajustes::sAMs("sesion.latidoS", 30); }
inline int kAuthMs() { return ba::ajustes::sAMs("sesion.authS", 15); }
inline int kIntentosPorIp() { return ba::ajustes::entero("sesion.intentosPorIp", 10); }      // cada 10 minutos
inline int kFallasParaBloquear() { return ba::ajustes::entero("sesion.fallasParaBloquear", 5); }
inline std::int64_t kBloqueoMs() { return ba::ajustes::entero64("sesion.bloqueoMin", 15) * 60 * 1000; }
inline std::size_t kMaxFoto() { return static_cast<std::size_t>(ba::ajustes::entero("sesion.fotoKB", 96)) * 1024; }
// Lo que la página puede mandar; lo demás se ignora. Las de la palabra y el voto
// se agregan en sus fases (R2, R4): la app decide si las acepta.
const std::set<std::string> kTipos{"hola", "palabra", "voto"};
// Lo que manda la consola de la presidencia (la app lo valida y lo hace).
const std::set<std::string> kTiposConsola{"hola",      "habilitar",  "deshabilitar", "expulsar", "dar_palabra",
                                          "terminar",  "quitar_fila", "sumar_tiempo", "siguiente", "votacion",
                                          "ptt",       "cast",        "foto_cuerpo",  "tiempo",
                                          "silenciar_todos", "hablar_presidencia", "debate"};

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

bool SesionBorde::servir(long long fd, const std::string& rel, const std::string& webroot) {
    std::error_code ec;
    const auto raiz = std::filesystem::weakly_canonical(webroot, ec);
    if (ec || webroot.empty()) return http::sendResponse(fd, 404, "text/plain", "no encontrado\n"), true;
    const auto destino = std::filesystem::weakly_canonical(std::filesystem::path(webroot) / http::urlDecode(rel), ec);
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
    if (static_cast<int>(v.size()) >= kIntentosPorIp()) return false;
    v.push_back(ahora);
    return true;
}

int SesionBorde::conectados() const {
    std::lock_guard<std::mutex> g(mu_);
    int n = 0;
    for (const auto& [id, c] : conns_)
        if (c && id.rfind("consola:", 0) != 0) ++n;
    return n;
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
        if (++it->second.fallas >= kFallasParaBloquear()) {
            it->second.fallas = 0;
            it->second.bloqueadoHastaMs = ahoraMs() + kBloqueoMs();
            seBloqueo = true;
        }
    }
    if (seBloqueo) {
        log_.warn("sesion: " + id + " bloqueado 15 min por PIN errado", -1);
        alApp_({{"tipo", "bloqueo"}, {"id", id}, {"minutos", kBloqueoMs() / 60000}});
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
                          const std::string& headers, std::uint32_t clientIp, bool externo) {
    const auto q = http::parseQuery(query);
    const bool wsConsola = path == "/ws" && q.count("presidencia") > 0;
    const bool ws = (path == "/ws" && q.count("sesion") > 0) || wsConsola;
    const bool pagina = path == "/sesion" || path.rfind("/sesion/", 0) == 0;
    const bool paginaConsola = path == "/presidencia" || path.rfind("/presidencia/", 0) == 0;
    if (!ws && !pagina && !paginaConsola) return false;
    // La IP real si viene por el túnel de Cloudflare (para el registro y los
    // límites). Por el de pinggy la cabecera no vale (la pone cualquiera): la IP
    // es la de la conexión, que ya trae la real si vino PROXY v1.
    std::string ip = externo ? std::string() : http::getHeader(headers, "CF-Connecting-IP");
    // En la sala (red local) o afuera (por un túnel o una IP pública): la página lo
    // usa para no reproducir la sala en un celular que está adentro (acoplaría).
    const bool tunel = externo || !ip.empty();
    if (ip.empty() || ip.size() > 45) ip = ipTexto(clientIp);
    const std::string lugar = !tunel && BotoneraBorde::ipLocal(clientIp) ? "sala" : "afuera";
    if (!origenValido(headers)) {
        log_.warn("sesion: origen ajeno desde " + ip, -1);
        http::sendResponse(fd, 403, "text/plain", "origen invalido\n");
        return true;
    }
    if (pagina || paginaConsola) {
        if (method != "GET") return http::sendResponse(fd, 405, "text/plain", "\n"), true;
        const std::string base = pagina ? "/sesion" : "/presidencia";
        if (path == base) return http::sendResponse(fd, 301, "text/plain", "", "Location: " + base + "/\r\n"), true;
        std::string rel = path.substr(base.size() + 1);
        if (rel.empty()) rel = "index.html";
        // La consola de la presidencia vive al lado: Web/presidencia.
        return servir(fd, rel, paginaConsola ? (std::filesystem::path(webroot_).parent_path() / "presidencia").string()
                                             : webroot_);
    }
    const std::string key = http::getHeader(headers, "Sec-WebSocket-Key");
    if (key.empty()) return http::sendResponse(fd, 400, "text/plain", "falta Sec-WebSocket-Key\n"), true;
    {
        std::lock_guard<std::mutex> l(mu_);
        if (static_cast<int>(conns_.size()) >= kMaxConexiones())
            return http::sendResponse(fd, 503, "text/plain", "sesion llena\n"), true;
    }
    const std::string accept = http::base64(http::sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").data(), 20);
    const std::string ok = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                           "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
    if (!http::sendAll(fd, ok.data(), ok.size())) return true;
    http::setSocketTimeout(fd, 0);
    wsLoop(fd, ip, lugar, wsConsola);
    return true;
}

void SesionBorde::enviar(const std::shared_ptr<Conn>& c, const json& msg) {
    std::lock_guard<std::mutex> l(c->sendLock);
    if (c->fd >= 0) http::writeWsFrame(c->fd, 0x1, msg.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
}

void SesionBorde::wsLoop(long long fd, const std::string& ip, const std::string& lugar, bool consola) {
    auto c = std::make_shared<Conn>();
    c->fd = fd;
    c->ip = ip;
    c->lugar = lugar;
    // 1. Autenticación: el primer mensaje, a los 15 s como mucho.
    {
        if (http::waitReadable(fd, kAuthMs()) <= 0) return;
        std::string payload;
        int op = 0;
        if (!http::readWsFrame(fd, payload, op) || op != 0x1) return;
        const json m = json::parse(payload, nullptr, false);
        const auto rechazar = [&](const std::string& error) {
            enviar(c, {{"type", "auth"}, {"ok", false}, {"error", error}});
            http::writeWsFrame(fd, 0x8, "");
        };
        if (!m.is_object() || m.value("type", std::string()) != "auth") return rechazar("Mensaje inválido.");
        // La consola de la presidencia tiene su propia credencial (id "consola:<x>"); un
        // integrante nunca entra por la consola ni la consola por la página del integrante.
        const bool conUsuario = !consola && m.contains("u");
        std::string id = consola ? "consola:" + m.value("c", std::string()) : m.value("i", std::string());
        if (!consola && id.rfind("consola:", 0) == 0) return rechazar("Mensaje inválido.");
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
    int canalMedios = 0;
    {
        std::lock_guard<std::mutex> l(mu_);
        if (!consola)
            if (auto it = mediosDe_.find(c->id); it != mediosDe_.end()) canalMedios = it->second;
    }
    const bool tieneMedios = canalMedios > 0;
    // Si tenía la palabra, su WebRTC hacia la sala era de la conexión vieja (las
    // respuestas iban a ella): se cierra para que la oferta nueva arranque limpia.
    if (tieneMedios && vieja && medios_.cerrar) medios_.cerrar(c->id, canalMedios);
    enviar(c, {{"type", "auth"}, {"ok", true}, {"id", c->id}, {"nombre", datos.value("nombre", std::string())},
               {"banca", datos.value("banca", std::string())}, {"lugar", c->lugar}});
    log_.info("sesion: " + c->id + " conectado desde " + c->ip);
    alApp_({{"tipo", "conexion"}, {"id", c->id}, {"conectado", true}, {"ip", c->ip}, {"lugar", c->lugar}});
    // Volvió el que tiene la palabra (se le cortó la red, recargó la página): se le
    // vuelve a pedir el micrófono y la cámara.
    if (tieneMedios) enviar(c, {{"type", "medios"}, {"activo", true}});
    if (!consola) {
        json lista;
        {
            std::lock_guard<std::mutex> l(mu_);
            lista = listaVistas();
        }
        enviar(c, {{"type", "vistas"}, {"lista", lista}});
        // Su vista de la conexión vieja (si la había) era de esa página: la nueva ofrece otra.
        if (vieja && medios_.vistaCerrar) medios_.vistaCerrar(c->id);
    }
    // 2. El lazo.
    std::vector<std::int64_t> ventana;
    while (true) {
        const int r = http::waitReadable(fd, kLatidoMs());
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
        if (op != 0x1 || payload.size() > kMaxFoto() * 2) continue;
        const std::int64_t ahora = ahoraMs();
        ventana.erase(std::remove_if(ventana.begin(), ventana.end(), [&](std::int64_t t) { return ahora - t > 1000; }),
                      ventana.end());
        // La consola junta candidatos ICE de varias conexiones a la vez: más margen.
        if (static_cast<int>(ventana.size()) >= (consola ? kMensajesPorSegundo() * 4 : kMensajesPorSegundo())) {
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
        // R3: el que tiene la palabra manda su micrófono y su cámara a la sala.
        if (!consola && (t == "medios-oferta" || t == "medios-ice")) {
            int canal = 0;
            {
                std::lock_guard<std::mutex> l(mu_);
                if (auto it = mediosDe_.find(c->id); it != mediosDe_.end()) canal = it->second;
            }
            if (canal == 0 || !medios_.oferta) continue;
            if (t == "medios-oferta") {
                std::weak_ptr<Conn> w = c;
                const bool ok = medios_.oferta(c->id, canal, m.value("sdp", std::string()), [this, w](const json& r) {
                    if (auto cc = w.lock()) enviar(cc, r);
                });
                if (!ok) enviar(c, {{"type", "medios"}, {"activo", false}, {"error", "no hay lugar para el audio en la sala"}});
            } else if (medios_.ice) {
                const json& cand = m.contains("candidate") ? m["candidate"] : json();
                medios_.ice(c->id, cand.is_string() ? cand.get<std::string>() : cand.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
            }
            continue;
        }
        // Lo que mira en su celular: un WebRTC que solo recibe, y el canal que elige.
        if (!consola && (t == "vista-oferta" || t == "vista-ice" || t == "vista")) {
            if (t == "vista-ice") {
                const json& cand = m.contains("candidate") ? m["candidate"] : json();
                if (medios_.vistaIce) medios_.vistaIce(c->id, cand.is_string() ? cand.get<std::string>() : cand.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
                continue;
            }
            int canal;
            bool habla;
            {
                std::lock_guard<std::mutex> l(mu_);
                std::string v = m.value("vista", std::string("yo"));
                if (v.size() > 16) v = "yo";
                vistaDe_[c->id] = v;
                canal = canalDeVista(v);
                habla = hablando_[c->id];
            }
            if (t == "vista") {
                if (medios_.vistaRuta) medios_.vistaRuta(c->id, canal, habla);
            } else if (medios_.vistaOferta) {
                std::weak_ptr<Conn> w = c;
                const bool ok = medios_.vistaOferta(c->id, m.value("sdp", std::string()), canal, habla, [this, w](const json& r) {
                    if (auto cc = w.lock()) enviar(cc, r);
                });
                if (!ok) enviar(c, {{"type", "vista-error"}, {"error", "La sala no tiene lugar para mandarte la imagen ahora."}});
            }
            continue;
        }
        if (t == "rtc") {
            // Señalización WebRTC directa entre la consola de la presidencia y un
            // integrante (ver la cara del que habla, hablarle «apretar para hablar»).
            // El gateway solo la pasa: un integrante solo le puede escribir a una
            // consola, y una consola solo a un integrante.
            const std::string a = m.value("a", std::string());
            const bool aConsola = a.rfind("consola:", 0) == 0;
            if (!a.empty() && aConsola != consola) {
                json r = m;
                r.erase("a");
                r["de"] = c->id;
                std::shared_ptr<Conn> dest;
                {
                    std::lock_guard<std::mutex> l(mu_);
                    auto it = conns_.find(a);
                    if (it != conns_.end()) dest = it->second;
                }
                if (dest) enviar(dest, r);
            }
            continue;
        }
        if (consola) {
            if (t == "foto") {
                // La cara de un integrante conectado (la última foto de su cámara).
                const std::string quien = m.value("id", std::string());
                std::string jpeg;
                {
                    std::lock_guard<std::mutex> l(mu_);
                    auto it = fotos_.find(quien);
                    if (it != fotos_.end()) jpeg = it->second;
                }
                if (!jpeg.empty())
                    enviar(c, {{"type", "foto"}, {"id", quien},
                               {"jpeg", http::base64(reinterpret_cast<const uint8_t*>(jpeg.data()), jpeg.size())}});
                continue;
            }
            // Lo demás lo decide la app (habilitar, dar la palabra, votar…).
            if (kTiposConsola.count(t)) alApp_({{"tipo", "consola"}, {"id", c->id}, {"msg", m}});
            continue;
        }
        if (t == "foto") {
            // La foto de su cámara: queda acá; la app la pide cuando la muestra.
            std::string jpeg = http::deBase64(m.value("jpeg", std::string()));
            if (jpeg.size() >= 4 && jpeg.size() <= kMaxFoto() && static_cast<unsigned char>(jpeg[0]) == 0xFF &&
                static_cast<unsigned char>(jpeg[1]) == 0xD8) {
                if (alFoto_) alFoto_(c->id, jpeg);
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
    if (eraLaActual && medios_.cerrar) {
        int canal = 0;
        {
            std::lock_guard<std::mutex> l(mu_);
            if (auto it = mediosDe_.find(c->id); it != mediosDe_.end()) canal = it->second;
        }
        if (canal > 0) medios_.cerrar(c->id, canal); // su WebRTC hacia la sala no sigue sin él
    }
    if (eraLaActual && !consola && medios_.vistaCerrar) medios_.vistaCerrar(c->id);
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
        http::sendResponse(fd, 200, "application/json", json{{"conexiones", cs}, {"credenciales", credenciales_.size()}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
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
        http::sendResponse(fd, 400, "application/json", json{{"error", "bad_request"}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
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
        http::sendResponse(fd, 200, "application/json", json{{"ok", true}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        return true;
    }
    if (que == "vistas") {
        json lista;
        std::vector<std::pair<std::string, std::pair<int, bool>>> rutas;
        std::vector<std::shared_ptr<Conn>> a;
        {
            std::lock_guard<std::mutex> l(mu_);
            vistas_ = json::array();
            for (const auto& v : r.value("vistas", json::array()))
                if (v.is_object() && !v.value("id", std::string()).empty())
                    vistas_.push_back({{"id", v.value("id", std::string())}, {"nombre", v.value("nombre", std::string())},
                                       {"canal", v.value("canal", -1)}});
            lista = listaVistas();
            for (const auto& [id, v] : vistaDe_) rutas.push_back({id, {canalDeVista(v), hablando_[id]}});
            for (const auto& [cid, c] : conns_)
                if (cid.rfind("consola:", 0) != 0) a.push_back(c);
        }
        for (const auto& [id, ru] : rutas)
            if (medios_.vistaRuta) medios_.vistaRuta(id, ru.first, ru.second);
        for (const auto& c : a) enviar(c, {{"type", "vistas"}, {"lista", lista}});
        http::sendResponse(fd, 200, "application/json", json{{"ok", true}, {"vistas", lista}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        return true;
    }
    if (que == "hablar") {
        const std::string id = r.value("id", std::string());
        const bool on = r.value("activo", false);
        int canal = -2;
        bool conVista = false;
        {
            std::lock_guard<std::mutex> l(mu_);
            hablando_[id] = on;
            if (!on) hablando_.erase(id);
            auto it = vistaDe_.find(id);
            conVista = it != vistaDe_.end();
            if (conVista) canal = canalDeVista(it->second);
        }
        if (conVista && medios_.vistaRuta) medios_.vistaRuta(id, canal, on);
        http::sendResponse(fd, 200, "application/json", json{{"ok", true}, {"con_vista", conVista}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        return true;
    }
    if (que == "medios") {
        // Con número: una lista vieja que llega después de una nueva no la pisa.
        if (r.contains("seq") && r["seq"].is_number_integer()) {
            const long long seq = r["seq"].get<long long>();
            std::lock_guard<std::mutex> l(mu_);
            if (seq <= ultimoSeqMedios_) {
                http::sendResponse(fd, 200, "application/json", json{{"ok", true}, {"ignorado", "vieja"}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
                return true;
            }
            ultimoSeqMedios_ = seq;
        }
        std::vector<std::string> ids;
        if (r.contains("ids") && r["ids"].is_array()) {
            for (const auto& x : r["ids"])
                if (x.is_string() && !x.get<std::string>().empty()) ids.push_back(x.get<std::string>());
        } else if (!r.value("id", std::string()).empty()) {
            ids.push_back(r.value("id", std::string()));
        }
        if (static_cast<int>(ids.size()) > kCanalesRemotos) ids.resize(kCanalesRemotos);
        ponerMedios(ids);
        http::sendResponse(fd, 200, "application/json", json{{"ok", true}, {"ids", ids}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        return true;
    }
    if (que == "enviar" || que == "cortar") {
        const std::string id = r.value("id", std::string());
        std::vector<std::shared_ptr<Conn>> a;
        {
            std::lock_guard<std::mutex> l(mu_);
            // "*" = todos los integrantes; "consolas" = todas las consolas de la presidencia.
            for (const auto& [cid, c] : conns_) {
                const bool esConsola = cid.rfind("consola:", 0) == 0;
                if ((id == "*" && !esConsola) || (id == "consolas" && esConsola) || cid == id) a.push_back(c);
            }
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
        http::sendResponse(fd, 200, "application/json", json{{"ok", true}, {"entregado", a.size()}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        return true;
    }
    http::sendResponse(fd, 404, "application/json", json{{"error", "not_found"}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
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

int SesionBorde::canalDeVista(const std::string& vista) const {
    if (vista == "programa") return -1;
    for (const auto& v : vistas_)
        if (v.value("id", std::string()) == vista) return v.value("canal", -1);
    return -2; // "yo" o una que ya no está: sin imagen, con el sonido
}

json SesionBorde::listaVistas() const {
    json out = json::array();
    for (const auto& v : vistas_) out.push_back({{"id", v.value("id", std::string())}, {"nombre", v.value("nombre", std::string())}});
    return out;
}

void SesionBorde::ponerMedios(const std::vector<std::string>& ids) {
    // Los que siguen conservan su canal; los nuevos toman el que quede libre.
    std::map<std::string, int> nuevo;
    std::vector<std::pair<std::string, int>> salen;
    std::vector<std::shared_ptr<Conn>> avisarSale, avisarEntra;
    {
        std::lock_guard<std::mutex> l(mu_);
        std::set<int> libres;
        for (int k = 1; k <= kCanalesRemotos; ++k) libres.insert(k);
        for (const auto& id : ids)
            if (auto it = mediosDe_.find(id); it != mediosDe_.end()) {
                nuevo[id] = it->second;
                libres.erase(it->second);
            }
        for (const auto& id : ids)
            if (!nuevo.count(id) && !libres.empty()) {
                nuevo[id] = *libres.begin();
                libres.erase(libres.begin());
            }
        for (const auto& [id, canal] : mediosDe_)
            if (!nuevo.count(id)) {
                salen.push_back({id, canal});
                if (auto it = conns_.find(id); it != conns_.end()) avisarSale.push_back(it->second);
            }
        for (const auto& [id, canal] : nuevo)
            if (!mediosDe_.count(id))
                if (auto it = conns_.find(id); it != conns_.end()) avisarEntra.push_back(it->second);
        if (nuevo == mediosDe_) return;
        mediosDe_ = nuevo;
    }
    for (const auto& [id, canal] : salen)
        if (medios_.cerrar) medios_.cerrar(id, canal);
    for (const auto& c : avisarSale) enviar(c, {{"type", "medios"}, {"activo", false}});
    for (const auto& c : avisarEntra) enviar(c, {{"type", "medios"}, {"activo", true}});
    std::string quienes;
    for (const auto& [id, canal] : nuevo) quienes += (quienes.empty() ? "" : ", ") + id + " (canal " + std::to_string(canal) + ")";
    log_.info("sesion: medios a la sala: " + (quienes.empty() ? std::string("nadie") : quienes));
}

} // namespace ba::gateway
