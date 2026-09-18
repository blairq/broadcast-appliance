// test_gateway_http.cpp
//
// Humo de la superficie HTTP + WebSocket + tokens del gateway, SIN libwebrtc.
//
// Por que existe: el unico test del gateway que habia (test_gateway_integration)
// solo compila con el SDK de libwebrtc presente; sin el imprime SKIP y no
// ejerce nada. Bajo esa cobertura ausente vivio un fallo que dejaba el proceso
// entero inalcanzable: `getHeader()` no pasaba las cabeceras a minusculas, asi
// que TODA cabecera HTTP devolvia "" y, en consecuencia, `/ws` nunca hacia
// upgrade (caia al handler HTTP y respondia 404) y `POST /api/join` leia un
// cuerpo vacio (Content-Length no se encontraba) y devolvia 400.
//
// Nada de eso necesita libwebrtc: el backend NULL de WebrtcHub alcanza para
// ejercer el servidor de verdad. Este test corre SIEMPRE, tambien en CI.
//
// Casos:
//   1. GET /healthz -> 200.
//   2. GET / -> sirve el index del webroot (estatico).
//   3. POST /api/join con el PIN correcto -> 200 + token; con PIN incorrecto
//      -> 401.
//   4. Upgrade a WebSocket con el token -> 101 y un Sec-WebSocket-Accept que
//      coincide con el SHA-1/base64 del RFC 6455 (prueba que el valor de la
//      cabecera NO se altero al leerlo: es sensible a mayusculas).
//   5. El mismo token una segunda vez -> 401 (anti-replay: el nonce ya se
//      canjeo).
//   6. Cabeceras en capitalizacion arbitraria (uPgRaDe:) -> siguen
//      reconociendose: la busqueda es case-insensitive, el valor no.

#include "Log.hpp"
#include "SignalingServer.hpp"
#include "TokenAuth.hpp"
#include "Webrtc.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

/// Extrae el valor de "slot":N de una respuesta JSON. 0 si no esta.
int extractSlot(const std::string& body) {
    const std::string key = "\"slot\":";
    const size_t p = body.find(key);
    if (p == std::string::npos) return 0;
    return std::atoi(body.c_str() + p + key.size());
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

/// Abre una conexion, completa el handshake WS y devuelve el descriptor.
TestFd openWs(uint16_t port, const std::string& token, const std::string& room, int slot) {
    TestFd fd = connectLoopback(port);
    if (fd == kFdInvalid) return kFdInvalid;
    const std::string req = "GET /ws?token=" + token + "&room=" + room + "&slot=" + std::to_string(slot) +
                            " HTTP/1.1\r\n"
                            "Host: 127.0.0.1\r\n"
                            "Upgrade: websocket\r\n"
                            "Connection: Upgrade\r\n"
                            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";
    if (!sendAllTest(fd, req)) {
        closeTestFd(fd);
        return kFdInvalid;
    }
    // Leer la respuesta del handshake.
    std::string resp;
    char buf[1024];
    for (int i = 0; i < 16; ++i) {
        const int n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        resp.append(buf, static_cast<size_t>(n));
        if (resp.find("\r\n\r\n") != std::string::npos) break;
    }
    if (resp.find("101") == std::string::npos) {
        closeTestFd(fd);
        return kFdInvalid;
    }
    return fd;
}

} // namespace

