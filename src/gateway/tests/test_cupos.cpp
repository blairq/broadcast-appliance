// test_cupos.cpp — los cupos de conexiones del gateway por escucha y por IP (F0-07)
//
// Inundación sintética, SIN libwebrtc (el backend NULL de WebrtcHub alcanza):
//   1. Cientos de conexiones lentas (cabecera a medias) por el túnel: llenan el cupo
//      del túnel y nada más. Mientras tanto la escucha de invitados, la puerta de la
//      sala y la API de control (con su token y su SSE) siguen atendiendo; una
//      conexión más por el túnel recibe 503.
//   2. Slowloris: la cabecera de a un byte se corta al vencer `room.cabeceraMs`,
//      aunque cada byte llegue mucho antes del timeout del socket (`room.handshakeMs`).
//   3. Por IP (la de PROXY v1): una IP con todos sus lugares ocupados recibe 503;
//      otra IP, por la misma escucha, entra.
//   4. La API de control con sus lugares llenos: la app (con el token) entra igual,
//      incluido el SSE; sin token, 503.

#include "Log.hpp"
#include "Room.hpp"
#include "ShareStore.hpp"
#include "SignalingServer.hpp"
#include "TokenAuth.hpp"
#include "Webrtc.hpp"
#include "ajustes/Ajustes.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using TestFd = SOCKET;
static constexpr TestFd kFdInvalid = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using TestFd = int;
static constexpr TestFd kFdInvalid = -1;
#endif

using ba::gateway::Log;
using ba::gateway::Room;
using ba::gateway::ShareStore;
using ba::gateway::SignalingServer;
using ba::gateway::TokenAuth;
using ba::gateway::WebrtcHub;

namespace {

int g_fallas = 0;

#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallas;                                                                                      \
        }                                                                                                    \
    } while (0)

void cerrar(TestFd fd) {
#ifdef _WIN32
    closesocket(fd);
#else
    ::close(fd);
#endif
}

TestFd conectar(uint16_t puerto, int esperaMs = 3000) {
    TestFd fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == kFdInvalid) return kFdInvalid;
#ifdef _WIN32
    DWORD v = static_cast<DWORD>(esperaMs);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&v), sizeof(v));
#else
    timeval tv{esperaMs / 1000, (esperaMs % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(puerto);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        cerrar(fd);
        return kFdInvalid;
    }
    return fd;
}

bool mandar(TestFd fd, const std::string& s) {
    return send(fd, s.data(), static_cast<int>(s.size()), 0) == static_cast<int>(s.size());
}

/// Lo que responda hasta EOF (o hasta el fin de la cabecera, si `soloCabecera`).
std::string leer(TestFd fd, bool soloCabecera = false) {
    std::string out;
    char buf[4096];
    for (int i = 0; i < 64; ++i) {
        const int n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        out.append(buf, static_cast<size_t>(n));
        if (soloCabecera && out.find("\r\n\r\n") != std::string::npos) break;
    }
    return out;
}

std::string ida(uint16_t puerto, const std::string& pedido) {
    const TestFd fd = conectar(puerto);
    if (fd == kFdInvalid) return "";
    std::string r = mandar(fd, pedido) ? leer(fd) : "";
    cerrar(fd);
    return r;
}

bool es(const std::string& resp, const char* codigo) {
    return resp.substr(0, resp.find("\r\n")).find(codigo) != std::string::npos;
}

/// ¿El servidor cerró? (EOF ya, sin esperar).
bool cerrada(TestFd fd) {
    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(fd, &rd);
    timeval cero{0, 0};
    if (select(static_cast<int>(fd) + 1, &rd, nullptr, nullptr, &cero) <= 0) return false;
    char b;
    return recv(fd, &b, 1, MSG_PEEK) <= 0;
}

std::string proxyV1(const std::string& ip) { return "PROXY TCP4 " + ip + " 127.0.0.1 40000 8786\r\n"; }

} // namespace

