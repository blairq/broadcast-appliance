// ============================================================================
// src/gateway/SignalingServer.cpp — superficie de invitados (ver el header)
//
// Capas:
//   - conexiones: accept por socket de escucha, un thread por conexion,
//     cierre ordenado (shutdown de los fds vivos + drenaje acotado);
//   - HTTP de invitados: estatico, join/verify, subida de archivos;
//   - WebSocket de sala: alta en el lobby / reenganche, mensajes de sala y
//     senalizacion WebRTC (solo una vez admitido).
// La API de control del operador esta en AdminApi.cpp.
// ============================================================================

#include "SignalingServer.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "HttpIo.hpp"
#include "ipc/CanalesEscena.hpp"
#include "ipc/ControlProtocol.hpp"
#include "Log.hpp"
#include "MediaPump.hpp"
#include "qr/Qr.hpp"
#include "Room.hpp"
#include "security/SecretsStore.hpp" // randomSecretHex (ids de participante)
#include "security/TokenService.hpp" // nowUnix() para el rate-limit por IP
#include "ShareStore.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

namespace ba::gateway {

using json = nlohmann::json;
using ba::ipc::Frame;

namespace {

/// Un QR como SVG, para la pagina que se manda a un televisor. En SVG y no en
/// PNG porque un QR son cuadraditos: escala sin verse borroso en una pantalla
/// de 55 pulgadas y pesa menos que la imagen.
/// El QR de una pantalla, con el estilo que venga en la direccion.
///
/// ANTES ESTO DIBUJABA SU PROPIO QR: negro sobre blanco, esquinas cuadradas,
/// correccion fija. O sea que un codigo guardado con su icono y sus esquinas
/// redondeadas se veia de una manera en la biblioteca y salia de otra en el
/// televisor. El generador con estilo vive en el nucleo justamente para que
/// eso no pase (ver qr/Qr.hpp), y este era el unico lugar que no lo usaba.
std::string qrSvg(const std::string& texto, const std::string& query) {
    return ba::qr::svg(texto, ba::qr::desdeConsulta(query));
}

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string ipText(std::uint32_t ipNetOrder) {
    char buf[INET_ADDRSTRLEN] = {0};
    in_addr a{};
    a.s_addr = ipNetOrder;
    inet_ntop(AF_INET, &a, buf, sizeof(buf));
    return buf;
}

const char* contentTypeFor(const std::string& rel) {
    const auto ends = [&](const char* ext) {
        const std::string e(ext);
        return rel.size() >= e.size() && rel.compare(rel.size() - e.size(), e.size(), e) == 0;
    };
    if (ends(".html")) return "text/html; charset=utf-8";
    if (ends(".js")) return "text/javascript; charset=utf-8";
    if (ends(".css")) return "text/css; charset=utf-8";
    if (ends(".svg")) return "image/svg+xml";
    if (ends(".png")) return "image/png";
    if (ends(".ico")) return "image/x-icon";
    if (ends(".json")) return "application/json";
    return "application/octet-stream";
}

} // namespace

SignalingServer::SignalingServer(const Cfg& cfg, TokenAuth& auth, WebrtcHub& hub, Room& room,
                                 ShareStore& shares, Log& log)
    : cfg_(cfg), auth_(auth), hub_(hub), room_(room), shares_(shares), log_(log) {
    Room::Sinks sinks;
    sinks.toGuest = [this](const std::string& pid, const json& msg) { sendToGuest(pid, msg); };
    sinks.toHosts = [this](const std::string& ev, const json& data) { broadcastToHosts(ev, data); };
    botonera_ = std::make_unique<BotoneraBorde>(cfg_.botoneraWebroot, log_,
                                                [this](const json& d) { broadcastToHosts("botonera", d); });
    botonera_->setCaras(cfg_.carasBotonera);
    sesion_ = std::make_unique<SesionBorde>(cfg_.sesionWebroot, log_,
                                            [this](const json& d) { broadcastToHosts("sesion", d); });
    // R3: el micrófono y la cámara del remoto con la palabra van a la app por el
    // mismo WebRTC que los invitados (una sesión del pool, ruta «Remoto»).
    {
        SesionBorde::Medios m;
        m.oferta = [this](const std::string& id, const std::string& sdp, std::function<void(const json&)> enviar) {
            return ofertaRemoto(id, sdp, std::move(enviar));
        };
        m.ice = [this](const std::string& id, const std::string& cand) {
            int idx = 0;
            {
                std::lock_guard<std::mutex> lock(sessLock_);
                const auto it = sessionOf_.find("remoto:" + id);
                if (it != sessionOf_.end()) idx = it->second;
            }
            if (WebrtcSession* s = idx > 0 ? hub_.session(idx) : nullptr) s->addRemoteIce(cand);
        };
        m.vistaOferta = [this](const std::string& id, const std::string& sdp, int canal, bool hablar,
                               std::function<void(const json&)> enviar) {
            return ofertaVista(id, sdp, canal, hablar, std::move(enviar));
        };
        m.vistaIce = [this](const std::string& id, const std::string& cand) {
            int idx = 0;
            {
                std::lock_guard<std::mutex> lock(sessLock_);
                const auto it = sessionOf_.find("vista:" + id);
                if (it != sessionOf_.end()) idx = it->second;
            }
            if (WebrtcSession* s = idx > 0 ? hub_.session(idx) : nullptr) s->addRemoteIce(cand);
        };
        m.vistaRuta = [this](const std::string& id, int canal, bool hablar) {
            int idx = 0;
            {
                std::lock_guard<std::mutex> lock(sessLock_);
                const auto it = sessionOf_.find("vista:" + id);
                if (it != sessionOf_.end()) idx = it->second;
            }
            if (idx > 0 && pump_ != nullptr) pump_->setRoute(idx, rutaDeVista(canal, hablar));
        };
        m.vistaCerrar = [this](const std::string& id) { releaseSession("vista:" + id, 0, true); };
        m.cerrar = [this](const std::string& id) {
            releaseSession("remoto:" + id, 0, true);
            if (pump_ != nullptr) pump_->apagarRemoto(1); // que no quede su último cuadro
        };
        sesion_->setMedios(std::move(m));
    }
    sinks.dropGuest = [this](const std::string& pid) { dropGuest(pid); };
    // Los archivos de quien se fue quedan (el operador puede estar usandolos):
    // se borran a mano o al reiniciar B. La sesion WebRTC la cierra la ruta
    // None que la sala emite al sacarlo.
    sinks.route = [this](const std::string& pid, const MediaRoute& r) { applyRoute(pid, r); };
    room_.setSinks(std::move(sinks));
    // Sin entropia queda vacia y el tablero no abre (401), nunca con clave predecible.
    if (!ba::security::randomSecretHex(16, boardToken_)) boardToken_.clear();
}

SignalingServer::~SignalingServer() { stop(); }

// ============================================================================
// Arranque / parada
// ============================================================================

bool SignalingServer::start() {
    if (!http::socketInit()) {
        log_.error("senalizacion: fallo la inicializacion de sockets", -1);
        return false;
    }
    listenFd_ = http::makeListen(cfg_.bindAddress, cfg_.port);
    if (listenFd_ < 0) {
        log_.error("senalizacion: no se pudo bind " + cfg_.bindAddress + ":" + std::to_string(cfg_.port) +
                       " (puerto en uso o direccion invalida?)",
                   -1);
        http::socketCleanup();
        return false;
    }
    stop_.store(false);
    guestBoundPort_ = http::localPort(listenFd_);
    acceptThread_ = std::thread(&SignalingServer::acceptLoop, this, listenFd_, false);
    // La calidad de la conexion de cada invitado (bitrate, perdida, jitter, RTT).
    statsThread_ = std::thread([this] {
        while (!stop_.load()) {
            for (int i = 0; i < 20 && !stop_.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::vector<std::pair<std::string, int>> sesiones;
            {
                std::lock_guard<std::mutex> l(sessLock_);
                for (const auto& [pid, idx] : sessionOf_) sesiones.emplace_back(pid, idx);
            }
            const int64_t ahora = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::steady_clock::now().time_since_epoch())
                                      .count();
            for (const auto& [pid, idx] : sesiones)
                if (WebrtcSession* s = hub_.session(idx)) {
                    const nlohmann::json j = nlohmann::json::parse(s->estadisticas(), nullptr, false);
                    if (j.is_object() && !j.empty()) room_.setRed(pid, j, ahora);
                }
        }
    });
    log_.info("senalizacion: escuchando en " + cfg_.bindAddress + ":" + std::to_string(guestBoundPort_) +
              " (webroot=" + cfg_.webroot + ")");

    // API de control: SIEMPRE loopback. Exponerla por el tunel o la red local
    // daria el mando de la sala a cualquiera que adivine el token.
    if (!cfg_.adminToken.empty()) {
        adminListenFd_ = http::makeListen("127.0.0.1", cfg_.adminPort);
        if (adminListenFd_ < 0) {
            log_.error("control: no se pudo abrir 127.0.0.1:" + std::to_string(cfg_.adminPort) +
                           " (la sala queda sin API de control)",
                       -1);
        } else {
            adminBoundPort_ = http::localPort(adminListenFd_);
            adminAcceptThread_ = std::thread(&SignalingServer::acceptLoop, this, adminListenFd_, true);
            log_.info("control: API del operador en 127.0.0.1:" + std::to_string(adminBoundPort_));
        }
    }
    return true;
}

std::vector<std::string> SignalingServer::tomarAcreditaciones() {
    std::lock_guard<std::mutex> g(publicoM_);
    std::vector<std::string> out;
    out.swap(acreditaciones_);
    return out;
}

void SignalingServer::setPublicoInfo(const nlohmann::json& info) {
    std::lock_guard<std::mutex> g(publicoM_);
    publicoInfo_ = info.is_object() ? info : nlohmann::json::object();
}

nlohmann::json SignalingServer::publicoInfo() const {
    std::lock_guard<std::mutex> g(publicoM_);
    return publicoInfo_;
}

void SignalingServer::setPublicoFondo(std::string bytes, std::string tipo) {
    std::lock_guard<std::mutex> g(publicoM_);
    fondoBytes_ = std::move(bytes);
    fondoTipo_ = std::move(tipo);
}

void SignalingServer::stop() {
    // Idempotente: el destructor llama a stop() y main() tambien.
    if (listenFd_ < 0 && adminListenFd_ < 0 && activeConns_.load() == 0 && !acceptThread_.joinable() &&
        !adminAcceptThread_.joinable() && !statsThread_.joinable()) {
        return;
    }
    stop_.store(true);

    // 1) Los threads de accept salen por `stop_` (su espera es acotada).
    if (acceptThread_.joinable()) acceptThread_.join();
    if (statsThread_.joinable()) statsThread_.join();
    if (adminAcceptThread_.joinable()) adminAcceptThread_.join();
    http::closeFd(listenFd_);
    listenFd_ = -1;
    http::closeFd(adminListenFd_);
    adminListenFd_ = -1;
    adminBoundPort_ = 0;

    // 2) WS y SSE pueden estar en un recv sin timeout: shutdown de todos.
    closeActiveConns();

    // 3) Espera acotada a que los threads detachados terminen.
    constexpr int kDrainTimeoutMs = 3000;
    constexpr int kDrainStepMs = 20;
    int waited = 0;
    while (activeConns_.load(std::memory_order_relaxed) > 0 && waited < kDrainTimeoutMs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kDrainStepMs));
        waited += kDrainStepMs;
    }
    const int leftover = activeConns_.load(std::memory_order_relaxed);
    if (leftover > 0) {
        log_.warn("senalizacion: " + std::to_string(leftover) + " conexiones sin cerrar tras " +
                      std::to_string(kDrainTimeoutMs) + " ms",
                  -1);
    }
    http::socketCleanup();
    log_.info("senalizacion: detenida");
}

