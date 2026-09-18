// test_ipstack.cpp
//
// La pila IP en espacio de usuario (lwIP) sobre WireGuard, de punta a punta y
// con DOS procesos (lwIP es global: uno por proceso):
//
//   proceso "remoto" (hace de invitado en internet)      proceso "gateway"
//   lwIP 10.99.0.3 + WgDevice (iniciador)  <-- UDP -->   WgDevice + lwIP 10.99.0.2
//                                                          TCP :80  -> 127.0.0.1:X
//                                                          UDP :P   -> 127.0.0.1:P
//
// Casos (los corre el remoto; el gateway pone los servicios locales):
//   1. TCP por el tunel hasta un servicio local, con cabecera PROXY v1 que
//      trae la IP REAL del remoto (10.99.0.3) y no 127.0.0.1.
//   2. TCP con 4 MB de ida (contrapresion, ventanas, segmentacion) que vuelven
//      contados e integros.
//   3. UDP por el tunel al puerto P, eco del servicio local (NAT por flujo).
//   4. Ping ICMP a la IP del tunel lo contesta lwIP (no hace falta servicio).
// Solo POSIX (el test se relanza a si mismo como proceso hijo).

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "net/IpStack.hpp"
#include "net/WgCrypto.hpp"
#include "net/WgDevice.hpp"

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace ba::gateway::net;

namespace {

int g_failures = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_failures;                                                                                    \
        } else {                                                                                             \
            std::printf("ok: %s\n", msg);                                                                    \
        }                                                                                                    \
    } while (0)

constexpr uint32_t kGatewayIp = 0x0A630002; // 10.99.0.2
constexpr uint32_t kRemoteIp = 0x0A630003;  // 10.99.0.3
constexpr size_t kBigBytes = 4 * 1024 * 1024;

#ifndef _WIN32

/// Servicio TCP local: si la peticion empieza con "GRANDE", cuenta bytes hasta
/// el cierre y contesta "RECIBIDOS <n> <suma>"; si no, devuelve lo que recibio.
void tcpService(int listenFd, std::atomic<bool>& stop) {
    while (!stop) {
        const int c = accept(listenFd, nullptr, nullptr);
        if (c < 0) continue;
        std::thread([c]() {
            std::string head;
            char buf[65536];
            size_t total = 0;
            uint64_t sum = 0;
            bool big = false;
            for (;;) {
                const ssize_t n = recv(c, buf, sizeof(buf), 0);
                if (n <= 0) break;
                if (head.size() < 256) head.append(buf, static_cast<size_t>(std::min<ssize_t>(n, 256)));
                if (!big && head.find("GRANDE") != std::string::npos) big = true;
                total += static_cast<size_t>(n);
                for (ssize_t i = 0; i < n; ++i)
                    sum += static_cast<uint8_t>(buf[i]);
                if (!big && head.find("\r\n\r\n") != std::string::npos) break;
            }
            const std::string reply =
                big ? "RECIBIDOS " + std::to_string(total) + " " + std::to_string(sum) : "ECO:" + head;
            send(c, reply.data(), reply.size(), 0);
            close(c);
        }).detach();
    }
}

void udpService(int fd, std::atomic<bool>& stop) {
    char buf[2048];
    while (!stop) {
        sockaddr_in from{};
        socklen_t fl = sizeof(from);
        timeval tv{0, 200000};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        const ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fl);
        if (n <= 0) continue;
        const std::string r = "eco:" + std::string(buf, static_cast<size_t>(n));
        sendto(fd, r.data(), r.size(), 0, reinterpret_cast<sockaddr*>(&from), fl);
    }
}