int main() {
    std::printf("== test_gateway_http (sin libwebrtc) ==\n");
    CHECK(socketInitTest(), "init de sockets");

    Log log;
    WebrtcHub hub(2, 640, 360, 48000, 2);
    TokenAuth auth("secreto-de-prueba", "9182");

    SignalingServer::Cfg scfg;
    scfg.room = "sala-test";
    scfg.maxGuests = 2;
    // El webroot apunta a la app real del invitado, que se instala junto al
    // binario de test desde assets/web/guest.
    scfg.webroot = BA_TEST_WEBROOT;

    SignalingServer server(scfg, auth, hub, log);

    bool listening = false;
    for (int port = 18800; port < 18830; ++port) {
        scfg.port = static_cast<uint16_t>(port);
        SignalingServer probe(scfg, auth, hub, log);
        if (probe.start()) {
            probe.stop();
            listening = true;
            break;
        }
    }
    CHECK(listening, "hay un puerto libre en el rango de prueba");
    if (!listening) return 1;

    SignalingServer srv(scfg, auth, hub, log);
    CHECK(srv.start(), "el servidor arranco");
    if (!srv.running()) return 1;
    const uint16_t port = scfg.port;

    // --- 1) /healthz --------------------------------------------------------
    {
        const std::string r = roundTrip(port, "GET /healthz HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
        CHECK(statusLine(r).find("200") != std::string::npos, "/healthz responde 200");
    }

    // --- 2) estatico --------------------------------------------------------
    {
        const std::string r = roundTrip(port, "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
        CHECK(statusLine(r).find("200") != std::string::npos, "/ sirve el index del invitado");
        CHECK(r.find("text/html") != std::string::npos, "/ tiene Content-Type text/html");
    }

    // --- 3) /api/join -------------------------------------------------------
    // Este es el caso que el bug de getHeader() rompia: sin leer
    // Content-Length el cuerpo llegaba vacio y el parseo JSON fallaba con 400.
    std::string token;
    {
        const std::string r = httpPost(port, "/api/join", "{\"room\":\"sala-test\",\"pin\":\"9182\"}");
        CHECK(statusLine(r).find("200") != std::string::npos, "join con PIN correcto responde 200");
        token = extractToken(r);
        CHECK(!token.empty(), "join devuelve un token");
    }
    {
        const std::string r = httpPost(port, "/api/join", "{\"room\":\"sala-test\",\"pin\":\"0000\"}");
        CHECK(statusLine(r).find("401") != std::string::npos, "join con PIN incorrecto responde 401");
    }

    // --- 4) upgrade a WebSocket --------------------------------------------
    // La clave del RFC 6455 y su accept esperado (el ejemplo canonico del
    // estandar). Que coincida prueba que el valor de Sec-WebSocket-Key llego
    // INTACTO: es base64 y bajarlo a minusculas cambiaria el hash.
    const std::string kKey = "dGhlIHNhbXBsZSBub25jZQ==";
    const std::string kExpectedAccept = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";
    {
        const std::string req = "GET /ws?token=" + token +
                                "&room=sala-test&slot=1 HTTP/1.1\r\n"
                                "Host: 127.0.0.1\r\n"
                                "Upgrade: websocket\r\n"
                                "Connection: Upgrade\r\n"
                                "Sec-WebSocket-Key: " +
                                kKey +
                                "\r\n"
                                "Sec-WebSocket-Version: 13\r\n\r\n";
        const std::string r = roundTrip(port, req);
        CHECK(statusLine(r).find("101") != std::string::npos, "el upgrade a WebSocket responde 101");
        CHECK(r.find(kExpectedAccept) != std::string::npos,
              "Sec-WebSocket-Accept correcto (la cabecera no se altero al leerla)");
    }

    // --- 5) anti-replay -----------------------------------------------------
    {
        // Pequeña espera: el slot se libera cuando el thread de la conexion
        // anterior termina.
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        const std::string req = "GET /ws?token=" + token +
                                "&room=sala-test&slot=1 HTTP/1.1\r\n"
                                "Host: 127.0.0.1\r\n"
                                "Upgrade: websocket\r\n"
                                "Connection: Upgrade\r\n"
                                "Sec-WebSocket-Key: " +
                                kKey + "\r\n\r\n";
        const std::string r = roundTrip(port, req);
        CHECK(statusLine(r).find("401") != std::string::npos,
              "reusar el mismo token es rechazado (anti-replay)");
    }

    // --- 6) cabeceras con capitalizacion arbitraria -------------------------
    // RFC 7230 §3.2: los nombres de cabecera no distinguen mayusculas. Un
    // cliente que escriba "uPgRaDe" debe funcionar igual.
    {
        const std::string t2 =
            extractToken(httpPost(port, "/api/join", "{\"room\":\"sala-test\",\"pin\":\"9182\"}"));
        CHECK(!t2.empty(), "segundo token emitido");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        const std::string req = "GET /ws?token=" + t2 +
                                "&room=sala-test&slot=2 HTTP/1.1\r\n"
                                "hOsT: 127.0.0.1\r\n"
                                "uPgRaDe: WebSocket\r\n"
                                "CONNECTION: Upgrade\r\n"
                                "sec-websocket-key: " +
                                kKey + "\r\n\r\n";
        const std::string r = roundTrip(port, req);
        CHECK(statusLine(r).find("101") != std::string::npos,
              "cabeceras en cualquier capitalizacion se reconocen");
    }

    // --- 7) mensaje WS FRAGMENTADO -----------------------------------------
    // Los navegadores parten mensajes grandes (un SDP lo es) en varios frames:
    // el primero con el opcode real y FIN=0, el resto con opcode 0 y el ultimo
    // con FIN=1. Antes se ignoraba el bit FIN y cada fragmento se procesaba
    // como un mensaje entero, asi que el JSON llegaba cortado y la
    // señalizacion fallaba. Aqui se envia una oferta partida en tres y se
    // espera la respuesta (answer) del lado servidor.
    {
        const std::string t3 =
            extractToken(httpPost(port, "/api/join", "{\"room\":\"sala-test\",\"pin\":\"9182\"}"));
        CHECK(!t3.empty(), "tercer token emitido");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        TestFd ws = openWs(port, t3, "sala-test", 1);
        CHECK(ws != kFdInvalid, "handshake WS para la prueba de fragmentacion");
        if (ws != kFdInvalid) {
            // SDP largo para que el mensaje sea de tamaño realista.
            std::string sdp = "v=0\\r\\no=- 0 0 IN IP4 127.0.0.1\\r\\n";
            for (int i = 0; i < 40; ++i) {
                sdp += "a=relleno-de-prueba-para-alargar-el-sdp-" + std::to_string(i) + "\\r\\n";
            }
            const std::string msg = "{\"type\":\"offer\",\"sdp\":\"" + sdp + "\"}";
            const std::string framed = encodeBaFrame(msg);

            // Partir en tres fragmentos.
            const size_t third = framed.size() / 3;
            const std::string f1 = framed.substr(0, third);
            const std::string f2 = framed.substr(third, third);
            const std::string f3 = framed.substr(2 * third);
            bool sent = sendAllTest(ws, makeClientFrame(0x2, false, f1));
            sent = sent && sendAllTest(ws, makeClientFrame(0x0, false, f2));
            sent = sent && sendAllTest(ws, makeClientFrame(0x0, true, f3));
            CHECK(sent, "se enviaron los tres fragmentos");

            // El servidor debe reensamblar, parsear el offer y responder con un
            // answer. Si no reensambla, el JSON no parsea y no llega nada.
            std::string resp;
            char rbuf[4096];
            for (int i = 0; i < 40; ++i) {
                const int n = recv(ws, rbuf, sizeof(rbuf), 0);
                if (n <= 0) break;
                resp.append(rbuf, static_cast<size_t>(n));
                if (resp.find("answer") != std::string::npos) break;
            }
            CHECK(resp.find("answer") != std::string::npos,
                  "el servidor reensamblo el mensaje fragmentado y respondio el answer");
            closeTestFd(ws);
        }
    }

    // --- 8) DOS invitados simultaneos ---------------------------------------
    // El servidor asigna slots distintos y ambos pueden tener su WebSocket
    // abierto a la vez. Antes el cliente pedia siempre el slot 1, de modo que
    // el segundo invitado recibia 409 y la mesa de varios invitados -- que es
    // la propuesta del producto -- no podia existir.
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));

        // Flujo realista: cada invitado pide token, se prepara y abre su WS.
        const std::string bodyA = httpPost(port, "/api/join", "{\"room\":\"sala-test\",\"pin\":\"9182\"}");
        const std::string tA = extractToken(bodyA);
        const int slotA = extractSlot(bodyA);
        CHECK(!tA.empty(), "token del primer invitado");
        CHECK(slotA >= 1, "el servidor asigno un slot al primer invitado");
        TestFd wsA = openWs(port, tA, "sala-test", slotA);
        CHECK(wsA != kFdInvalid, "el primer invitado abrio su WebSocket");

        const std::string bodyB = httpPost(port, "/api/join", "{\"room\":\"sala-test\",\"pin\":\"9182\"}");
        const std::string tB = extractToken(bodyB);
        const int slotB = extractSlot(bodyB);
        CHECK(!tB.empty(), "token del segundo invitado");
        CHECK(slotB >= 1, "el servidor asigno un slot al segundo invitado");
        CHECK(slotA != slotB, "los slots asignados son distintos");
        TestFd wsB = openWs(port, tB, "sala-test", slotB);
        CHECK(wsB != kFdInvalid, "el SEGUNDO invitado esta conectado a la vez que el primero");

        // Con los dos slots tomados (maxGuests = 2) la sala esta llena.
        const std::string bodyC = httpPost(port, "/api/join", "{\"room\":\"sala-test\",\"pin\":\"9182\"}");
        CHECK(bodyC.find("full") != std::string::npos, "un tercer invitado recibe sala llena");

        if (wsA != kFdInvalid) closeTestFd(wsA);
        if (wsB != kFdInvalid) closeTestFd(wsB);

        // Al soltarse las conexiones los slots vuelven al pool.
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        const std::string bodyD = httpPost(port, "/api/join", "{\"room\":\"sala-test\",\"pin\":\"9182\"}");
        CHECK(extractSlot(bodyD) >= 1, "los slots se liberan al desconectarse");
    }

    srv.stop();

    if (g_failures == 0) {
        std::printf("test_gateway_http: OK\n");
        return 0;
    }
    std::printf("test_gateway_http: %d fallo(s)\n", g_failures);
    return 1;
}