bool SignalingServer::running() const { return listenFd_ >= 0; }

void SignalingServer::trackFd(long long fd) {
    std::lock_guard<std::mutex> lock(activeFdsLock_);
    activeFds_.insert(fd);
}

void SignalingServer::untrackFd(long long fd) {
    std::lock_guard<std::mutex> lock(activeFdsLock_);
    activeFds_.erase(fd);
}

void SignalingServer::closeActiveConns() {
    // shutdown(), no close(): el descriptor es del thread de conexion, que lo
    // cierra al salir. Cerrarlo aca abriria una ventana en la que el numero se
    // reutiliza y ese thread opera sobre un socket ajeno.
    std::lock_guard<std::mutex> lock(activeFdsLock_);
    for (long long fd : activeFds_)
        http::shutdownFd(fd);
}

// ============================================================================
// Accept + despacho
// ============================================================================

void SignalingServer::acceptLoop(long long listenFd, bool admin) {
    constexpr int kAcceptPollMs = 200;
    while (!stop_.load(std::memory_order_relaxed)) {
        const int ready = http::waitReadable(listenFd, kAcceptPollMs);
        if (ready == 0) continue;
        if (ready < 0) {
            if (stop_.load(std::memory_order_relaxed)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(kAcceptPollMs));
            continue;
        }
        sockaddr_in cli{};
        socklen_t len = sizeof(cli);
        const auto c =
            accept(static_cast<decltype(socket(0, 0, 0))>(listenFd), reinterpret_cast<sockaddr*>(&cli), &len);
#ifdef _WIN32
        if (c == INVALID_SOCKET) continue;
#else
        if (c < 0) continue;
#endif
        const long long fd = static_cast<long long>(c);

        // Tope de concurrencia: por encima se responde 503 y se cierra, en vez
        // de lanzar threads sin limite.
        if (activeConns_.load(std::memory_order_relaxed) >= kMaxConcurrentConns) {
            http::sendResponse(fd, 503, "text/plain", "servidor saturado\n");
            http::closeFd(fd);
            log_.warn("senalizacion: conexion rechazada, tope de " + std::to_string(kMaxConcurrentConns) +
                          " conexiones",
                      -1);
            continue;
        }
        http::setNoDelay(fd);
        http::setSocketTimeout(fd, kHandshakeTimeoutMs);
        activeConns_.fetch_add(1, std::memory_order_relaxed);
        trackFd(fd);
#ifdef _WIN32
        const std::uint32_t clientIp = static_cast<std::uint32_t>(cli.sin_addr.S_un.S_addr);
#else
        const std::uint32_t clientIp = static_cast<std::uint32_t>(cli.sin_addr.s_addr);
#endif
        // Ultima red: un JSON con tipos inesperados (nlohmann lanza) no debe
        // tumbar el proceso desde un thread detachado.
        std::thread([this, fd, clientIp, admin] {
            try {
                handleConnection(fd, clientIp, admin);
            } catch (const std::exception& e) {
                log_.warn(std::string("conexion: excepcion ") + e.what(), -1);
            }
        }).detach();
    }
}

void SignalingServer::handleConnection(long long fd, std::uint32_t clientIp, bool admin) {
    // El thread esta detachado: el contador DEBE bajar por cualquier salida.
    struct ConnGuard {
        SignalingServer* self;
        long long fd;
        ~ConnGuard() {
            self->untrackFd(fd);
            http::closeFd(fd);
            self->activeConns_.fetch_sub(1, std::memory_order_relaxed);
        }
    } guard{this, fd};

    std::string headers;
    if (!http::readHeaders(fd, headers)) return;

    // PROXY v1 (del puente del tunel WireGuard o del terminador HTTPS, ambos
    // locales): la conexion llega desde 127.0.0.1 pero la IP que cuenta
    // (rate-limit, registro) es la del invitado. Solo se acepta desde loopback.
    if (headers.rfind("PROXY ", 0) == 0) {
        const size_t eol = headers.find("\r\n");
        const bool fromLoopback = ntohl(clientIp) == INADDR_LOOPBACK;
        if (eol == std::string::npos || !fromLoopback) return;
        char src[64] = {0};
        char dst[64] = {0};
        unsigned sp = 0;
        unsigned dp = 0;
        if (std::sscanf(headers.c_str(), "PROXY TCP4 %63s %63s %u %u", src, dst, &sp, &dp) == 4) {
            in_addr a{};
            if (inet_pton(AF_INET, src, &a) == 1) clientIp = static_cast<std::uint32_t>(a.s_addr);
        }
        headers.erase(0, eol + 2);
        // La cabecera PROXY puede haber llegado sola: se completa la peticion.
        if (headers.find("\r\n\r\n") == std::string::npos) {
            std::string rest;
            if (!http::readHeaders(fd, rest)) return;
            headers += rest;
        }
    }

    // Linea de peticion: METHOD TARGET HTTP/1.1
    const std::string line = headers.substr(0, headers.find("\r\n"));
    const size_t sp1 = line.find(' ');
    const size_t sp2 = line.rfind(' ');
    const std::string method = line.substr(0, sp1);
    const std::string target = (sp1 == std::string::npos || sp2 == std::string::npos || sp2 <= sp1)
                                   ? ""
                                   : line.substr(sp1 + 1, sp2 - sp1 - 1);
    std::string path = target;
    std::string query;
    if (const size_t q = target.find('?'); q != std::string::npos) {
        path = target.substr(0, q);
        query = target.substr(q + 1);
    }

    // Subida de archivo: el cuerpo va en streaming a disco, NO a memoria.
    if (!admin && method == "POST" && path == "/api/v1/files") {
        apiUpload(fd, headers, query);
        return;
    }

    // Cuerpo acotado (POST de API). Un Content-Length hostil se descarta.
    std::string body;
    const long long n = http::parseLong(http::getHeader(headers, "Content-Length"), 0);
    if (n > 0) {
        if (n >= static_cast<long long>(http::kMaxBodyBytes)) {
            http::sendResponse(fd, 413, "application/json", R"({"ok":false,"error":"too_large"})");
            return;
        }
        body.resize(static_cast<size_t>(n));
        if (!http::readExact(fd, reinterpret_cast<uint8_t*>(&body[0]), body.size())) return;
    }

    if (admin) {
        handleAdmin(fd, method, path, query, headers, body);
        return;
    }
    if (method == "POST" && path == "/api/v1/avatar") {
        apiAvatarUpload(fd, headers, body);
        return;
    }
    if (path.rfind("/api/v1/board", 0) == 0) {
        apiBoard(fd, method, path, query, headers, body);
        return;
    }

    // La botonera (página, emparejamiento y su WebSocket) antes que la sala.
    if (botonera_->atender(fd, method, path, query, headers, body, clientIp)) return;
    // La sesión remota (página y su WebSocket), también antes que la sala.
    if (sesion_->atender(fd, method, path, query, headers, clientIp)) return;
    std::string upgrade = http::getHeader(headers, "Upgrade");
    for (auto& ch : upgrade)
        ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
    if (upgrade == "websocket" && path == "/ws") {
        const std::string pid = wsOpen(fd, headers, query, clientIp);
        if (pid.empty()) return;
        const uint64_t gen = registerConn(pid, fd);
        // La pantalla no espera nada de la sala: se le dice ya que arme su
        // PeerConnection (un invitado espera a tener lugar).
        if (esPantalla(pid))
            sendToGuest(pid, json{{"type", "hello"},
                                  {"pantalla", true},
                                  {"id", pid},
                                  // Con esto vuelve sola si se le corta el WS:
                                  // el token de ingreso ya se gasto.
                                  {"key", claveDePantalla(pid)}});
        wsLoop(fd, pid, gen);
        const bool current = unregisterConn(pid, gen);
        releaseSession(pid, gen, false);
        if (pid.rfind("superficie:", 0) == 0) {
            superficies_.desconectar(pid);
            return;
        }
        if (esPantalla(pid)) {
            bajaPantalla(pid);
            return;
        }
        // Solo la conexion VIGENTE informa la desconexion: una vieja que cierra
        // tarde no debe marcar como caido a quien ya se reengancho.
        if (current) room_.disconnect(pid, nowMs());
        return;
    }
    handleGuestHttp(fd, method, path, query, body, clientIp);
}

// ============================================================================
// HTTP de invitados
// ============================================================================

