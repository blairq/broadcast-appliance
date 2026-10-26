// ============================================================================
// src/gateway/RegistroAtiende.cpp — la atención remota del registro por tótem
//
// Ver Registro.hpp («Atención remota») y docs/funciones/REGISTRO-TOTEM.md. Es la
// misma clase que el borde de los tótems (RegistroBorde: comparte las conexiones,
// los canales y las defensas), en un archivo aparte porque es otra página con
// otra credencial: la de quien atiende desde la web.
//
// Como el resto del borde, no decide nada: quién atiende qué conversación lo dice
// la app (POST /api/v1/registro/atendida). Lo único que este lado sabe hacer con
// eso es la ruta de medios: quien mira una conversación ve su cámara y oye lo que
// pasa ahí; quien la atiende, además, le habla (y solo a ese tótem).
// ============================================================================

#include <algorithm>
#include <chrono>
#include <filesystem>

#include "Botonera.hpp" // sha256Hex, ipLocal
#include "HttpIo.hpp"
#include "Log.hpp"
#include "MediaRoute.hpp"
#include "Registro.hpp"
#include "Sesion.hpp" // servir, origenValido: las mismas defensas
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

bool esHex64(const std::string& s) {
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

std::string volcar(const json& j) { return j.dump(-1, ' ', false, json::error_handler_t::replace); }

/// Un `conn` de una conversación ("c17").
bool connValido(const std::string& c) {
    return c.size() >= 2 && c.size() <= 24 && c[0] == 'c' &&
           std::all_of(c.begin() + 1, c.end(), [](char x) { return x >= '0' && x <= '9'; });
}

/// La clave de un campo, con la misma regla que el «escrito» del tótem.
bool claveValida(const std::string& clave) {
    return !clave.empty() && clave.size() <= 40 && std::all_of(clave.begin(), clave.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

// Las mismas perillas que la sesión remota y los tótems (Configuración → Avanzado).
inline int kMensajesPorSegundo() { return ba::ajustes::entero("sesion.mensajesPorSegundo", 20); }
inline int kLatidoMs() { return ba::ajustes::sAMs("sesion.latidoS", 30); }
inline int kAuthMs() { return ba::ajustes::sAMs("sesion.authS", 15); }
/// Las páginas de atención a la vez: es gente del equipo, no el público.
inline int kMaxAtiendenConectadas() { return ba::ajustes::entero("registro.maxAtienden", 32); }
constexpr std::size_t kMaxMensaje = 64 * 1024; // una oferta SDP con audio y video
const char* const kNombreDeFabrica = "Alguien del equipo";

} // namespace

// --- La credencial ------------------------------------------------------------------------

std::vector<std::string> RegistroBorde::ponerCredencialAtiende(const json& c) {
    std::string sal, hash;
    if (c.is_object()) {
        sal = c.value("sal", std::string());
        hash = c.value("hash_token", std::string());
    }
    if (!esHex64(hash) || sal.empty()) sal.clear(), hash.clear();
    std::vector<std::string> cortar;
    std::lock_guard<std::mutex> l(mu_);
    // Otro link (renovado, u otro evento) o ninguno: el viejo ya no vale y quien entró con él se va.
    if (hash != hashAtiende_)
        for (const auto& [aconn, a] : atienden_) cortar.push_back(aconn);
    salAtiende_ = sal;
    hashAtiende_ = hash;
    return cortar;
}

bool RegistroBorde::autenticarAtiende(const std::string& token) const {
    if (token.size() < 32 || token.size() > 128) return false;
    std::lock_guard<std::mutex> l(mu_);
    if (hashAtiende_.empty()) return false;
    return igualesTiempoConstante(BotoneraBorde::sha256Hex(salAtiende_ + token), hashAtiende_);
}

std::string RegistroBorde::nombreAtiende(const std::string& s) {
    // Lo ve la persona del tótem en grande («Te atiende …»): sin controles, sin espacios de más
    // y corto (cortado en un límite de UTF-8, para no dejar media letra).
    std::string out;
    bool espacio = false;
    for (unsigned char c : s) {
        if (c < 0x20 || c == 0x7F) c = ' ';
        if (c == ' ') {
            espacio = !out.empty();
            continue;
        }
        if (espacio) out.push_back(' ');
        espacio = false;
        out.push_back(static_cast<char>(c));
    }
    if (out.size() > 40) {
        std::size_t n = 40;
        while (n > 0 && (static_cast<unsigned char>(out[n]) & 0xC0) == 0x80) --n;
        out.resize(n);
    }
    return out;
}

// --- Las páginas de atención ----------------------------------------------------------------

std::shared_ptr<RegistroBorde::Atiende> RegistroBorde::buscarAtiende(const std::string& aconn) const {
    std::lock_guard<std::mutex> l(mu_);
    const auto it = atienden_.find(aconn);
    return it == atienden_.end() ? nullptr : it->second;
}

std::vector<std::string> RegistroBorde::atiendenTodas() const {
    std::vector<std::string> out;
    std::lock_guard<std::mutex> l(mu_);
    for (const auto& [aconn, a] : atienden_) out.push_back(aconn);
    return out;
}

std::string RegistroBorde::altaAtiende(const std::string& nombre, const std::string& ip, const std::string& lugar,
                                       long long fd) {
    auto a = std::make_shared<Atiende>();
    a->fd = fd;
    a->nombre = nombre.empty() ? std::string(kNombreDeFabrica) : nombre;
    a->ip = ip;
    a->lugar = lugar;
    a->desdeMs = ahoraMs();
    {
        std::lock_guard<std::mutex> l(mu_);
        if (static_cast<int>(atienden_.size()) >= kMaxAtiendenConectadas()) return {};
        a->aconn = "a" + std::to_string(++siguienteAtiende_);
        atienden_[a->aconn] = a;
    }
    log_.info("registro: atención remota " + a->aconn + " («" + a->nombre + "») conectada desde " + ip);
    alApp_({{"tipo", "atiende-conexion"}, {"aconn", a->aconn}, {"nombre", a->nombre}, {"conectado", true}, {"ip", ip},
            {"lugar", lugar}});
    return a->aconn;
}

void RegistroBorde::bajaAtiende(const std::string& aconn) {
    std::shared_ptr<Atiende> a;
    bool medios = false;
    {
        std::lock_guard<std::mutex> l(mu_);
        const auto it = atienden_.find(aconn);
        if (it == atienden_.end()) return;
        a = it->second;
        medios = a->medios;
        atienden_.erase(it);
        // Lo que atendía queda sin su voz; volverlo al asistente lo decide la app (se entera abajo).
        for (auto at = atendida_.begin(); at != atendida_.end();)
            at = at->second == aconn ? atendida_.erase(at) : std::next(at);
    }
    {
        std::lock_guard<std::mutex> l(a->sendLock);
        a->fd = -1;
    }
    if (medios && mediosAtiende_.cerrar) mediosAtiende_.cerrar(aconn);
    log_.info("registro: atención remota " + aconn + " desconectada");
    alApp_({{"tipo", "atiende-conexion"}, {"aconn", aconn}, {"nombre", a->nombre}, {"conectado", false}});
}

std::pair<int, bool> RegistroBorde::rutaAtiendeSinLock(const Atiende& a) const {
    if (a.mira.empty()) return {0, false};
    const auto it = conns_.find(a.mira);
    const int canal = it == conns_.end() ? 0 : it->second->canal;
    const auto at = atendida_.find(a.mira);
    return {canal, canal > 0 && at != atendida_.end() && at->second == a.aconn};
}

std::pair<int, bool> RegistroBorde::rutaAtiende(const std::string& aconn) const {
    std::lock_guard<std::mutex> l(mu_);
    const auto it = atienden_.find(aconn);
    return it == atienden_.end() ? std::pair<int, bool>{0, false} : rutaAtiendeSinLock(*it->second);
}

void RegistroBorde::aplicarRuta(const std::shared_ptr<Atiende>& a) {
    std::pair<int, bool> r;
    bool medios = false;
    std::string conn;
    {
        std::lock_guard<std::mutex> l(mu_);
        r = rutaAtiendeSinLock(*a);
        medios = a->medios;
        conn = a->mira;
    }
    if (medios && mediosAtiende_.ruta) mediosAtiende_.ruta(a->aconn, r.first, r.second);
    enviarAtiende(a, {{"type", "mirando"}, {"conn", conn}, {"canal", r.first}, {"habla", r.second}});
}

void RegistroBorde::reubicarAtienden(const std::string& conn) {
    std::vector<std::shared_ptr<Atiende>> miran;
    {
        std::lock_guard<std::mutex> l(mu_);
        if (!conns_.count(conn)) atendida_.erase(conn); // se fue: nadie la atiende
        for (const auto& [aconn, a] : atienden_)
            if (a->mira == conn) miran.push_back(a);
    }
    for (const auto& a : miran) aplicarRuta(a);
}

bool RegistroBorde::mirar(const std::string& aconn, const std::string& conn) {
    std::shared_ptr<Atiende> a;
    {
        std::lock_guard<std::mutex> l(mu_);
        const auto it = atienden_.find(aconn);
        if (it == atienden_.end()) return false;
        if (!conn.empty() && !conns_.count(conn)) return false;
        a = it->second;
        a->mira = conn;
    }
    aplicarRuta(a);
    return true;
}

bool RegistroBorde::ponerAtendida(const std::string& conn, const std::string& aconn) {
    std::shared_ptr<Atiende> nueva;
    {
        std::lock_guard<std::mutex> l(mu_);
        if (!conns_.count(conn)) return false;
        if (aconn.empty()) {
            atendida_.erase(conn);
        } else {
            const auto it = atienden_.find(aconn);
            if (it == atienden_.end()) return false;
            atendida_[conn] = aconn;
            // Quien la atiende, la mira: no se le habla a alguien que no se ve.
            if (it->second->mira != conn) {
                it->second->mira = conn;
                nueva = it->second;
            }
        }
    }
    if (nueva) aplicarRuta(nueva);
    reubicarAtienden(conn); // la anterior deja de hablarle; la nueva empieza
    return true;
}

std::string RegistroBorde::atendidaPor(const std::string& conn) const {
    std::lock_guard<std::mutex> l(mu_);
    const auto it = atendida_.find(conn);
    return it == atendida_.end() ? std::string() : it->second;
}

json RegistroBorde::estadoAtiendenSinLock() const {
    json out = json::array();
    for (const auto& [aconn, a] : atienden_) {
        const auto r = rutaAtiendeSinLock(*a);
        out.push_back({{"aconn", aconn}, {"nombre", a->nombre}, {"ip", a->ip}, {"lugar", a->lugar},
                       {"desdeMs", a->desdeMs}, {"mira", a->mira}, {"canal", r.first}, {"habla", r.second},
                       {"medios", a->medios}});
    }
    return out;
}

json RegistroBorde::accionAtiende(const std::string& aconn, const std::string& nombre, const json& m) {
    const std::string t = m.is_object() ? m.value("type", std::string()) : std::string();
    if (t != "atender" && t != "devolver" && t != "guardar" && t != "cancelar" && t != "dato") return nullptr;
    const std::string conn = m.value("conn", std::string());
    if (!connValido(conn)) return nullptr;
    json out{{"tipo", "atiende"}, {"aconn", aconn}, {"nombre", nombre}, {"accion", t}, {"conn", conn}};
    if (t == "dato") {
        // Un dato que completa o corrige: la clave del campo y el valor (vacío = borrarlo),
        // acotados. La app lo valida contra el campo, como lo tipeado en el tótem.
        const std::string clave = m.value("clave", std::string());
        if (!claveValida(clave) || !m.contains("valor")) return nullptr;
        std::string valor;
        if (m["valor"].is_string()) valor = m["valor"].get<std::string>();
        else if (m["valor"].is_number() || m["valor"].is_boolean()) valor = m["valor"].dump();
        else return nullptr;
        if (valor.size() > 500) valor.resize(500);
        out["clave"] = clave;
        out["valor"] = valor;
    }
    return out;
}

void RegistroBorde::enviarAtiende(const std::shared_ptr<Atiende>& a, const json& msg) {
    std::lock_guard<std::mutex> l(a->sendLock);
    if (a->fd >= 0) http::writeWsFrame(a->fd, 0x1, volcar(msg));
}

// --- La página y su WebSocket -----------------------------------------------------------------

bool RegistroBorde::atenderAtiende(long long fd, const std::string& method, const std::string& path,
                                   const std::string& query, const std::string& headers, std::uint32_t clientIp,
                                   bool externo) {
    const auto q = http::parseQuery(query);
    const bool ws = path == "/ws" && q.count("registro_atiende") > 0;
    const bool pagina = path == "/registro-atiende" || path.rfind("/registro-atiende/", 0) == 0;
    if (!ws && !pagina) return false;
    std::string ip = externo ? std::string() : http::getHeader(headers, "CF-Connecting-IP");
    const bool tunel = externo || !ip.empty();
    if (ip.empty() || ip.size() > 45) ip = ipTexto(clientIp);
    const std::string lugar = !tunel && BotoneraBorde::ipLocal(clientIp) ? "sala" : "afuera";
    if (!SesionBorde::origenValido(headers)) {
        log_.warn("registro: origen ajeno en la atención remota desde " + ip, -1);
        http::sendResponse(fd, 403, "text/plain", "origen invalido\n");
        return true;
    }
    if (pagina) {
        if (method != "GET") return http::sendResponse(fd, 405, "text/plain", "\n"), true;
        if (path == "/registro-atiende")
            return http::sendResponse(fd, 301, "text/plain", "", "Location: /registro-atiende/\r\n"), true;
        std::string rel = path.substr(std::string("/registro-atiende/").size());
        if (rel.empty()) rel = "index.html";
        // Al lado de la del tótem (Web/registro-atiende), como la consola de la presidencia.
        return SesionBorde::servir(fd, rel, (std::filesystem::path(webroot_).parent_path() / "registro-atiende").string());
    }
    const std::string key = http::getHeader(headers, "Sec-WebSocket-Key");
    if (key.empty()) return http::sendResponse(fd, 400, "text/plain", "falta Sec-WebSocket-Key\n"), true;
    {
        std::lock_guard<std::mutex> l(mu_);
        if (static_cast<int>(atienden_.size()) >= kMaxAtiendenConectadas())
            return http::sendResponse(fd, 503, "text/plain", "atencion llena\n"), true;
    }
    const std::string accept = http::base64(http::sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").data(), 20);
    const std::string ok = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                           "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
    if (!http::sendAll(fd, ok.data(), ok.size())) return true;
    http::setSocketTimeout(fd, 0);
    wsLoopAtiende(fd, ip, lugar);
    return true;
}

void RegistroBorde::wsLoopAtiende(long long fd, const std::string& ip, const std::string& lugar) {
    // 1. Autenticación: el primer mensaje, a los 15 s como mucho.
    std::string aconn, nombre;
    {
        if (http::waitReadable(fd, kAuthMs()) <= 0) return;
        std::string payload;
        int op = 0;
        if (!http::readWsFrame(fd, payload, op) || op != 0x1) return;
        const json m = json::parse(payload, nullptr, false);
        const auto rechazar = [&](const std::string& error) {
            http::writeWsFrame(fd, 0x1, volcar({{"type", "error"}, {"error", error}}));
            http::writeWsFrame(fd, 0x8, "");
        };
        if (!m.is_object() || m.value("type", std::string()) != "auth") return rechazar("Mensaje inválido.");
        if (!intentoPermitido(ip)) {
            log_.warn("registro: demasiados intentos fallidos (atención remota) desde " + ip, -1);
            return rechazar("Demasiados intentos desde esta conexión. Esperá 10 minutos.");
        }
        if (!autenticarAtiende(m.value("t", std::string()))) {
            anotarIntentoFallido(ip);
            alApp_({{"tipo", "intento"}, {"atiende", true}, {"id", ""}, {"ip", ip}});
            return rechazar("Este enlace de atención no es válido (o lo cambiaron). Pedí el nuevo.");
        }
        nombre = nombreAtiende(m.contains("nombre") && m["nombre"].is_string() ? m["nombre"].get<std::string>() : "");
        aconn = altaAtiende(nombre, ip, lugar, fd);
        if (aconn.empty()) return rechazar("Hay demasiadas personas atendiendo ahora. Probá en un rato.");
    }
    const auto a = buscarAtiende(aconn);
    if (!a) return;
    nombre = a->nombre;
    enviarAtiende(a, {{"type", "hola"}, {"aconn", aconn}, {"nombre", nombre}});
    // 2. El lazo.
    std::vector<std::int64_t> ventana;
    while (true) {
        const int r = http::waitReadable(fd, kLatidoMs());
        if (r <= 0) break;
        std::string payload;
        int op = 0;
        if (!http::readWsFrame(fd, payload, op)) break;
        if (op == 0x8) break;
        if (op == 0x9) {
            std::lock_guard<std::mutex> l(a->sendLock);
            http::writeWsFrame(fd, 0xA, payload);
            continue;
        }
        if (op != 0x1 || payload.size() > kMaxMensaje) continue;
        const std::int64_t ahora = ahoraMs();
        ventana.erase(std::remove_if(ventana.begin(), ventana.end(), [&](std::int64_t t) { return ahora - t > 1000; }),
                      ventana.end());
        if (static_cast<int>(ventana.size()) >= kMensajesPorSegundo()) {
            log_.warn("registro: la atención remota " + aconn + " manda demasiado; se corta", -1);
            break;
        }
        ventana.push_back(ahora);
        const json m = json::parse(payload, nullptr, false);
        if (!m.is_object()) continue;
        const std::string t = m.value("type", std::string());
        if (t == "ping") {
            enviarAtiende(a, {{"type", "pong"}});
        } else if (t == "mirar") {
            const std::string conn = m.value("conn", std::string());
            if (!conn.empty() && !connValido(conn)) continue;
            if (!mirar(aconn, conn)) enviarAtiende(a, {{"type", "aviso"}, {"texto", "Esa conversación ya terminó."}});
        } else if (t == "oferta") {
            // Su micrófono (por ahí oye) y a lo sumo un video (por ahí ve la cámara del tótem): lo
            // mismo que el tótem, y nada más (ni una pantalla, ni datos).
            const std::string sdp = m.value("sdp", std::string());
            if (!ofertaValida(sdp)) {
                enviarAtiende(a, {{"type", "medios-error"}, {"error", "La página ofreció algo más que audio y video."}});
                continue;
            }
            const auto ruta = rutaAtiende(aconn);
            std::weak_ptr<Atiende> w = a;
            const bool ok = mediosAtiende_.oferta && mediosAtiende_.oferta(aconn, ruta.first, ruta.second, sdp,
                                                                            [this, w](const json& resp) {
                                                                                if (auto x = w.lock()) enviarAtiende(x, resp);
                                                                            });
            {
                std::lock_guard<std::mutex> l(mu_);
                a->medios = ok;
            }
            if (!ok) enviarAtiende(a, {{"type", "medios-error"}, {"error", "No hay lugar para ver y oír ahora."}});
            else aplicarRuta(a); // por si cambió mientras se armaba
        } else if (t == "ice") {
            bool medios = false;
            {
                std::lock_guard<std::mutex> l(mu_);
                medios = a->medios;
            }
            if (!medios || !mediosAtiende_.ice) continue;
            const json& cand = m.contains("candidate") ? m["candidate"] : json();
            mediosAtiende_.ice(aconn, cand.is_string() ? cand.get<std::string>() : volcar(cand));
        } else {
            const json ac = accionAtiende(aconn, nombre, m);
            if (!ac.is_null()) alApp_(ac);
        }
    }
    bajaAtiende(aconn);
}

// --- La API de control ------------------------------------------------------------------------

bool RegistroBorde::adminAtiende(long long fd, const std::string& method, const std::string& que,
                                 const std::string& body) {
    if (method != "POST" || (que != "atencion" && que != "atendida" && que != "enviar")) return false;
    const json r = json::parse(body.empty() ? "{}" : body, nullptr, false);
    if (!r.is_object()) return false; // el borde de siempre contesta el 400
    if (que == "atencion") {
        const auto cortar = ponerCredencialAtiende(r);
        for (const auto& aconn : cortar)
            if (auto a = buscarAtiende(aconn)) {
                enviarAtiende(a, {{"type", "cortado"}, {"motivo", "Cambiaron el enlace de atención (o se cerró el evento)."}});
                std::lock_guard<std::mutex> l(a->sendLock);
                if (a->fd >= 0) http::shutdownFd(a->fd);
            }
        http::sendResponse(fd, 200, "application/json", volcar({{"ok", true}, {"cortadas", cortar.size()}}));
        return true;
    }
    if (que == "atendida") {
        const bool ok = ponerAtendida(r.value("conn", std::string()), r.value("aconn", std::string()));
        http::sendResponse(fd, ok ? 200 : 404, "application/json", volcar({{"ok", ok}}));
        return true;
    }
    // enviar: solo si es para las páginas de atención; si no, el de los tótems.
    if (!r.contains("aconn") && !r.contains("atiende")) return false;
    if (!r.contains("msg") || !r["msg"].is_object()) {
        http::sendResponse(fd, 400, "application/json", volcar({{"ok", false}, {"error", "falta msg"}}));
        return true;
    }
    std::vector<std::string> a;
    if (!r.value("aconn", std::string()).empty()) a.push_back(r.value("aconn", std::string()));
    else a = atiendenTodas();
    std::size_t entregado = 0;
    for (const auto& aconn : a)
        if (auto x = buscarAtiende(aconn)) {
            enviarAtiende(x, r["msg"]);
            ++entregado;
        }
    http::sendResponse(fd, 200, "application/json", volcar({{"ok", true}, {"entregado", entregado}}));
    return true;
}

} // namespace ba::gateway
