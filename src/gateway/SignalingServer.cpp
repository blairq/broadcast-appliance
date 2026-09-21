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

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "HttpIo.hpp"
#include "ipc/ControlProtocol.hpp"
#include "Log.hpp"
#include "MediaPump.hpp"
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

void SignalingServer::stop() {
    // Idempotente: el destructor llama a stop() y main() tambien.
    if (listenFd_ < 0 && adminListenFd_ < 0 && activeConns_.load() == 0 && !acceptThread_.joinable() &&
        !adminAcceptThread_.joinable()) {
        return;
    }
    stop_.store(true);

    // 1) Los threads de accept salen por `stop_` (su espera es acotada).
    if (acceptThread_.joinable()) acceptThread_.join();
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

    std::string upgrade = http::getHeader(headers, "Upgrade");
    for (auto& ch : upgrade)
        ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
    if (upgrade == "websocket" && path == "/ws") {
        const std::string pid = wsOpen(fd, headers, query, clientIp);
        if (pid.empty()) return;
        const uint64_t gen = registerConn(pid, fd);
        wsLoop(fd, pid, gen);
        const bool current = unregisterConn(pid, gen);
        releaseSession(pid, gen, false);
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
    if (method == "GET" && (path == "/board" || path.rfind("/board/", 0) == 0)) {
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
    http::sendResponse(fd, 200, contentTypeFor(relPath), ss.str());
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

std::string SignalingServer::wsOpen(long long fd, const std::string& headers, const std::string& query,
                                    std::uint32_t clientIp) {
    const std::string wsKey = http::getHeader(headers, "Sec-WebSocket-Key");
    if (wsKey.empty()) {
        http::sendResponse(fd, 400, "text/plain", "falta Sec-WebSocket-Key\n");
        return {};
    }
    const auto q = http::parseQuery(query);
    const auto arg = [&](const char* k) { return q.count(k) ? http::urlDecode(q.at(k)) : std::string(); };

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
    const MediaRoute route = room_.routeOf(pid);
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
    }
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
