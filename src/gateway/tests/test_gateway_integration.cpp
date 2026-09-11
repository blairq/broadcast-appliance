// ============================================================================
// src/gateway/tests/test_gateway_integration.cpp — Test de integracion del gateway
//
// Proposito:
//   Test de integracion en loopback minimo del Proceso B (spec §8, §8.1, §8.5):
//   levanta un SignalingServer real en 127.0.0.1, crea la sesion "host" de
//   WebrtcSession (lado B, el ANSWERER; la posee el hub) y una sesion "guest"
//   (el invitado, el OFFERER), intercambia offer/answer por el WebSocket del
//   servidor (framing ba_core::ipc::Frame) y verifica que el MediaPump enrute
//   un buffer de audio del ring de retorno (SharedAudioRing) al track de la
//   sesion conectada.
//
//   Gated por el feature-flag BA_GATEWAY_HAVE_WEBRTC (mismo flag que el target
//   ba-gateway; el CMakeLists del gateway lo propaga al test):
//     - 0 (libwebrtc ausente: Linux / build offline): imprime
//       "SKIP: no libwebrtc" y devuelve 0. Este path compila SOLO con std +
//       ba_core + los headers del gateway (que no exponen libwebrtc).
//     - 1 (Windows + SDK): corre el loopback completo. El path es defensivo:
//       cada paso se chequea con CHECK(), los puertos en uso se reintentan en
//       el rango siguiente y la sincronizacion es por polling con timeout.
//
// Modelo de threading:
//   El thread de main dirige el flujo. El servidor corre sus threads de
//   accept/conexion y el MediaPump sus threads de retorno. No hay
//   condition variables: el test espera con waitFor(pred, timeout) (polling
//   de 20 ms), que es suficiente para un flujo de senalizacion y evita
//   deadlocks de prueba. Los callbacks de sesion pueden llegar desde el thread
//   WS (backend NULL) o desde threads de libwebrtc (backend real); el test
//   solo observa estado atomico/bool, asi que no necesita locks propios.
//
// Estrategia de errores:
//   No lanza. Cada paso se chequea con CHECK() (loguea el fallo y lo cuenta);
//   main() devuelve 0 si todo paso y 1 si hubo al menos un fallo. Un puerto en
//   uso NO es fallo del test: se reintenta con el puerto siguiente del rango
//   18787..18796. En el backend NULL no hay RTP real, por lo que la
//   verificacion de medios comprueba la ruta (ring -> pump -> track) via
//   contadores, no la integridad de samples (esa la cubre el unit test del
//   ring en ba_core).
// ============================================================================

#include <cstdio>

// Headers del gateway: autocontenidos (no exponen tipos de libwebrtc; el
// backend se elige en compile-time en los .cpp).
#include "Log.hpp"
#include "MediaPump.hpp"
#include "SignalingServer.hpp"
#include "TokenAuth.hpp"
#include "Webrtc.hpp"

// Headers de ba_core (el test enlaza ba_core).
#include "ipc/ControlProtocol.hpp"
#include "ipc/SharedAudioRing.hpp"

#ifndef BA_GATEWAY_HAVE_WEBRTC
#define BA_GATEWAY_HAVE_WEBRTC 0
#endif

#if BA_GATEWAY_HAVE_WEBRTC

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

// --- Sockets dual-plataforma (mismo patron que SignalingServer.cpp) --------
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
using TestFd = SOCKET;
static const TestFd kFdInvalid = INVALID_SOCKET;
static void closeTestFd(TestFd f) { closesocket(f); }
static bool socketInitTest() {
    WSADATA wsa{};
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
}
static void socketCleanupTest() { WSACleanup(); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using TestFd = int;
static const TestFd kFdInvalid = -1;
static void closeTestFd(TestFd f) { ::close(f); }
static bool socketInitTest() { return true; }
static void socketCleanupTest() {}
#endif

namespace {

using namespace ba::gateway;
using namespace ba::ipc;
using json = nlohmann::json;

// --- Assert minimo ----------------------------------------------------------
int g_failures = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg);                                       \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

/// PID del proceso actual (para el writerPid del ring de retorno).
uint32_t currentPid() {
#ifdef _WIN32
    return static_cast<uint32_t>(GetCurrentProcessId());
#else
    return static_cast<uint32_t>(::getpid());
#endif
}

/// Lee exactamente `n` bytes (maneja lecturas parciales). true si se leyeron.
bool readExact(TestFd fd, uint8_t* out, size_t n) {
    size_t got = 0;
    while (got < n) {
        const int r = ::recv(fd, reinterpret_cast<char*>(out + got), static_cast<int>(n - got), 0);
        if (r <= 0) return false; // EOF o error
        got += static_cast<size_t>(r);
    }
    return true;
}

/// Envía exactamente `n` bytes (maneja envíos parciales). true si se enviaron.
bool sendAllBytes(TestFd fd, const void* data, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        const int r = ::send(fd, reinterpret_cast<const char*>(static_cast<const uint8_t*>(data) + sent),
                             static_cast<int>(n - sent), 0);
        if (r <= 0) return false;
        sent += static_cast<size_t>(r);
    }
    return true;
}