void SignalingServer::handleGuestHttp(long long fd, const std::string& method, const std::string& path,
                                      const std::string& query, const std::string& body,
                                      std::uint32_t clientIp) {
    if (path == "/" || path == "/index.html") {
        serveStatic(fd, cfg_.webroot, "index.html");
        return;
    }
    if (path == "/healthz") {
        http::sendResponse(fd, 200, "text/plain", "ok\n");
        return;
    }
    if (path == "/api/join" && method == "POST") {
        apiJoin(fd, body, clientIp);
        return;
    }
    if (path == "/api/verify") {
        apiVerify(fd, query);
        return;
    }
    if (path == "/api/sala" && method == "GET") {
        // Lo poco que la pantalla de ingreso necesita saber ANTES de entrar.
        // Nada de esto es secreto: el nombre de la sala ya viaja en el enlace
        // y lo otro es si el asistente esta atendiendo, para no pedirle al
        // invitado datos que le va a preguntar el asistente por chat.
        http::sendResponse(fd, 200, "application/json",
                           json{{"ok", true}, {"room", cfg_.room}, {"pedirDatos", !room_.intake()}}.dump());
        return;
    }
    // --- Teleprompter -------------------------------------------------------
    // La tablet del orador (/prompter) y el control del celular
    // (/prompter/control) son la misma pagina con distinta vista.
    if (method == "GET" && (path == "/prompter" || path.rfind("/prompter/", 0) == 0)) {
        // Sin la barra final, el navegador resuelve "app.js" y "style.css"
        // contra la RAIZ, y ahi vive la app del invitado: la pagina sale con
        // el codigo equivocado y sin decir nada. Se redirige, como hace
        // cualquier servidor con un directorio.
        if (path == "/prompter") {
            http::sendResponse(fd, 301, "text/plain", "", "Location: /prompter/\r\n");
            return;
        }
        std::string rel = path.size() > 10 ? path.substr(10) : std::string("index.html");
        if (rel.empty() || rel == "control") rel = "index.html";
        if (!cfg_.prompterWebroot.empty() && serveStatic(fd, cfg_.prompterWebroot, rel)) return;
        http::sendResponse(fd, 404, "text/plain", "no encontrado\n");
        return;
    }
    if (path == "/api/prompter") {
        if (method == "GET") {
            // Quien pregunta queda anotado: la pagina manda su id, su vista y
            // (si alguien lo escribio) su nombre. Es lo que le muestra al
            // operador que la tablet del atril esta prendida.
            const auto q = http::parseQuery(query);
            const auto campo = [&q](const char* k) {
                return q.count(k) ? http::urlDecode(q.at(k)) : std::string();
            };
            prompter_.visto(campo("id"), campo("vista"), campo("nombre"), clientIp);
            // Publico: es el texto que el orador va a decir en voz alta en un
            // rato. Lo que se protege es CAMBIARLO, no verlo.
            http::sendResponse(fd, 200, "application/json", prompter_.estado().dump());
            return;
        }
        if (method == "POST") {
            const json req = json::parse(body, nullptr, false);
            if (!req.is_object()) {
                http::sendResponse(fd, 400, "application/json", R"({"ok":false,"error":"json"})");
                return;
            }
            // Sin clave no se comanda: frenarle el texto a alguien en medio de
            // un discurso, desde cualquier telefono de la LAN, seria un chiste
            // caro. La clave viaja en el enlace que le pasa el operador.
            if (cfg_.prompterKey.empty() || req.value("clave", std::string()) != cfg_.prompterKey) {
                http::sendResponse(fd, 403, "application/json", R"({"ok":false,"error":"clave"})");
                return;
            }
            prompter_.comandar(req);
            http::sendResponse(fd, 200, "application/json", prompter_.estado().dump());
            return;
        }
    }

    // --- Puesto de corrección de subtítulos (T44) ------------------------------
    // La página del corrector (/correccion/#k=<clave>) pide cada medio segundo
    // las frases que esperan y manda las correcciones. Con clave (por arranque):
    // corregir lo que sale al aire es poder escribir en pantalla.
    if (method == "GET" && (path == "/correccion" || path.rfind("/correccion/", 0) == 0)) {
        if (path == "/correccion") {
            http::sendResponse(fd, 301, "text/plain", "", "Location: /correccion/\r\n");
            return;
        }
        std::string rel = path.substr(std::string("/correccion/").size());
        if (rel.empty()) rel = "index.html";
        if (!cfg_.correccionWebroot.empty() && serveStatic(fd, cfg_.correccionWebroot, rel)) return;
        http::sendResponse(fd, 404, "text/plain", "no encontrado\n");
        return;
    }
    if (path == "/api/correccion") {
        const auto q = http::parseQuery(query);
        const json req = method == "POST" ? json::parse(body, nullptr, false) : json::object();
        const std::string clave = method == "POST" ? (req.is_object() ? req.value("k", std::string()) : std::string())
                                                   : (q.count("k") ? http::urlDecode(q.at("k")) : std::string());
        if (cfg_.correccionKey.empty() || clave != cfg_.correccionKey) {
            ipRecordFailure(clientIp);
            http::sendResponse(fd, 403, "application/json", R"({"ok":false,"error":"clave"})");
            return;
        }
        if (method == "GET") {
            std::lock_guard<std::mutex> l(correccionMu_);
            const auto ahora = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch()).count();
            http::sendResponse(fd, 200, "application/json",
                               json{{"pendientes", correccionPend_}, {"edad_ms", correccionMs_ ? ahora - correccionMs_ : 0}}.dump(),
                               "Cache-Control: no-store\r\n");
            return;
        }
        if (method == "POST" && req.is_object() && req.contains("id")) {
            // A la app (la fuente de verdad): ella decide si la frase todavía está.
            json ev{{"id", req["id"]}, {"descartar", req.value("descartar", false)}};
            if (req.contains("texto") && req["texto"].is_string()) {
                std::string t = req["texto"].get<std::string>();
                if (t.size() > 2000) t.resize(2000);
                ev["texto"] = t;
            }
            broadcastToHosts("correccion", ev);
            http::sendResponse(fd, 200, "application/json", R"({"ok":true})");
            return;
        }
        http::sendResponse(fd, 400, "application/json", R"({"ok":false,"error":"pedido"})");
        return;
    }

    // --- El latido de las superficies web -----------------------------------
    // Cada pagina del aparato avisa que esta viva y que muestra. Sin esto, que
    // el comando de mostrar salga bien no dice nada sobre si la pagina cargo.
    if (path == "/api/superficie" && method == "POST") {
        const json r = json::parse(body, nullptr, false);
        if (!r.is_object()) {
            http::sendResponse(fd, 400, "application/json", R"({"ok":false})");
            return;
        }
        superficies_.latido(r.value("tipo", ""), r.value("id", ""), r.value("que", ""), clientIp);
        http::sendResponse(fd, 200, "application/json", R"({"ok":true})");
        return;
    }

    // --- Lo que comparten TODAS las superficies ------------------------------
    // El canal de control (canal.js). Publico y estatico: es codigo, no datos.
    if (method == "GET" && path.rfind("/comun/", 0) == 0) {
        const std::string rel = path.substr(7);
        if (!rel.empty() && !cfg_.comunWebroot.empty() && serveStatic(fd, cfg_.comunWebroot, rel)) return;
        http::sendResponse(fd, 404, "text/plain", "no encontrado\n");
        return;
    }

    // --- El programa en vivo, para un televisor ------------------------------
    // Es la pagina del invitado sin las partes de invitado: entra por /ws con
    // un token, no publica nada y muestra lo que le baja. Estatica y sin
    // clave: el token va en la URL que le pasa la app.
    if (method == "GET" && (path == "/cast" || path.rfind("/cast/", 0) == 0)) {
        if (path == "/cast") {
            http::sendResponse(fd, 301, "text/plain", "", "Location: /cast/\r\n");
            return;
        }
        std::string rel = path.size() > 6 ? path.substr(6) : std::string("index.html");
        if (rel.empty()) rel = "index.html";
        if (!cfg_.castWebroot.empty() && serveStatic(fd, cfg_.castWebroot, rel)) return;
        http::sendResponse(fd, 404, "text/plain", "no encontrado\n");
        return;
    }

    // --- Lo que se manda a un televisor -------------------------------------
    // Publico y de solo lectura: es una pagina que MUESTRA lo que dice la URL.
    // No lleva clave porque no hay nada que proteger (quien la abre ve lo que
    // ya se esta viendo en una pantalla de la sala) y porque el televisor no
    // tiene donde guardar una.
    if (method == "GET" && (path == "/pantalla" || path.rfind("/pantalla/", 0) == 0)) {
        if (path == "/pantalla") {
            http::sendResponse(fd, 301, "text/plain", "", "Location: /pantalla/\r\n");
            return;
        }
        std::string rel = path.size() > 10 ? path.substr(10) : std::string("index.html");
        // Una subcarpeta con barra final ("carrusel/") es su index, no un
        // directorio: sin esto, /pantalla/carrusel/ daba 404.
        if (rel.empty() || rel.back() == '/') rel += "index.html";
        // La lista de un carrusel, para la pagina que lo rota. Publica y de
        // solo lectura, como el resto de /pantalla/: es lo que ya se esta
        // viendo en una pantalla de la sala.
        // El tally: lo que A publico (ver /api/v1/tally). Publico y de solo
        // lectura, como el resto de /pantalla/.
        if (rel == "tally.json") {
            std::lock_guard<std::mutex> lock(tallyLock_);
            http::sendResponse(fd, 200, "application/json", tally_.dump());
            return;
        }
        if (rel == "carrusel.json") {
            const auto q = http::parseQuery(query);
            const std::string id = q.count("id") ? http::urlDecode(q.at("id")) : std::string();
            const json c = carruseles_.uno(id);
            if (c.is_null()) {
                http::sendResponse(fd, 404, "application/json", R"({"error":"no existe ese carrusel"})");
                return;
            }
            http::sendResponse(fd, 200, "application/json", c.dump());
            return;
        }
        // El QR lo dibuja el servidor: asi la pagina no arrastra una
        // biblioteca de QR y anda igual en un televisor viejo.
        if (rel == "qr.svg") {
            const auto q = http::parseQuery(query);
            const std::string texto = q.count("texto") ? http::urlDecode(q.at("texto")) : std::string();
            if (texto.empty() || texto.size() > 1200) {
                http::sendResponse(fd, 400, "text/plain", "falta `texto` (o es demasiado largo)\n");
                return;
            }
            http::sendResponse(fd, 200, "image/svg+xml", qrSvg(texto, query));
            return;
        }
        if (!cfg_.pantallaWebroot.empty() && serveStatic(fd, cfg_.pantallaWebroot, rel)) return;
        http::sendResponse(fd, 404, "text/plain", "no encontrado\n");
        return;
    }

    // --- Rotulos desde el celular -------------------------------------------
    // La pagina es estatica y no lleva datos: se sirve sin clave. Los datos
    // (que es /api/rotulos) SI la piden, para mirar y para cambiar.
    if (method == "GET" && (path == "/rotulos" || path.rfind("/rotulos/", 0) == 0)) {
        if (path == "/rotulos") { // ver el comentario de /prompter
            http::sendResponse(fd, 301, "text/plain", "", "Location: /rotulos/\r\n");
            return;
        }
        std::string rel = path.size() > 9 ? path.substr(9) : std::string("index.html");
        if (rel.empty()) rel = "index.html";
        if (!cfg_.rotulosWebroot.empty() && serveStatic(fd, cfg_.rotulosWebroot, rel)) return;
        http::sendResponse(fd, 404, "text/plain", "no encontrado\n");
        return;
    }
    if (path == "/api/rotulos") {
        // La clave hace falta para las dos cosas. Sin clave configurada, la
        // superficie no existe: es preferible que no ande a que ande abierta.
        const json req = method == "POST" ? json::parse(body, nullptr, false) : json::object();
        std::string clave;
        if (method == "GET") {
            const auto q = http::parseQuery(query);
            if (q.count("k")) clave = http::urlDecode(q.at("k"));
        } else if (req.is_object()) {
            clave = req.value("clave", std::string());
        }
        if (cfg_.rotulosKey.empty() || clave != cfg_.rotulosKey) {
            http::sendResponse(fd, 403, "application/json", R"({"ok":false,"error":"clave"})");
            return;
        }
        if (method == "GET") {
            http::sendResponse(fd, 200, "application/json", rotulador_.estado().dump());
            return;
        }
        if (method == "POST") {
            if (!req.is_object() || !rotulador_.encargar(req)) {
                http::sendResponse(fd, 400, "application/json",
                                   R"({"ok":false,"error":"falta id de fuente"})");
                return;
            }
            http::sendResponse(fd, 200, "application/json", rotulador_.estado().dump());
            return;
        }
    }

    // --- La pagina del publico ----------------------------------------------
    // El que esta en la butaca, con su telefono. NO entra a la sala: no pide
    // camara ni microfono y no hay una conexion de video por persona. En un
    // salon con doscientas personas, entrar como participante seria un acople
    // garantizado y el ancho de banda tirado.
    //
    // La pagina es estatica y se sirve sin clave; los datos (/api/preguntas) SI
    // la piden.
    if (method == "GET" && (path == "/publico" || path.rfind("/publico/", 0) == 0)) {
        if (path == "/publico") { // ver el comentario de /prompter
            http::sendResponse(fd, 301, "text/plain", "", "Location: /publico/\r\n");
            return;
        }
        std::string rel = path.size() > 9 ? path.substr(9) : std::string("index.html");
        if (rel.empty()) rel = "index.html";
        if (!cfg_.publicoWebroot.empty() && serveStatic(fd, cfg_.publicoWebroot, rel)) return;
        http::sendResponse(fd, 404, "text/plain", "no encontrado\n");
        return;
    }
    // El fondo del evento. Va aparte del JSON a proposito: son doscientos
    // telefonos pidiendo cada tres segundos, y una imagen repetida en cada
    // respuesta es el mismo fondo mandado mil veces. Asi va una vez y el
    // navegador la cachea.
    if (method == "GET" && path == "/api/publico-fondo") {
        std::string bytes, tipo;
        {
            std::lock_guard<std::mutex> g(publicoM_);
            bytes = fondoBytes_;
            tipo = fondoTipo_;
        }
        if (bytes.empty()) {
            http::sendResponse(fd, 404, "text/plain", "sin fondo\n");
            return;
        }
        http::sendResponse(fd, 200, tipo.empty() ? "image/jpeg" : tipo, bytes,
                           "Cache-Control: max-age=300\r\n");
        return;
    }
    if (method == "GET" && path == "/api/publico-info") {
        const auto q = http::parseQuery(query);
        const std::string clave = q.count("k") ? http::urlDecode(q.at("k")) : std::string();
        if (cfg_.publicoKey.empty() || clave != cfg_.publicoKey) {
            http::sendResponse(fd, 403, "application/json", R"({"ok":false,"error":"clave"})");
            return;
        }
        json info;
        bool hayFondo = false;
        {
            std::lock_guard<std::mutex> g(publicoM_);
            info = publicoInfo_;
            hayFondo = !fondoBytes_.empty();
        }
        info["ok"] = true;
        if (hayFondo) info["fondo"] = "/api/publico-fondo";
        http::sendResponse(fd, 200, "application/json", info.dump());
        return;
    }
    // La urna, del lado del telefono. El `codigo` solo lo manda el que esta en
    // una votacion formal: en una encuesta no existe.
    if (path == "/api/votacion") {
        const json req = method == "POST" ? json::parse(body, nullptr, false) : json::object();
        std::string clave, quien, codigo;
        if (method == "GET") {
            const auto q = http::parseQuery(query);
            if (q.count("k")) clave = http::urlDecode(q.at("k"));
            if (q.count("yo")) quien = http::urlDecode(q.at("yo"));
            if (q.count("codigo")) codigo = http::urlDecode(q.at("codigo"));
        } else if (req.is_object()) {
            clave = req.value("clave", std::string());
            quien = req.value("yo", std::string());
            codigo = req.value("codigo", std::string());
        }
        if (cfg_.publicoKey.empty() || clave != cfg_.publicoKey) {
            http::sendResponse(fd, 403, "application/json", R"({"ok":false,"error":"clave"})");
            return;
        }
        if (quien.size() > 64) quien.resize(64);
        // El codigo viene de afuera y se compara contra una tabla: se recorta y
        // se pasa a mayusculas, porque el que lo tipea en el telefono lo
        // escribe como le sale.
        if (codigo.size() > 16) codigo.resize(16);
        for (char& c : codigo)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (method == "GET") {
            http::sendResponse(
                fd, 200, "application/json",
                json{{"ok", true}, {"votacion", votacion_.estadoPublico(quien, codigo)}}.dump());
            return;
        }
        if (method == "POST") {
            const int opcion = req.is_object() && req.contains("opcion") && req["opcion"].is_number_integer()
                                   ? req["opcion"].get<int>()
                                   : -1;
            const std::string motivo = votacion_.votar(quien, codigo, opcion);
            http::sendResponse(fd, 200, "application/json",
                               json{{"ok", motivo.empty()},
                                    {"error", motivo},
                                    {"votacion", votacion_.estadoPublico(quien, codigo)}}
                                   .dump());
            return;
        }
    }
    // Escanear la invitacion acredita. El QR lleva el codigo de la persona, y
    // el que llega no tiene que hacer nada mas que mirarlo con el telefono.
    if (method == "POST" && path == "/api/acreditar") {
        const json req = json::parse(body, nullptr, false);
        const std::string clave = req.is_object() ? req.value("clave", std::string()) : "";
        if (cfg_.publicoKey.empty() || clave != cfg_.publicoKey) {
            http::sendResponse(fd, 403, "application/json", R"({"ok":false,"error":"clave"})");
            return;
        }
        std::string codigo = req.value("codigo", std::string());
        if (codigo.size() > 16) codigo.resize(16);
        for (char& c : codigo)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (codigo.empty()) {
            http::sendResponse(fd, 400, "application/json", R"({"ok":false,"error":"sin codigo"})");
            return;
        }
        {
            std::lock_guard<std::mutex> g(publicoM_);
            // Tope: el buzon no puede crecer sin fin si A no viene a vaciarlo.
            // Repetido no molesta —acreditar dos veces no mueve la hora— pero
            // tampoco hace falta guardarlo dos veces.
            if (acreditaciones_.size() < 500 &&
                std::find(acreditaciones_.begin(), acreditaciones_.end(), codigo) == acreditaciones_.end())
                acreditaciones_.push_back(codigo);
        }
        // Se contesta que si SIEMPRE: B no sabe si ese codigo existe (el padron
        // lo tiene A), y decirle «no se» al que acaba de llegar no le sirve.
        // Si el codigo no esta en el padron, A lo avisa del lado del operador.
        http::sendResponse(fd, 200, "application/json", R"({"ok":true})");
        return;
    }
    if (path == "/api/preguntas") {
        const json req = method == "POST" ? json::parse(body, nullptr, false) : json::object();
        std::string clave;
        std::string quien;
        if (method == "GET") {
            const auto q = http::parseQuery(query);
            if (q.count("k")) clave = http::urlDecode(q.at("k"));
            if (q.count("yo")) quien = http::urlDecode(q.at("yo"));
        } else if (req.is_object()) {
            clave = req.value("clave", std::string());
            quien = req.value("yo", std::string());
        }
        if (cfg_.publicoKey.empty() || clave != cfg_.publicoKey) {
            http::sendResponse(fd, 403, "application/json", R"({"ok":false,"error":"clave"})");
            return;
        }
        // `yo` lo inventa el navegador y se guarda en el telefono. NO es
        // identidad: sirve para que cada uno vea las suyas y para que un toque
        // repetido no sume diez votos. Se recorta porque viene de afuera.
        if (quien.size() > 64) quien.resize(64);
        if (method == "GET") {
            http::sendResponse(fd, 200, "application/json",
                               json{{"ok", true},
                                    {"preguntas", preguntas_.listaPublica(quien)},
                                    {"recuento", preguntas_.recuento()}}
                                   .dump());
            return;
        }
        if (method == "POST") {
            if (!req.is_object()) {
                http::sendResponse(fd, 400, "application/json", R"({"ok":false,"error":"json"})");
                return;
            }
            if (req.contains("votar")) {
                const bool sumo = preguntas_.votar(req["votar"].get<std::string>(), quien);
                http::sendResponse(fd, 200, "application/json",
                                   json{{"ok", sumo}, {"preguntas", preguntas_.listaPublica(quien)}}.dump());
                return;
            }
            const std::string id =
                preguntas_.preguntar(req.value("nombre", std::string()), req.value("texto", std::string()),
                                     "publico", quien, nowMs());
            if (id.empty()) {
                http::sendResponse(fd, 400, "application/json",
                                   R"({"ok":false,"error":"la pregunta esta vacia"})");
                return;
            }
            http::sendResponse(
                fd, 200, "application/json",
                json{{"ok", true}, {"id", id}, {"preguntas", preguntas_.listaPublica(quien)}}.dump());
            return;
        }
    }

    if (method == "GET" && (path == "/board" || path.rfind("/board/", 0) == 0)) {
        if (path == "/board") { // ver el comentario de /prompter
            http::sendResponse(fd, 301, "text/plain", "", "Location: /board/\r\n");
            return;
        }
        std::string rel = path.size() > 7 ? path.substr(7) : std::string("index.html");
        if (rel.empty()) rel = "index.html";
        if (!cfg_.boardWebroot.empty() && serveStatic(fd, cfg_.boardWebroot, rel)) return;
        http::sendResponse(fd, 404, "text/plain", "no encontrado\n");
        return;
    }
    if (!path.empty() && path[0] == '/' && method == "GET") {
        if (serveStatic(fd, cfg_.webroot, path.substr(1))) return;
    }
    http::sendResponse(fd, 404, "text/plain", "no encontrado\n");
}