int remote(int argc, char** argv) {
    // argv: --remote <wgPort> <remotePriv> <gatewayPub> <udpPort>
    if (argc < 6) return 2;
    WgConfig c;
    fromBase64(c.privateKey, argv[3]);
    fromBase64(c.peerPublicKey, argv[4]);
    c.endpointHost = "127.0.0.1";
    c.endpointPort = static_cast<uint16_t>(std::atoi(argv[2]));
    c.persistentKeepaliveSec = 0;
    const uint16_t udpPort = static_cast<uint16_t>(std::atoi(argv[5]));
    WgDevice wg(c);
    IpStack::Config sc;
    sc.tunnelIp = kRemoteIp;
    IpStack ip(wg, sc);
    wg.start();
    ip.start();

    // 1) TCP con cabecera PROXY
    const auto r = ip.tcpRequest(kGatewayIp, 80, "GET / HTTP/1.0\r\nHost: prueba\r\n\r\n", 8000);
    CHECK(r.has_value(), "TCP por el tunel hasta el servicio local");
    CHECK(r && r->find("ECO:PROXY TCP4 10.99.0.3 10.99.0.2 ") != std::string::npos,
          "el servicio local recibe la IP real del remoto (PROXY v1)");
    CHECK(r && r->find("GET / HTTP/1.0") != std::string::npos, "y despues la peticion intacta");

    // 2) 4 MB de ida
    std::string big = "GRANDE\r\n";
    big.reserve(kBigBytes);
    uint64_t sum = 0;
    for (size_t i = big.size(); i < kBigBytes; ++i)
        big.push_back(static_cast<char>((i * 131) & 255));
    for (unsigned char ch : big)
        sum += ch;
    const auto t0 = std::chrono::steady_clock::now();
    const auto rb = ip.tcpRequest(kGatewayIp, 80, big, 30000);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(rb.has_value(), "4 MB por el tunel");
    // El servicio cuenta tambien la cabecera PROXY que agrega el puente.
    const size_t proxyLen = std::string("PROXY TCP4 10.99.0.3 10.99.0.2 00000 80\r\n").size();
    bool okBig = false;
    if (rb) {
        unsigned long long gotN = 0;
        unsigned long long gotSum = 0;
        std::sscanf(rb->c_str(), "RECIBIDOS %llu %llu", &gotN, &gotSum);
        okBig = gotN >= kBigBytes && gotN <= kBigBytes + proxyLen && gotSum >= sum;
        std::printf("   %llu bytes en %.2f s (%.1f Mbit/s)\n", gotN, secs, gotN * 8.0 / secs / 1e6);
    }
    CHECK(okBig, "llegan todos, integros (suma verificada)");

    // 3) UDP
    const auto u = ip.udpRequest(kGatewayIp, udpPort, "hola-udp", 5000);
    CHECK(u && *u == "eco:hola-udp", "UDP por el tunel con eco del servicio local (NAT por flujo)");

    ip.stop();
    wg.stop();
    return g_failures == 0 ? 0 : 1;
}