/// Conecta a 127.0.0.1:port (loopback, spec §5.6). kFdInvalid en fallo.
TestFd connectLoopback(uint16_t port) {
    TestFd s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kFdInvalid) return kFdInvalid;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closeTestFd(s);
        return kFdInvalid;
    }
    return s;
}

/// Lee hasta `\r\n\r\n` (fin de cabecera HTTP del handshake WS).
bool readHeaders(TestFd fd, std::string& headers) {
    headers.clear();
    uint8_t c;
    while (headers.size() < 64 * 1024) {
        const int r = ::recv(fd, reinterpret_cast<char*>(&c), 1, 0);
        if (r <= 0) return false;
        headers.push_back(static_cast<char>(c));
        if (headers.size() >= 4 && headers.compare(headers.size() - 4, 4, "\r\n\r\n") == 0) return true;
    }
    return false;
}

/// Escribe un frame WS cliente→servidor (ENMASCARADO, RFC 6455 §5.3).
///
/// :param fd: socket ya en modo WebSocket.
/// :param opcode: 0x1 texto / 0x2 binario / 0x8 close.
/// :param payload: bytes del payload (se enmascara con una mascara fija).
bool writeWsFrame(TestFd fd, int opcode, const std::string& payload) {
    std::string out;
    out.push_back(static_cast<char>(0x80 | (opcode & 0x0F))); // FIN + opcode
    const size_t n = payload.size();
    if (n < 126) {
        out.push_back(static_cast<char>(0x80 | n));
    } else if (n < 65536) {
        out.push_back(static_cast<char>(0x80 | 126));
        out.push_back(static_cast<char>((n >> 8) & 0xFF));
        out.push_back(static_cast<char>(n & 0xFF));
    } else {
        out.push_back(static_cast<char>(0x80 | 127));
        for (int i = 7; i >= 0; --i)
            out.push_back(static_cast<char>((n >> (i * 8)) & 0xFF));
    }
    static const uint8_t mask[4] = {0x12, 0x34, 0x56, 0x78};
    out.append(reinterpret_cast<const char*>(mask), 4);
    for (size_t i = 0; i < n; ++i)
        out.push_back(static_cast<char>(static_cast<uint8_t>(payload[i]) ^ mask[i % 4]));
    return sendAllBytes(fd, out.data(), out.size());
}

/// Lee un frame WS servidor→cliente (SIN mascara). true si se leyo completo.
bool readWsFrame(TestFd fd, std::string& payload, int& opcode) {
    uint8_t h[2];
    if (!readExact(fd, h, 2)) return false;
    const int op = h[0] & 0x0F;
    uint64_t len = h[1] & 0x7F;
    if (len == 126) {
        uint8_t ext[2];
        if (!readExact(fd, ext, 2)) return false;
        len = (uint64_t(ext[0]) << 8) | ext[1];
    } else if (len == 127) {
        uint8_t ext[8];
        if (!readExact(fd, ext, 8)) return false;
        len = 0;
        for (int i = 0; i < 8; ++i)
            len = (len << 8) | ext[i];
    }
    if (len > Frame::kMaxPayloadBytes) return false;
    payload.resize(static_cast<size_t>(len));
    if (len > 0 && !readExact(fd, reinterpret_cast<uint8_t*>(&payload[0]), len)) return false;
    opcode = op;
    return true;
}