bool SignalingServer::serveStatic(long long fd, const std::string& root, const std::string& relPath) {
    // Contra path traversal por CANONICALIZACION: la ruta resuelta debe quedar
    // dentro del webroot resuelto.
    std::error_code ec;
    const std::filesystem::path rootCanon = std::filesystem::weakly_canonical(root, ec);
    if (ec || root.empty()) {
        http::sendResponse(fd, 404, "text/plain", "no encontrado\n");
        return true;
    }
    const std::filesystem::path target =
        std::filesystem::weakly_canonical(std::filesystem::path(root) / http::urlDecode(relPath), ec);
    if (ec) {
        http::sendResponse(fd, 400, "text/plain", "ruta invalida\n");
        return true;
    }
    const auto rootStr = rootCanon.generic_string();
    const auto targetStr = target.generic_string();
    if (targetStr.size() < rootStr.size() || targetStr.compare(0, rootStr.size(), rootStr) != 0 ||
        (targetStr.size() > rootStr.size() && targetStr[rootStr.size()] != '/')) {
        http::sendResponse(fd, 400, "text/plain", "ruta invalida\n");
        return true;
    }
    std::ifstream f(target, std::ios::binary);
    if (!f || std::filesystem::is_directory(target, ec)) {
        http::sendResponse(fd, 404, "text/plain", "no encontrado\n");
        return true;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    // `no-cache` NO quiere decir «no guardes»: quiere decir «preguntá antes de
    // usar lo guardado». Sin esto el navegador decide solo cuanto se queda con
    // un archivo, y como los nombres no llevan version, un telefono se queda
    // con el app.js de la actualizacion anterior y lo arreglado no aparece.
    // Pasa siempre y no se nota: la pagina carga, anda, y anda como antes.
    http::sendResponse(fd, 200, contentTypeFor(relPath), ss.str(), "Cache-Control: no-cache\r\n");
    return true;
}

void SignalingServer::apiJoin(long long fd, const std::string& body, std::uint32_t clientIp) {
    // Rate-limit por IP ANTES de tocar el validador de PIN.
    if (!ipAttemptAllowed(clientIp)) {
        http::sendResponse(fd, 429, "application/json", R"({"ok":false,"error":"rate"})");
        return;
    }
    const json req = json::parse(body, nullptr, false);
    if (!req.is_object()) {
        http::sendResponse(fd, 400, "application/json", R"({"ok":false,"error":"json"})");
        return;
    }
    const std::string room = req.value("room", cfg_.room);
    const std::string pin = req.value("pin", "");
    if (room_.snapshot().value("locked", false)) {
        http::sendResponse(fd, 423, "application/json", R"({"ok":false,"error":"locked"})");
        return;
    }
    const std::string token = auth_.issue(room, pin);
    if (token.empty()) {
        ipRecordFailure(clientIp);
        http::sendResponse(fd, 401, "application/json", R"({"ok":false,"error":"pin"})");
        return;
    }
    ipRecordSuccess(clientIp);
    // El slot ya no se reserva aca: se asigna al ADMITIR (el lobby decide).
    const json resp{{"ok", true},
                    {"token", token},
                    {"room", room},
                    {"maxGuests", cfg_.maxGuests},
                    {"ttlSec", auth_.ttlSec()},
                    {"maxUploadBytes", shares_.maxBytes()}};
    http::sendResponse(fd, 200, "application/json", resp.dump());
}

void SignalingServer::apiVerify(long long fd, const std::string& query) {
    const auto q = http::parseQuery(query);
    const std::string token = q.count("token") ? http::urlDecode(q.at("token")) : "";
    const std::string room = q.count("room") ? http::urlDecode(q.at("room")) : cfg_.room;
    const int code = auth_.verify(token, room);
    if (code == 0) {
        http::sendResponse(fd, 200, "application/json", R"({"ok":true})");
    } else {
        http::sendResponse(fd, 401, "application/json", json{{"ok", false}, {"code", code}}.dump());
    }
}

void SignalingServer::apiUpload(long long fd, const std::string& headers, const std::string& query) {
    Room::Participant who;
    if (!room_.admittedByKey(http::getHeader(headers, "X-BA-Key"), who)) {
        http::sendResponse(fd, 401, "application/json", R"({"ok":false,"error":"auth"})");
        return;
    }
    if (static_cast<int>(room_.sharesOf(who.id).size()) >= cfg_.maxSharesPerGuest) {
        http::sendResponse(fd, 429, "application/json", R"({"ok":false,"error":"quota"})");
        return;
    }
    const long long total = http::parseLong(http::getHeader(headers, "Content-Length"), -1);
    if (total <= 0) {
        http::sendResponse(fd, 411, "application/json", R"({"ok":false,"error":"length"})");
        return;
    }
    if (static_cast<uint64_t>(total) > shares_.maxBytes()) {
        http::sendResponse(fd, 413, "application/json", R"({"ok":false,"error":"too_large"})");
        return;
    }
    // Una subida grande tarda: el timeout es por recv (sin datos 60 s = cortar).
    http::setSocketTimeout(fd, 60000);
    const auto res = shares_.save(static_cast<uint64_t>(total),
                                  [fd](uint8_t* buf, size_t n) { return http::recvSome(fd, buf, n); });
    if (res.err == ShareStore::kUnsupported) {
        http::sendResponse(fd, 415, "application/json", R"({"ok":false,"error":"type"})");
        return;
    }
    if (res.err != ShareStore::kOk) {
        log_.warn("archivos: subida fallida de " + who.name + " (codigo " + std::to_string(res.err) + ")",
                  -1);
        http::sendResponse(fd, res.err == ShareStore::kTooLarge ? 413 : 400, "application/json",
                           R"({"ok":false,"error":"io"})");
        return;
    }
    const auto q = http::parseQuery(query);
    Room::Share s;
    s.participantId = who.id;
    s.name = Room::cleanName(q.count("name") ? http::urlDecode(q.at("name")) : std::string("archivo"));
    s.kind = res.kind.kind;
    s.mime = res.kind.mime;
    {
        const std::u8string u = res.path.u8string();
        s.path.assign(u.begin(), u.end());
    }
    s.bytes = res.bytes;
    const std::string id = room_.addShare(s, nowMs());
    log_.info("archivos: " + who.name + " compartio '" + s.name + "' (" + s.mime + ", " +
              std::to_string(res.bytes) + " bytes)");
    http::sendResponse(fd, 201, "application/json", json{{"ok", true}, {"id", id}, {"kind", s.kind}}.dump());
}

// ============================================================================
// Oradores: fotos de las tarjetas y tablero de solo lectura
// ============================================================================

void SignalingServer::apiAvatarUpload(long long fd, const std::string& headers, const std::string& body) {
    // Cualquiera de la sala (tambien el lobby): la tarjeta lo muestra antes de
    // que hable. Solo JPEG chico: lo arma el navegador con un canvas.
    Room::Participant who;
    if (!room_.byKey(http::getHeader(headers, "X-BA-Key"), who)) {
        http::sendResponse(fd, 401, "application/json", R"({"ok":false,"error":"auth"})");
        return;
    }
    constexpr size_t kMaxAvatar = 64 * 1024;
    const bool jpeg = body.size() >= 4 && static_cast<uint8_t>(body[0]) == 0xFF &&
                      static_cast<uint8_t>(body[1]) == 0xD8 && static_cast<uint8_t>(body[2]) == 0xFF;
    if (!jpeg || body.size() > kMaxAvatar) {
        http::sendResponse(fd, body.size() > kMaxAvatar ? 413 : 415, "application/json",
                           R"({"ok":false,"error":"type"})");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(avatarsLock_);
        avatars_[who.id] = body;
        // Limpieza perezosa: las fotos de quien ya no esta en la sala.
        for (auto it = avatars_.begin(); it != avatars_.end();) {
            Room::Participant p;
            it = room_.participant(it->first, p) ? std::next(it) : avatars_.erase(it);
        }
    }
    room_.setHasAvatar(who.id);
    http::sendResponse(fd, 200, "application/json", R"({"ok":true})");
}

std::string SignalingServer::avatarOf(const std::string& id) {
    std::lock_guard<std::mutex> lock(avatarsLock_);
    const auto it = avatars_.find(id);
    if (it == avatars_.end()) return {};
    Room::Participant p;
    if (!room_.participant(id, p)) {
        avatars_.erase(it);
        return {};
    }
    return it->second;
}

void SignalingServer::sendAvatar(long long fd, const std::string& id) {
    const std::string img = avatarOf(id);
    if (img.empty()) {
        http::sendResponse(fd, 404, "text/plain", "sin foto\n");
        return;
    }
    http::sendResponse(fd, 200, "image/jpeg", img, "Cache-Control: private, max-age=10\r\n");
}

void SignalingServer::apiBoard(long long fd, const std::string& method, const std::string& path,
                               const std::string& query, const std::string& headers,
                               const std::string& body) {
    // Clave por arranque: la muestra la consola del operador (enlace/QR del
    // tablero). Comparacion en tiempo constante.
    const auto q = http::parseQuery(query);
    std::string key = http::getHeader(headers, "X-BA-Board");
    if (key.empty() && q.count("k")) key = http::urlDecode(q.at("k"));
    bool ok = !boardToken_.empty() && key.size() == boardToken_.size();
    unsigned diff = 0;
    for (size_t i = 0; ok && i < key.size(); ++i)
        diff |= static_cast<unsigned>(key[i] ^ boardToken_[i]);
    if (!ok || diff != 0) {
        http::sendResponse(fd, 401, "application/json", R"({"ok":false,"error":"auth"})");
        return;
    }
    if (path == "/api/v1/board/view" && method == "POST") {
        apiView(fd, body, true);
        return;
    }
    static const std::string kView = "/api/v1/board/view/";
    if (path.rfind(kView, 0) == 0) {
        apiViewAction(fd, method, path.substr(kView.size()), true);
        return;
    }
    if (method != "GET") {
        http::sendResponse(fd, 405, "application/json", R"({"ok":false,"error":"method"})");
        return;
    }
    if (path == "/api/v1/board") {
        http::sendResponse(fd, 200, "application/json", room_.boardSnapshot(nowMs()).dump(),
                           "Cache-Control: no-store\r\n");
        return;
    }
    static const std::string kAv = "/api/v1/board/avatar/";
    if (path.rfind(kAv, 0) == 0) {
        sendAvatar(fd, path.substr(kAv.size()));
        return;
    }
    http::sendResponse(fd, 404, "text/plain", "no encontrado\n");
}

// ============================================================================
// Espectadores: video en vivo de los participantes para las vistas web
// ============================================================================

std::string SignalingServer::publicSdp(const std::string& sdp) const {
    std::string out;
    size_t pos = 0;
    while (pos < sdp.size()) {
        size_t eol = sdp.find('\n', pos);
        if (eol == std::string::npos) eol = sdp.size();
        std::string line = sdp.substr(pos, eol - pos + (eol < sdp.size() ? 1 : 0));
        pos = eol + 1;
        if (line.rfind("a=candidate:", 0) == 0) {
            std::string cand = line.substr(2);
            while (!cand.empty() && (cand.back() == '\n' || cand.back() == '\r'))
                cand.pop_back();
            const std::string rewritten = publicCandidate(cand);
            if (rewritten.empty()) continue; // loopback sin tunel: no le sirve a nadie
            line = "a=" + rewritten + "\r\n";
        }
        out += line;
    }
    return out;
}

void SignalingServer::apiView(long long fd, const std::string& body, bool board) {
    const json req = json::parse(body, nullptr, false);
    if (!req.is_object() || !req.contains("participant") || !req["participant"].is_string() ||
        !req.contains("sdp") || !req["sdp"].is_string()) {
        http::sendResponse(fd, 400, "application/json", R"({"ok":false,"error":"json"})");
        return;
    }
    const std::string pid = req["participant"].get<std::string>();
    const bool screen = req.contains("kind") && req["kind"] == "screen";
    if (board) {
        // El tablero solo ve a los oradores de la fila: nunca el lobby.
        const json order = room_.snapshot().value("order", json::array());
        if (std::find(order.begin(), order.end(), json(pid)) == order.end()) {
            http::sendResponse(fd, 404, "application/json", R"({"ok":false,"error":"not_found"})");
            return;
        }
    }
    int idx = 0;
    {
        std::lock_guard<std::mutex> lock(sessLock_);
        const auto it = sessionOf_.find(pid);
        if (it != sessionOf_.end()) idx = it->second;
    }
    if (idx == 0) {
        // Todavia sin medios (o sin lugar para publicar): la vista reintenta.
        http::sendResponse(fd, 409, "application/json", R"({"ok":false,"error":"no_media"})");
        return;
    }
    std::string answer;
    const int viewer = hub_.openViewer(idx, screen, req["sdp"].get<std::string>(), answer);
    if (viewer <= 0) {
        http::sendResponse(fd, 503, "application/json", R"({"ok":false,"error":"unavailable"})");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(viewersLock_);
        viewerOwners_[viewer] = board;
    }
    http::sendResponse(fd, 200, "application/json",
                       json{{"ok", true}, {"viewer", viewer}, {"sdp", publicSdp(answer)}}.dump());
}

void SignalingServer::apiViewAction(long long fd, const std::string& method, const std::string& rest,
                                    bool board) {
    const size_t slash = rest.find('/');
    const int viewer = std::atoi(rest.substr(0, slash).c_str());
    const std::string action = slash == std::string::npos ? "" : rest.substr(slash + 1);
    bool owned = false;
    {
        std::lock_guard<std::mutex> lock(viewersLock_);
        const auto it = viewerOwners_.find(viewer);
        // El tablero solo toca sus propios espectadores; el operador, todos.
        owned = it != viewerOwners_.end() && (!board || it->second);
        if (owned && method == "DELETE" && action.empty()) viewerOwners_.erase(it);
    }
    if (!owned) {
        http::sendResponse(fd, 404, "application/json", R"({"ok":false,"error":"not_found"})");
        return;
    }
    if (method == "DELETE" && action.empty()) {
        hub_.closeViewer(viewer);
    } else if (method == "POST" && action == "keyframe") {
        hub_.requestKeyFrame(viewer);
    } else {
        http::sendResponse(fd, 404, "application/json", R"({"ok":false,"error":"route"})");
        return;
    }
    http::sendResponse(fd, 200, "application/json", R"({"ok":true})");
}

// ============================================================================
// WebSocket de sala
// ============================================================================

bool SignalingServer::esPantalla(const std::string& pid) const {
    std::lock_guard<std::mutex> lock(pantallasLock_);
    return pantallas_.find(pid) != pantallas_.end();
}

std::string SignalingServer::altaPantalla(std::string& claveOut, int canal) {
    std::string hex, clave;
    if (!ba::security::randomSecretHex(16, hex)) return {};
    if (!ba::security::randomSecretHex(16, clave)) return {};
    // Prefijo propio: los participantes de la sala son "p"+hex, asi que un id
    // de pantalla no puede chocar con uno de invitado ni pasar por tal.
    const std::string id = "pantalla-" + hex.substr(0, 12);
    std::lock_guard<std::mutex> lock(pantallasLock_);
    // Se limpian las que hace rato que no vuelven. Se hace aca (y no con un
    // temporizador) porque es el unico momento en que la tabla crece.
    const std::int64_t ahora = nowMs();
    for (auto it = pantallas_.begin(); it != pantallas_.end();)
        it = (it->second.sueltaMs != 0 && ahora - it->second.sueltaMs > kPantallaTtlMs) ? pantallas_.erase(it)
                                                                                        : std::next(it);
    // Con la tabla llena, se desaloja a la que hace mas que esta suelta. Una
    // conectada no se toca: esa esta mostrando algo ahora mismo.
    while (pantallas_.size() >= kMaxPantallas) {
        auto peor = pantallas_.end();
        for (auto it = pantallas_.begin(); it != pantallas_.end(); ++it)
            if (it->second.sueltaMs != 0 &&
                (peor == pantallas_.end() || it->second.sueltaMs < peor->second.sueltaMs))
                peor = it;
        if (peor == pantallas_.end()) return {}; // todas conectadas: no hay lugar
        pantallas_.erase(peor);
    }
    pantallas_[id] = Pantalla{clave, canal, 0};
    claveOut = clave;
    return id;
}

std::string SignalingServer::claveDePantalla(const std::string& pid) const {
    std::lock_guard<std::mutex> lock(pantallasLock_);
    const auto it = pantallas_.find(pid);
    return it == pantallas_.end() ? std::string() : it->second.clave;
}

int SignalingServer::canalDePantalla(const std::string& pid) const {
    std::lock_guard<std::mutex> lock(pantallasLock_);
    const auto it = pantallas_.find(pid);
    return it == pantallas_.end() ? -1 : it->second.canal;
}

bool SignalingServer::reengancharPantalla(const std::string& pid, const std::string& clave, int canal) {
    if (pid.empty() || clave.empty()) return false;
    std::lock_guard<std::mutex> lock(pantallasLock_);
    const auto it = pantallas_.find(pid);
    if (it == pantallas_.end()) return false;
    if (it->second.clave != clave) return false;
    it->second.sueltaMs = 0;
    it->second.canal = canal;
    return true;
}

void SignalingServer::bajaPantalla(const std::string& pid) {
    std::lock_guard<std::mutex> lock(pantallasLock_);
    const auto it = pantallas_.find(pid);
    // No se borra: se marca suelta, para que pueda volver con su clave.
    if (it != pantallas_.end()) it->second.sueltaMs = nowMs();
}

/// Un offer es de solo recepcion si NINGUNA de sus pistas sube. En SDP eso se
/// lee en cada seccion "m=": recvonly o inactive esta bien; sendonly, sendrecv
/// o que no diga nada (el valor por omision es sendrecv) NO.
///
/// Se mira el SDP y no lo que prometa el cliente porque el token de ingreso lo
/// puede tener cualquiera: alcanza con abrir la URL del televisor en otra
/// maquina. Lo que garantiza que una pantalla no sea un microfono abierto en
/// la sala es esto.
bool SignalingServer::soloRecibe(const std::string& sdp) {
    bool enSeccion = false;
    bool direccionVista = false;
    bool hayMedios = false;
    size_t pos = 0;
    while (pos < sdp.size()) {
        size_t fin = sdp.find('\n', pos);
        std::string linea = sdp.substr(pos, fin == std::string::npos ? std::string::npos : fin - pos);
        pos = fin == std::string::npos ? sdp.size() : fin + 1;
        if (!linea.empty() && linea.back() == '\r') linea.pop_back();
        if (linea.rfind("m=", 0) == 0) {
            // La seccion anterior tenia que haber declarado su direccion.
            if (enSeccion && !direccionVista) return false;
            enSeccion = true;
            direccionVista = false;
            // Solo importan audio y video: los canales de datos no llevan medios.
            hayMedios = linea.rfind("m=audio", 0) == 0 || linea.rfind("m=video", 0) == 0;
            if (!hayMedios) direccionVista = true; // no se le exige direccion
            continue;
        }
        if (!enSeccion || !hayMedios) continue;
        if (linea == "a=recvonly" || linea == "a=inactive") {
            direccionVista = true;
        } else if (linea == "a=sendrecv" || linea == "a=sendonly") {
            return false;
        }
    }
    return !enSeccion || direccionVista;
}

std::string SignalingServer::wsOpen(long long fd, const std::string& headers, const std::string& query,
                                    std::uint32_t clientIp) {
    const std::string wsKey = http::getHeader(headers, "Sec-WebSocket-Key");
    if (wsKey.empty()) {
        http::sendResponse(fd, 400, "text/plain", "falta Sec-WebSocket-Key\n");
        return {};
    }
    const auto q = http::parseQuery(query);
    const auto arg = [&](const char* k) { return q.count(k) ? http::urlDecode(q.at(k)) : std::string(); };

    // UNA SUPERFICIE del aparato (una pantalla, un carrusel, el teleprompter,
    // los rotulos). Entra sin token: lo que hace por este canal es DECIR que
    // esta viva y que muestra, y recibir ordenes de que mostrar. No lee ni
    // toca nada de la sala. Lo que se protege en este aparato es cambiar
    // cosas, y eso sigue pasando por donde pasaba.
    if (!arg("superficie").empty()) {
        const std::string accept =
            http::base64(http::sha1(wsKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").data(), 20);
        const std::string ok = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                               "Connection: Upgrade\r\nSec-WebSocket-Accept: " +
                               accept + "\r\n\r\n";
        if (!http::sendAll(fd, ok.data(), ok.size())) return {};
        http::setSocketTimeout(fd, 0);
        // El id lo elige la pagina y lo guarda: asi recargarse no la hace
        // aparecer como una superficie nueva.
        std::string id = arg("id");
        if (id.empty() || id.size() > 64) {
            std::string hex;
            if (!ba::security::randomSecretHex(8, hex)) return {};
            id = "sup-" + hex;
        }
        const std::string pid = "superficie:" + id;
        superficies_.conectar(pid, arg("superficie"), id);
        log_.info("ws: superficie " + arg("superficie") + " (" + id + ") conectada");
        return pid;
    }

    // Una PANTALLA entra por su propia puerta: mismo token, pero no se
    // conecta a la sala. Se resuelve antes que nada para que ninguna de las
    // ramas de invitado la toque.
    if (arg("pantalla") == "1") {
        // Dos formas de entrar: token de ingreso (la primera vez) o reenganche
        // con la clave que se le dio en el "hello". El token es de UN SOLO
        // USO, asi que sin el reenganche una pantalla que pierde la conexion
        // no podria volver jamas.
        const bool vuelve = !arg("resume").empty();
        // `canal`: la escena que muestra (BA.Escena.<k>, la elige la app al
        // pedir el enlace); sin él, el programa.
        int canal = -1;
        if (!arg("canal").empty()) {
            try {
                canal = std::stoi(arg("canal"));
            } catch (...) {
                canal = -1;
            }
            if (canal < 0 || canal >= ba::ipc::kCanalesEscena) canal = -1;
        }
        if (vuelve) {
            if (!reengancharPantalla(arg("resume"), arg("key"), canal)) {
                http::sendResponse(fd, 401, "text/plain", "reenganche invalido\n");
                return {};
            }
        } else {
            const std::string room = arg("room").empty() ? cfg_.room : arg("room");
            if (auth_.redeem(arg("token"), room) != 0) {
                http::sendResponse(fd, 401, "text/plain", "token invalido\n");
                return {};
            }
        }
        const std::string ok =
            "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " +
            http::base64(http::sha1(wsKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").data(), 20) + "\r\n\r\n";
        if (!http::sendAll(fd, ok.data(), ok.size())) return {};
        http::setSocketTimeout(fd, 0);
        std::string id = arg("resume"), clave;
        if (!vuelve) {
            id = altaPantalla(clave, canal);
            if (id.empty()) {
                http::writeWsFrame(fd, 0x8, "");
                return {};
            }
        }
        log_.info("ws: pantalla " + id + (vuelve ? " volvio" : " conectada") + " (solo mira" +
                  (canal >= 0 ? ", escena del canal " + std::to_string(canal) : std::string()) + ")");
        return id;
    }

    // Dos formas de entrar: token nuevo (entra al lobby) o reenganche con la
    // clave que la sala le dio al participante en su "hello".
    std::string pid;
    bool resumed = false;
    if (!arg("resume").empty()) {
        pid = arg("resume");
        resumed = true;
        // La validacion de la clave la hace la sala despues del 101 (para
        // poder avisar el motivo); aca solo se verifica que exista.
        Room::Participant p;
        if (!room_.participant(pid, p) || p.key != arg("key")) {
            http::sendResponse(fd, 401, "text/plain", "sesion vencida\n");
            return {};
        }
    } else {
        const std::string room = arg("room").empty() ? cfg_.room : arg("room");
        // Se CANJEA el token (un solo uso): un replay dentro del TTL se rechaza.
        const int vcode = auth_.redeem(arg("token"), room);
        if (vcode != 0) {
            log_.fmt("warn", vcode, "ws: token invalido (code=%d)", vcode);
            http::sendResponse(fd, 401, "text/plain", "token invalido\n");
            return {};
        }
    }

    const std::string accept =
        http::base64(http::sha1(wsKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").data(), 20);
    const std::string resp = "HTTP/1.1 101 Switching Protocols\r\n"
                             "Upgrade: websocket\r\n"
                             "Connection: Upgrade\r\n"
                             "Sec-WebSocket-Accept: " +
                             accept + "\r\n\r\n";
    if (!http::sendAll(fd, resp.data(), resp.size())) return {};
    // Promovida: sin timeout (un invitado puede estar callado minutos). El
    // desbloqueo al cerrar lo da stop()/dropGuest con shutdown().
    http::setSocketTimeout(fd, 0);

    if (resumed) {
        // Se registra ANTES de reenganchar para que el "hello" le llegue.
        const uint64_t gen = registerConn(pid, fd);
        if (room_.resume(pid, arg("key"), nowMs()) != Room::kOk) {
            unregisterConn(pid, gen);
            http::writeWsFrame(fd, 0x8, "");
            return {};
        }
        log_.info("ws: " + pid + " reenganchado");
        return pid;
    }

    // Alta: el id se elige ACA para registrar la conexion antes de entrar a
    // la sala; asi el "hello" (y un "admitted" inmediato si el lobby esta
    // apagado) ya encuentran por donde salir.
    std::string hex;
    if (!ba::security::randomSecretHex(16, hex)) {
        http::writeWsFrame(fd, 0x8, "");
        return {};
    }
    const std::string newId = "p" + hex.substr(0, 12);
    const uint64_t gen = registerConn(newId, fd);
    const Room::Joined j =
        room_.connect(arg("name"), ipText(clientIp), nowMs(), newId, arg("info"), arg("contact"));
    if (j.err != Room::kOk) {
        unregisterConn(newId, gen);
        const char* why = j.err == Room::kLocked      ? "locked"
                          : j.err == Room::kLobbyFull ? "lobby_full"
                                                      : "error";
        const auto frame = Frame::encodeFrame(json{{"type", "rejected"}, {"reason", why}}.dump());
        http::writeWsFrame(fd, 0x2, std::string(frame.begin(), frame.end()));
        http::writeWsFrame(fd, 0x8, "");
        return {};
    }
    log_.info("ws: '" + Room::cleanName(arg("name")) + "' entro a la sala (" + j.id + ")");
    return j.id;
}

uint64_t SignalingServer::registerConn(const std::string& pid, long long fd) {
    std::lock_guard<std::mutex> lock(connsLock_);
    auto it = conns_.find(pid);
    if (it != conns_.end() && it->second->fd == fd) return it->second->gen; // ya registrada
    auto conn = std::make_shared<WsConn>();
    conn->fd = fd;
    conn->gen = ++nextGen_;
    if (it != conns_.end()) {
        // Reenganche con una conexion vieja todavia abierta: se la corta.
        http::shutdownFd(it->second->fd);
    }
    conns_[pid] = conn;
    return conn->gen;
}

bool SignalingServer::unregisterConn(const std::string& pid, uint64_t gen) {
    std::shared_ptr<WsConn> conn;
    {
        std::lock_guard<std::mutex> lock(connsLock_);
        const auto it = conns_.find(pid);
        if (it == conns_.end() || it->second->gen != gen) return false;
        conn = it->second;
        conns_.erase(it);
    }
    // Nadie mas escribe en este fd: el thread de conexion lo va a cerrar.
    std::lock_guard<std::mutex> lock(conn->sendLock);
    conn->fd = -1;
    return true;
}

std::shared_ptr<SignalingServer::WsConn> SignalingServer::connFor(const std::string& pid) {
    std::lock_guard<std::mutex> lock(connsLock_);
    const auto it = conns_.find(pid);
    return it == conns_.end() ? nullptr : it->second;
}

void SignalingServer::sendToGuest(const std::string& pid, const json& msg) {
    const auto conn = connFor(pid);
    if (!conn) return;
    const auto frame = Frame::encodeFrame(msg.dump());
    std::lock_guard<std::mutex> lock(conn->sendLock);
    if (conn->fd >= 0) http::writeWsFrame(conn->fd, 0x2, std::string(frame.begin(), frame.end()));
}

void SignalingServer::dropGuest(const std::string& pid) {
    const auto conn = connFor(pid);
    if (!conn) return;
    std::lock_guard<std::mutex> lock(conn->sendLock);
    if (conn->fd >= 0) {
        http::writeWsFrame(conn->fd, 0x8, "");
        http::shutdownFd(conn->fd);
    }
}

void SignalingServer::releaseSession(const std::string& pid, uint64_t gen, bool anyGen) {
    int idx = 0;
    {
        std::lock_guard<std::mutex> lock(sessLock_);
        const auto it = sessionOf_.find(pid);
        if (it == sessionOf_.end()) return;
        const auto own = sessionOwner_.find(it->second);
        if (own != sessionOwner_.end() && !anyGen && own->second.gen != gen) return;
        idx = it->second;
        sessionOwner_.erase(it->second);
        sessionOf_.erase(it);
    }
    if (pump_ != nullptr) pump_->setRoute(idx, MediaRoute{});
    hub_.closeViewersOf(idx); // quien lo miraba deja de verlo (la sesion se reusa)
    if (WebrtcSession* s = hub_.session(idx)) s->close();
}

void SignalingServer::applyRoute(const std::string& pid, const MediaRoute& route) {
    if (route.kind == MediaRoute::Kind::None) {
        releaseSession(pid, 0, true);
        return;
    }
    int idx = 0;
    {
        std::lock_guard<std::mutex> lock(sessLock_);
        const auto it = sessionOf_.find(pid);
        if (it != sessionOf_.end()) idx = it->second;
    }
    if (idx > 0) {
        if (pump_ != nullptr) pump_->setRoute(idx, route);
        return;
    }
    // Todavia sin sesion: ya puede publicar (entro al lobby con lugar de
    // preview, o fue admitido). El navegador arma su PeerConnection al recibir
    // esto; antes no, para no ocupar una sesion que no se va a rutear.
    sendToGuest(pid, json{{"type", "publish"}});
}

std::string SignalingServer::publicCandidate(const std::string& line) const {
    // "candidate:<fundacion> <comp> <proto> <prio> <ip> <puerto> typ ..."
    const std::string loop = " 127.0.0.1 ";
    const size_t at = line.find(loop);
    if (at == std::string::npos) return line;
    if (publicAddress_.empty()) return {};
    // El puente del tunel entrega en 127.0.0.1:P lo que llega a IPpublica:P.
    return line.substr(0, at) + " " + publicAddress_ + " " + line.substr(at + loop.size());
}

void SignalingServer::handleOffer(const std::string& pid, uint64_t gen, const std::string& sdp) {
    // Una pantalla no tiene ruta en la sala: la suya es backstage SIN preview,
    // o sea que recibe el programa y lo que mandara no va a ningun lado.
    const bool pantalla = esPantalla(pid);
    if (pantalla && !soloRecibe(sdp)) {
        log_.warn("ws: la pantalla " + pid + " ofrecio mandar medios; se corta", -1);
        sendToGuest(pid, json{{"type", "rejected"}, {"reason", "solo_mirar"}});
        dropGuest(pid);
        return;
    }
    const int canalEscena = pantalla ? canalDePantalla(pid) : -1;
    const MediaRoute route = !pantalla          ? room_.routeOf(pid)
                             : canalEscena >= 0 ? MediaRoute::escena(canalEscena)
                                                : MediaRoute::backstage(0);
    if (route.kind == MediaRoute::Kind::None) {
        log_.warn("ws: offer de " + pid + " sin ruta de medios (sin lugar); se ignora", -1);
        return;
    }
    int idx = 0;
    bool fresh = false;
    {
        std::lock_guard<std::mutex> lock(sessLock_);
        const auto it = sessionOf_.find(pid);
        if (it != sessionOf_.end()) {
            idx = it->second;
            fresh = sessionOwner_[idx].gen != gen; // reenganche: PC nueva
        } else {
            for (int s = 1; s <= hub_.sessions() && idx == 0; ++s) {
                if (sessionOwner_.count(s) == 0) idx = s;
            }
            fresh = idx > 0;
        }
        if (idx > 0) {
            sessionOwner_[idx] = SessionOwner{pid, gen};
            sessionOf_[pid] = idx;
        }
    }
    WebrtcSession* sess = hub_.session(idx);
    if (sess == nullptr) {
        log_.warn("ws: sin sesiones WebRTC libres para " + pid, -1);
        sendToGuest(pid, json{{"type", "busy"}});
        return;
    }
    if (fresh) {
        SessionCallbacks cb;
        cb.onLocalAnswer = [this, pid](const std::string& s) {
            // Los candidatos que ya vengan en el answer tambien se reescriben.
            std::string sdp;
            size_t pos = 0;
            while (pos < s.size()) {
                const size_t eol = s.find('\n', pos);
                const std::string line =
                    s.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos + 1);
                if (line.rfind("a=candidate:", 0) == 0) {
                    const std::string pub = publicCandidate(line.substr(2));
                    if (!pub.empty()) sdp += "a=" + pub; // loopback sin tunel: afuera
                } else {
                    sdp += line;
                }
                if (eol == std::string::npos) break;
                pos = eol + 1;
            }
            sendToGuest(pid, json{{"type", "answer"}, {"sdp", sdp}});
        };
        cb.onLocalIce = [this, pid](const std::string& c) {
            json cand = json::parse(c, nullptr, false);
            if (cand.is_object() && cand.contains("candidate") && cand["candidate"].is_string()) {
                const std::string line = cand["candidate"].get<std::string>();
                const std::string pub = publicCandidate(line);
                if (pub.empty()) return; // loopback sin tunel: no le sirve a nadie
                cand["candidate"] = pub;
            }
            sendToGuest(pid, json{{"type", "ice"}, {"candidate", cand.is_object() ? cand : json(c)}});
        };
        cb.onStateChange = [this, pid, idx](bool up) {
            log_.fmt("info", 0, "ws: medios de %s (sesion %d) %s", pid.c_str(), idx,
                     up ? "conectados" : "cerrados");
            room_.setMediaUp(pid, up);
        };
        if (pump_ != nullptr) {
            cb.onRemoteAudio = [this, idx](const float* s, size_t n, int rate) {
                pump_->forwardAudio(idx, s, n, rate);
            };
            cb.onRemoteVideo = [this, idx](const uint8_t* p, size_t b, int w, int h) {
                pump_->forwardVideo(idx, p, b, w, h);
            };
            cb.onRemoteScreen = [this, idx](const uint8_t* p, size_t b, int w, int h) {
                pump_->forwardScreen(idx, p, b, w, h);
            };
        }
        sess->configure(cb, hub_.rtcFactory());
    }
    if (pump_ != nullptr) pump_->setRoute(idx, route);
    // Un re-offer (p. ej. al sumar la pantalla compartida) reusa la sesion.
    if (!sess->setRemoteOffer(sdp)) {
        log_.warn("ws: setRemoteOffer fallo (sesion " + std::to_string(idx) + ")", -1);
        return;
    }
    // Se anota TAMBIEN cuando sale bien. Antes solo se anotaba el fracaso, asi
    // que un offer que nunca llegaba al servidor y uno aceptado se veian
    // igual en el registro: nada. Un televisor en "conectando" no se podia
    // distinguir de uno al que no se le leyo el mensaje.
    log_.fmt("info", 0, "ws: offer de %s aceptado (sesion %d, %s%s)", pid.c_str(), idx,
             pantalla ? "pantalla" : "invitado", fresh ? ", sesion nueva" : "");
}

bool SignalingServer::ofertaRemoto(const std::string& id, const std::string& sdp,
                                   std::function<void(const json&)> enviar) {
    const std::string pid = "remoto:" + id;
    int idx = 0;
    bool fresh = false;
    {
        std::lock_guard<std::mutex> lock(sessLock_);
        // Un solo «remoto con la palabra»: si había otro, se le saca la sesión.
        std::string otro;
        for (const auto& [p, i] : sessionOf_)
            if (p.rfind("remoto:", 0) == 0 && p != pid) otro = p;
        if (!otro.empty()) {
            const int vi = sessionOf_[otro];
            sessionOwner_.erase(vi);
            sessionOf_.erase(otro);
            if (pump_ != nullptr) {
                pump_->setRoute(vi, MediaRoute{});
                pump_->apagarRemoto(1); // el nuevo no arranca con la cara del anterior
            }
            if (WebrtcSession* s = hub_.session(vi)) s->close();
        }
        const auto it = sessionOf_.find(pid);
        if (it != sessionOf_.end()) {
            idx = it->second;
        } else {
            for (int s = 1; s <= hub_.sessions() && idx == 0; ++s)
                if (sessionOwner_.count(s) == 0) idx = s;
            fresh = idx > 0;
        }
        if (idx > 0) {
            sessionOwner_[idx] = SessionOwner{pid, 0};
            sessionOf_[pid] = idx;
        }
    }
    WebrtcSession* sess = hub_.session(idx);
    if (sess == nullptr) {
        log_.warn("sesion: sin sesiones WebRTC libres para el remoto " + id, -1);
        return false;
    }
    if (fresh) {
        SessionCallbacks cb;
        cb.onLocalAnswer = [this, enviar](const std::string& s) {
            std::string out;
            size_t pos = 0;
            while (pos < s.size()) {
                const size_t eol = s.find('\n', pos);
                const std::string line = s.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos + 1);
                if (line.rfind("a=candidate:", 0) == 0) {
                    const std::string pub = publicCandidate(line.substr(2));
                    if (!pub.empty()) out += "a=" + pub;
                } else {
                    out += line;
                }
                if (eol == std::string::npos) break;
                pos = eol + 1;
            }
            enviar(json{{"type", "medios-respuesta"}, {"sdp", out}});
        };
        cb.onLocalIce = [this, enviar](const std::string& c) {
            json cand = json::parse(c, nullptr, false);
            if (cand.is_object() && cand.contains("candidate") && cand["candidate"].is_string()) {
                const std::string pub = publicCandidate(cand["candidate"].get<std::string>());
                if (pub.empty()) return;
                cand["candidate"] = pub;
            }
            enviar(json{{"type", "medios-ice"}, {"candidate", cand.is_object() ? cand : json(c)}});
        };
        cb.onStateChange = [this, id, idx](bool up) {
            log_.fmt("info", 0, "sesion: medios del remoto %s (sesion %d) %s", id.c_str(), idx, up ? "conectados" : "cerrados");
            broadcastToHosts("sesion", json{{"tipo", "medios"}, {"id", id}, {"conectado", up}});
        };
        if (pump_ != nullptr) {
            cb.onRemoteAudio = [this, idx](const float* s, size_t n, int rate) { pump_->forwardAudio(idx, s, n, rate); };
            cb.onRemoteVideo = [this, idx](const uint8_t* p, size_t b, int w, int h) { pump_->forwardVideo(idx, p, b, w, h); };
            // Lo que comparte (pantalla u otra cámara): el segundo video de la misma conexión.
            cb.onRemoteScreen = [this, idx](const uint8_t* p, size_t b, int w, int h) { pump_->forwardScreen(idx, p, b, w, h); };
        }
        sess->configure(cb, hub_.rtcFactory());
    }
    if (pump_ != nullptr) pump_->setRoute(idx, MediaRoute::remoto(1));
    if (!sess->setRemoteOffer(sdp)) {
        log_.warn("sesion: setRemoteOffer del remoto " + id + " falló", -1);
        return false;
    }
    log_.fmt("info", 0, "sesion: oferta del remoto %s aceptada (sesion %d)", id.c_str(), idx);
    return true;
}

MediaRoute SignalingServer::rutaDeVista(int canal, bool hablar) {
    // El programa como lo ve la fila de oradores (backstage sin preview: no sube nada);
    // una escena por su canal; «nada» es un canal de escena que no existe: llega el
    // sonido (y la voz del operador si le habla) pero ninguna imagen.
    if (canal == -1) return MediaRoute::backstage(0, hablar);
    MediaRoute r = MediaRoute::escena(canal >= 0 ? canal : 1000);
    r.talk = hablar;
    return r;
}

bool SignalingServer::ofertaVista(const std::string& id, const std::string& sdp, int canal, bool hablar,
                                  std::function<void(const json&)> enviar) {
    if (!soloRecibe(sdp)) {
        log_.warn("sesion: la vista de " + id + " quiso mandar medios; se ignora", -1);
        return false;
    }
    const std::string pid = "vista:" + id;
    // Una vista por integrante: la oferta nueva (otra página, o la PC que se rearmó)
    // reemplaza a la anterior, con callbacks que le contestan a ESTA conexión.
    releaseSession(pid, 0, true);
    int idx = 0;
    {
        std::lock_guard<std::mutex> lock(sessLock_);
        for (int s = 1; s <= hub_.sessions() && idx == 0; ++s)
            if (sessionOwner_.count(s) == 0) idx = s;
        if (idx > 0) {
            sessionOwner_[idx] = SessionOwner{pid, 0};
            sessionOf_[pid] = idx;
        }
    }
    WebrtcSession* sess = hub_.session(idx);
    if (sess == nullptr) {
        log_.warn("sesion: sin sesiones WebRTC libres para la vista de " + id + " (vistasRemotas)", -1);
        return false;
    }
    SessionCallbacks cb;
    cb.onLocalAnswer = [this, enviar](const std::string& s) {
        std::string out;
        size_t pos = 0;
        while (pos < s.size()) {
            const size_t eol = s.find('\n', pos);
            const std::string line = s.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos + 1);
            if (line.rfind("a=candidate:", 0) == 0) {
                const std::string pub = publicCandidate(line.substr(2));
                if (!pub.empty()) out += "a=" + pub;
            } else {
                out += line;
            }
            if (eol == std::string::npos) break;
            pos = eol + 1;
        }
        enviar(json{{"type", "vista-respuesta"}, {"sdp", out}});
    };
    cb.onLocalIce = [this, enviar](const std::string& c) {
        json cand = json::parse(c, nullptr, false);
        if (cand.is_object() && cand.contains("candidate") && cand["candidate"].is_string()) {
            const std::string pub = publicCandidate(cand["candidate"].get<std::string>());
            if (pub.empty()) return;
            cand["candidate"] = pub;
        }
        enviar(json{{"type", "vista-ice"}, {"candidate", cand.is_object() ? cand : json(c)}});
    };
    cb.onStateChange = [this, id, idx](bool up) {
        log_.fmt("info", 0, "sesion: vista de %s (sesion %d) %s", id.c_str(), idx, up ? "conectada" : "cerrada");
    };
    sess->configure(cb, hub_.rtcFactory());
    if (pump_ != nullptr) pump_->setRoute(idx, rutaDeVista(canal, hablar));
    if (!sess->setRemoteOffer(sdp)) {
        log_.warn("sesion: setRemoteOffer de la vista de " + id + " falló", -1);
        releaseSession(pid, 0, true);
        return false;
    }
    return true;
}

void SignalingServer::wsLoop(long long fd, const std::string& pid, uint64_t gen) {
    std::string payload;
    int opcode = 0;
    while (!stop_.load(std::memory_order_relaxed)) {
        if (!http::readWsFrame(fd, payload, opcode)) break; // EOF/error
        if (opcode == 0x8) break;                           // close
        if (opcode == 0x9) {                                // ping -> pong
            const auto conn = connFor(pid);
            if (conn) {
                std::lock_guard<std::mutex> lock(conn->sendLock);
                if (conn->fd >= 0) http::writeWsFrame(conn->fd, 0xA, payload);
            }
            continue;
        }
        if (opcode != 0x1 && opcode != 0x2) continue;

        std::string jsonStr;
        size_t consumed = 0;
        if (!Frame::decodeFrame(reinterpret_cast<const uint8_t*>(payload.data()), payload.size(), jsonStr,
                                consumed)) {
            log_.warn("ws: frame corrupto (" + pid + ")", -1);
            continue;
        }
        const json msg = json::parse(jsonStr, nullptr, false);
        if (!msg.is_object()) continue;
        try {
            handleWsMessage(pid, gen, msg);
        } catch (const std::exception& e) {
            log_.warn("ws: mensaje invalido de " + pid + " (" + e.what() + ")", -1);
            continue;
        }
        if (msg.value("type", json()) == "bye") break;
    }
}

void SignalingServer::handleWsMessage(const std::string& pid, uint64_t gen, const json& msg) {
    {
        const std::string type = msg.value("type", "");
        // Una superficie solo dice que esta viva y que muestra. Nada mas: lo
        // que llega de un televisor es informacion, no ordenes.
        if (pid.rfind("superficie:", 0) == 0) {
            if (type == "late") {
                superficies_.latido(msg.value("tipo", ""), pid.substr(11), msg.value("que", ""), 0);
                superficies_.dice(pid, msg.value("que", ""));
            } else if (type == "ping") {
                sendToGuest(pid, json{{"type", "pong"}});
            }
            return;
        }
        // Una pantalla solo negocia su video y late. Nada de chat, ni ficha,
        // ni estado de medios, ni mano levantada: esas cosas ni se miran.
        if (esPantalla(pid) && type != "offer" && type != "ice" && type != "ping") {
            log_.warn("ws: la pantalla " + pid + " mando '" + type + "'; se descarta", -1);
            return;
        }
        if (type == "offer") {
            // El navegador manda la descripcion entera ({type, sdp}) o solo el sdp.
            const json& s = msg.contains("sdp") ? msg["sdp"] : json();
            std::string sdp;
            if (s.is_string()) {
                sdp = s.get<std::string>();
            } else if (s.is_object()) {
                sdp = s.value("sdp", "");
            }
            handleOffer(pid, gen, sdp);
        } else if (type == "ice") {
            int idx = 0;
            {
                std::lock_guard<std::mutex> lock(sessLock_);
                const auto it = sessionOf_.find(pid);
                if (it != sessionOf_.end()) idx = it->second;
            }
            if (WebrtcSession* sess = idx > 0 ? hub_.session(idx) : nullptr) {
                const json& c = msg.contains("candidate") ? msg["candidate"] : json();
                sess->addRemoteIce(c.is_string() ? c.get<std::string>() : c.dump());
            }
        } else if (type == "media") {
            room_.guestMedia(pid, msg);
        } else if (type == "chat") {
            room_.chatFromGuest(pid, msg.value("text", ""), nowMs());
        } else if (type == "profile") {
            // El invitado solo corrige su info y contacto; el nombre lo cambia el operador.
            json f = json::object();
            if (msg.contains("info") && msg["info"].is_string()) f["info"] = msg["info"];
            if (msg.contains("contact") && msg["contact"].is_string()) f["contact"] = msg["contact"];
            room_.setProfile(pid, f);
        } else if (type == "ping") {
            // Latido de la app del invitado: sin respuesta, el cliente da el
            // socket por muerto (TCP medio abierto tras un cambio de red).
            sendToGuest(pid, json{{"type", "pong"}});
        } else if (type == "bye") {
            room_.leave(pid);
        }
    }
}

// ============================================================================
// Rate-limit de PIN por IP
// ============================================================================

bool SignalingServer::ipAttemptAllowed(std::uint32_t ip) {
    const int now = static_cast<int>(ba::security::nowUnix());
    std::lock_guard<std::mutex> lock(ipAttemptsLock_);
    auto it = ipAttempts_.find(ip);
    if (it == ipAttempts_.end()) return true;
    if (it->second.lockedUntilUnix != 0) {
        if (now < it->second.lockedUntilUnix) return false;
        ipAttempts_.erase(it); // vencio la ventana: presupuesto fresco
    }
    return true;
}

void SignalingServer::ipRecordFailure(std::uint32_t ip) {
    const int now = static_cast<int>(ba::security::nowUnix());
    std::lock_guard<std::mutex> lock(ipAttemptsLock_);
    // Poda oportunista: el mapa no debe crecer a voluntad de un atacante.
    for (auto it = ipAttempts_.begin(); it != ipAttempts_.end();) {
        if (it->second.lockedUntilUnix != 0 && now >= it->second.lockedUntilUnix) {
            it = ipAttempts_.erase(it);
        } else {
            ++it;
        }
    }
    auto& a = ipAttempts_[ip];
    ++a.failures;
    if (a.failures >= kPinMaxAttemptsPerIp) a.lockedUntilUnix = now + kPinLockoutSecPerIp;
}

void SignalingServer::ipRecordSuccess(std::uint32_t ip) {
    std::lock_guard<std::mutex> lock(ipAttemptsLock_);
    ipAttempts_.erase(ip);
}

} // namespace ba::gateway
