// test_sse_epoch.cpp — el flujo de eventos de la sala hacia la app (G-7, G-9, G-10)
//
//   1. El primer `room` de cada conexión dice de qué arranque es la sala (`epoch`), la
//      versión del contrato (`protocolo`) y si pudo reenviar lo perdido (`reanudado`).
//   2. Last-Event-ID de esta misma sala: se reenvían los eventos de un solo disparo que
//      se perdieron mientras la app no estaba (una corrección, acá) y `reanudado` es true.
//   3. De otra sala (otro epoch): nada se reenvía y `reanudado` es false.
//   4. Si la cola corta ya no tiene todo lo perdido: `reanudado` false (la app republica).
//   5. Una app que no lee no traba a quien emite: los pedidos que generan eventos
//      terminan enseguida aunque haya una conexión de eventos que no lee nada.

#include "Log.hpp"
#include "Room.hpp"
#include "ShareStore.hpp"
#include "SignalingServer.hpp"
#include "TokenAuth.hpp"
#include "Webrtc.hpp"
#include "ipc/ProtocoloSala.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

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
#include <sys/time.h>
#include <unistd.h>
using TestFd = int;
static constexpr TestFd kFdInvalid = -1;
#endif

using namespace ba::gateway;
using json = nlohmann::json;

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

TestFd conectar(uint16_t puerto) {
    TestFd fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == kFdInvalid) return fd;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(puerto);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        cerrar(fd);
        return kFdInvalid;
    }
#ifdef _WIN32
    DWORD ms = 300;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
#else
    timeval tv{0, 300000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    return fd;
}

/// Un pedido entero y la respuesta hasta que el servidor cierra.
std::string pedir(uint16_t puerto, const std::string& pedido) {
    TestFd fd = conectar(puerto);
    if (fd == kFdInvalid) return "";
    std::string out;
    if (send(fd, pedido.data(), static_cast<int>(pedido.size()), 0) == static_cast<int>(pedido.size())) {
        char buf[4096];
        const auto hasta = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < hasta) {
            const int n = static_cast<int>(recv(fd, buf, sizeof(buf), 0));
            if (n == 0) break;
            if (n > 0) out.append(buf, static_cast<size_t>(n));
        }
    }
    cerrar(fd);
    return out;
}

std::string con(const std::string& metodo, const std::string& ruta, const std::string& cuerpo,
                const std::string& cabeceras = {}) {
    return metodo + " " + ruta + " HTTP/1.1\r\nHost: x\r\nConnection: close\r\n" + cabeceras +
           "Content-Type: application/json\r\nContent-Length: " + std::to_string(cuerpo.size()) + "\r\n\r\n" + cuerpo;
}

/// Abre el flujo de eventos (con Last-Event-ID si viene) y lee lo que llegue en `ms`.
struct Flujo {
    TestFd fd = kFdInvalid;
    std::string leido;
    void leer(int ms) {
        char buf[8192];
        const auto hasta = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < hasta) {
            const int n = static_cast<int>(recv(fd, buf, sizeof(buf), 0));
            if (n == 0) break;
            if (n > 0) leido.append(buf, static_cast<size_t>(n));
        }
    }
    /// Los eventos leídos: [{id, event, data}].
    std::vector<json> eventos() const {
        std::vector<json> out;
        std::size_t p = leido.find("\r\n\r\n");
        p = p == std::string::npos ? 0 : p + 4;
        while (true) {
            const std::size_t fin = leido.find("\n\n", p);
            if (fin == std::string::npos) break;
            json e = json::object();
            const std::string bloque = leido.substr(p, fin - p);
            std::size_t q = 0;
            while (q < bloque.size()) {
                std::size_t eol = bloque.find('\n', q);
                if (eol == std::string::npos) eol = bloque.size();
                const std::string l = bloque.substr(q, eol - q);
                if (l.rfind("id: ", 0) == 0) e["id"] = l.substr(4);
                if (l.rfind("event: ", 0) == 0) e["event"] = l.substr(7);
                if (l.rfind("data: ", 0) == 0) e["data"] = json::parse(l.substr(6), nullptr, false);
                q = eol + 1;
            }
            if (e.contains("event")) out.push_back(e);
            p = fin + 2;
        }
        return out;
    }
    ~Flujo() {
        if (fd != kFdInvalid) cerrar(fd);
    }
};

void abrir(Flujo& f, uint16_t admin, const std::string& token, const std::string& ultimo) {
    f.fd = conectar(admin);
    const std::string p = "GET /api/v1/events HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer " + token + "\r\n" +
                          (ultimo.empty() ? std::string() : "Last-Event-ID: " + ultimo + "\r\n") + "\r\n";
    send(f.fd, p.data(), static_cast<int>(p.size()), 0);
}

