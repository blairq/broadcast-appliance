// test_gateway_http.cpp
//
// La superficie de red del gateway de punta a punta, SIN libwebrtc (el backend
// NULL de WebrtcHub alcanza para ejercer el servidor de verdad). Corre siempre.
//
// Casos:
//   1. /healthz, estatico del invitado, path traversal codificado.
//   2. /api/join con PIN correcto/incorrecto.
//   3. Upgrade WS: cabeceras en cualquier capitalizacion, Sec-WebSocket-Accept
//      del RFC 6455 (el valor de la cabecera no se altera) y anti-replay.
//   4. API de control: solo Bearer, y que NO exista en la superficie de invitados.
//   5. Lobby: el invitado espera, el operador lo ve por SSE, un offer desde el
//      lobby se ignora, admitir por la API le da su slot.
//   6. Offer fragmentado (FIN=0 + continuaciones) -> answer.
//   7. Chat invitado -> operador (SSE) y operador -> invitado.
//   8. Subida de archivos: tipo por contenido, descarga identica, ejecutable
//      disfrazado -> 415, sin clave -> 401, tope -> 413, borrado.
//   9. Sala llena -> 409, expulsion (aviso + corte), el slot se reusa.
//  10. Reconexion: el corte conserva el slot; resume con clave mala -> 401,
//      con la buena vuelve a su slot.
//  11. Sala cerrada -> 423; lobby apagado -> admision directa.
//  12. Cambio de PIN (uno por dia).
//  13. QR de ingreso (URL local/remota, PIN opcional en el fragmento).
//  14. Modo oradores: admitir = a la fila (backstage) con info y contacto,
//      aviso "speaker" al invitado, al aire por la API, foto (JPEG chico,
//      tipo por contenido, con su clave), tablero con su propia clave (sin la
//      clave -> 401), enlace del tablero, y un JSON con tipos inesperados no
//      tumba al gateway.

#include "Log.hpp"
#include "Room.hpp"
#include "ShareStore.hpp"
#include "SignalingServer.hpp"
#include "TokenAuth.hpp"
#include "Webrtc.hpp"

#include <array>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using TestFd = SOCKET;
static constexpr TestFd kFdInvalid = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using TestFd = int;
static constexpr TestFd kFdInvalid = -1;
#endif

using ba::gateway::Log;
using ba::gateway::Room;
using ba::gateway::ShareStore;
using json = nlohmann::json;
using ba::gateway::SignalingServer;
using ba::gateway::TokenAuth;
using ba::gateway::WebrtcHub;

namespace {

int g_failures = 0;

#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

bool socketInitTest() {
#ifdef _WIN32
    WSADATA wsa{};
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    return true;
#endif
}

void closeTestFd(TestFd fd) {
#ifdef _WIN32
    closesocket(fd);
#else
    ::close(fd);
#endif
}

/// Timeout de recepcion para los sockets del test.
///
/// Sin esto, un `recv` que espera una respuesta que nunca llega bloquea el
/// test para siempre y en CI aparece como un timeout opaco en vez de como el
/// caso que fallo.
void setRecvTimeout(TestFd fd, int ms) {
#ifdef _WIN32
    DWORD v = static_cast<DWORD>(ms);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&v), sizeof(v));
#else
    timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

TestFd connectLoopback(uint16_t port) {
    TestFd fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == kFdInvalid) return kFdInvalid;
    setRecvTimeout(fd, 3000);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
#ifdef _WIN32
    addr.sin_addr.S_un.S_addr = htonl(INADDR_LOOPBACK);
#else
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#endif
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closeTestFd(fd);
        return kFdInvalid;
    }
    return fd;
}