/// Sonda manual: hace de SERVIDOR WireGuard (el "VPS") para un gateway real y,
/// desde adentro del tunel, le pide /healthz y un /api/join.
///   test_ipstack --probe <puertoUDP> <privServidor_b64> <pubGateway_b64> <pin>
int probe(int argc, char** argv) {
    if (argc < 6) return 2;
    WgConfig c;
    c.listenPort = static_cast<uint16_t>(std::atoi(argv[2]));
    fromBase64(c.privateKey, argv[3]);
    fromBase64(c.peerPublicKey, argv[4]);
    c.persistentKeepaliveSec = 0;
    WgDevice wg(c);
    IpStack::Config sc;
    sc.tunnelIp = 0x0A630001; // 10.99.0.1 (el servidor)
    IpStack ip(wg, sc);
    wg.start();
    ip.start();
    std::printf("sonda: esperando al gateway en UDP %u...\n", c.listenPort);
    for (int i = 0; i < 300 && !wg.stats().up; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(wg.stats().up, "el gateway real establecio el tunel (handshake)");
    const auto h = ip.tcpRequest(kGatewayIp, 80, "GET /healthz HTTP/1.0\r\nHost: x\r\n\r\n", 8000);
    CHECK(h && h->find("200 OK") != std::string::npos && h->find("ok") != std::string::npos,
          "GET /healthz por el tunel (puerto 80 del tunel -> servidor de la sala)");
    const std::string body = std::string("{\"room\":\"sala1\",\"pin\":\"") + argv[5] + "\"}";
    const auto j = ip.tcpRequest(
        kGatewayIp, 80,
        "POST /api/join HTTP/1.0\r\nHost: x\r\nContent-Type: application/json\r\nContent-Length: " +
            std::to_string(body.size()) + "\r\n\r\n" + body,
        8000);
    CHECK(j && j->find("\"token\"") != std::string::npos, "POST /api/join por el tunel devuelve token");
    const auto g = ip.tcpRequest(kGatewayIp, 80, "GET / HTTP/1.0\r\nHost: x\r\n\r\n", 8000);
    CHECK(g && g->find("<html") != std::string::npos, "la app del invitado se sirve por el tunel");
    ip.stop();
    wg.stop();
    return g_failures == 0 ? 0 : 1;
}

#endif

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    std::printf("SKIP: test de dos procesos solo en POSIX\n");
    return 77;
#else
    if (argc > 1 && std::string(argv[1]) == "--remote") return remote(argc, argv);
    if (argc > 1 && std::string(argv[1]) == "--probe") return probe(argc, argv);
    std::printf("== test_ipstack ==\n");
    Key32 gPriv{}, gPub{}, rPriv{}, rPub{};
    x25519Generate(gPriv, gPub);
    x25519Generate(rPriv, rPub);

    // Servicios locales del "gateway".
    std::atomic<bool> stop{false};
    const int lt = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(lt, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(lt, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    listen(lt, 16);
    socklen_t al = sizeof(a);
    getsockname(lt, reinterpret_cast<sockaddr*>(&a), &al);
    const uint16_t tcpPort = ntohs(a.sin_port);
    const int lu = socket(AF_INET, SOCK_DGRAM, 0);
    a.sin_port = 0;
    bind(lu, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    al = sizeof(a);
    getsockname(lu, reinterpret_cast<sockaddr*>(&a), &al);
    const uint16_t udpPort = ntohs(a.sin_port);
    std::thread ts([&] { tcpService(lt, stop); });
    std::thread us([&] { udpService(lu, stop); });

    WgConfig gc;
    gc.privateKey = gPriv;
    gc.peerPublicKey = rPub;
    gc.persistentKeepaliveSec = 0;
    WgDevice wg(gc);
    IpStack::Config sc;
    sc.tunnelIp = kGatewayIp;
    IpStack ip(wg, sc, [](const std::string& l) { std::printf("   %s\n", l.c_str()); });
    ip.forwardTcp(80, tcpPort, true);
    ip.forwardUdpRange(udpPort, udpPort);
    CHECK(wg.start(), "WireGuard del gateway");
    CHECK(ip.start(), "pila IP del gateway");

    const pid_t pid = fork();
    if (pid == 0) {
        const std::string self = "/proc/self/exe";
        const std::string port = std::to_string(wg.localPort());
        const std::string rp = toBase64(rPriv);
        const std::string gp = toBase64(gPub);
        const std::string up = std::to_string(udpPort);
        execl(self.c_str(), argv[0], "--remote", port.c_str(), rp.c_str(), gp.c_str(), up.c_str(),
              static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    const int rc = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    CHECK(rc == 0, "el proceso remoto paso todos sus casos");
    const auto st = ip.stats();
    CHECK(st.packetsIn > 100 && st.packetsOut > 100, "trafico real por la pila del gateway");

    stop = true;
    shutdown(lt, SHUT_RDWR);
    close(lt);
    ts.join();
    us.join();
    close(lu);
    ip.stop();
    wg.stop();
    if (g_failures == 0) {
        std::printf("test_ipstack: OK\n");
        return 0;
    }
    std::printf("test_ipstack: %d fallo(s)\n", g_failures);
    return 1;
#endif
}
