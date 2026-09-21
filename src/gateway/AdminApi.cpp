// ============================================================================
// src/gateway/AdminApi.cpp — API de control de la sala (operador)
//
// Proposito:
//   La UNICA forma de operar la conferencia. De esta API cuelgan la consola
//   web (Web/admin) y la vista Qt de la app; ninguna tiene logica propia de
//   sala, asi que cualquier otra vista futura se hace igual.
//
//   Escucha SIEMPRE en 127.0.0.1 (puerto `adminPort`, 8788 por defecto).
//
// Autenticacion:
//   - `Authorization: Bearer <token>` (la app Qt; el token lo publica B en
//     Runtime/gateway-admin.json (dentro del bundle) con permisos solo-usuario), o
//   - cookie `ba_admin` (la consola web). Se obtiene con un codigo de un solo
//     uso (POST /api/v1/login-code -> GET /admin/login?code=...) o pegando el
//     token en la consola (POST /api/v1/login).
//   Con cookie, toda escritura exige ademas la cabecera `X-BA-Admin: 1`: un
//   sitio ajeno no puede mandarla sin un preflight CORS que aca no se
//   responde (defensa CSRF), y la cookie es SameSite=Strict.
//
// Rutas (JSON; errores {ok:false, error}):
//   GET    /api/v1/room                    estado completo (snapshot)
//   GET    /api/v1/events                  SSE: "room" (snapshot) y "chat"
//   GET    /api/v1/network                 HTTPS, tunel WireGuard, IPs y enlaces
//   GET    /api/v1/prompter                estado del teleprompter (+ la clave del control)
//   POST   /api/v1/prompter                {texto?, titulo?, panico?, ...comandos}
//   GET    /api/v1/prompter-link?target=local|remote&vista=control|orador  enlace + QR
//   GET    /api/v1/qr?target=local|remote[&pin=1]  URL de ingreso (+PIN) + matriz del QR
//   POST   /api/v1/network/server-config   {endpoint, publicAddress?} -> wg0.conf del servidor
//   POST   /api/v1/room                    {lobby?, locked?, intake?}
//   POST   /api/v1/pin                     {pin?}  (vacio = generar)
//   POST   /api/v1/participants/<id>/admit | reject | kick
//   POST   /api/v1/participants/<id>/request   {what}
//   POST   /api/v1/participants/<id>/talk      {on}  (lobby: hablarle en privado)
//   POST   /api/v1/participants/<id>/onair     {on}  modo oradores: al aire / fuera
//   POST   /api/v1/participants/<id>/move      {index}  lugar en la fila (0 = primero)
//   POST   /api/v1/participants/<id>/time      {sec}  tiempo propio para hablar
//   POST   /api/v1/participants/<id>/requeue   vuelve al final de la fila
//   POST   /api/v1/participants/<id>/presenter  {on} presentador fijo
//   POST   /api/v1/participants/<id>/profile   {name?, info?, contact?}
//   GET    /api/v1/participants/<id>/avatar    foto de la tarjeta (JPEG)
//   POST   /api/v1/speakers                {enabled?, defaultSec?, carousel?, graceSec?,
//                                           multi?, defaultSlot?}
//   POST   /api/v1/speakers/next           al aire el siguiente de la fila
//   GET    /api/v1/board-link?target=local|remote  enlace + QR del tablero (tablet)
//   POST   /api/v1/view                    {participant, kind?:"camera"|"screen", sdp}
//                                          -> {viewer, sdp}: video en vivo (relay de B)
//   POST   /api/v1/view/<id>/keyframe      la vista se congelo
//   DELETE /api/v1/view/<id>
//   POST   /api/v1/chat                    {text, to?}
//   GET    /api/v1/shares/<id>/content     el archivo (vista previa)
//   DELETE /api/v1/shares/<id>
//   POST   /api/v1/login-code              -> {code, url} (consola web)
//   POST   /api/v1/login                   {token} -> cookie
//   GET    /admin/...                      consola web (estatico)
// ============================================================================

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "HttpIo.hpp"
#include "Log.hpp"
#include "qrcodegen.hpp"
#include "Room.hpp"
#include "security/SecretsStore.hpp"
#include "ShareStore.hpp"
#include "SignalingServer.hpp"

