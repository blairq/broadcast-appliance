// test_botonera_borde.cpp — el borde de red de la botonera (Botonera.hpp):
// solo red local, Origin igual a Host (y nada de Cloudflare), SHA-256 de mbedtls.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <nlohmann/json.hpp>

#include "Botonera.hpp"
#include "Log.hpp"

using ba::gateway::BotoneraBorde;

namespace {
int g_fallas = 0;
#define CHECK(c, m)                                              \
    do {                                                         \
        if (!(c)) {                                              \
            std::printf("FALLO: %s (linea %d)\n", m, __LINE__);  \
            ++g_fallas;                                          \
        } else {                                                 \
            std::printf("ok: %s\n", m);                          \
        }                                                        \
    } while (0)

/// La IP como la guarda el socket (orden de red).
std::uint32_t ip(unsigned a, unsigned b, unsigned c, unsigned d) {
    const unsigned char x[4] = {static_cast<unsigned char>(a), static_cast<unsigned char>(b),
                                static_cast<unsigned char>(c), static_cast<unsigned char>(d)};
    std::uint32_t r;
    std::memcpy(&r, x, 4);
    return r;
}
} // namespace

int main() {
    CHECK(BotoneraBorde::ipLocal(ip(192, 168, 1, 20)), "192.168.x.x es local");
    CHECK(BotoneraBorde::ipLocal(ip(10, 0, 0, 5)), "10.x es local");
    CHECK(BotoneraBorde::ipLocal(ip(172, 20, 0, 1)), "172.16/12 es local");
    CHECK(!BotoneraBorde::ipLocal(ip(172, 32, 0, 1)), "172.32 no");
    CHECK(BotoneraBorde::ipLocal(ip(127, 0, 0, 1)), "loopback");
    CHECK(BotoneraBorde::ipLocal(ip(169, 254, 3, 3)), "link-local");
    CHECK(!BotoneraBorde::ipLocal(ip(8, 8, 8, 8)), "Internet no");

    CHECK(BotoneraBorde::origenValido("Host: 192.168.1.5:8789\r\nOrigin: https://192.168.1.5:8789"), "mismo origen");
    CHECK(!BotoneraBorde::origenValido("Host: 192.168.1.5:8789\r\nOrigin: https://malo.example"), "origen ajeno");
    CHECK(BotoneraBorde::origenValido("Host: 192.168.1.5:8789"), "sin Origin (no es un navegador de otra página)");
    CHECK(!BotoneraBorde::origenValido("Host: x\r\nCF-Connecting-IP: 1.2.3.4"), "lo que viene por Cloudflare no");

    CHECK(BotoneraBorde::sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "sha256 de referencia");

#ifndef _WIN32
    // La API remota HTTP (T59): un token con permiso «http» aprieta una celda (202 y
    // evento a la app); sin permiso, o con otro token, 401 y nada.
    {
        ba::gateway::Log log;
        std::vector<nlohmann::json> eventos;
        BotoneraBorde b("", log, [&](const nlohmann::json& e) { eventos.push_back(e); });
        const std::string token(64, 'a'), sal = "00112233";
        const auto pedir = [&](const std::string& metodo, const std::string& ruta, const std::string& cabeceras,
                               const std::string& cuerpo, bool admin) {
            int sv[2];
            if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return std::string();
            if (admin) b.admin(sv[0], metodo, ruta, cuerpo);
            else b.atender(sv[0], metodo, ruta, "", cabeceras, cuerpo, ip(192, 168, 1, 30));
            ::close(sv[0]);
            std::string r;
            char buf[2048];
            for (ssize_t n; (n = ::read(sv[1], buf, sizeof buf)) > 0;) r.append(buf, static_cast<size_t>(n));
            ::close(sv[1]);
            return r;
        };
        const auto credenciales = [&](bool http) {
            const nlohmann::json c{{"dispositivos",
                                    {{{"id", "http-1"}, {"hash", BotoneraBorde::sha256Hex(sal + token)}, {"sal", sal}, {"http", http}}}}};
            pedir("POST", "/api/v1/botonera/credenciales", "", c.dump(), true);
        };
        const std::string ruta = "/api/botonera/director/pagina/p1/celda/2/1/tocar";
        credenciales(true);
        std::string r = pedir("POST", ruta, "Host: 192.168.1.5:8787\r\nAuthorization: Bearer " + token, "", false);
        CHECK(r.find(" 202 ") != std::string::npos, "API remota: token con permiso -> 202");
        CHECK(eventos.size() == 1 && eventos[0].value("tipo", "") == "remota" && eventos[0].value("x", -1) == 2 &&
                  eventos[0].value("y", -1) == 1 && eventos[0].value("pagina", "") == "p1" &&
                  eventos[0].value("dispositivo", "") == "http-1",
              "API remota: el evento lleva aparato, página y celda");
        r = pedir("POST", ruta, "Host: x\r\nAuthorization: Bearer " + std::string(64, 'b'), "", false);
        CHECK(r.find(" 401 ") != std::string::npos && eventos.size() == 1, "API remota: otro token -> 401");
        credenciales(false);
        r = pedir("POST", ruta, "Host: x\r\nAuthorization: Bearer " + token, "", false);
        CHECK(r.find(" 401 ") != std::string::npos && eventos.size() == 1, "API remota: sin permiso http -> 401");
        r = pedir("GET", ruta, "Host: x", "", false);
        CHECK(r.find(" 405 ") != std::string::npos, "API remota: GET no");
        r = pedir("POST", "/api/botonera/director/pagina/p1/celda/2/1/borrar", "Host: x", "", false);
        CHECK(r.find(" 400 ") != std::string::npos, "API remota: acción desconocida -> 400");
    }
#endif

    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
