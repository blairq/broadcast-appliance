// test_config_caliente.cpp — la configuración de la sala en caliente (F3-06)
//
// PUT /api/v1/config/<sección> por la API de control, con la sala andando:
//   1. ajustes: un avanzado «live» rige enseguida; uno que solo se lee al arrancar,
//      uno de la app, uno fuera de rango o de otro tipo se rechazan (todo o nada);
//      los «live» que no vienen vuelven a su valor de siempre.
//   2. sala: el lobby, el tope por archivo y los intentos de PIN cambian andando
//      (se ven en lo que informa la sala); un valor fuera de rango no cambia nada.
//   3. Una sección que no existe: 404; otro método: 405; sin token: 401.

#include "Log.hpp"
#include "Room.hpp"
#include "ShareStore.hpp"
#include "SignalingServer.hpp"
#include "TokenAuth.hpp"
#include "Webrtc.hpp"
#include "ajustes/Ajustes.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using TestFd = SOCKET;
static constexpr TestFd kFdInvalid = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <csignal>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using TestFd = int;
static constexpr TestFd kFdInvalid = -1;
#endif

using namespace ba::gateway;

namespace {

int g_fallas = 0;
#define CHECK(c, m)                                                       \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", m, #c, __LINE__);   \
            ++g_fallas;                                                   \
        } else {                                                          \
            std::printf("ok: %s\n", m);                                   \
        }                                                                 \
    } while (0)

void cerrar(TestFd fd) {
#ifdef _WIN32
    closesocket(fd);
#else
    ::close(fd);
#endif
}

/// Un pedido entero y la respuesta hasta que el servidor cierra.
std::string pedir(uint16_t puerto, const std::string& pedido) {
    TestFd fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == kFdInvalid) return "";
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(puerto);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    std::string out;
    if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0 &&
        send(fd, pedido.data(), static_cast<int>(pedido.size()), 0) == static_cast<int>(pedido.size())) {
        char buf[4096];
        for (int n; (n = recv(fd, buf, sizeof(buf), 0)) > 0;) out.append(buf, static_cast<size_t>(n));
    }
    cerrar(fd);
    return out;
}

std::string con(const std::string& metodo, const std::string& ruta, const std::string& cuerpo, const std::string& token) {
    return metodo + " " + ruta + " HTTP/1.1\r\nHost: x\r\nConnection: close\r\n" +
           (token.empty() ? std::string() : "Authorization: Bearer " + token + "\r\n") +
           "Content-Type: application/json\r\nContent-Length: " + std::to_string(cuerpo.size()) + "\r\n\r\n" + cuerpo;
}

bool es(const std::string& r, const char* codigo) { return r.substr(0, r.find("\r\n")).find(codigo) != std::string::npos; }

} // namespace

int main() {
    std::printf("== test_config_caliente ==\n");
#ifdef _WIN32
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
#else
    std::signal(SIGPIPE, SIG_IGN);
#endif
    Log log;
    WebrtcHub hub(2, 640, 360, 48000, 2);
    TokenAuth auth("secreto-de-prueba", "9182");
    Room::Cfg rcfg;
    rcfg.name = "sala-test";
    rcfg.lobby = true;
    Room room(rcfg);
    const auto dir = std::filesystem::temp_directory_path() / "ba-test-config-caliente";
    std::filesystem::remove_all(dir);
    ShareStore shares(dir, 1u << 20);
    SignalingServer::Cfg c;
    c.room = "sala-test";
    c.port = 0;
    c.adminPort = 0;
    const std::string kToken = "token-de-la-app";
    c.adminToken = kToken;
    SignalingServer srv(c, auth, hub, room, shares, log);
    CHECK(srv.start(), "el servidor arranco");
    const uint16_t admin = srv.adminPort();

    // --- 1) ajustes ---------------------------------------------------------------
    ba::ajustes::poner("room.visorCaidoS", 77); // un «live» que después no viene: vuelve al de siempre
    std::string r = pedir(admin, con("PUT", "/api/v1/config/ajustes", R"({"valores":{"room.maxVisores":10}})", kToken));
    CHECK(es(r, "200"), "un avanzado en caliente: 200");
    CHECK(ba::ajustes::entero("room.maxVisores", 48) == 10, "y rige enseguida");
    CHECK(ba::ajustes::entero("room.visorCaidoS", 10) == 10, "el «live» que no vino vuelve al de siempre");

    r = pedir(admin, con("PUT", "/api/v1/config/ajustes", R"({"valores":{"room.maxVisores":20,"turn.maxAsignaciones":5}})", kToken));
    CHECK(es(r, "409") && r.find("turn.maxAsignaciones") != std::string::npos, "uno que se lee al arrancar: 409 y dice cuál");
    CHECK(ba::ajustes::entero("room.maxVisores", 48) == 10, "todo o nada: no se aplicó ninguno");
    r = pedir(admin, con("PUT", "/api/v1/config/ajustes", R"({"valores":{"room.maxVisores":9999}})", kToken));
    CHECK(es(r, "409"), "fuera de rango: 409");
    r = pedir(admin, con("PUT", "/api/v1/config/ajustes", R"({"valores":{"room.maxVisores":"diez"}})", kToken));
    CHECK(es(r, "409"), "de otro tipo: 409");
    r = pedir(admin, con("PUT", "/api/v1/config/ajustes", R"({"valores":{"stream.colaMs":500}})", kToken));
    CHECK(es(r, "409"), "uno de la app (no de la sala): 409");

    // --- 2) sala ------------------------------------------------------------------
    r = pedir(admin, con("PUT", "/api/v1/config/sala", R"({"lobby":false,"pinIntentos":3,"pinBloqueoS":30,"maxUploadMB":5})", kToken));
    CHECK(es(r, "200"), "la sala en caliente: 200");
    CHECK(shares.maxBytes() == 5ull * 1024 * 1024, "el tope por archivo cambió andando");
    CHECK(!room.snapshot().value("lobby", true), "el lobby se apagó andando");
    r = pedir(admin, con("GET", "/api/v1/pin/bloqueos", "", kToken));
    CHECK(r.find("\"intentosPorIp\":3") != std::string::npos && r.find("\"bloqueoS\":30") != std::string::npos,
          "los intentos de PIN cambiaron andando");
    r = pedir(admin, con("PUT", "/api/v1/config/sala", R"({"pinIntentos":0,"maxUploadMB":7})", kToken));
    CHECK(es(r, "400") && shares.maxBytes() == 5ull * 1024 * 1024, "fuera de rango: 400 y no cambia nada");

    // --- 3) lo demás --------------------------------------------------------------
    CHECK(es(pedir(admin, con("PUT", "/api/v1/config/puertos", "{}", kToken)), "404"), "una sección que no existe: 404");
    CHECK(es(pedir(admin, con("POST", "/api/v1/config/sala", "{}", kToken)), "405"), "otro método: 405");
    CHECK(es(pedir(admin, con("PUT", "/api/v1/config/sala", R"({"lobby":true})", "")), "401"), "sin token: 401");

    srv.stop();
    std::filesystem::remove_all(dir);
    std::printf(g_fallas == 0 ? "test_config_caliente: OK\n" : "test_config_caliente: %d fallas\n", g_fallas);
    return g_fallas == 0 ? 0 : 1;
}