namespace ba::gateway {

using json = nlohmann::json;

namespace {

int64_t nowMsA() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

/// Comparacion en tiempo constante (no filtra el prefijo correcto por timing).
bool sameSecret(const std::string& a, const std::string& b) {
    if (a.size() != b.size() || a.empty()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

std::string cookieValue(const std::string& headers, const std::string& name) {
    const std::string all = http::getHeader(headers, "Cookie");
    size_t i = 0;
    while (i < all.size()) {
        while (i < all.size() && (all[i] == ' ' || all[i] == ';'))
            ++i;
        const size_t eq = all.find('=', i);
        if (eq == std::string::npos) break;
        const size_t end = all.find(';', eq);
        const std::string k = all.substr(i, eq - i);
        const std::string v = all.substr(eq + 1, end == std::string::npos ? std::string::npos : end - eq - 1);
        if (k == name) return v;
        if (end == std::string::npos) break;
        i = end + 1;
    }
    return {};
}

void sendJson(long long fd, int code, const json& j) {
    http::sendResponse(fd, code, "application/json", j.dump());
}

void sendErr(long long fd, int code, const char* error) {
    sendJson(fd, code, json{{"ok", false}, {"error", error}});
}

/// Room::Err -> respuesta HTTP.
void sendRoomResult(long long fd, int err) {
    switch (err) {
        case Room::kOk: sendJson(fd, 200, json{{"ok", true}}); return;
        case Room::kNotFound: sendErr(fd, 404, "not_found"); return;
        case Room::kFull: sendErr(fd, 409, "full"); return;
        case Room::kEmpty: sendErr(fd, 400, "empty"); return;
        default: sendErr(fd, 409, "state"); return;
    }
}

const char* kCookieName = "ba_admin";

} // namespace

bool SignalingServer::adminAuthorized(const std::string& headers, const std::string& method) const {
    const std::string auth = http::getHeader(headers, "Authorization");
    if (auth.rfind("Bearer ", 0) == 0 && sameSecret(auth.substr(7), cfg_.adminToken)) return true;
    if (sameSecret(cookieValue(headers, kCookieName), cfg_.adminToken)) {
        // Con cookie, las escrituras piden la cabecera anti-CSRF.
        if (method == "GET" || method == "HEAD") return true;
        return http::getHeader(headers, "X-BA-Admin") == "1";
    }
    return false;
}

std::string SignalingServer::issueLoginCode() {
    std::string code;
    if (!ba::security::randomSecretHex(16, code)) return {};
    std::lock_guard<std::mutex> lock(loginLock_);
    const long long now = nowMsA();
    for (auto it = loginCodes_.begin(); it != loginCodes_.end();) {
        it = (it->second <= now) ? loginCodes_.erase(it) : std::next(it);
    }
    loginCodes_[code] = now + 120000; // 2 minutos para abrir el navegador
    return code;
}

bool SignalingServer::redeemLoginCode(const std::string& code) {
    std::lock_guard<std::mutex> lock(loginLock_);
    const auto it = loginCodes_.find(code);
    if (it == loginCodes_.end()) return false;
    const bool ok = it->second > nowMsA();
    loginCodes_.erase(it); // un solo uso, valga o no
    return ok;
}

std::string SignalingServer::changePin(const std::string& pin) {
    std::string p = pin;
    if (p.empty()) {
        std::string hex;
        if (!ba::security::randomSecretHex(16, hex)) return {};
        // 6 digitos a partir de 32 bits aleatorios (sesgo del modulo ~0.02%).
        const unsigned long v = std::stoul(hex.substr(0, 8), nullptr, 16) % 1000000UL;
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%06lu", v);
        p = buf;
    }
    auth_.setPin(p);
    const json snap = room_.snapshot();
    room_.setHostInfo(p, snap.value("guestUrl", ""));
    log_.info("control: PIN de la sala cambiado");
    return p;
}

void SignalingServer::handleAdmin(long long fd, const std::string& method, const std::string& path,
                                  const std::string& query, const std::string& headers,
                                  const std::string& body) {
    // --- Sin autenticacion: salud, consola web estatica y login -------------
    if (path == "/healthz") {
        http::sendResponse(fd, 200, "text/plain", "ok\n");
        return;
    }
    const std::string cookieAttrs = "; Path=/; HttpOnly; SameSite=Strict";
    if (path == "/admin/login" && method == "GET") {
        const auto q = http::parseQuery(query);
        const std::string code = q.count("code") ? http::urlDecode(q.at("code")) : "";
        if (!redeemLoginCode(code)) {
            http::sendResponse(fd, 302, "text/plain", "codigo vencido\n", "Location: /admin/\r\n");
            return;
        }
        http::sendResponse(fd, 302, "text/plain", "ok\n",
                           "Set-Cookie: " + std::string(kCookieName) + "=" + cfg_.adminToken + cookieAttrs +
                               "\r\nLocation: /admin/\r\n");
        return;
    }
    if (path == "/api/v1/login" && method == "POST") {
        const json req = json::parse(body, nullptr, false);
        const std::string tok = req.is_object() ? req.value("token", "") : "";
        if (!sameSecret(tok, cfg_.adminToken)) {
            sendErr(fd, 401, "auth");
            return;
        }
        http::sendResponse(fd, 200, "application/json", R"({"ok":true})",
                           "Set-Cookie: " + std::string(kCookieName) + "=" + cfg_.adminToken + cookieAttrs +
                               "\r\n");
        return;
    }
    if (path == "/admin" || path.rfind("/admin/", 0) == 0) {
        if (method != "GET") {
            sendErr(fd, 405, "method");
            return;
        }
        std::string rel = path.size() > 7 ? path.substr(7) : std::string();
        if (rel.empty()) rel = "index.html";
        serveStatic(fd, cfg_.adminWebroot, rel);
        return;
    }
    if (path == "/" && method == "GET") {
        http::sendResponse(fd, 302, "text/plain", "", "Location: /admin/\r\n");
        return;
    }

    // --- Todo lo demas pide credenciales ------------------------------------
    if (!adminAuthorized(headers, method)) {
        sendErr(fd, 401, "auth");
        return;
    }

    const json req = body.empty() ? json::object() : json::parse(body, nullptr, false);
    if (!req.is_object()) {
        sendErr(fd, 400, "json");
        return;
    }

    if (path == "/api/v1/room") {
        if (method == "GET") {
            sendJson(fd, 200, room_.snapshot());
            return;
        }
        if (method == "POST") {
            if (req.contains("lobby") && req["lobby"].is_boolean())
                room_.setLobby(req["lobby"].get<bool>(), nowMsA());
            if (req.contains("locked") && req["locked"].is_boolean())
                room_.setLocked(req["locked"].get<bool>());
            if (req.contains("intake") && req["intake"].is_boolean())
                room_.setIntake(req["intake"].get<bool>());
            sendJson(fd, 200, json{{"ok", true}});
            return;
        }
    }
    if (path == "/api/v1/network/server-config" && method == "POST") {
        if (!serverConfig_) {
            sendErr(fd, 404, "route");
            return;
        }
        const json out = serverConfig_(req);
        sendJson(fd, out.value("ok", false) ? 200 : 400, out);
        return;
    }
    if (path == "/api/v1/qr" && method == "GET") {
        // QR de ingreso al lobby: la URL la decide main (red local o dominio
        // de Cloudflare); aca solo se codifica. Cada vista lo dibuja con la
        // matriz (filas de '0'/'1'), sin depender de ninguna biblioteca.
        const auto q = http::parseQuery(query);
        const std::string target = q.count("target") ? http::urlDecode(q.at("target")) : "local";
        json out = joinUrl_ ? joinUrl_(target) : json{{"error", "sin proveedor de URL"}};
        if (!out.contains("url")) {
            // 200 con ok=false: falta configuracion, no es una falla de la
            // API; la vista muestra `message` en el lugar del QR.
            out["ok"] = false;
            sendJson(fd, 200, out);
            return;
        }
        std::string url = out["url"].get<std::string>();
        // Con pin=1 el PIN viaja en el FRAGMENTO del enlace (no llega a ningun
        // servidor ni a sus logs; la app del invitado lo toma y lo borra de la
        // barra de direcciones). Quien tenga el QR entra sin tipear el PIN.
        const bool withPin = q.count("pin") && q.at("pin") == "1";
        if (withPin) {
            const std::string pin = room_.snapshot().value("pin", "");
            if (!pin.empty()) url += (url.find('#') == std::string::npos ? "#pin=" : "&pin=") + pin;
            out["url"] = url;
        }
        out["withPin"] = withPin;
        const qrcodegen::QrCode qr =
            qrcodegen::QrCode::encodeText(url.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        json rows = json::array();
        for (int y = 0; y < qr.getSize(); ++y) {
            std::string row;
            for (int x = 0; x < qr.getSize(); ++x)
                row.push_back(qr.getModule(x, y) ? '1' : '0');
            rows.push_back(row);
        }
        out["ok"] = true;
        out["target"] = target;
        out["size"] = qr.getSize();
        out["modules"] = rows;
        sendJson(fd, 200, out);
        return;
    }
    if (path == "/api/v1/view" && method == "POST") {
        apiView(fd, body, false);
        return;
    }
    static const std::string kView = "/api/v1/view/";
    if (path.rfind(kView, 0) == 0) {
        apiViewAction(fd, method, path.substr(kView.size()), false);
        return;
    }
    if (path == "/api/v1/speakers" && method == "POST") {
        sendRoomResult(fd, room_.setSpeakerConfig(req, nowMsA()));
        return;
    }
    if (path == "/api/v1/speakers/next" && method == "POST") {
        sendRoomResult(fd, room_.nextSpeaker(nowMsA()));
        return;
    }
    if (path == "/api/v1/board-link" && method == "GET") {
        // Mismo origen que el ingreso de invitados (red local o dominio), con
        // la clave del tablero en el FRAGMENTO (no viaja en la URL pedida).
        const auto q = http::parseQuery(query);
        const std::string target = q.count("target") ? http::urlDecode(q.at("target")) : "local";
        json out = joinUrl_ ? joinUrl_(target) : json{{"error", "sin proveedor de URL"}};
        if (!out.contains("url")) {
            out["ok"] = false;
            sendJson(fd, 200, out);
            return;
        }
        std::string origin = out["url"].get<std::string>();
        if (const size_t s = origin.find("://"); s != std::string::npos) {
            const size_t end = origin.find_first_of("/?#", s + 3);
            if (end != std::string::npos) origin.resize(end);
        }
        const std::string url = origin + "/board/#k=" + boardToken();
        const qrcodegen::QrCode qr =
            qrcodegen::QrCode::encodeText(url.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        json rows = json::array();
        for (int y = 0; y < qr.getSize(); ++y) {
            std::string row;
            for (int x = 0; x < qr.getSize(); ++x)
                row.push_back(qr.getModule(x, y) ? '1' : '0');
            rows.push_back(row);
        }
        sendJson(
            fd, 200,
            json{{"ok", true}, {"target", target}, {"url", url}, {"size", qr.getSize()}, {"modules", rows}});
        return;
    }
    // --- Teleprompter -------------------------------------------------------
    if (path == "/api/v1/prompter") {
        // A ve siempre la clave del control: la necesita para armar el enlace
        // que le manda al operador. Por la superficie de invitados no sale.
        const auto conClave = [this](json e) {
            e["clave"] = cfg_.prompterKey;
            return e;
        };
        if (method == "GET") {
            sendJson(fd, 200, conClave(prompter_.estado()));
            return;
        }
        if (method == "POST") {
            // A comanda sin clave: ya paso por el token de la API de control.
            if (req.contains("texto") && req["texto"].is_string()) {
                prompter_.cargar(req.value("titulo", std::string()), req["texto"].get<std::string>());
            }
            if (req.value("panico", false)) prompter_.pararEnSeco();
            prompter_.comandar(req);
            sendJson(fd, 200, conClave(prompter_.estado()));
            return;
        }
    }
    if (path == "/api/v1/prompter-link" && method == "GET") {
        // El enlace del control, con la clave en el FRAGMENTO para que no
        // viaje en la URL pedida ni quede en los registros del servidor.
        const auto q = http::parseQuery(query);
        const std::string target = q.count("target") ? http::urlDecode(q.at("target")) : "local";
        const bool control = !q.count("vista") || http::urlDecode(q.at("vista")) == "control";
        json out = joinUrl_ ? joinUrl_(target) : json{{"error", "sin proveedor de URL"}};
        if (!out.contains("url")) {
            out["ok"] = false;
            sendJson(fd, 200, out);
            return;
        }
        std::string origin = out["url"].get<std::string>();
        if (const size_t s = origin.find("://"); s != std::string::npos) {
            const size_t end = origin.find_first_of("/?#", s + 3);
            if (end != std::string::npos) origin.resize(end);
        }
        // La tablet del orador no lleva clave: solo lee.
        const std::string url =
            origin + "/prompter/" + (control ? "#control&k=" + cfg_.prompterKey : "");
        const qrcodegen::QrCode qr =
            qrcodegen::QrCode::encodeText(url.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        json rows = json::array();
        for (int y = 0; y < qr.getSize(); ++y) {
            std::string row;
            for (int x = 0; x < qr.getSize(); ++x)
                row.push_back(qr.getModule(x, y) ? '1' : '0');
            rows.push_back(row);
        }
        sendJson(fd, 200,
                 json{{"ok", true},
                      {"vista", control ? "control" : "orador"},
                      {"url", url},
                      {"size", qr.getSize()},
                      {"modules", rows}});
        return;
    }

    if (path == "/api/v1/network" && method == "GET") {
        sendJson(fd, 200, networkStatus_ ? networkStatus_() : json::object());
        return;
    }
    if (path == "/api/v1/events" && method == "GET") {
        adminEvents(fd);
        return;
    }
    if (path == "/api/v1/pin" && method == "POST") {
        const std::string pin = req.value("pin", "");
        if (!pin.empty() && (pin.size() < 4 || pin.size() > 32)) {
            sendErr(fd, 400, "pin");
            return;
        }
        const std::string now = changePin(pin);
        if (now.empty()) {
            sendErr(fd, 500, "entropy");
            return;
        }
        sendJson(fd, 200, json{{"ok", true}, {"pin", now}});
        return;
    }
    if (path == "/api/v1/chat" && method == "POST") {
        sendRoomResult(fd, room_.chatFromHost(req.value("to", "all"), req.value("text", ""), nowMsA()));
        return;
    }
    if (path == "/api/v1/login-code" && method == "POST") {
        const std::string code = issueLoginCode();
        if (code.empty()) {
            sendErr(fd, 500, "entropy");
            return;
        }
        sendJson(fd, 200,
                 json{{"ok", true},
                      {"code", code},
                      {"url",
                       "http://127.0.0.1:" + std::to_string(adminBoundPort_) + "/admin/login?code=" + code}});
        return;
    }

    // /api/v1/participants/<id>/<accion>
    static const std::string kPart = "/api/v1/participants/";
    if (path.rfind(kPart, 0) == 0 && method == "GET") {
        const std::string rest = path.substr(kPart.size());
        const size_t slash = rest.find('/');
        if (slash != std::string::npos && rest.substr(slash + 1) == "avatar") {
            sendAvatar(fd, rest.substr(0, slash));
            return;
        }
    }
    if (path.rfind(kPart, 0) == 0 && method == "POST") {
        const std::string rest = path.substr(kPart.size());
        const size_t slash = rest.find('/');
        if (slash == std::string::npos) {
            sendErr(fd, 404, "route");
            return;
        }
        const std::string id = rest.substr(0, slash);
        const std::string action = rest.substr(slash + 1);
        if (action == "admit") {
            sendRoomResult(fd, room_.admit(id, nowMsA()));
        } else if (action == "reject") {
            sendRoomResult(fd, room_.reject(id));
        } else if (action == "kick") {
            sendRoomResult(fd, room_.kick(id));
        } else if (action == "talk") {
            sendRoomResult(fd, room_.setTalk(id, req.value("on", true)));
        } else if (action == "request") {
            sendRoomResult(fd, room_.request(id, req.value("what", "")));
        } else if (action == "onair") {
            sendRoomResult(fd, room_.setOnAir(id, req.value("on", true), nowMsA()));
        } else if (action == "move") {
            sendRoomResult(fd, room_.moveSpeaker(id, req.value("index", 0)));
        } else if (action == "time") {
            sendRoomResult(fd, room_.setAllotted(id, req.value("sec", 0)));
        } else if (action == "requeue") {
            sendRoomResult(fd, room_.requeue(id));
        } else if (action == "presenter") {
            sendRoomResult(fd, room_.setPresenter(id, req.value("on", true), nowMsA()));
        } else if (action == "profile") {
            sendRoomResult(fd, room_.setProfile(id, req));
        } else {
            sendErr(fd, 404, "route");
        }
        return;
    }

    // /api/v1/shares/<id>[/content]
    static const std::string kShares = "/api/v1/shares/";
    if (path.rfind(kShares, 0) == 0) {
        const std::string rest = path.substr(kShares.size());
        const size_t slash = rest.find('/');
        const std::string id = rest.substr(0, slash);
        if (method == "GET" && slash != std::string::npos && rest.substr(slash + 1) == "content") {
            adminShareContent(fd, id);
            return;
        }
        if (method == "DELETE" && slash == std::string::npos) {
            std::string file;
            if (!room_.removeShare(id, &file)) {
                sendErr(fd, 404, "not_found");
                return;
            }
            std::error_code ec;
            std::filesystem::remove(pathFromUtf8(file), ec);
            sendJson(fd, 200, json{{"ok", true}});
            return;
        }
    }
    sendErr(fd, 404, "route");
}

void SignalingServer::adminShareContent(long long fd, const std::string& id) {
    Room::Share s;
    if (!room_.share(id, s)) {
        sendErr(fd, 404, "not_found");
        return;
    }
    std::ifstream f(pathFromUtf8(s.path), std::ios::binary);
    if (!f) {
        sendErr(fd, 404, "file");
        return;
    }
    // Cabecera y cuerpo en streaming (un video puede pesar cientos de MB).
    std::string head = "HTTP/1.1 200 OK\r\nContent-Type: " + s.mime +
                       "\r\nContent-Length: " + std::to_string(s.bytes) +
                       "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
                       "Content-Security-Policy: sandbox\r\nConnection: close\r\n\r\n";
    if (!http::sendAll(fd, head.data(), head.size())) return;
    std::vector<char> buf(256 * 1024);
    while (f) {
        f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize n = f.gcount();
        if (n <= 0) break;
        if (!http::sendAll(fd, buf.data(), static_cast<size_t>(n))) return;
    }
}

void SignalingServer::adminEvents(long long fd) {
    const std::string head = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
                             "Cache-Control: no-store\r\nConnection: keep-alive\r\n\r\n";
    if (!http::sendAll(fd, head.data(), head.size())) return;
    // Los recv solo ocurren despues de waitReadable; el timeout acota los
    // ENVIOS: un operador colgado no debe trabar a la sala que le escribe.
    http::setSocketTimeout(fd, 5000);

    auto conn = std::make_shared<SseConn>();
    conn->fd = fd;
    {
        // Primero el estado completo; despues, cambios. Se registra con el
        // envio del snapshot bajo su lock para que ningun evento se adelante.
        std::lock_guard<std::mutex> sendLock(conn->sendLock);
        {
            std::lock_guard<std::mutex> lock(sseLock_);
            sse_.push_back(conn);
        }
        const std::string first = "retry: 2000\n\nevent: room\ndata: " + room_.snapshot().dump() + "\n\n";
        http::sendAll(fd, first.data(), first.size());
    }

    // El cliente no manda nada: se espera su cierre, con un latido cada 15 s
    // que ademas detecta conexiones muertas (el envio falla).
    while (!stop_.load(std::memory_order_relaxed)) {
        const int r = http::waitReadable(fd, 15000);
        if (r < 0) break;
        if (r > 0) {
            uint8_t sink[256];
            if (http::recvSome(fd, sink, sizeof(sink)) <= 0) break;
            continue;
        }
        std::lock_guard<std::mutex> lock(conn->sendLock);
        if (!http::sendAll(fd, ": ping\n\n", 8)) break;
    }
    {
        std::lock_guard<std::mutex> lock(sseLock_);
        sse_.erase(std::remove(sse_.begin(), sse_.end(), conn), sse_.end());
    }
    std::lock_guard<std::mutex> lock(conn->sendLock);
    conn->fd = -1;
}

void SignalingServer::broadcastToHosts(const std::string& event, const json& data) {
    std::vector<std::shared_ptr<SseConn>> conns;
    {
        std::lock_guard<std::mutex> lock(sseLock_);
        conns = sse_;
    }
    if (conns.empty()) return;
    const std::string msg = "event: " + event + "\ndata: " + data.dump() + "\n\n";
    for (const auto& c : conns) {
        std::lock_guard<std::mutex> lock(c->sendLock);
        if (c->fd >= 0 && !http::sendAll(c->fd, msg.data(), msg.size())) {
            http::shutdownFd(c->fd); // su thread lo da de baja
        }
    }
}

} // namespace ba::gateway
