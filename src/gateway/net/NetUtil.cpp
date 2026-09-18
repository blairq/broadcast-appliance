// ============================================================================
// src/gateway/net/NetUtil.cpp — ver NetUtil.hpp
// ============================================================================

#include "net/NetUtil.hpp"

#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#endif

namespace ba::gateway::net {

std::vector<std::string> localIPv4s() {
    std::vector<std::string> out;
#ifdef _WIN32
    // Sin iphlpapi: las direcciones del nombre del equipo alcanzan para los
    // enlaces y el certificado.
    char host[256] = {0};
    if (gethostname(host, sizeof(host)) != 0) return out;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* res = nullptr;
    if (getaddrinfo(host, nullptr, &hints, &res) != 0) return out;
    for (addrinfo* a = res; a != nullptr; a = a->ai_next) {
        char buf[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(a->ai_addr)->sin_addr, buf, sizeof(buf));
        if (std::string(buf).rfind("127.", 0) != 0) out.emplace_back(buf);
    }
    freeaddrinfo(res);
#else
    ifaddrs* ifs = nullptr;
    if (getifaddrs(&ifs) != 0) return out;
    for (ifaddrs* i = ifs; i != nullptr; i = i->ifa_next) {
        if (i->ifa_addr == nullptr || i->ifa_addr->sa_family != AF_INET) continue;
        if ((i->ifa_flags & IFF_UP) == 0 || (i->ifa_flags & IFF_LOOPBACK) != 0) continue;
        char buf[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(i->ifa_addr)->sin_addr, buf, sizeof(buf));
        out.emplace_back(buf);
    }
    freeifaddrs(ifs);
#endif
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
