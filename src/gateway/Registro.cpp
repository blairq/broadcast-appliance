// ============================================================================
// src/gateway/Registro.cpp — ver Registro.hpp
// ============================================================================

#include "Registro.hpp"

#include <algorithm>
#include <chrono>
#include <set>

#include "Botonera.hpp" // sha256Hex, ipLocal
#include "HttpIo.hpp"
#include "Log.hpp"
#include "MediaRoute.hpp"
#include "Sesion.hpp" // servir, origenValido: las mismas defensas
#include "ajustes/Ajustes.hpp"
#include "i18n/I18n.hpp"

// Lo que se le manda a la página del tótem (error, motivo) no se traduce acá: se
// marca para el catálogo de la web y la página lo traduce (el valor que viaja es
// el original).
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

// Las mismas perillas que la sesión remota (son las mismas defensas, y así se
// ajustan juntas desde Configuración → Avanzado).
inline int kMensajesPorSegundo() { return ba::ajustes::entero("sesion.mensajesPorSegundo", 20); }
inline int kLatidoMs() { return ba::ajustes::sAMs("sesion.latidoS", 30); }
inline int kAuthMs() { return ba::ajustes::sAMs("sesion.authS", 15); }
inline int kIntentosPorIp() { return ba::ajustes::entero("sesion.intentosPorIp", 10); } // fallidos, cada 10 minutos
// Conexiones a la vez entre todos los tótems: cada una es un hilo y un socket. Una
// TV con su QR y la gente que lo escanea en la entrada no se acercan a esto.
// El mensaje más grande que manda la página: una foto entera de la cámara mientras muestra
// su documento (JPEG en base64, ver `cuadro`). Lo demás no pasa de una oferta SDP (~10 KB).
constexpr std::size_t kMaxCuadro = 1500 * 1024;
constexpr std::size_t kMaxMensaje = kMaxCuadro * 4 / 3 + 1024;
constexpr std::int64_t kEntreCuadrosMs = 400;  // a lo sumo dos y pico por segundo
constexpr std::size_t kMaxTextoCodigo = 4096;  // un PDF417 de documento no llega a 1 KB
inline int kMaxConexiones() { return ba::ajustes::entero("registro.maxConexiones", 256); }

} // namespace

RegistroBorde::RegistroBorde(std::string webroot, Log& log, Evento alApp)
    : webroot_(std::move(webroot)), log_(log), alApp_(std::move(alApp)) {}