bool sendAllTest(TestFd fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const int n = send(fd, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

/// Envia una peticion cruda y devuelve la respuesta completa (hasta EOF o
/// hasta que no llegue mas en un tiempo corto).
std::string roundTrip(uint16_t port, const std::string& request) {
    TestFd fd = connectLoopback(port);
    if (fd == kFdInvalid) return "";
    if (!sendAllTest(fd, request)) {
        closeTestFd(fd);
        return "";
    }
    std::string out;
    char buf[4096];
    for (int i = 0; i < 64; ++i) {
        const int n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        out.append(buf, static_cast<size_t>(n));
        // Con Connection: close basta con leer hasta EOF; para el 101 (que no
        // cierra) se corta al ver el fin de cabeceras.
        if (out.find("\r\n\r\n") != std::string::npos && out.find("101") != std::string::npos) {
            break;
        }
    }
    closeTestFd(fd);
    return out;
}

/// Primera linea de una respuesta HTTP ("HTTP/1.1 200 OK").
std::string statusLine(const std::string& resp) {
    const size_t eol = resp.find("\r\n");
    return (eol == std::string::npos) ? resp : resp.substr(0, eol);
}

/// Extrae el valor de "token":"..." de una respuesta JSON.
std::string extractToken(const std::string& body) {
    const std::string key = "\"token\":\"";
    const size_t p = body.find(key);
    if (p == std::string::npos) return "";
    const size_t start = p + key.size();
    const size_t end = body.find('"', start);
    if (end == std::string::npos) return "";
    return body.substr(start, end - start);
}

std::string httpPost(uint16_t port, const std::string& path, const std::string& body) {
    return roundTrip(port, "POST " + path +
                               " HTTP/1.1\r\n"
                               "Host: 127.0.0.1\r\n"
                               "Content-Type: application/json\r\n"
                               "Content-Length: " +
                               std::to_string(body.size()) + "\r\n\r\n" + body);
}

/// Construye un frame WS de cliente (enmascarado, como exige el RFC 6455).
///
/// :param opcode: 0x1 texto, 0x2 binario, 0x0 continuacion.
/// :param fin: true si es el ultimo fragmento del mensaje.
std::string makeClientFrame(int opcode, bool fin, const std::string& payload) {
    std::string out;
    out.push_back(static_cast<char>((fin ? 0x80 : 0x00) | (opcode & 0x0F)));
    const size_t n = payload.size();
    if (n < 126) {
        out.push_back(static_cast<char>(0x80 | n)); // bit de mascara + len
    } else {
        out.push_back(static_cast<char>(0x80 | 126));
        out.push_back(static_cast<char>((n >> 8) & 0xFF));
        out.push_back(static_cast<char>(n & 0xFF));
    }
    const uint8_t mask[4] = {0x12, 0x34, 0x56, 0x78};
    for (uint8_t m : mask)
        out.push_back(static_cast<char>(m));
    for (size_t i = 0; i < n; ++i) {
        out.push_back(static_cast<char>(static_cast<uint8_t>(payload[i]) ^ mask[i % 4]));
    }
    return out;
}

/// Codifica un JSON con el framing del proyecto (prefijo LE de 4 bytes).
std::string encodeBaFrame(const std::string& jsonStr) {
    std::string out;
    const uint32_t len = static_cast<uint32_t>(jsonStr.size());
    out.push_back(static_cast<char>(len & 0xFF));
    out.push_back(static_cast<char>((len >> 8) & 0xFF));
    out.push_back(static_cast<char>((len >> 16) & 0xFF));
    out.push_back(static_cast<char>((len >> 24) & 0xFF));
    out += jsonStr;
    return out;
}

/// Peticion HTTP completa con cabeceras extra (cada una terminada en \r\n).
std::string httpReq(uint16_t port, const std::string& method, const std::string& path,
                    const std::string& extra, const std::string& body = {}) {
    std::string req = method + " " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n" + extra;
    if (!body.empty() || method == "POST") req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    req += "\r\n" + body;
    return roundTrip(port, req);
}

/// Cuerpo de una respuesta HTTP (despues de la cabecera).
std::string bodyOf(const std::string& resp) {
    const size_t p = resp.find("\r\n\r\n");
    return p == std::string::npos ? std::string() : resp.substr(p + 4);
}

json jsonBody(const std::string& resp) { return json::parse(bodyOf(resp), nullptr, false); }

/// Lee UN mensaje del servidor por el WebSocket (frame sin mascara con el
/// encoding Frame adentro). null si no llega nada en el timeout del socket.
json readWsMsg(TestFd fd) {
    uint8_t h[2];
    auto readN = [&](uint8_t* out, size_t n) {
        size_t got = 0;
        while (got < n) {
            const int r = recv(fd, reinterpret_cast<char*>(out + got), static_cast<int>(n - got), 0);
            if (r <= 0) return false;
            got += static_cast<size_t>(r);
        }
        return true;
    };
    for (;;) {
        if (!readN(h, 2)) return nullptr;
        const int op = h[0] & 0x0F;
        uint64_t len = h[1] & 0x7F;
        if (len == 126) {
            uint8_t e[2];
            if (!readN(e, 2)) return nullptr;
            len = (uint64_t(e[0]) << 8) | e[1];
        } else if (len == 127) {
            uint8_t e[8];
            if (!readN(e, 8)) return nullptr;
            len = 0;
            for (uint8_t b : e)
                len = (len << 8) | b;
        }
        std::string payload(static_cast<size_t>(len), '\0');
        if (len > 0 && !readN(reinterpret_cast<uint8_t*>(&payload[0]), payload.size())) return nullptr;
        if (op == 0x8) return json{{"type", "__close"}};
        if (op != 0x2 || payload.size() < 4) continue;
        return json::parse(payload.substr(4), nullptr, false);
    }
}

/// Lee mensajes hasta uno de tipo `type` (o null si se agota el tiempo).
json waitWsType(TestFd fd, const std::string& type) {
    for (int i = 0; i < 20; ++i) {
        const json m = readWsMsg(fd);
        if (m.is_null()) return nullptr;
        if (m.value("type", "") == type) return m;
    }
    return nullptr;
}

void sendWsJson(TestFd fd, const json& j) {
    sendAllTest(fd, makeClientFrame(0x2, true, encodeBaFrame(j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))));
}

/// Abre el WebSocket de sala con `query` (token nuevo o resume) y devuelve el
/// descriptor tras el 101.
TestFd openWsQuery(uint16_t port, const std::string& query) {
    TestFd fd = connectLoopback(port);
    if (fd == kFdInvalid) return kFdInvalid;
    const std::string req = "GET /ws?" + query +
                            " HTTP/1.1\r\n"
                            "Host: 127.0.0.1\r\n"
                            "Upgrade: websocket\r\n"
                            "Connection: Upgrade\r\n"
                            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";
    if (!sendAllTest(fd, req)) {
        closeTestFd(fd);
        return kFdInvalid;
    }
    std::string resp;
    char c;
    while (resp.find("\r\n\r\n") == std::string::npos) {
        if (recv(fd, &c, 1, 0) != 1) break;
        resp.push_back(c);
    }
    if (resp.find(" 101 ") == std::string::npos) {
        closeTestFd(fd);
        return kFdInvalid;
    }
    return fd;
}

/// Invitado completo: join + WS + hello. `id`/`key` salen del hello.
struct Guest {
    TestFd fd = kFdInvalid;
    std::string id;
    std::string key;
};

Guest joinGuest(uint16_t port, const std::string& name) {
    Guest g;
    const std::string tok =
        extractToken(httpPost(port, "/api/join", "{\"room\":\"sala-test\",\"pin\":\"9182\"}"));
    if (tok.empty()) return g;
    g.fd = openWsQuery(port, "token=" + tok + "&room=sala-test&name=" + name);
    if (g.fd == kFdInvalid) return g;
    const json hello = waitWsType(g.fd, "hello");
    if (hello.is_object()) {
        g.id = hello.value("id", "");
        g.key = hello.value("key", "");
    }
    return g;
}

/// Conexion SSE a la API de control: devuelve el fd tras la cabecera.
TestFd openSse(uint16_t port, const std::string& token) {
    TestFd fd = connectLoopback(port);
    if (fd == kFdInvalid) return kFdInvalid;
    sendAllTest(fd, "GET /api/v1/events HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Bearer " + token +
                        "\r\n\r\n");
    return fd;
}

/// Lee del SSE hasta ver `event: <name>` completo. :returns: su data (JSON).
json waitSse(TestFd fd, const std::string& name, std::string& buf) {
    const std::string tag = "event: " + name + "\ndata: ";
    char tmp[4096];
    for (int i = 0; i < 50; ++i) {
        const size_t p = buf.find(tag);
        if (p != std::string::npos) {
            const size_t end = buf.find("\n\n", p);
            if (end != std::string::npos) {
                const std::string data = buf.substr(p + tag.size(), end - p - tag.size());
                buf.erase(0, end + 2);
                return json::parse(data, nullptr, false);
            }
        }
        const int n = recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0) return nullptr;
        buf.append(tmp, static_cast<size_t>(n));
    }
    return nullptr;
}

