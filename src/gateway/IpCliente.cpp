// src/gateway/IpCliente.cpp — ver IpCliente.hpp

#include "IpCliente.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include "Borde.hpp"

namespace ba::gateway::ipcliente {

namespace {

bool esLoopback(std::uint32_t ipRed) { return ntohl(ipRed) == INADDR_LOOPBACK; }

std::string recortar(const std::string& s) {
    const auto ini = s.find_first_not_of(" \t");
    if (ini == std::string::npos) return {};
    const auto fin = s.find_last_not_of(" \t");
    return s.substr(ini, fin - ini + 1);
}

} // namespace

std::size_t lineasDe(const std::string& cabeceras, const std::string& nombre, std::string* ultima) {
    std::size_t n = 0;
    std::size_t p = 0;
    while (p < cabeceras.size()) {
        std::size_t eol = cabeceras.find('\n', p);
        if (eol == std::string::npos) eol = cabeceras.size();
        std::size_t fin = eol;
        if (fin > p && cabeceras[fin - 1] == '\r') --fin;
        if (fin == p) break; // la línea vacía: fin de la cabecera
        const std::size_t dp = cabeceras.find(':', p);
        if (dp != std::string::npos && dp < fin && dp - p == nombre.size()) {
            bool igual = true;
            for (std::size_t i = 0; i < nombre.size() && igual; ++i)
                igual = std::tolower(static_cast<unsigned char>(cabeceras[p + i])) ==
                        std::tolower(static_cast<unsigned char>(nombre[i]));
            if (igual) {
                ++n;
                if (ultima) *ultima = recortar(cabeceras.substr(dp + 1, fin - dp - 1));
            }
        }
        p = eol + 1;
    }
    return n;
}

bool ipDeTexto(const std::string& texto, std::uint32_t& ipRed) {
    if (texto.empty() || texto.size() > 45) return false;
    in_addr a{};
    if (inet_pton(AF_INET, texto.c_str(), &a) == 1) {
        ipRed = static_cast<std::uint32_t>(a.s_addr);
        return true;
    }
    in6_addr a6{};
    if (inet_pton(AF_INET6, texto.c_str(), &a6) != 1) return false;
    const auto* b = reinterpret_cast<const unsigned char*>(&a6);
    static const unsigned char kMapeada[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
    if (std::memcmp(b, kMapeada, 12) == 0) {
        std::memcpy(&ipRed, b + 12, 4);
        return true;
    }
    // FNV-1a del /64 (lo que suele ser UN cliente) -> 240.x.y.z.
    std::uint32_t h = 2166136261u;
    for (int i = 0; i < 8; ++i) h = (h ^ b[i]) * 16777619u;
    const unsigned char ip4[4] = {240, static_cast<unsigned char>(h >> 16), static_cast<unsigned char>(h >> 8),
                                  static_cast<unsigned char>(h)};
    std::memcpy(&ipRed, ip4, 4);
    return true;
}

bool ipDeProxyV1(const std::string& linea, std::uint32_t& ipRed) {
    char src[64] = {0};
    char dst[64] = {0};
    unsigned sp = 0;
    unsigned dp = 0;
    if (std::sscanf(linea.c_str(), "PROXY TCP4 %63s %63s %u %u", src, dst, &sp, &dp) == 4 ||
        std::sscanf(linea.c_str(), "PROXY TCP6 %63s %63s %u %u", src, dst, &sp, &dp) == 4)
        return ipDeTexto(src, ipRed);
    return false;
}

bool ipDeXff(const std::string& valor, std::uint32_t& ipRed) {
    // El ÚLTIMO de la lista es el que agregó el proxy; los de antes los pudo escribir el cliente.
    const size_t coma = valor.rfind(',');
    const std::string ultimo = recortar(coma == std::string::npos ? valor : valor.substr(coma + 1));
    return !ultimo.empty() && ipDeTexto(ultimo, ipRed);
}

Origen resolver(const Entrada& e) {
    Origen o;
    o.ip = e.ipSocket;
    o.porTunel = e.escuchaTunel;
    o.via = e.escuchaTunel ? Via::Pinggy : Via::Directa;
    // 1. De otra máquina: la del socket, sin mirar nada más.
    if (!esLoopback(e.ipSocket)) return o;
    static const std::string kVacio;
    const std::string& cab = e.cabeceras ? *e.cabeceras : kVacio;
    // 2. Un proceso nuestro con PROXY v1: lo que vio él.
    if (e.conProxyV1) {
        o.ip = e.ipProxyV1;
        if (!e.escuchaTunel) o.via = Via::ProxyLocal;
    } else if (e.escuchaTunel) {
        // 3. ba-pinggy sin PROXY v1: el último de la última línea X-Forwarded-For.
        std::string xff;
        std::uint32_t real = 0;
        if (lineasDe(cab, "X-Forwarded-For", &xff) > 0 && ipDeXff(xff, real)) o.ip = real;
        return o;
    } else {
        o.via = Via::Maquina;
    }
    if (e.escuchaTunel || !esLoopback(o.ip)) return o;
    // 4. Escucha común y todavía 127.0.0.1: ¿cloudflared?
    std::string cf;
    const std::size_t nCf = lineasDe(cab, "CF-Connecting-IP", &cf);
    const bool cfRay = lineasDe(cab, "CF-Ray", nullptr) > 0;
    if (nCf == 0 && !cfRay) return o;
    o.porTunel = true;
    o.via = Via::Cloudflare;
    std::uint32_t real = 0;
    if (nCf == 1 && ipDeTexto(cf, real)) o.ip = real;
    return o;
}

bool enSala(const Origen& o) { return !o.porTunel && borde::ipLocal(o.ip); }

} // namespace ba::gateway::ipcliente