/// Polling con timeout: evalua `pred` cada 20 ms hasta que sea true o venza
/// el `timeout`. Evita condition variables en el test (ver el header).
bool waitFor(const std::function<bool()>& pred, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return pred();
}

} // namespace

int main() {
    using namespace std::chrono_literals;

    std::printf("== test_gateway_integration (libwebrtc) ==\n");
    Log log;

    // --- Stack: hub (1 invitado), pump, auth, servidor ----------------------
    // El hub posee la sesion "host" del slot 1 (lado B, el answerer).
    WebrtcHub hub(1, 640, 360, 48000, 2);

    // Nombres de flujos IPC de prueba (aislados de los reales, para no
    // chocar con un A corriendo).
    MediaPump::Cfg pcfg;
    pcfg.returnAudioName = L"BA.Test.Gateway.ReturnAudio";
    pcfg.programVideoName = L"BA.Test.Gateway.Program";
    pcfg.guestAudioNames = {L"", L"BA.Test.Gateway.GuestAudio1"};
    pcfg.guestVideoNames = {L"", L"BA.Test.Gateway.GuestVideo1"};
    MediaPump pump(hub, pcfg, log);

    TokenAuth auth("test-secret", "1234");

    SignalingServer::Cfg scfg;
    scfg.room = "test";
    scfg.maxGuests = 1;
    SignalingServer server(scfg, auth, hub, log);
    server.setPump(&pump);

    pump.start();

    // Puerto en uso no es fallo del test: se reintenta en el rango siguiente.
    bool listening = false;
    for (int port = 18787; port < 18797; ++port) {
        scfg.port = static_cast<uint16_t>(port);
        if (server.start()) {
            listening = true;
            break;
        }
    }
    CHECK(listening, "el servidor de senalizacion no pudo escuchar (rango de puertos ocupado)");
    if (!listening) {
        pump.stop();
        return g_failures ? 1 : 0;
    }
    CHECK(server.running(), "server.running()");

    // --- Token de invitado (spec §8.3) --------------------------------------
    const std::string token = auth.issue(scfg.room, "1234");
    CHECK(!token.empty(), "auth.issue() no emito token");

    // --- Sesion host (lado B, el answerer; la posee el hub) -----------------
    WebrtcSession* host = hub.session(1);
    CHECK(host != nullptr, "hub.session(1)");
    if (host == nullptr) {
        server.stop();
        pump.stop();
        return g_failures ? 1 : 0;
    }

    // --- El guest se conecta al servidor por WS (el navegador) --------------
    CHECK(socketInitTest(), "WSAStartup");
    const TestFd fd = connectLoopback(scfg.port);
    CHECK(fd != kFdInvalid, "connect 127.0.0.1 (WS)");
    if (fd == kFdInvalid) {
        server.stop();
        pump.stop();
        return g_failures ? 1 : 0;
    }

    // Handshake RFC 6455. El token es b64url + '.', sin caracteres que
    // requieran URL-encoding en la query.
    const std::string req = "GET /ws?token=" + token + "&room=" + scfg.room +
                            "&slot=1 HTTP/1.1\r\n"
                            "Host: 127.0.0.1\r\n"
                            "Upgrade: websocket\r\n"
                            "Connection: Upgrade\r\n"
                            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                            "\r\n";
    CHECK(sendAllBytes(fd, req.data(), req.size()), "enviar handshake WS");
    std::string headers;
    CHECK(readHeaders(fd, headers), "leer respuesta del handshake WS");
    CHECK(headers.find("101") != std::string::npos, "esperado 101 Switching Protocols");

    // --- Offer del guest (SDP del navegador) → servidor → host --------------
    // SDP minimo coherente: en el backend real seria el offer del navegador;
    // el contenido no cambia el flujo de senalizacion que se esta probando.
    const std::string kOfferSdp = "v=0\r\n"
                                  "o=- 0 0 IN IP4 127.0.0.1\r\n"
                                  "s=-\r\n"
                                  "t=0 0\r\n"
                                  "m=audio 9 UDP/TLS/RTP/SAVPF 0\r\n"
                                  "a=rtpmap:0 PCMU/8000\r\n"
                                  "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"
                                  "a=rtpmap:96 VP8/90000\r\n";
    {
        json offer;
        offer["type"] = "offer";
        offer["sdp"] = kOfferSdp;
        const std::vector<uint8_t> frame = Frame::encodeFrame(offer.dump());
        CHECK(writeWsFrame(fd, 0x2, std::string(frame.begin(), frame.end())),
              "enviar offer (Frame + frame WS binario)");
    }

    // --- Answer: servidor → WS → guest ---------------------------------------
    std::string payload;
    int opcode = 0;
    CHECK(readWsFrame(fd, payload, opcode), "leer frame WS del answer");
    CHECK(opcode == 0x2, "esperado frame binario");
    std::string jsonStr;
    size_t consumed = 0;
    CHECK(Frame::decodeFrame(reinterpret_cast<const uint8_t*>(payload.data()), payload.size(), jsonStr,
                             consumed),
          "decodificar Frame del answer");
    json ans;
    bool parsed = false;
    try {
        ans = json::parse(jsonStr);
        parsed = true;
    } catch (...) {
    }
    CHECK(parsed, "parsear JSON del answer");
    CHECK(ans.value("type", "") == "answer", "el mensaje es un answer");
    const std::string answerSdp = ans.value("sdp", "");
    CHECK(!answerSdp.empty(), "sdp del answer no vacio");

    // --- La sesion host quedo conectada (offer procesado) --------------------
    CHECK(waitFor([host] { return host->connected(); }, 2000ms), "la sesion host no quedo conectada");

    // --- Sesion guest: aplica el answer (setRemoteDescription del navegador)
    //     y queda conectada. Cierra el intercambio offer/answer a nivel de
    //     sesion: offer salio por WS, answer volvio por WS y ambos lados
    //     conectados.
    WebrtcSession guest(2, 640, 360, 48000, 2);
    SessionCallbacks gcb;
    CHECK(guest.configure(gcb, nullptr), "configurar sesion guest");
    CHECK(guest.setRemoteOffer(answerSdp), "el guest aplica el answer");
    CHECK(guest.connected(), "el guest quedo conectado");

    // --- MediaPump: el ring de retorno enruta audio al track -----------------
    // A (simulado): crea el ring de retorno y empuja un bloque de 10 ms.
    // B (el pump): returnAudioThread lo pop()ea y lo inyecta con
    // pushLocalAudio en la sesion conectada. El backend NULL no expone los
    // samples, asi que se verifica la RUTA (ring → pump → track) con los
    // contadores, no la integridad de samples (cubre el unit test del ring).
    SharedAudioRing ret;
    const Status st = SharedAudioRing::init(pcfg.returnAudioName.c_str(), 2, 16, currentPid(), 1, ret);
    CHECK(st.ok, "init del ring de retorno");
    if (st.ok) {
        const size_t blockFloats = ret.blockFloats(); // 480 frames * 2 canales
        std::vector<float> blk(blockFloats, 0.25f);
        CHECK(ret.push(blk.data(), 12345), "push del bloque de audio");
        // El reader del pump consume el ring (readIndex avanza).
        CHECK(waitFor([&ret] { return ret.header()->readIndex >= 1; }, 3000ms),
              "el pump no consumo el ring de retorno");
        // El pump inyecto bloques en el track de la sesion conectada.
        CHECK(waitFor([host] { return host->localAudioBlocks() > 0; }, 3000ms),
              "el pump no enruto audio al track");
    }

    // --- Teardown: close WS, detener servidor y pump -------------------------
    writeWsFrame(fd, 0x8, std::string()); // close frame
    closeTestFd(fd);
    server.stop();
    pump.stop();
    socketCleanupTest();

    std::printf("== test_gateway_integration: %s ==\n", g_failures ? "FAIL" : "OK");
    return g_failures ? 1 : 0;
}

#else // !BA_GATEWAY_HAVE_WEBRTC: libwebrtc ausente (Linux / build offline)

// El backend NULL ya esta cubierto por los unit tests; este test se saltea.
// Este path compila SOLO con std + ba_core + headers del gateway (sin
// libwebrtc): verifica que el arbol configure y compile con el flag a 0.
int main() {
    std::printf("SKIP: no libwebrtc\n");
    return 0;
}

#endif // BA_GATEWAY_HAVE_WEBRTC