int main() {
    std::printf("== test_cupos ==\n");
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
    // Cientos de sockets de los dos lados en el mismo proceso.
    rlimit rl{};
    getrlimit(RLIMIT_NOFILE, &rl);
    rl.rlim_cur = rl.rlim_max == RLIM_INFINITY ? 4096 : std::min<rlim_t>(rl.rlim_max, 4096);
    setrlimit(RLIMIT_NOFILE, &rl);
#else
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    constexpr int kCupo = 16, kCupoAdmin = 4, kPorIp = 6, kCabeceraMs = 3000;
    ba::ajustes::poner("room.maxConexiones", kCupo);
    ba::ajustes::poner("room.maxConexionesAdmin", kCupoAdmin);
    ba::ajustes::poner("room.maxConexionesPorIp", kPorIp);
    ba::ajustes::poner("room.cabeceraMs", kCabeceraMs);

    Log log;
    WebrtcHub hub(2, 640, 360, 48000, 2);
    TokenAuth auth("secreto-de-prueba", "9182");
    Room::Cfg rcfg;
    rcfg.name = "sala-test";
    Room room(rcfg);
    const auto dir = std::filesystem::temp_directory_path() / "ba-test-cupos";
    std::filesystem::remove_all(dir);
    ShareStore shares(dir, 1u << 20);

    SignalingServer::Cfg c;
    c.room = "sala-test";
    c.port = 0;
    c.adminPort = 0;
    c.tunel = true;
    c.tunnelPort = 0;
    c.controlLan = true;
    const std::string kToken = "token-de-la-app";
    c.adminToken = kToken;
    SignalingServer srv(c, auth, hub, room, shares, log);
    CHECK(srv.start(), "el servidor arranco");
    const uint16_t invitados = srv.guestPort(), admin = srv.adminPort(), tunel = srv.tunnelPort(),
                   puerta = srv.controlPort();
    CHECK(invitados && admin && tunel && puerta, "las cuatro escuchas abiertas");
    const std::string bearer = "Authorization: Bearer " + kToken + "\r\n";
    const std::string salud = "GET /healthz HTTP/1.1\r\nHost: x\r\n\r\n";

    // --- 1) Inundacion por el tunel -------------------------------------------
    {
        constexpr int kInundacion = 300;
        std::vector<TestFd> lentas;
        int rechazadas = 0;
        for (int i = 0; i < kInundacion; ++i) {
            const TestFd fd = conectar(tunel, 200);
            if (fd == kFdInvalid) continue;
            // Cada una dice venir de otra IP: el cupo por IP no la frena, solo el de la escucha.
            mandar(fd, proxyV1("198.51." + std::to_string(i / 250) + "." + std::to_string(i % 250 + 1)) +
                           "GET / HTTP/1.1\r\nHost: x\r\n");
            lentas.push_back(fd);
        }
        // Ya, mientras las del cupo siguen esperando su cabecera: lo demas entra.
        CHECK(es(ida(tunel, salud), "503"), "una mas por el tunel: 503");
        CHECK(es(ida(invitados, salud), "200"), "la escucha de invitados sigue atendiendo");
        CHECK(es(ida(puerta, salud), "200"), "la puerta de la sala sigue atendiendo");
        CHECK(es(ida(admin, "GET /api/v1/network HTTP/1.1\r\nHost: x\r\n" + bearer + "\r\n"), "200"),
              "la API de control sigue atendiendo a la app");
        {
            const TestFd sse = conectar(admin);
            CHECK(sse != kFdInvalid && mandar(sse, "GET /api/v1/events HTTP/1.1\r\nHost: x\r\n" + bearer + "\r\n") &&
                      es(leer(sse, true), "200"),
                  "el SSE de la app entra durante la inundacion");
            if (sse != kFdInvalid) cerrar(sse);
        }
        // Las de mas recibieron su 503 (y se cerraron).
        for (TestFd fd : lentas) {
            char b[64];
            const int n = recv(fd, b, sizeof(b), MSG_PEEK);
            if (n > 0 && std::string(b, static_cast<size_t>(n)).find("503") != std::string::npos) ++rechazadas;
        }
        std::printf("  inundacion: %d conexiones, %d con 503\n", static_cast<int>(lentas.size()), rechazadas);
        CHECK(rechazadas >= static_cast<int>(lentas.size()) - kCupo, "por el tunel entran a lo sumo su cupo");
        for (TestFd fd : lentas) cerrar(fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(kCabeceraMs + 300));
        CHECK(es(ida(tunel, salud), "200"), "pasada la inundacion, el tunel vuelve a atender");
    }

    // --- 2) Slowloris: un byte cada 200 ms (antes que el timeout del socket) --------
    {
        const TestFd fd = conectar(invitados);
        const auto t0 = std::chrono::steady_clock::now();
        const std::string goteo = "GET / HTTP/1.1\r\nX-Goteo: " + std::string(200, 'a');
        bool cortada = false;
        for (size_t i = 0; i < goteo.size() && !cortada; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            cortada = !mandar(fd, goteo.substr(i, 1));
            cortada = cortada || cerrada(fd);
        }
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  slowloris: cortada a los %lld ms\n", static_cast<long long>(ms));
        CHECK(cortada, "la cabecera que gotea se corta");
        CHECK(ms >= kCabeceraMs - 300 && ms < kCabeceraMs * 2, "y se corta al vencer room.cabeceraMs");
        cerrar(fd);
    }

    // --- 3) Por IP ----------------------------------------------------------------
    {
        std::vector<TestFd> deUna;
        for (int i = 0; i < kPorIp; ++i) {
            const TestFd fd = conectar(tunel);
            // Cabecera completa que anuncia un cuerpo que no llega: queda abierta
            // ocupando su lugar de esa IP.
            mandar(fd, proxyV1("203.0.113.7") + "GET /healthz HTTP/1.1\r\nHost: x\r\nContent-Length: 10\r\n\r\n");
            deUna.push_back(fd);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CHECK(es(ida(tunel, proxyV1("203.0.113.7") + salud), "503"), "esa IP ya tiene todos sus lugares: 503");
        CHECK(es(ida(tunel, proxyV1("203.0.113.8") + salud), "200"), "otra IP entra");
        for (TestFd fd : deUna) cerrar(fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        CHECK(es(ida(tunel, proxyV1("203.0.113.7") + salud), "200"), "soltadas, la IP vuelve a entrar");
    }

    // --- 4) La API de control con sus lugares llenos --------------------------------
    {
        std::vector<TestFd> llenas;
        for (int i = 0; i < kCupoAdmin; ++i) llenas.push_back(conectar(admin));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CHECK(es(ida(admin, "GET /api/v1/network HTTP/1.1\r\nHost: x\r\n" + bearer + "\r\n"), "200"),
              "con el cupo lleno, la app (con token) entra igual");
        const TestFd sse = conectar(admin);
        CHECK(sse != kFdInvalid && mandar(sse, "GET /api/v1/events HTTP/1.1\r\nHost: x\r\n" + bearer + "\r\n") &&
                  es(leer(sse, true), "200"),
              "con el cupo lleno, el SSE de la app entra igual");
        CHECK(es(ida(admin, salud), "503"), "con el cupo lleno, sin token: 503");
        if (sse != kFdInvalid) cerrar(sse);
        for (TestFd fd : llenas) cerrar(fd);
    }

    srv.stop();
    std::filesystem::remove_all(dir);
    std::printf(g_fallas == 0 ? "== test_cupos: OK ==\n" : "== test_cupos: %d fallas ==\n", g_fallas);
    return g_fallas == 0 ? 0 : 1;
}
