// test_netutil.cpp — las IP de la máquina para el QR de la sala (net::localIPv4s)
//
// El QR de la sala decía «solo esta máquina» aunque se eligiera una IP de la
// red: en Windows las IP salían de gethostname + getaddrinfo, que antes del
// WSAStartup del servidor no devuelven nada. Ahora salen de los adaptadores.
//   - sin repetidas, ninguna 127.x, todas IPv4 bien formadas;
//   - las 169.254 (sin red) al final.
// Además las imprime, en orden: la primera es la que va en el QR.
#include "net/NetUtil.hpp"

#include <cstdio>
#include <set>
#include <string>

int main() {
    int fallas = 0;
    const auto ips = ba::gateway::net::localIPv4s();
    std::printf("%zu IP:\n", ips.size());
    for (const auto& ip : ips) std::printf("  %s\n", ip.c_str());
    std::set<std::string> vistas;
    bool linkLocalVista = false;
    for (const auto& ip : ips) {
        uint32_t v = 0;
        int pre = 0;
        if (!ba::gateway::net::parseCidr(ip, v, pre)) std::printf("FALLA: «%s» no es una IPv4\n", ip.c_str()), ++fallas;
        if (ip.rfind("127.", 0) == 0) std::printf("FALLA: %s es local\n", ip.c_str()), ++fallas;
        if (!vistas.insert(ip).second) std::printf("FALLA: %s repetida\n", ip.c_str()), ++fallas;
        const bool linkLocal = ip.rfind("169.254.", 0) == 0;
        if (linkLocalVista && !linkLocal) std::printf("FALLA: %s después de una 169.254\n", ip.c_str()), ++fallas;
        linkLocalVista = linkLocalVista || linkLocal;
    }
    std::printf(fallas ? "test_netutil: %d fallas\n" : "test_netutil: todo ok\n", fallas);
    return fallas ? 1 : 0;
}
