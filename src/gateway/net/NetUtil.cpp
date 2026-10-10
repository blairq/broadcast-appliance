// ============================================================================
// src/gateway/net/NetUtil.cpp — ver NetUtil.hpp
// ============================================================================

#include "net/NetUtil.hpp"

#include <algorithm>
#include <cstdlib>

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

std::vector<std::string> localIPv4s() {
    // Primero las de la red DE VERDAD: con puerta de enlace, Ethernet o Wi-Fi; los
    // adaptadores virtuales (Hyper-V, VPN, WSL, VirtualBox, Docker) y 169.254 al final.
    // El primero es el que va en el QR si no se eligió otro.
    struct Ip {
        std::string ip;
        int puntos;
    };
    std::vector<Ip> ips;
#ifdef _WIN32
    // GetAdaptersAddresses no necesita Winsock iniciado: con gethostname + getaddrinfo,
    // llamado antes del WSAStartup del servidor, no salía NINGUNA y el QR decía «local».
    ULONG largo = 16 * 1024;
    std::vector<unsigned char> buf;
    ULONG r = ERROR_BUFFER_OVERFLOW;
    for (int intento = 0; intento < 3 && r == ERROR_BUFFER_OVERFLOW; ++intento) {
        buf.resize(largo);
        r = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                 nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &largo);
    }
    if (r == NO_ERROR) {
        for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()); a != nullptr; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            std::wstring d = a->Description ? a->Description : L"";
            d += L" ";
            d += a->FriendlyName ? a->FriendlyName : L"";
            for (auto& c : d) c = static_cast<wchar_t>(towlower(c));
            const bool virtual_ = d.find(L"virtual") != std::wstring::npos || d.find(L"hyper-v") != std::wstring::npos ||
                                  d.find(L"vmware") != std::wstring::npos || d.find(L"vpn") != std::wstring::npos ||
                                  d.find(L"wsl") != std::wstring::npos || d.find(L"tap-") != std::wstring::npos ||
                                  d.find(L"loopback") != std::wstring::npos || d.find(L"tailscale") != std::wstring::npos ||
                                  d.find(L"zerotier") != std::wstring::npos || d.find(L"docker") != std::wstring::npos;
            int base = (a->FirstGatewayAddress != nullptr ? 4 : 0) +
                       ((a->IfType == IF_TYPE_ETHERNET_CSMACD || a->IfType == IF_TYPE_IEEE80211) ? 2 : 0) - (virtual_ ? 8 : 0);
            for (auto* u = a->FirstUnicastAddress; u != nullptr; u = u->Next) {
                if (u->Address.lpSockaddr == nullptr || u->Address.lpSockaddr->sa_family != AF_INET) continue;
                char t[INET_ADDRSTRLEN] = {0};
                inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr, t, sizeof(t));
                const std::string ip = t;
                if (ip.rfind("127.", 0) == 0) continue;
                ips.push_back({ip, base - (ip.rfind("169.254.", 0) == 0 ? 16 : 0)});
            }
        }
    }
#else
    ifaddrs* ifs = nullptr;
    if (getifaddrs(&ifs) != 0) return {};
    for (ifaddrs* i = ifs; i != nullptr; i = i->ifa_next) {
        if (i->ifa_addr == nullptr || i->ifa_addr->sa_family != AF_INET) continue;
        if ((i->ifa_flags & IFF_UP) == 0 || (i->ifa_flags & IFF_LOOPBACK) != 0) continue;
        char t[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(i->ifa_addr)->sin_addr, t, sizeof(t));
        const std::string nombre = i->ifa_name ? i->ifa_name : "";
        const bool virtual_ = nombre.rfind("docker", 0) == 0 || nombre.rfind("br-", 0) == 0 || nombre.rfind("veth", 0) == 0 ||
                              nombre.rfind("virbr", 0) == 0 || nombre.rfind("tun", 0) == 0 || nombre.rfind("utun", 0) == 0 ||
                              nombre.rfind("tailscale", 0) == 0 || nombre.rfind("zt", 0) == 0 || nombre.rfind("vmnet", 0) == 0;
        const std::string ip = t;
        ips.push_back({ip, (virtual_ ? -8 : 0) - (ip.rfind("169.254.", 0) == 0 ? 16 : 0)});
    }
    freeifaddrs(ifs);
#endif
    std::stable_sort(ips.begin(), ips.end(), [](const Ip& x, const Ip& y) { return x.puntos > y.puntos; });
    std::vector<std::string> out;
    for (const auto& x : ips)
        if (std::find(out.begin(), out.end(), x.ip) == out.end()) out.push_back(x.ip);
    return out;
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
