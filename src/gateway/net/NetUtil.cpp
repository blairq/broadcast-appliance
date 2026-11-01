// ============================================================================
// src/gateway/net/NetUtil.cpp — ver NetUtil.hpp
// ============================================================================

#include "net/NetUtil.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <cwctype>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#endif

namespace ba::gateway::net {

bool esVirtual(const std::string& n) {
    for (const char* d : {"virtual", "hyper-v", "vmware", "vpn", "wsl", "tap-", "loopback", "tailscale", "zerotier", "docker"})
        if (n.find(d) != std::string::npos) return true;
    // Por cómo empieza el nombre (Linux, la Mac): bridge100… son los de las máquinas
    // virtuales y el compartir internet de la Mac (bridge0, el de Thunderbolt, es de verdad).
    for (const char* d : {"br-", "veth", "virbr", "tun", "utun", "zt", "vmnet", "vmenet", "vboxnet", "lxcbr", "lxdbr",
                          "podman", "cni", "flannel", "bridge1", "awdl", "llw", "anpi"})
        if (n.rfind(d, 0) == 0) return true;
    return false;
}

namespace {
std::string minusculas(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string textoIp(const sockaddr* a) {
    char t[INET6_ADDRSTRLEN] = {0};
    if (a->sa_family == AF_INET)
        inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in*>(a)->sin_addr, t, sizeof(t));
    else
        inet_ntop(AF_INET6, &reinterpret_cast<const sockaddr_in6*>(a)->sin6_addr, t, sizeof(t));
    return t;
}

bool deEnlace(const sockaddr* a) {
    if (a->sa_family == AF_INET) return (ntohl(reinterpret_cast<const sockaddr_in*>(a)->sin_addr.s_addr) >> 16) == 0xA9FE;
    const auto* b = reinterpret_cast<const sockaddr_in6*>(a)->sin6_addr.s6_addr;
    return b[0] == 0xFE && (b[1] & 0xC0) == 0x80;
}

bool esLoopback(const sockaddr* a) {
    if (a->sa_family == AF_INET) return (ntohl(reinterpret_cast<const sockaddr_in*>(a)->sin_addr.s_addr) >> 24) == 127;
    const auto& b = reinterpret_cast<const sockaddr_in6*>(a)->sin6_addr;
    return IN6_IS_ADDR_LOOPBACK(&b);
}
} // namespace

std::vector<DireccionLocal> direccionesLocales() {
    // Primero las de la red DE VERDAD: con puerta de enlace, Ethernet o Wi-Fi; los
    // adaptadores virtuales y las de enlace local al final.
    std::vector<DireccionLocal> out;
    const auto sumar = [&out](const std::string& interfaz, const sockaddr* a, bool virtual_, int base) {
        if (a == nullptr || (a->sa_family != AF_INET && a->sa_family != AF_INET6) || esLoopback(a)) return;
        DireccionLocal d;
        d.interfaz = interfaz;
        d.ip = textoIp(a);
        d.v6 = a->sa_family == AF_INET6;
        d.virtual_ = virtual_;
        d.enlaceLocal = deEnlace(a);
        d.puntos = base - (virtual_ ? 8 : 0) - (d.enlaceLocal ? 16 : 0);
        out.push_back(std::move(d));
    };
#ifdef _WIN32
    // GetAdaptersAddresses no necesita Winsock iniciado: con gethostname + getaddrinfo,
    // llamado antes del WSAStartup del servidor, no salía NINGUNA y el QR decía «local».
    ULONG largo = 16 * 1024;
    std::vector<unsigned char> buf;
    ULONG r = ERROR_BUFFER_OVERFLOW;
    for (int intento = 0; intento < 3 && r == ERROR_BUFFER_OVERFLOW; ++intento) {
        buf.resize(largo);
        r = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                 nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &largo);
    }
    if (r == NO_ERROR) {
        for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()); a != nullptr; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            std::wstring d = a->Description ? a->Description : L"";
            d += L" ";
            d += a->FriendlyName ? a->FriendlyName : L"";
            std::string n;
            for (wchar_t c : d) n += static_cast<char>(c < 128 ? std::towlower(c) : L'?');
            const int base = (a->FirstGatewayAddress != nullptr ? 4 : 0) +
                             ((a->IfType == IF_TYPE_ETHERNET_CSMACD || a->IfType == IF_TYPE_IEEE80211) ? 2 : 0);
            for (auto* u = a->FirstUnicastAddress; u != nullptr; u = u->Next)
                sumar(a->AdapterName ? a->AdapterName : "", u->Address.lpSockaddr, esVirtual(n), base);
        }
    }
#else
    ifaddrs* ifs = nullptr;
    if (getifaddrs(&ifs) != 0) return {};
    for (ifaddrs* i = ifs; i != nullptr; i = i->ifa_next) {
        if ((i->ifa_flags & IFF_UP) == 0 || (i->ifa_flags & IFF_LOOPBACK) != 0) continue;
        const std::string nombre = i->ifa_name ? i->ifa_name : "";
        sumar(nombre, i->ifa_addr, esVirtual(minusculas(nombre)), 0);
    }
    freeifaddrs(ifs);
#endif
    std::stable_sort(out.begin(), out.end(), [](const DireccionLocal& x, const DireccionLocal& y) { return x.puntos > y.puntos; });
    return out;
}

std::vector<std::string> localIPv4s() {
    // El primero es el que va en el QR si no se eligió otro.
    std::vector<std::string> out;
    for (const auto& d : direccionesLocales())
        if (!d.v6 && std::find(out.begin(), out.end(), d.ip) == out.end()) out.push_back(d.ip);
    return out;
}

RedesRtc redesRtc(const std::vector<DireccionLocal>& dirs, bool conVirtuales) {
    // libwebrtc junta candidatos por RED (una interfaz y un prefijo) y abre un puerto UDP
    // en cada una por sesión. Las de enlace local no las usa (no se pueden atar sin el
    // índice de la interfaz); las virtuales no le sirven a ningún invitado: se ignoran.
    // Se cuenta por interfaz y familia (las IPv6 de una interfaz suelen ser un prefijo).
    RedesRtc r;
    std::set<std::string> vistas, ignoradas;
    for (const auto& d : dirs) {
        if (d.virtual_ && !conVirtuales) {
            if (ignoradas.insert(d.interfaz).second) r.ignorar.push_back(d.interfaz);
            continue;
        }
        if (d.enlaceLocal) continue;
        if (vistas.insert(d.interfaz + (d.v6 ? "/6" : "/4")).second) ++r.redes;
    }
    return r;
}

bool parseCidr(const std::string& cidr, uint32_t& ip, int& prefix) {
    const size_t slash = cidr.find('/');
    const std::string addr = cidr.substr(0, slash);
    prefix = slash == std::string::npos ? 32 : std::atoi(cidr.c_str() + slash + 1);
    in_addr a{};
    if (inet_pton(AF_INET, addr.c_str(), &a) != 1 || prefix < 0 || prefix > 32) return false;
    ip = ntohl(a.s_addr);
    return true;
}

void splitHostPort(const std::string& s, std::string& host, uint16_t& port, uint16_t defPort) {
    const size_t c = s.rfind(':');
    if (c == std::string::npos) {
        host = s;
        port = defPort;
        return;
    }
    host = s.substr(0, c);
    const int p = std::atoi(s.c_str() + c + 1);
    port = (p > 0 && p < 65536) ? static_cast<uint16_t>(p) : defPort;
}

} // namespace ba::gateway::net
