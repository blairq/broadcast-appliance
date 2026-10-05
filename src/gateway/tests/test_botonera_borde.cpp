// test_botonera_borde.cpp — el borde de red de la botonera (Botonera.hpp):
// solo red local, Origin igual a Host (y nada de Cloudflare), SHA-256 de mbedtls.

#include <cstdio>
#include <cstring>
#include <string>

#include "Botonera.hpp"

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

    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