bool RegistroBorde::idValido(const std::string& id) {
    if (id.empty() || id.size() > 32) return false;
    return std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

std::vector<std::string> RegistroBorde::ponerTotems(const json& totems) {
    std::map<std::string, Totem> nuevos;
    if (totems.is_array())
        for (const auto& t : totems) {
            if (!t.is_object()) continue;
            const std::string id = t.value("id", std::string());
            Totem x;
            x.nombre = t.value("nombre", std::string());
            x.sal = t.value("sal", std::string());
            x.hashToken = t.value("hash_token", std::string());
            if (!idValido(id) || !esHex64(x.hashToken) || x.sal.empty()) continue;
            nuevos[id] = std::move(x);
        }
    std::vector<std::string> cortar;
    std::lock_guard<std::mutex> l(mu_);
    // Lo que se dio de baja o cambió de token (la url vieja ya no vale) se corta ya.
    for (const auto& [conn, c] : conns_) {
        const auto it = nuevos.find(c->id);
        const auto antes = totems_.find(c->id);
        if (it == nuevos.end() || antes == totems_.end() || antes->second.hashToken != it->second.hashToken)
            cortar.push_back(conn);
    }
    totems_ = std::move(nuevos);
    return cortar;
}

bool RegistroBorde::autenticar(const std::string& id, const std::string& token, std::string& nombre) const {
    // 256 bits en hex son 64; se acepta otra codificación razonable (base64url: 43),
    // pero nada que no pueda tener esa entropía ni algo desmedido.
    if (!idValido(id) || token.size() < 32 || token.size() > 128) return false;
    std::lock_guard<std::mutex> l(mu_);
    const auto it = totems_.find(id);
    if (it == totems_.end()) return false;
    if (!igualesTiempoConstante(BotoneraBorde::sha256Hex(it->second.sal + token), it->second.hashToken)) return false;
    nombre = it->second.nombre;
    return true;
}

bool RegistroBorde::intentoPermitido(const std::string& ip) {
    std::lock_guard<std::mutex> l(mu_);
    auto& v = fallidos_[ip];
    const std::int64_t ahora = ahoraMs();
    v.erase(std::remove_if(v.begin(), v.end(), [&](std::int64_t t) { return ahora - t > 10 * 60 * 1000; }), v.end());
    return static_cast<int>(v.size()) < kIntentosPorIp();
}

void RegistroBorde::anotarIntentoFallido(const std::string& ip) {
    std::lock_guard<std::mutex> l(mu_);
    fallidos_[ip].push_back(ahoraMs());
}

std::shared_ptr<RegistroBorde::Conn> RegistroBorde::buscar(const std::string& conn) const {
    std::lock_guard<std::mutex> l(mu_);
    const auto it = conns_.find(conn);
    return it == conns_.end() ? nullptr : it->second;
}

std::string RegistroBorde::alta(const std::string& id, const std::string& ip, const std::string& lugar, long long fd) {
    auto c = std::make_shared<Conn>();
    c->fd = fd;
    c->id = id;
    c->ip = ip;
    c->lugar = lugar;
    c->desdeMs = ahoraMs();
    {
        std::lock_guard<std::mutex> l(mu_);
        if (static_cast<int>(conns_.size()) >= kMaxConexiones()) return {};
        c->conn = "c" + std::to_string(++siguiente_);
        conns_[c->conn] = c;
    }
    log_.info(llenar(traducir("gateway", "registro: %1 (tótem %2) conectado desde %3"), {c->conn, id, ip}));
    alApp_({{"tipo", "conexion"}, {"conn", c->conn}, {"id", id}, {"conectado", true}, {"ip", ip}, {"lugar", lugar}});
    return c->conn;
}

void RegistroBorde::baja(const std::string& conn) {
    soltarCanal(conn); // primero el canal: la app lo cierra antes de enterarse de que se fue
    std::shared_ptr<Conn> c;
    {
        std::lock_guard<std::mutex> l(mu_);
        const auto it = conns_.find(conn);
        if (it == conns_.end()) return;
        c = it->second;
        conns_.erase(it);
        cuadros_.erase(conn);
    }
    {
        std::lock_guard<std::mutex> l(c->sendLock);
        c->fd = -1;
    }
    log_.info(llenar(traducir("gateway", "registro: %1 (tótem %2) desconectado"), {conn, c->id}));
    alApp_({{"tipo", "conexion"}, {"conn", conn}, {"id", c->id}, {"conectado", false}});
    reubicarAtienden(conn); // quienes la miraban por web se quedan sin imagen
}

int RegistroBorde::tomarCanal(const std::string& conn) {
    std::string id;
    int canal = 0;
    {
        std::lock_guard<std::mutex> l(mu_);
        const auto it = conns_.find(conn);
        if (it == conns_.end()) return 0;
        if (it->second->canal > 0) return it->second->canal; // ya tenía: el mismo (reintento de la página)
        for (int k = 1; k <= kMaxTotems && canal == 0; ++k)
            if (!canales_.count(k)) canal = k;
        if (canal == 0) return 0;
        canales_[canal] = conn;
        it->second->canal = canal;
        it->second->audio = false;
        id = it->second->id;
    }
    log_.info(llenar(traducir("gateway", "registro: %1 toma el canal %2"), {conn, std::to_string(canal)}));
    alApp_({{"tipo", "canal"}, {"conn", conn}, {"id", id}, {"canal", canal}});
    reubicarAtienden(conn); // quien ya la miraba por web la ve desde ahora
    return canal;
}

void RegistroBorde::soltarCanal(const std::string& conn) {
    std::string id;
    bool conAudio = false;
    int canal = 0;
    {
        std::lock_guard<std::mutex> l(mu_);
        const auto it = conns_.find(conn);
        if (it == conns_.end() || it->second->canal == 0) return;
        canal = it->second->canal;
        canales_.erase(canal);
        it->second->canal = 0;
        conAudio = it->second->audio;
        it->second->audio = false;
        id = it->second->id;
        atendida_.erase(conn); // sin canal no hay a quién hablarle (la conversación se pierde)
    }
    // Su WebRTC no sigue sin canal: la ruta Registro(k) queda para el próximo.
    if (medios_.cerrar) medios_.cerrar(conn, canal);
    log_.info(llenar(traducir("gateway", "registro: %1 suelta el canal %2"), {conn, std::to_string(canal)}));
    if (conAudio) alApp_({{"tipo", "audio"}, {"conn", conn}, {"id", id}, {"listo", false}});
    alApp_({{"tipo", "canal"}, {"conn", conn}, {"id", id}, {"canal", 0}});
    reubicarAtienden(conn);
}

int RegistroBorde::canalDe(const std::string& conn) const {
    std::lock_guard<std::mutex> l(mu_);
    const auto it = conns_.find(conn);
    return it == conns_.end() ? 0 : it->second->canal;
}

void RegistroBorde::audioListo(const std::string& conn, bool listo) {
    std::string id;
    {
        std::lock_guard<std::mutex> l(mu_);
        const auto it = conns_.find(conn);
        // Sin canal, el audio ya se dio por caído al soltarlo (y no vuelve sin otro).
        if (it == conns_.end() || it->second->canal == 0 || it->second->audio == listo) return;
        it->second->audio = listo;
        id = it->second->id;
    }
    alApp_({{"tipo", "audio"}, {"conn", conn}, {"id", id}, {"listo", listo}});
}

bool RegistroBorde::ofertaValida(const std::string& sdp) {
    // Sección por sección (m=): audio (el micrófono, y por ahí baja la voz del asistente) y a
    // lo sumo UN video (la cámara). Nada más: ni una segunda cámara, ni datos, ni una pantalla.
    int audios = 0, videos = 0, otras = 0;
    std::size_t pos = 0;
    while (pos < sdp.size()) {
        const std::size_t fin = sdp.find('\n', pos);
        const std::string linea = sdp.substr(pos, fin == std::string::npos ? std::string::npos : fin - pos);
        pos = fin == std::string::npos ? sdp.size() : fin + 1;
        if (linea.rfind("m=", 0) != 0) continue;
        if (linea.rfind("m=audio", 0) == 0) ++audios;
        else if (linea.rfind("m=video", 0) == 0) ++videos;
        else ++otras;
    }
    return audios == 1 && videos <= 1 && otras == 0;
}

bool RegistroBorde::ponerCuadro(const std::string& conn, std::string jpeg, std::int64_t ahora) {
    if (jpeg.size() < 4 || jpeg.size() > kMaxCuadro || static_cast<unsigned char>(jpeg[0]) != 0xFF ||
        static_cast<unsigned char>(jpeg[1]) != 0xD8)
        return false;
    std::lock_guard<std::mutex> l(mu_);
    const auto it = conns_.find(conn);
    if (it == conns_.end() || it->second->canal == 0) return false;
    if (ahora - it->second->cuadroMs < kEntreCuadrosMs) return false; // de más: se descarta
    it->second->cuadroMs = ahora;
    cuadros_[conn] = std::move(jpeg);
    return true;
}

std::string RegistroBorde::cuadro(const std::string& conn) const {
    std::lock_guard<std::mutex> l(mu_);
    const auto it = cuadros_.find(conn);
    return it == cuadros_.end() ? std::string() : it->second;
}

json RegistroBorde::estado() const {
    json cs = json::array();
    std::lock_guard<std::mutex> l(mu_);
    for (const auto& [conn, c] : conns_)
        cs.push_back({{"conn", conn}, {"id", c->id}, {"canal", c->canal}, {"ip", c->ip}, {"lugar", c->lugar},
                      {"desdeMs", c->desdeMs}, {"audio", c->audio}});
    return {{"conexiones", cs}, {"totems", totems_.size()}, {"canales", kMaxTotems}, {"atienden", estadoAtiendenSinLock()}};
}

std::vector<std::string> RegistroBorde::conexionesDe(const std::string& id) const {
    std::vector<std::string> out;
    std::lock_guard<std::mutex> l(mu_);
    for (const auto& [conn, c] : conns_)
        if (c->id == id) out.push_back(conn);
    return out;
}

json RegistroBorde::toque(const std::string& conn, const std::string& id, const json& m) {
    const std::string t = m.is_object() ? m.value("type", std::string()) : std::string();
    if (t != "empezar" && t != "cancelar" && t != "escrito") return nullptr;
    json out{{"tipo", "toque"}, {"conn", conn}, {"id", id}, {"accion", t}};
    if (t == "escrito") {
        // Lo que tipeó en la pantalla táctil: la clave del campo que se le pidió y el valor,
        // acotados (la app los valida contra el campo).
        const std::string clave = m.value("clave", std::string());
        const bool claveOk = !clave.empty() && clave.size() <= 40 && std::all_of(clave.begin(), clave.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        });
        if (!claveOk || !m.contains("valor")) return nullptr;
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

bool RegistroBorde::atender(long long fd, const std::string& method, const std::string& path, const std::string& query,
                            const std::string& headers, std::uint32_t clientIp, bool externo) {
    // La atención remota (su página y su WebSocket): RegistroAtiende.cpp.
    if (atenderAtiende(fd, method, path, query, headers, clientIp, externo)) return true;
    const auto q = http::parseQuery(query);
    const bool ws = path == "/ws" && q.count("registro") > 0;
    const bool pagina = path == "/registro" || path.rfind("/registro/", 0) == 0;
    if (!ws && !pagina) return false;
    // La IP real si viene por el túnel de Cloudflare; por el de pinggy la cabecera no
    // vale (la pone cualquiera): la de la conexión, que ya trae la real (PROXY v1).
    std::string ip = externo ? std::string() : http::getHeader(headers, "CF-Connecting-IP");
    const bool tunel = externo || !ip.empty();
    if (ip.empty() || ip.size() > 45) ip = ipTexto(clientIp);
    const std::string lugar = !tunel && BotoneraBorde::ipLocal(clientIp) ? "sala" : "afuera";
    if (!SesionBorde::origenValido(headers)) {
        log_.warn(llenar(traducir("gateway", "registro: origen ajeno desde %1"), {ip}), -1);
        http::sendResponse(fd, 403, "text/plain", traducir("gateway", "origen invalido") + "\n");
        return true;
    }
    if (pagina) {
        if (method != "GET") return http::sendResponse(fd, 405, "text/plain", "\n"), true;
        if (path == "/registro") return http::sendResponse(fd, 301, "text/plain", "", "Location: /registro/\r\n"), true;
        std::string rel = path.substr(std::string("/registro/").size());
        if (rel.empty()) rel = "index.html";
        return SesionBorde::servir(fd, rel, webroot_);
    }
    const std::string key = http::getHeader(headers, "Sec-WebSocket-Key");
    if (key.empty()) return http::sendResponse(fd, 400, "text/plain", traducir("gateway", "falta Sec-WebSocket-Key") + "\n"), true;
    {
        std::lock_guard<std::mutex> l(mu_);
        if (static_cast<int>(conns_.size()) >= kMaxConexiones())
            return http::sendResponse(fd, 503, "text/plain", traducir("gateway", "registro lleno") + "\n"), true;
    }
    const std::string accept = http::base64(http::sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").data(), 20);
    const std::string ok = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                           "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
    if (!http::sendAll(fd, ok.data(), ok.size())) return true;
    http::setSocketTimeout(fd, 0);
    wsLoop(fd, ip, lugar);
    return true;
}

void RegistroBorde::enviar(const std::shared_ptr<Conn>& c, const json& msg) {
    std::lock_guard<std::mutex> l(c->sendLock);
    if (c->fd >= 0) http::writeWsFrame(c->fd, 0x1, volcar(msg));
}

void RegistroBorde::enviarA(const std::string& conn, const json& msg) {
    if (auto c = buscar(conn)) enviar(c, msg);
}

void RegistroBorde::cortarConn(const std::string& conn, const std::string& motivo) {
    auto c = buscar(conn);
    if (!c) return;
    enviar(c, {{"type", "cortado"}, {"motivo", motivo}});
    std::lock_guard<std::mutex> l(c->sendLock);
    if (c->fd >= 0) http::shutdownFd(c->fd); // su lazo sale y hace la baja
}

void RegistroBorde::wsLoop(long long fd, const std::string& ip, const std::string& lugar) {
    // 1. Autenticación: el primer mensaje, a los 15 s como mucho.
    std::string conn, id;
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
        if (!m.is_object() || m.value("type", std::string()) != "auth")
            return rechazar(QT_TRANSLATE_NOOP("web/comun", "Mensaje inválido."));
        if (!intentoPermitido(ip)) {
            log_.warn(llenar(traducir("gateway", "registro: demasiados intentos fallidos desde %1"), {ip}), -1);
            return rechazar(QT_TRANSLATE_NOOP("web/comun", "Demasiados intentos desde esta conexión. Esperá 10 minutos."));
        }
        id = m.value("r", std::string());
        std::string nombre;
        if (!autenticar(id, m.value("t", std::string()), nombre)) {
            anotarIntentoFallido(ip);
            alApp_({{"tipo", "intento"}, {"id", idValido(id) ? id : std::string()}, {"ip", ip}});
            return rechazar(QT_TRANSLATE_NOOP("web/comun", "Este enlace de registro no es válido (o lo cambiaron). Pedí el nuevo."));
        }
        conn = alta(id, ip, lugar, fd);
        if (conn.empty()) return rechazar(QT_TRANSLATE_NOOP("web/comun", "Hay demasiadas conexiones ahora. Probá en un rato."));
        enviarA(conn, {{"type", "hola"}, {"nombre", nombre}, {"conn", conn}});
    }
    const auto c = buscar(conn);
    if (!c) return;
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
        if (op != 0x1 || payload.size() > kMaxMensaje) continue;
        const std::int64_t ahora = ahoraMs();
        ventana.erase(std::remove_if(ventana.begin(), ventana.end(), [&](std::int64_t t) { return ahora - t > 1000; }),
                      ventana.end());
        if (static_cast<int>(ventana.size()) >= kMensajesPorSegundo()) {
            log_.warn(llenar(traducir("gateway", "registro: %1 manda demasiado; se corta"), {conn}), -1);
            break;
        }
        ventana.push_back(ahora);
        const json m = json::parse(payload, nullptr, false);
        if (!m.is_object()) continue;
        const std::string t = m.value("type", std::string());
        if (t == "ping") {
            enviar(c, {{"type", "pong"}});
        } else if (t == "canal") {
            const int k = tomarCanal(conn);
            if (k > 0)
                enviar(c, {{"type", "canal"}, {"canal", k}});
            else
                enviar(c, {{"type", "canal"}, {"canal", 0}, {"error", "lleno"}});
        } else if (t == "soltar") {
            soltarCanal(conn);
        } else if (t == "oferta") {
            // Micrófono y cámara, y solo por su canal: el token lo tiene cualquiera que vio el QR,
            // y lo que garantiza que no se cuele otra cosa (una pantalla, datos) es esto.
            const int k = canalDe(conn);
            const std::string sdp = m.value("sdp", std::string());
            if (k == 0) {
                enviar(c, {{"type", "audio-error"}, {"error", QT_TRANSLATE_NOOP("web/comun", "Todavía no hay un canal de audio para vos.")}});
            } else if (!ofertaValida(sdp)) {
                log_.warn(llenar(traducir("gateway", "registro: %1 quiso mandar algo más que el micrófono y la cámara; se ignora"),
                                 {conn}),
                          -1);
                enviar(c, {{"type", "audio-error"},
                           {"error", QT_TRANSLATE_NOOP("web/comun", "La página ofreció algo más que el micrófono y la cámara.")}});
            } else {
                std::weak_ptr<Conn> w = c;
                if (!medios_.oferta || !medios_.oferta(conn, k, sdp, [this, w](const json& resp) {
                        if (auto cc = w.lock()) enviar(cc, resp);
                    }))
                    enviar(c, {{"type", "audio-error"}, {"error", QT_TRANSLATE_NOOP("web/comun", "No hay lugar para escucharte ahora.")}});
            }
        } else if (t == "codigo") {
            // Lo que leyó el navegador mismo (BarcodeDetector) mientras se le pide el documento.
            // Solo con canal (conversando) y con un formato conocido; el texto no va al registro.
            static const std::set<std::string> kFormatosCodigo{"PDF417", "QRCode", "DataMatrix", "Aztec", "Code128"};
            const std::string formato = m.value("formato", std::string());
            const std::string texto = m.value("texto", std::string());
            if (canalDe(conn) == 0 || !kFormatosCodigo.count(formato) || texto.empty() || texto.size() > kMaxTextoCodigo)
                continue;
            alApp_({{"tipo", "codigo"}, {"conn", conn}, {"id", id}, {"formato", formato}, {"texto", texto}});
        } else if (t == "cuadro") {
            if (ponerCuadro(conn, http::deBase64(m.value("jpeg", std::string())), ahora))
                alApp_({{"tipo", "cuadro"}, {"conn", conn}, {"id", id}});
        } else if (t == "ice") {
            if (canalDe(conn) == 0 || !medios_.ice) continue;
            const json& cand = m.contains("candidate") ? m["candidate"] : json();
            medios_.ice(conn, cand.is_string() ? cand.get<std::string>() : volcar(cand));
        } else {
            const json tq = toque(conn, id, m);
            if (!tq.is_null()) alApp_(tq);
        }
    }
    baja(conn);
}

bool RegistroBorde::admin(long long fd, const std::string& method, const std::string& path, const std::string& query,
                          const std::string& body) {
    static const std::string kBase = "/api/v1/registro/";
    if (path.rfind(kBase, 0) != 0) return false;
    const std::string que = path.substr(kBase.size());
    if (adminAtiende(fd, method, que, body)) return true; // la atención remota: RegistroAtiende.cpp
    if (method == "GET" && que == "cuadro") {
        const auto q = http::parseQuery(query);
        const std::string jpeg = cuadro(q.count("conn") ? http::urlDecode(q.at("conn")) : std::string());
        if (jpeg.empty()) return http::sendResponse(fd, 404, "text/plain", traducir("gateway", "sin cuadro") + "\n"), true;
        http::sendResponse(fd, 200, "image/jpeg", jpeg, "Cache-Control: no-store\r\n");
        return true;
    }
    if (method == "GET" && que == "estado") {
        http::sendResponse(fd, 200, "application/json", volcar(estado()));
        return true;
    }
    const json r = json::parse(body.empty() ? "{}" : body, nullptr, false);
    if (method != "POST" || !r.is_object()) {
        http::sendResponse(fd, 400, "application/json", volcar({{"ok", false}, {"error", "bad_request"}}));
        return true;
    }
    if (que == "totems") {
        const auto cortar = ponerTotems(r.value("totems", json::array()));
        for (const auto& conn : cortar)
            cortarConn(conn, QT_TRANSLATE_NOOP("web/comun", "Este tótem se dio de baja o le cambiaron el enlace."));
        std::size_t n = 0;
        {
            std::lock_guard<std::mutex> l(mu_);
            n = totems_.size();
        }
        http::sendResponse(fd, 200, "application/json", volcar({{"ok", true}, {"totems", n}, {"cortadas", cortar.size()}}));
        return true;
    }
    if (que == "enviar") {
        if (!r.contains("msg") || !r["msg"].is_object()) {
            http::sendResponse(fd, 400, "application/json", volcar({{"ok", false}, {"error", traducir("gateway", "falta msg")}}));
            return true;
        }
        std::vector<std::string> a;
        if (!r.value("conn", std::string()).empty())
            a.push_back(r.value("conn", std::string()));
        else if (!r.value("id", std::string()).empty())
            a = conexionesDe(r.value("id", std::string()));
        std::size_t entregado = 0;
        for (const auto& conn : a)
            if (auto c = buscar(conn)) {
                enviar(c, r["msg"]);
                ++entregado;
            }
        http::sendResponse(fd, 200, "application/json", volcar({{"ok", true}, {"entregado", entregado}}));
        return true;
    }
    if (que == "cortar") {
        const std::string conn = r.value("conn", std::string());
        const bool habia = buscar(conn) != nullptr;
        cortarConn(conn, r.value("motivo", std::string(QT_TRANSLATE_NOOP("web/comun", "Se cerró esta pantalla de registro."))));
        http::sendResponse(fd, habia ? 200 : 404, "application/json", volcar({{"ok", habia}}));
        return true;
    }
    http::sendResponse(fd, 404, "application/json", volcar({{"ok", false}, {"error", "not_found"}}));
    return true;
}

void RegistroBorde::cerrar() {
    std::vector<std::shared_ptr<Conn>> a;
    {
        std::lock_guard<std::mutex> l(mu_);
        for (const auto& [conn, c] : conns_) a.push_back(c);
    }
    for (const auto& c : a) {
        std::lock_guard<std::mutex> l(c->sendLock);
        if (c->fd >= 0) http::shutdownFd(c->fd);
    }
    for (const auto& aconn : atiendenTodas())
        if (auto x = buscarAtiende(aconn)) {
            std::lock_guard<std::mutex> l(x->sendLock);
            if (x->fd >= 0) http::shutdownFd(x->fd);
        }
}

} // namespace ba::gateway
