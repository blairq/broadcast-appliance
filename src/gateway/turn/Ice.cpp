// ============================================================================
// src/gateway/turn/Ice.cpp — implementación de Ice.hpp (ver el header)
// ============================================================================

#include "turn/Ice.hpp"

#include <array>
#include <chrono>

#include "HttpIo.hpp"       // base64
#include "ajustes/Ajustes.hpp"
#include "net/WgCrypto.hpp" // randomBytes
#include "turn/Stun.hpp"    // hmacSha1

namespace ba::gateway::turn {

using json = nlohmann::json;

namespace {
std::string hex(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        s.push_back(d[p[i] >> 4]);
        s.push_back(d[p[i] & 15]);
    }
    return s;
}
} // namespace

std::string claveDeUsuario(const std::string& secreto, const std::string& usuario) {
    const auto h = stun::hmacSha1(secreto, reinterpret_cast<const uint8_t*>(usuario.data()), usuario.size());
    return http::base64(h.data(), h.size());
}

Credencial credencialEfimera(const std::string& secreto, int ttlS, const std::string& id, int64_t ahoraUnix) {
    if (ahoraUnix < 0)
        ahoraUnix = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    std::string sufijo = id;
    if (sufijo.empty()) {
        std::array<uint8_t, 6> r{};
        net::randomBytes(r.data(), r.size());
        sufijo = hex(r.data(), r.size());
    }
    Credencial c;
    c.vence = ahoraUnix + (ttlS > 0 ? ttlS : 0);
    c.usuario = std::to_string(c.vence) + ":" + sufijo;
    c.clave = claveDeUsuario(secreto, c.usuario);
    return c;
}

std::string secretoAleatorio() {
    std::array<uint8_t, 32> r{};
    net::randomBytes(r.data(), r.size());
    return hex(r.data(), r.size());
}

namespace {
/// Una sola entrada con credencial efímera para todas las URL (o [] si no hay).
json conCredencial(const Servidor::Config& cfg, const json& urls, int ttlS) {
    json arr = json::array();
    if (urls.empty() || cfg.secreto.empty()) return arr;
    const Credencial c = credencialEfimera(cfg.secreto, ttlS);
    arr.push_back({{"urls", urls}, {"username", c.usuario}, {"credential", c.clave}});
    return arr;
}
} // namespace

json iceServersPara(const Servidor::Config& cfg, const std::string& hostPublico, uint16_t puertoTls,
                    uint16_t puertoTcp, const std::string& udpPublico, int ttlS) {
    json urls = json::array();
    if (!hostPublico.empty() && puertoTls != 0)
        urls.push_back("turns:" + hostPublico + ":" + std::to_string(puertoTls) + "?transport=tcp");
    if (!hostPublico.empty() && puertoTcp != 0)
        urls.push_back("turn:" + hostPublico + ":" + std::to_string(puertoTcp) + "?transport=tcp");
    if (!udpPublico.empty()) urls.push_back("turn:" + udpPublico + "?transport=udp");
    return conCredencial(cfg, urls, ttlS);
}

std::string respuestaIce(const Servidor::Config& cfg, bool turnActivo, const ExtremosIce& ext, bool externa,
                         int ttlS) {
    if (ttlS < 0) ttlS = ba::ajustes::entero("turn.credencialS", 3600);
    json servidores = json::array();
    std::string politica = "all";
    if (turnActivo && externa) {
        json urls = json::array();
        if (!ext.hostPublico.empty() && ext.puertoTls != 0)
            urls.push_back("turns:" + ext.hostPublico + ":" + std::to_string(ext.puertoTls) + "?transport=tcp");
        // pinggy le da host:puerto propio a cada forwarding TCP: si viene, gana.
        if (!ext.tcpPublico.empty())
            urls.push_back("turn:" + ext.tcpPublico + "?transport=tcp");
        else if (!ext.hostPublico.empty() && ext.puertoTcp != 0)
            urls.push_back("turn:" + ext.hostPublico + ":" + std::to_string(ext.puertoTcp) + "?transport=tcp");
        if (!ext.udpPublico.empty()) urls.push_back("turn:" + ext.udpPublico + "?transport=udp");
        servidores = conCredencial(cfg, urls, ttlS);
        if (!servidores.empty()) politica = "relay";
    } else if (turnActivo && !ext.hostLan.empty()) {
        servidores = iceServersPara(cfg, ext.hostLan, 0, ext.puertoLanTcp,
                                    ext.puertoLanUdp ? ext.hostLan + ":" + std::to_string(ext.puertoLanUdp) : "",
                                    ttlS);
    }
    json r;
    r["iceServers"] = servidores;
    r["politica"] = politica;
    r["ttl"] = servidores.empty() ? 0 : ttlS;
    return r.dump();
}

} // namespace ba::gateway::turn