json findP(const json& snap, const std::string& id) {
    if (snap.is_object() && snap.contains("participants")) {
        for (const auto& p : snap["participants"]) {
            if (p["id"] == id) return p;
        }
    }
    return json::object(); // vacio (no null): .value() devuelve el default
}

} // namespace

int main() {
    std::printf("== test_gateway_http (sin libwebrtc) ==\n");
#ifndef _WIN32
    // El propio test escribe a sockets que el servidor corta (expulsion).
    std::signal(SIGPIPE, SIG_IGN);
#endif
    CHECK(socketInitTest(), "init de sockets");

    Log log;
    WebrtcHub hub(2, 640, 360, 48000, 2);
    TokenAuth auth("secreto-de-prueba", "9182");
    Room::Cfg rcfg;
    rcfg.name = "sala-test";
    rcfg.maxGuests = 2;
    rcfg.graceMs = 60000;
    rcfg.lobbySlots = 1; // un solo lugar de preview: se prueba la cola
    Room room(rcfg);
    const auto sharesDir = std::filesystem::temp_directory_path() / "ba-test-http-shares";
    std::filesystem::remove_all(sharesDir);
    ShareStore shares(sharesDir, 1u << 20);

    SignalingServer::Cfg scfg;
    scfg.room = "sala-test";
    scfg.maxGuests = 2;
    scfg.port = 0;      // efimero: nada de pelear por un puerto fijo en CI
    scfg.adminPort = 0; // idem
    scfg.webroot = BA_TEST_WEBROOT;
    scfg.boardWebroot = BA_TEST_WEBROOT;
    scfg.comunWebroot = BA_TEST_WEBROOT;
    scfg.controlWebroot = BA_TEST_WEBROOT;
    scfg.controlLan = true; // la puerta de los aparatos de control (efimera)
    const std::string kAdmin = "token-de-operador-de-prueba";
    scfg.adminToken = kAdmin;

    SignalingServer srv(scfg, auth, hub, room, shares, log);
    CHECK(srv.start(), "el servidor arranco");
    if (!srv.running()) return 1;
    const uint16_t port = srv.guestPort();
    const uint16_t aport = srv.adminPort();
    CHECK(port != 0 && aport != 0 && port != aport, "dos superficies en puertos distintos");
    const std::string bearer = "Authorization: Bearer " + kAdmin + "\r\n";
    const uint16_t aportEarly = aport;

    // --- 1) /healthz y estatico ---------------------------------------------
    CHECK(statusLine(roundTrip(port, "GET /healthz HTTP/1.1\r\nHost: x\r\n\r\n")).find("200") !=
              std::string::npos,
          "/healthz responde 200");
    {
        const std::string r = roundTrip(port, "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
        CHECK(statusLine(r).find("200") != std::string::npos, "/ sirve el index del invitado");
        CHECK(r.find("text/html") != std::string::npos, "/ tiene Content-Type text/html");
        const std::string t = roundTrip(port, "GET /..%2f..%2fetc/passwd HTTP/1.1\r\nHost: x\r\n\r\n");
        CHECK(statusLine(t).find("200") == std::string::npos, "path traversal codificado se rechaza");
    }

    // --- 1b) /api/sala: lo que la pantalla de ingreso mira antes de entrar ---
    {
        const std::string r = roundTrip(port, "GET /api/sala HTTP/1.1\r\nHost: x\r\n\r\n");
        CHECK(statusLine(r).find("200") != std::string::npos, "/api/sala responde 200");
        CHECK(r.find("\"pedirDatos\":true") != std::string::npos,
              "sin asistente atendiendo, al invitado se le piden los datos");
        CHECK(r.find("\"pin\"") == std::string::npos, "/api/sala no filtra el PIN");
        room.setIntake(true);
        const std::string r2 = roundTrip(port, "GET /api/sala HTTP/1.1\r\nHost: x\r\n\r\n");
        CHECK(r2.find("\"pedirDatos\":false") != std::string::npos,
              "con el asistente atendiendo, no se le piden los datos");
        room.setIntake(false);
    }

    // --- 2) /api/join -------------------------------------------------------
    std::string token;
    {
        const std::string r = httpPost(port, "/api/join", "{\"room\":\"sala-test\",\"pin\":\"9182\"}");
        CHECK(statusLine(r).find("200") != std::string::npos, "join con PIN correcto responde 200");
        token = extractToken(r);
        CHECK(!token.empty(), "join devuelve un token");
        const std::string bad = httpPost(port, "/api/join", "{\"room\":\"sala-test\",\"pin\":\"0000\"}");
        CHECK(statusLine(bad).find("401") != std::string::npos, "join con PIN incorrecto responde 401");
    }

    // --- 3) upgrade, accept del RFC 6455 y anti-replay ----------------------
    const std::string kKey = "dGhlIHNhbXBsZSBub25jZQ==";
    const std::string kExpectedAccept = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";
    {
        const std::string req = "GET /ws?token=" + token +
                                "&room=sala-test&name=Prueba HTTP/1.1\r\n"
                                "hOsT: 127.0.0.1\r\n"
                                "uPgRaDe: WebSocket\r\n"
                                "CONNECTION: Upgrade\r\n"
                                "sec-websocket-key: " +
                                kKey + "\r\n\r\n";
        const std::string r = roundTrip(port, req);
        CHECK(statusLine(r).find("101") != std::string::npos,
              "upgrade con cabeceras en cualquier capitalizacion");
        CHECK(r.find(kExpectedAccept) != std::string::npos, "Sec-WebSocket-Accept correcto");
        const std::string again = roundTrip(port, req);
        CHECK(statusLine(again).find("401") != std::string::npos,
              "reusar el token es rechazado (anti-replay)");
        // "Prueba" quedo en el lobby (desconectado, dentro del plazo de
        // reenganche): se lo rechaza para que no ocupe la fila de los casos
        // que siguen.
        const json snap = jsonBody(httpReq(aportEarly, "GET", "/api/v1/room", bearer));
        for (const auto& p : snap["participants"])
            httpReq(aportEarly, "POST", "/api/v1/participants/" + p.value("id", "") + "/reject", bearer);
    }

    // --- 4) API de control: credenciales ------------------------------------
    {
        CHECK(statusLine(httpReq(aport, "GET", "/api/v1/room", "")).find("401") != std::string::npos,
              "sin credenciales -> 401");
        CHECK(statusLine(httpReq(aport, "GET", "/api/v1/room", "Authorization: Bearer x\r\n")).find("401") !=
                  std::string::npos,
              "token incorrecto -> 401");
        const std::string ok = httpReq(aport, "GET", "/api/v1/room", bearer);
        CHECK(statusLine(ok).find("200") != std::string::npos, "con Bearer -> 200");
        CHECK(jsonBody(ok).value("pin", "") == "", "el PIN lo informa main (aca no se seteo)");
        CHECK(statusLine(httpReq(port, "GET", "/api/v1/room", bearer)).find("404") != std::string::npos,
              "la API de control NO existe en la superficie de invitados");
    }

    // --- 5) lobby + admision por la API + SSE -------------------------------
    TestFd sse = openSse(aport, kAdmin);
    std::string sseBuf;
    CHECK(waitSse(sse, "room", sseBuf).is_object(), "SSE entrega el estado inicial");

    Guest ana = joinGuest(port, "Ana");
    CHECK(!ana.id.empty() && !ana.key.empty(), "Ana entra y recibe id + clave");
    {
        const json lobby = waitWsType(ana.fd, "lobby");
        CHECK(lobby.value("position", 0) == 1, "Ana espera primera en el lobby");
        json snap;
        for (int i = 0; i < 5 && findP(snap, ana.id).empty(); ++i)
            snap = waitSse(sse, "room", sseBuf);
        CHECK(findP(snap, ana.id).value("state", "") == "waiting", "el operador la ve esperando (SSE)");

        // En el lobby YA publica, pero solo hacia el operador (preview privado):
        // el servidor le avisa "publish" y contesta su offer.
        CHECK(waitWsType(ana.fd, "publish").is_object(),
              "en el lobby se le permite publicar (preview del operador)");
        sendWsJson(ana.fd, json{{"type", "offer"}, {"sdp", "v=0"}});
        CHECK(waitWsType(ana.fd, "answer").is_object(), "su offer desde el lobby se contesta");
        {
            json s2;
            for (int i = 0; i < 5 && findP(s2, ana.id).value("lobbySlot", 0) == 0; ++i)
                s2 = jsonBody(httpReq(aport, "GET", "/api/v1/room", bearer));
            CHECK(findP(s2, ana.id).value("lobbySlot", 0) == 1, "ocupa el lugar de preview 1 del lobby");
        }
        CHECK(statusLine(httpReq(aport, "POST", "/api/v1/participants/" + ana.id + "/talk", bearer,
                                 R"({"on":true})"))
                      .find("200") != std::string::npos,
              "el operador le habla en privado");
        CHECK(waitWsType(ana.fd, "talk").value("on", false), "Ana sabe que el operador le esta hablando");

        const std::string r = httpReq(aport, "POST", "/api/v1/participants/" + ana.id + "/admit", bearer);
        CHECK(statusLine(r).find("200") != std::string::npos, "admitir por la API");
        CHECK(waitWsType(ana.fd, "admitted").value("slot", 0) == 1, "Ana recibe el slot 1");
        CHECK(statusLine(httpReq(aport, "POST", "/api/v1/participants/pnadie/admit", bearer)).find("404") !=
                  std::string::npos,
              "admitir a un id inexistente -> 404");
    }

    // --- 6) offer FRAGMENTADO una vez admitida -> answer ---------------------
    {
        std::string sdp = "v=0\\r\\no=- 0 0 IN IP4 127.0.0.1\\r\\n";
        for (int i = 0; i < 40; ++i)
            sdp += "a=relleno-de-prueba-para-alargar-el-sdp-" + std::to_string(i) + "\\r\\n";
        const std::string framed = encodeBaFrame("{\"type\":\"offer\",\"sdp\":\"" + sdp + "\"}");
        const size_t third = framed.size() / 3;
        bool sent = sendAllTest(ana.fd, makeClientFrame(0x2, false, framed.substr(0, third)));
        sent = sent && sendAllTest(ana.fd, makeClientFrame(0x0, false, framed.substr(third, third)));
        sent = sent && sendAllTest(ana.fd, makeClientFrame(0x0, true, framed.substr(2 * third)));
        CHECK(sent, "se enviaron los tres fragmentos");
        CHECK(waitWsType(ana.fd, "answer").is_object(),
              "el servidor reensamblo el offer y respondio el answer");
    }

    // --- 7) chat en los dos sentidos ----------------------------------------
    {
        sendWsJson(ana.fd, json{{"type", "chat"}, {"text", "hola operador"}});
        CHECK(waitSse(sse, "chat", sseBuf).value("text", "") == "hola operador",
              "el operador recibe el chat (SSE)");
        const std::string r = httpReq(aport, "POST", "/api/v1/chat", bearer, R"({"text":"bienvenida"})");
        CHECK(statusLine(r).find("200") != std::string::npos, "el operador escribe");
        // Ana recibe primero el eco de su propio mensaje (va a toda la sala).
        json m = waitWsType(ana.fd, "chat");
        CHECK(m.is_object() && m["msg"].value("text", "") == "hola operador", "Ana ve su mensaje en la sala");
        m = waitWsType(ana.fd, "chat");
        CHECK(m.is_object() && m["msg"].value("text", "") == "bienvenida", "Ana recibe el mensaje");
    }

    // --- 8) subida de archivos ----------------------------------------------
    std::string shareId;
    {
        std::string png = std::string("\x89PNG\r\n\x1a\n", 8) + std::string(2000, 'x');
        const std::string up =
            httpReq(port, "POST", "/api/v1/files?name=diapo%201.png", "X-BA-Key: " + ana.key + "\r\n", png);
        CHECK(statusLine(up).find("201") != std::string::npos, "subida aceptada");
        shareId = jsonBody(up).value("id", "");
        CHECK(!shareId.empty(), "devuelve el id del archivo");
        CHECK(waitWsType(ana.fd, "shared").value("id", "") == shareId, "Ana recibe la confirmacion");

        const json snap = jsonBody(httpReq(aport, "GET", "/api/v1/room", bearer));
        CHECK(snap["shares"].size() == 1 && snap["shares"][0].value("name", "") == "diapo 1.png",
              "el operador ve el archivo con su nombre");
        CHECK(snap["shares"][0].value("kind", "") == "image", "tipo detectado por contenido");

        const std::string content = httpReq(aport, "GET", "/api/v1/shares/" + shareId + "/content", bearer);
        CHECK(bodyOf(content) == png, "el operador descarga exactamente lo subido");
        CHECK(content.find("Content-Type: image/png") != std::string::npos, "con su tipo");

        const std::string exe = httpReq(port, "POST", "/api/v1/files?name=a.png",
                                        "X-BA-Key: " + ana.key + "\r\n", "MZ" + std::string(100, '\0'));
        CHECK(statusLine(exe).find("415") != std::string::npos, "un ejecutable disfrazado se rechaza");
        const std::string noKey = httpReq(port, "POST", "/api/v1/files?name=a.png", "", png);
        CHECK(statusLine(noKey).find("401") != std::string::npos, "sin clave de participante -> 401");
        // Solo la cabecera: el servidor decide por el Content-Length anunciado
        // y corta sin leer el cuerpo (el cliente web ademas lo valida antes).
        const std::string big =
            roundTrip(port, "POST /api/v1/files HTTP/1.1\r\nHost: x\r\nX-BA-Key: " + ana.key +
                                "\r\nContent-Length: " + std::to_string(2u << 20) + "\r\n\r\n");
        CHECK(statusLine(big).find("413") != std::string::npos, "mas grande que el tope -> 413");

        const std::string del = httpReq(aport, "DELETE", "/api/v1/shares/" + shareId, bearer);
        CHECK(statusLine(del).find("200") != std::string::npos, "el operador lo borra");
        CHECK(std::filesystem::is_empty(sharesDir), "y desaparece del disco");
    }

    // --- 9) segundo invitado, sala llena, expulsion -------------------------
    Guest beto = joinGuest(port, "Beto");
    Guest caro = joinGuest(port, "Caro");
    CHECK(!beto.id.empty() && !caro.id.empty(), "dos invitados mas en el lobby");
    // Un solo lugar de preview: Beto lo toma; Caro espera sin publicar.
    CHECK(waitWsType(beto.fd, "publish").is_object(), "Beto tiene lugar de preview");
    sendWsJson(caro.fd, json{{"type", "offer"}, {"sdp", "v=0"}});
    setRecvTimeout(caro.fd, 400);
    CHECK(waitWsType(caro.fd, "answer").is_null(), "sin lugar de preview, el offer de Caro no se atiende");
    setRecvTimeout(caro.fd, 3000);
    CHECK(statusLine(httpReq(aport, "POST", "/api/v1/participants/" + beto.id + "/admit", bearer))
                  .find("200") != std::string::npos,
          "admitir a Beto");
    CHECK(waitWsType(beto.fd, "admitted").value("slot", 0) == 2, "Beto recibe el slot 2");
    CHECK(waitWsType(caro.fd, "publish").is_object(), "al liberarse el preview, Caro ya puede publicar");
    CHECK(statusLine(httpReq(aport, "POST", "/api/v1/participants/" + caro.id + "/admit", bearer))
                  .find("409") != std::string::npos,
          "con dos adentro la sala esta llena -> 409");
    CHECK(
        statusLine(httpReq(aport, "POST", "/api/v1/participants/" + beto.id + "/kick", bearer)).find("200") !=
            std::string::npos,
        "expulsar a Beto");
    CHECK(waitWsType(beto.fd, "kicked").is_object(), "Beto se entera");
    CHECK(waitWsType(beto.fd, "__nada").is_null(), "y su conexion se corta");
    closeTestFd(beto.fd);
    CHECK(statusLine(httpReq(aport, "POST", "/api/v1/participants/" + caro.id + "/admit", bearer))
                  .find("200") != std::string::npos,
          "el slot liberado lo toma Caro");
    CHECK(waitWsType(caro.fd, "admitted").value("slot", 0) == 2, "Caro recibe el slot 2");

    // --- 10) reconexion con la clave ----------------------------------------
    {
        closeTestFd(caro.fd); // corte abrupto (recarga de la pagina)
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        const json snap = jsonBody(httpReq(aport, "GET", "/api/v1/room", bearer));
        CHECK(findP(snap, caro.id).value("connected", true) == false, "Caro figura desconectada");
        CHECK(findP(snap, caro.id).value("slot", 0) == 2, "pero conserva su slot");
        CHECK(openWsQuery(port, "resume=" + caro.id + "&key=mala") == kFdInvalid,
              "reenganche con clave mala -> 401");
        caro.fd = openWsQuery(port, "resume=" + caro.id + "&key=" + caro.key);
        CHECK(caro.fd != kFdInvalid, "reenganche con su clave");
        CHECK(waitWsType(caro.fd, "hello").value("resumed", false), "hello de reenganche");
        CHECK(waitWsType(caro.fd, "admitted").value("slot", 0) == 2, "vuelve a su slot");
    }

    // --- 11) sala cerrada y lobby apagado -----------------------------------
    {
        httpReq(aport, "POST", "/api/v1/room", bearer, R"({"locked":true})");
        const std::string r = httpPost(port, "/api/join", "{\"room\":\"sala-test\",\"pin\":\"9182\"}");
        CHECK(statusLine(r).find("423") != std::string::npos, "sala cerrada -> 423");
        httpReq(aport, "POST", "/api/v1/room", bearer, R"({"locked":false,"lobby":false})");
        sendWsJson(caro.fd, json{{"type", "bye"}});
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        Guest dani = joinGuest(port, "Dani");
        CHECK(waitWsType(dani.fd, "admitted").is_object(), "sin lobby entra directo");
        closeTestFd(dani.fd);
    }

    // --- 12) PIN de la sala ----------------------------------
    {
        const json pin = jsonBody(httpReq(aport, "POST", "/api/v1/pin", bearer, R"({"pin":"4455"})"));
        CHECK(pin.value("pin", "") == "4455", "cambiar el PIN");
        CHECK(statusLine(httpPost(port, "/api/join", "{\"room\":\"sala-test\",\"pin\":\"4455\"}"))
                      .find("200") != std::string::npos,
              "el PIN nuevo vale");
        const json gen = jsonBody(httpReq(aport, "POST", "/api/v1/pin", bearer, "{}"));
        CHECK(gen.value("pin", "").size() == 6, "PIN generado de 6 digitos");
        // El PIN es uno por día: queda como el de hoy y una sesión nueva el mismo día no lo cambia.
        CHECK(ba::gateway::SignalingServer::pinDeHoy() == gen.value("pin", ""), "el PIN queda como el de hoy");
        const json mismo = jsonBody(httpReq(aport, "POST", "/api/v1/pin", bearer, R"({"si_cambio_el_dia":true})"));
        CHECK(mismo.value("pin", "") == gen.value("pin", "") && !mismo.value("cambio", true),
              "sesión nueva el mismo día: el mismo PIN");

    }

    // --- 12b) La puerta de los aparatos de control: solo botonera, control y /comun/ ---
    {
        const uint16_t cport = srv.controlPort();
        CHECK(cport != 0 && cport != port && cport != aport, "la puerta de los aparatos tiene su puerto");
        CHECK(statusLine(httpReq(cport, "GET", "/control/", "")).find("200") != std::string::npos,
              "por la puerta: el control web");
        CHECK(statusLine(httpReq(cport, "GET", "/comun/index.html", "")).find("200") != std::string::npos,
              "por la puerta: lo común");
        const std::string raiz = httpReq(cport, "GET", "/", "");
        CHECK(statusLine(raiz).find("302") != std::string::npos && raiz.find("Location: /control/") != std::string::npos,
              "la raíz lleva al control web");
        CHECK(statusLine(httpReq(cport, "GET", "/invitados/", "")).find("404") != std::string::npos,
              "la página del invitado NO sale por la puerta");
        CHECK(statusLine(httpReq(cport, "GET", "/board/", "")).find("200") != std::string::npos,
              "por la puerta: los demás servicios de la sala (el tablero)");
        CHECK(statusLine(httpPost(cport, "/api/join", "{\"room\":\"sala-test\",\"pin\":\"4455\"}")).find("404") !=
                  std::string::npos,
              "entrar a la sala NO sale por la puerta");
        CHECK(statusLine(httpReq(cport, "GET", "/api/v1/room", bearer)).find("404") != std::string::npos,
              "la API de control NO sale por la puerta");
    }

    // --- 12c) El tema de las páginas web: lo pone la app, lo leen todas -------
    {
        CHECK(jsonBody(httpReq(port, "GET", "/comun/tema.json", "")).value("tema", "") == "neuro-oscuro",
              "tema de fábrica");
        CHECK(statusLine(httpReq(aport, "POST", "/api/v1/tema", bearer, R"({"tema":"../x"})")).find("400") !=
                  std::string::npos,
              "un tema con caracteres raros no vale");
        CHECK(jsonBody(httpReq(aport, "POST", "/api/v1/tema", bearer, R"({"tema":"neuro-claro"})")).value("ok", false),
              "la app cambia el tema");
        CHECK(jsonBody(httpReq(port, "GET", "/comun/tema.json", "")).value("tema", "") == "neuro-claro",
              "las páginas lo leen");
        CHECK(jsonBody(httpReq(srv.controlPort(), "GET", "/comun/tema.json", "")).value("tema", "") == "neuro-claro",
              "también por la puerta de los aparatos");
    }

    // --- 12d) El tema del evento: la app lo manda, el gateway arma la hoja ------
    {
        const std::string vacia = httpReq(port, "GET", "/comun/tema.css", "");
        CHECK(statusLine(vacia).find("200") != std::string::npos && bodyOf(vacia).find("html[") == std::string::npos,
              "sin tema: una hoja sin reglas (nunca un 404)");
        CHECK(jsonBody(httpReq(port, "GET", "/comun/tema.json", "")).at("evento").is_null(), "sin tema: evento null");
        const auto dir = std::filesystem::temp_directory_path() / "ba-test-http-tema";
        std::filesystem::create_directories(dir);
        {
            std::FILE* f = std::fopen((dir / "logo-abc.svg").string().c_str(), "wb");
            if (f) {
                std::fputs("<svg xmlns=\"http://www.w3.org/2000/svg\"/>", f);
                std::fclose(f);
            }
        }
        const json cuerpo = {{"tema", {{"colores", {{"acento", "#00a86b"}}}, {"marca", {{"logo", "logo-abc.svg"}}}}},
                             {"carpeta", dir.string()}};
        const json r = jsonBody(httpReq(aport, "POST", "/api/v1/tema-evento", bearer, cuerpo.dump()));
        CHECK(r.value("ok", false) && !r.value("v", std::string()).empty(), "la app manda el tema del evento");
        const json t = jsonBody(httpReq(port, "GET", "/comun/tema.json", ""));
        CHECK(t["evento"].value("acento", "") == "#00a86b" && t["evento"].value("logo", "").rfind("/comun/tema/logo-abc.svg?v=", 0) == 0,
              "tema.json dice el acento y dónde está el logo");
        CHECK(bodyOf(httpReq(port, "GET", "/comun/tema.css", "")).find("--ev-acento: #00a86b;") != std::string::npos,
              "la hoja trae el acento");
        const std::string logo = httpReq(port, "GET", "/comun/tema/logo-abc.svg", "");
        CHECK(statusLine(logo).find("200") != std::string::npos && logo.find("Content-Security-Policy: sandbox") != std::string::npos,
              "el logo SVG sale con CSP sandbox");
        CHECK(statusLine(httpReq(port, "GET", "/comun/tema/..%2Fsecreto.svg", "")).find("404") != std::string::npos,
              "nada fuera de Tema/");
        // Los logos PNG y WebP (lo que deja copiarRecurso en Tema/): con su tipo, sin sandbox;
        // uno de más de 4 MB, un GIF o un nombre que no es de Tema/ no salen.
        const auto poner = [&dir](const char* nombre, const std::string& datos) {
            std::FILE* f = std::fopen((dir / nombre).string().c_str(), "wb");
            if (f) {
                std::fwrite(datos.data(), 1, datos.size(), f);
                std::fclose(f);
            }
        };
        poner("logo-abc.png", std::string("\x89PNG\r\n\x1a\n", 8) + std::string(64, '\0'));
        poner("logo-abc.webp", std::string("RIFF") + std::string(4, '\x10') + "WEBPVP8 " + std::string(32, '\0'));
        poner("logo-grande.png", std::string("\x89PNG\r\n\x1a\n", 8) + std::string(4u * 1024 * 1024, '\0'));
        poner("logo-abc.gif", "GIF89a");
        poner("Logo-Abc.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\"/>");
        const std::string png = httpReq(port, "GET", "/comun/tema/logo-abc.png", "");
        CHECK(statusLine(png).find("200") != std::string::npos && png.find("Content-Type: image/png") != std::string::npos &&
                  png.find("Content-Security-Policy: sandbox") == std::string::npos && png.find("nosniff") != std::string::npos,
              "el logo PNG sale como image/png (nosniff, sin sandbox)");
        const std::string webp = httpReq(port, "GET", "/comun/tema/logo-abc.webp", "");
        CHECK(statusLine(webp).find("200") != std::string::npos && webp.find("Content-Type: image/webp") != std::string::npos,
              "el logo WebP sale como image/webp");
        CHECK(statusLine(httpReq(port, "GET", "/comun/tema/logo-grande.png", "")).find("404") != std::string::npos,
              "un archivo de más de 4 MB no sale");
        CHECK(statusLine(httpReq(port, "GET", "/comun/tema/logo-abc.gif", "")).find("404") != std::string::npos, "un GIF no sale");
        CHECK(statusLine(httpReq(port, "GET", "/comun/tema/Logo-Abc.svg", "")).find("404") != std::string::npos,
              "un nombre que no es de Tema/ (mayúsculas) no sale");
        CHECK(statusLine(httpReq(srv.controlPort(), "GET", "/comun/tema.css", "")).find("200") != std::string::npos,
              "la hoja también por la puerta de los aparatos");
        httpReq(aport, "POST", "/api/v1/tema-evento", bearer, "{}");
        CHECK(jsonBody(httpReq(port, "GET", "/comun/tema.json", "")).at("evento").is_null(), "sin evento: se va el tema");
        std::filesystem::remove_all(dir);
    }

    // --- 13) QR de ingreso --------------------------------------------------
    {
        // Las direcciones (Direcciones.hpp): la sala abierta a la red, sin túnel.
        srv.setDirecciones([] {
            ba::gateway::direcciones::Entradas e;
            e.ips = {"192.168.1.50"};
            e.bind = "0.0.0.0";
            e.invitadosHttps = 8789;
            e.puertaHttps = 8792;
            e.motivoRemoto = "falta el dominio";
            return e;
        });
        CHECK(jsonBody(httpReq(aport, "GET", "/api/v1/direcciones", bearer)).value("seq", 0) > 0,
              "el catálogo de direcciones, con su número");
        const json qr = jsonBody(httpReq(aport, "GET", "/api/v1/qr?target=local", bearer));
        CHECK(qr.value("ok", false) && qr.value("url", "") == "https://192.168.1.50:8789/invitados/#room=sala-test",
              "QR local con la URL de la red local");
        CHECK(jsonBody(httpReq(aport, "GET", "/api/v1/room", bearer)).value("guestUrl", "") ==
                  "https://192.168.1.50:8789/invitados/#room=sala-test",
              "el enlace de invitados de la sala sale del catálogo");
        const int n = qr.value("size", 0);
        bool square = n >= 21 && qr["modules"].size() == static_cast<size_t>(n);
        for (const auto& row : qr["modules"])
            square = square && row.get<std::string>().size() == static_cast<size_t>(n);
        CHECK(square, "matriz cuadrada de un QR valido (>= 21 modulos)");
        // Patron de posicion (esquina superior izquierda): borde de 7 modulos.
        CHECK(square && qr["modules"][0].get<std::string>().substr(0, 7) == "1111111" &&
                  qr["modules"][6].get<std::string>().substr(0, 7) == "1111111",
              "tiene los patrones de posicion del estandar");
        CHECK(qr.value("url", "").find("pin=") == std::string::npos, "sin pedirlo, el PIN NO va en el QR");
        const json qp = jsonBody(httpReq(aport, "GET", "/api/v1/qr?target=local&pin=1", bearer));
        const std::string pinNow = jsonBody(httpReq(aport, "GET", "/api/v1/room", bearer)).value("pin", "");
        CHECK(!pinNow.empty() &&
                  qp.value("url", "") == "https://192.168.1.50:8789/invitados/#room=sala-test&pin=" + pinNow,
              "con pin=1 el PIN va en el fragmento del enlace");
        const json rq = jsonBody(httpReq(aport, "GET", "/api/v1/qr?target=remote", bearer));
        CHECK(!rq.value("ok", true) && rq.value("message", "") == "falta el dominio",
              "QR remoto sin dominio: explica que falta");
        CHECK(statusLine(httpReq(aport, "GET", "/api/v1/qr?target=local", "")).find("401") !=
                  std::string::npos,
              "el QR tambien pide credenciales");
    }

    // --- 14) modo oradores ------------------------------------------------------
    {
        httpReq(aport, "POST", "/api/v1/room", bearer, R"({"locked":false,"lobby":true})");
        CHECK(jsonBody(httpReq(aport, "POST", "/api/v1/speakers", bearer,
                               R"({"enabled":true,"defaultSec":90,"carousel":"manual"})"))
                  .value("ok", false),
              "activar el modo oradores");
        const std::string pinNow = jsonBody(httpReq(aport, "GET", "/api/v1/room", bearer)).value("pin", "");
        const std::string tok =
            extractToken(httpPost(port, "/api/join", R"({"room":"sala-test","pin":")" + pinNow + R"("})"));
        TestFd fd =
            openWsQuery(port, "token=" + tok + "&room=sala-test&name=Oradora&info=Vecina%20del%20barrio" +
                                  "&contact=%2B54%2011%205555");
        CHECK(fd != kFdInvalid, "la oradora entra");
        const json hello = waitWsType(fd, "hello");
        const std::string id = hello.value("id", ""), key = hello.value("key", "");
        json p = findP(jsonBody(httpReq(aport, "GET", "/api/v1/room", bearer)), id);
        CHECK(p.value("contact", "") == "+54 11 5555" && p.value("info", "") == "Vecina del barrio",
              "info y contacto cargados al entrar");
        CHECK(jsonBody(httpReq(aport, "POST", "/api/v1/participants/" + id + "/admit", bearer, "{}"))
                  .value("ok", false),
              "admitir con la sala llena de slots: va a la fila");
        const json spk = waitWsType(fd, "speaker");
        CHECK(spk.value("state", "") == "queued" && spk.value("allottedSec", 0) == 90,
              "le avisan su turno y su tiempo");
        CHECK(jsonBody(
                  httpReq(aport, "POST", "/api/v1/participants/" + id + "/onair", bearer, R"({"on":true})"))
                  .value("ok", false),
              "al aire por la API");
        bool onAirMsg = false;
        for (int i = 0; i < 5 && !onAirMsg; ++i)
            onAirMsg = waitWsType(fd, "speaker").value("state", "") == "onair";
        CHECK(onAirMsg, "el invitado sabe que esta al aire");

        // Foto: JPEG por contenido, con la clave del participante.
        const std::string jpeg = std::string("\xFF\xD8\xFF\xE0", 4) + std::string(200, 'x');
        CHECK(statusLine(httpReq(port, "POST", "/api/v1/avatar", "X-BA-Key: " + key + "\r\n", jpeg))
                      .find("200") != std::string::npos,
              "sube su foto");
        CHECK(statusLine(httpReq(port, "POST", "/api/v1/avatar", "X-BA-Key: " + key + "\r\n", "GIF89a...."))
                      .find("415") != std::string::npos,
              "lo que no es JPEG -> 415");
        CHECK(statusLine(httpReq(port, "POST", "/api/v1/avatar", "X-BA-Key: mala\r\n", jpeg)).find("401") !=
                  std::string::npos,
              "sin su clave -> 401");
        const std::string av = httpReq(aport, "GET", "/api/v1/participants/" + id + "/avatar", bearer);
        CHECK(statusLine(av).find("200") != std::string::npos && bodyOf(av) == jpeg,
              "el operador ve la foto");

        // Tablero: clave propia, en el enlace que da la API de control.
        srv.setDirecciones([] {
            ba::gateway::direcciones::Entradas e;
            e.ips = {"10.0.0.5"};
            e.bind = "127.0.0.1";
            e.puertaHttps = 8792;
            return e;
        });
        const json link = jsonBody(httpReq(aport, "GET", "/api/v1/board-link?target=local", bearer));
        const std::string url = link.value("url", "");
        CHECK(url == "https://10.0.0.5:8792/board/#k=" + srv.boardToken() && link.value("size", 0) >= 21,
              "enlace + QR del tablero con su clave en el fragmento");
        CHECK(statusLine(httpReq(port, "GET", "/api/v1/board", "")).find("401") != std::string::npos,
              "el tablero sin clave -> 401");
        const json board =
            jsonBody(httpReq(port, "GET", "/api/v1/board", "X-BA-Board: " + srv.boardToken() + "\r\n"));
        bool listed = false;
        for (const auto& o : board["order"])
            listed = listed || (o.value("id", "") == id && o.value("speaker", "") == "onair");
        CHECK(listed && board.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).find(key) == std::string::npos,
              "el tablero la muestra al aire, sin claves");
        CHECK(statusLine(httpReq(port, "GET", "/api/v1/board/avatar/" + id + "?k=" + srv.boardToken(), ""))
                      .find("200") != std::string::npos,
              "foto en el tablero");
        CHECK(statusLine(httpReq(port, "GET", "/board/", "")).find("200") != std::string::npos,
              "la pagina del tablero se sirve");

        // Espectadores (relay): permisos y rutas (sin libwebrtc no hay RTP que
        // reenviar; el reenvio real lo cubre test_webrtc_loopback).
        const std::string viewReq = json{{"participant", id}, {"sdp", "v=0\r\n"}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        CHECK(statusLine(httpReq(aport, "POST", "/api/v1/view", "", viewReq)).find("401") !=
                  std::string::npos,
              "ver un video pide credenciales");
        CHECK(statusLine(httpReq(port, "POST", "/api/v1/board/view", "", viewReq)).find("401") !=
                  std::string::npos,
              "el tablero sin clave no ve videos");
        const std::string lobbyReq = json{{"participant", "pnoesta"}, {"sdp", "v=0\r\n"}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        CHECK(statusLine(httpReq(port, "POST", "/api/v1/board/view",
                                 "X-BA-Board: " + srv.boardToken() + "\r\n", lobbyReq))
                      .find("404") != std::string::npos,
              "el tablero solo ve oradores de la fila");
        const std::string v = statusLine(httpReq(aport, "POST", "/api/v1/view", bearer, viewReq));
        CHECK(v.find("409") != std::string::npos || v.find("503") != std::string::npos,
              "sin medios (o sin backend real) la vista reintenta despues");
        CHECK(statusLine(httpReq(aport, "DELETE", "/api/v1/view/9999", bearer)).find("404") !=
                  std::string::npos,
              "cerrar un espectador que no existe -> 404");

        // Tipos inesperados: ni la API ni el WebSocket tumban el proceso.
        httpReq(aport, "POST", "/api/v1/participants/" + id + "/move", bearer, R"({"index":"primero"})");
        sendWsJson(fd, json{{"type", "chat"}, {"text", 42}});
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CHECK(statusLine(roundTrip(port, "GET /healthz HTTP/1.1\r\nHost: x\r\n\r\n")).find("200") !=
                  std::string::npos,
              "sigue vivo tras JSON con tipos inesperados");
        CHECK(jsonBody(
                  httpReq(aport, "POST", "/api/v1/participants/" + id + "/onair", bearer, R"({"on":false})"))
                  .value("ok", false),
              "sacarla del aire");
        CHECK(findP(jsonBody(httpReq(aport, "GET", "/api/v1/room", bearer)), id).value("state", "") ==
                  "admitted",
              "sacarla del aire no la echa");
        closeTestFd(fd);
    }

    closeTestFd(ana.fd);
    closeTestFd(sse);
    srv.stop();
    std::filesystem::remove_all(sharesDir);

    if (g_failures == 0) {
        std::printf("test_gateway_http: OK\n");
        return 0;
    }
    std::printf("test_gateway_http: %d fallo(s)\n", g_failures);
    return 1;
}
