// test_ip_cliente.cpp — LA IP del cliente (IpCliente.hpp): las cabeceras de proxy solo
// valen si las manda un proceso nuestro desde 127.0.0.1 y en su escucha; desde la red
// local, la IP es la del socket. La integración (el bloqueo de PIN, la botonera) está
// en test_pinggy.

#include <cstdio>
#include <cstring>
#include <string>

#include "IpCliente.hpp"

using namespace ba::gateway::ipcliente;

namespace {
int g_fallas = 0;
#define CHECK(c, m)                                             \
    do {                                                        \
        if (!(c)) {                                             \
            std::printf("FALLO: %s (linea %d)\n", m, __LINE__); \
            ++g_fallas;                                         \
        } else {                                                \
            std::printf("ok: %s\n", m);                         \
        }                                                       \
    } while (0)

std::uint32_t ip(unsigned a, unsigned b, unsigned c, unsigned d) {
    const unsigned char x[4] = {static_cast<unsigned char>(a), static_cast<unsigned char>(b),
                                static_cast<unsigned char>(c), static_cast<unsigned char>(d)};
    std::uint32_t r;
    std::memcpy(&r, x, 4);
    return r;
}

const std::uint32_t kLoop = ip(127, 0, 0, 1);

Origen de(std::uint32_t socket, bool tunel, const std::string& cab, const char* proxy = nullptr) {
    Entrada e;
    e.ipSocket = socket;
    e.escuchaTunel = tunel;
    e.cabeceras = &cab;
    if (proxy) e.conProxyV1 = ipDeTexto(proxy, e.ipProxyV1);
    return resolver(e);
}

const std::string kTodas = "GET / HTTP/1.1\r\nHost: x\r\nCF-Connecting-IP: 8.8.8.8\r\nCF-Ray: abc\r\n"
                           "X-Forwarded-For: 8.8.4.4\r\nX-Real-IP: 1.1.1.1\r\nForwarded: for=1.0.0.1\r\n\r\n";
} // namespace

int main() {
    // --- Desde la red local: el socket, punto --------------------------------------------
    {
        const Origen o = de(ip(192, 168, 1, 30), false, kTodas);
        CHECK(o.ip == ip(192, 168, 1, 30) && !o.porTunel && o.via == Via::Directa,
              "red local con todas las cabeceras de proxy: se ignoran");
        CHECK(enSala(o), "y sigue en la sala");
        const Origen p = de(ip(200, 1, 2, 3), false, kTodas);
        CHECK(p.ip == ip(200, 1, 2, 3) && !p.porTunel && !enSala(p), "IP pública directa: la suya, afuera por la IP");
    }
    // --- 127.0.0.1 con PROXY v1 (ba-web, terminador HTTPS, proxy por nombre) ---------
    {
        const Origen o = de(kLoop, false, kTodas, "192.168.0.20");
        CHECK(o.ip == ip(192, 168, 0, 20) && !o.porTunel && o.via == Via::ProxyLocal && enSala(o),
              "PROXY v1 con una IP de la red local: esa IP; las cabeceras del visitante no cuentan");
        const Origen c = de(kLoop, false, "Host: x\r\nCF-Connecting-IP: 200.5.5.5\r\nCF-Ray: x\r\n", "127.0.0.1");
        CHECK(c.porTunel && c.via == Via::Cloudflare && c.ip == ip(200, 5, 5, 5),
              "cloudflared detrás de ba-web (PROXY con 127.0.0.1): Cloudflare, con su IP");
    }
    // --- La escucha del túnel de pinggy ------------------------------------------------
    {
        const Origen o = de(kLoop, true, "Host: x\r\nX-Forwarded-For: 9.9.9.9, 200.3.3.3\r\n");
        CHECK(o.ip == ip(200, 3, 3, 3) && o.porTunel && o.via == Via::Pinggy, "XFF: el último (el que agregó pinggy)");
        const Origen dos = de(kLoop, true, "Host: x\r\nX-Forwarded-For: 200.3.3.4\r\nx-forwarded-for: 1.2.3.4, 200.3.3.5\r\n");
        CHECK(dos.ip == ip(200, 3, 3, 5), "XFF en varias líneas: el último de la última línea");
        const Origen basura = de(kLoop, true, "Host: x\r\nX-Forwarded-For: 200.3.3.4, basura\r\n");
        CHECK(basura.ip == kLoop && basura.porTunel, "XFF con el último inválido: 127.0.0.1 (todos juntos), afuera");
        const Origen cf = de(kLoop, true, "Host: x\r\nCF-Connecting-IP: 200.1.1.1\r\nX-Real-IP: 200.1.1.1\r\n");
        CHECK(cf.ip == kLoop && cf.porTunel && cf.via == Via::Pinggy,
              "en el túnel de pinggy CF-Connecting-IP y X-Real-IP no valen");
        const Origen px = de(kLoop, true, "Host: x\r\nX-Forwarded-For: 200.3.3.3\r\n", "190.1.1.1");
        CHECK(px.ip == ip(190, 1, 1, 1) && px.porTunel, "con PROXY v1 en el túnel: la del PROXY, no el XFF");
        const Origen local = de(kLoop, true, "Host: x\r\n", "192.168.0.20");
        CHECK(local.porTunel && !enSala(local), "por el túnel con una IP privada: afuera igual");
    }
    // --- La escucha común desde 127.0.0.1 sin PROXY -------------------------------------
    {
        const Origen m = de(kLoop, false, "Host: x\r\nX-Forwarded-For: 200.1.1.1\r\nX-Real-IP: 200.1.1.1\r\n");
        CHECK(m.ip == kLoop && !m.porTunel && m.via == Via::Maquina && enSala(m),
              "un programa de esta máquina: 127.0.0.1 (XFF y X-Real-IP no valen en la común)");
        const Origen cf = de(kLoop, false, "Host: x\r\nCF-Connecting-IP: 2800:810:1:2::1\r\nCF-Ray: x\r\n");
        CHECK(cf.porTunel && cf.via == Via::Cloudflare && reinterpret_cast<const unsigned char*>(&cf.ip)[0] == 240,
              "Cloudflare con IPv6: la seudo-IP 240.x, afuera");
        const Origen privada = de(kLoop, false, "Host: x\r\nCF-Connecting-IP: 192.168.1.9\r\n");
        CHECK(privada.porTunel && !enSala(privada), "Cloudflare con una IP «privada»: afuera igual");
        const Origen dos = de(kLoop, false, "Host: x\r\nCF-Connecting-IP: 200.1.1.1\r\nCF-Connecting-IP: 200.1.1.2\r\n");
        CHECK(dos.porTunel && dos.ip == kLoop, "CF-Connecting-IP repetida: afuera, sin creerle ninguna");
        const Origen ray = de(kLoop, false, "Host: x\r\nCF-Ray: x\r\n");
        CHECK(ray.porTunel && ray.ip == kLoop, "solo CF-Ray: afuera");
    }
    // --- Lectura de cabeceras -----------------------------------------------------------
    {
        std::string u;
        CHECK(lineasDe("GET / HTTP/1.1\r\nA: 1\r\na: 2 \r\n\r\nA: 3\r\n", "A", &u) == 2 && u == "2",
              "lineasDe: sin distinguir mayúsculas, hasta la línea vacía");
        CHECK(lineasDe("Host: x\r\nX-Forwarded-For-Otra: 1\r\n", "X-Forwarded-For", nullptr) == 0,
              "lineasDe: el nombre entero, no un prefijo");
    }
    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