std::string corregir(uint16_t invitados, int id, const std::string& texto) {
    return pedir(invitados,
                 con("POST", "/api/correccion", json{{"k", "clave-corr"}, {"id", id}, {"texto", texto}}.dump()));
}

} // namespace

int main() {
    std::printf("== test_sse_epoch ==\n");
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
    Room room(rcfg);
    const auto dir = std::filesystem::temp_directory_path() / "ba-test-sse-epoch";
    std::filesystem::remove_all(dir);
    ShareStore shares(dir, 1u << 20);
    SignalingServer::Cfg c;
    c.room = "sala-test";
    c.port = 0;
    c.adminPort = 0;
    c.correccionKey = "clave-corr";
    const std::string kToken = "token-de-la-app";
    c.adminToken = kToken;
    SignalingServer srv(c, auth, hub, room, shares, log);
    CHECK(srv.start(), "el servidor arranco");
    const uint16_t admin = srv.adminPort();
    const uint16_t invitados = srv.guestPort();

    // --- 1) El primer room ----------------------------------------------------------
    std::string ultimo;
    {
        Flujo f;
        abrir(f, admin, kToken, "");
        f.leer(400);
        const auto ev = f.eventos();
        CHECK(!ev.empty() && ev[0].value("event", "") == "room", "lo primero es la foto");
        const json d = ev.empty() ? json() : ev[0].value("data", json());
        CHECK(d.value("epoch", "") == srv.epoch() && !srv.epoch().empty(), "con el epoch de este arranque");
        CHECK(d.value("protocolo", 0) == ba::ipc::kProtocoloSala, "y la versión del contrato");
        CHECK(d.value("reanudado", true) == false, "sin Last-Event-ID no hay nada que reanudar");
        ultimo = ev.empty() ? "" : ev[0].value("id", "");
        CHECK(ultimo.rfind(srv.epoch() + ":", 0) == 0, "el id es «<epoch>:<n>»");
        // Uno que llega con la conexión abierta, con su id.
        corregir(invitados, 1, "uno");
        f.leer(400);
        const auto ev2 = f.eventos();
        CHECK(ev2.size() >= 2 && ev2.back().value("event", "") == "correccion", "un evento en vivo llega");
        if (ev2.size() >= 2) ultimo = ev2.back().value("id", "");
    }

    // --- 2) Lo que se perdió, de esta misma sala --------------------------------------
    corregir(invitados, 7, "siete"); // con la app desconectada
    {
        Flujo f;
        abrir(f, admin, kToken, ultimo);
        f.leer(400);
        const auto ev = f.eventos();
        CHECK(!ev.empty() && ev[0].value("data", json()).value("reanudado", false), "misma sala: reanudado");
        bool siete = false, uno = false;
        for (const auto& e : ev) {
            if (e.value("event", "") != "correccion") continue;
            const int id = e.value("data", json()).value("id", 0);
            siete = siete || id == 7;
            uno = uno || id == 1;
        }
        CHECK(siete, "la corrección perdida se reenvía");
        CHECK(!uno, "la que ya había llegado, no");
    }

    // --- 3) De otra sala -----------------------------------------------------------------
    {
        Flujo f;
        abrir(f, admin, kToken, "otraSala:1");
        f.leer(400);
        const auto ev = f.eventos();
        CHECK(!ev.empty() && !ev[0].value("data", json()).value("reanudado", true), "otro epoch: no reanudado");
        int correcciones = 0;
        for (const auto& e : ev) correcciones += e.value("event", "") == "correccion";
        CHECK(correcciones == 0, "y no se reenvía nada");
    }

    // --- 5) Una app que no lee no traba a quien emite (antes que el 4: llena la cola) ---
    {
        Flujo mudo;
        abrir(mudo, admin, kToken, ""); // nunca lee
        const std::string grande(1500, 'x');
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 1200; ++i) corregir(invitados, 100 + i, grande);
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        std::printf("   1200 eventos con una conexión que no lee: %lld ms\n", static_cast<long long>(ms));
        CHECK(ms < 8000, "quien emite no espera a la app que no lee");
    }

    // --- 4) La cola corta ya no tiene todo ---------------------------------------------
    {
        Flujo f;
        abrir(f, admin, kToken, ultimo);
        f.leer(400);
        const auto ev = f.eventos();
        CHECK(!ev.empty() && !ev[0].value("data", json()).value("reanudado", true),
              "si se perdió más de lo que se guarda: no reanudado (la app republica todo)");
    }

    srv.stop();
    std::filesystem::remove_all(dir);
    std::printf(g_fallas == 0 ? "== OK ==\n" : "== %d FALLAS ==\n", g_fallas);
    return g_fallas == 0 ? 0 : 1;
}
