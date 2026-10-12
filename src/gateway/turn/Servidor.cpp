// ============================================================================
// src/gateway/turn/Servidor.cpp — implementación de Servidor.hpp (ver el header
// y docs/funciones/TURN.md)
// ============================================================================

#include "turn/Servidor.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

#include "ajustes/Ajustes.hpp"
#include "net/WgCrypto.hpp" // randomBytes
#include "turn/Ice.hpp"     // claveDeUsuario
#include "turn/Stun.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET Fd;
#define FD_INVALIDO INVALID_SOCKET
typedef WSAPOLLFD PollFd;
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
typedef int Fd;
#define FD_INVALIDO (-1)
typedef struct pollfd PollFd;
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace ba::gateway::turn {

namespace st = stun;
using json = nlohmann::json;

namespace {

constexpr int kPermisoMs = 300 * 1000;  // RFC 8656 §9
constexpr int kCanalMs = 600 * 1000;    // RFC 8656 §12
constexpr int kVidaDefS = 600;          // RFC 8656 §3.2
constexpr int kSinAsignacionMs = 30000; // conexión TCP sin asignación: se corta
constexpr size_t kEntradaMax = 70000;   // un mensaje STUN entra entero (64 KiB + cabecera)

int64_t ahoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
int64_t ahoraUnix() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void cerrarFd(Fd f) {
    if (f == FD_INVALIDO) return;
#ifdef _WIN32
    closesocket(f);
#else
    ::close(f);
#endif
}

bool noBloqueante(Fd f) {
#ifdef _WIN32
    u_long si = 1;
    return ioctlsocket(f, FIONBIO, &si) == 0;
#else
    const int fl = fcntl(f, F_GETFL, 0);
    return fl >= 0 && fcntl(f, F_SETFL, fl | O_NONBLOCK) == 0;
#endif
}

bool seguiriaBloqueado() {
#ifdef _WIN32
    const int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}

bool ipDeTexto(const std::string& s, uint32_t& ip) {
    in_addr a{};
    if (inet_pton(AF_INET, s.c_str(), &a) != 1) return false;
    ip = ntohl(a.s_addr);
    return true;
}

std::string textoDeIp(uint32_t ip) {
    return std::to_string(ip >> 24) + "." + std::to_string((ip >> 16) & 255) + "." + std::to_string((ip >> 8) & 255) +
           "." + std::to_string(ip & 255);
}

sockaddr_in sockDe(uint32_t ip, uint16_t puerto) {
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(ip);
    sa.sin_port = htons(puerto);
    return sa;
}

void sinSigpipe(Fd f) {
#ifdef SO_NOSIGPIPE
    int si = 1;
    setsockopt(f, SOL_SOCKET, SO_NOSIGPIPE, &si, sizeof(si));
#else
    (void)f;
#endif
}

void buffersGrandes(Fd f) {
    int b = 512 * 1024;
    setsockopt(f, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&b), sizeof(b));
    setsockopt(f, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&b), sizeof(b));
}

/// Socket UDP no bloqueante enlazado a ip:puerto. FD_INVALIDO si falla.
Fd udpEnlazado(uint32_t ip, uint16_t puerto) {
    Fd f = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (f == FD_INVALIDO) return f;
    sockaddr_in sa = sockDe(ip, puerto);
    if (bind(f, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0 || !noBloqueante(f)) {
        cerrarFd(f);
        return FD_INVALIDO;
    }
#ifdef _WIN32
    // Sin esto, un ICMP «puerto inalcanzable» hace fallar el próximo recvfrom.
    BOOL no = FALSE;
    DWORD dev = 0;
    WSAIoctl(f, SIO_UDP_CONNRESET, &no, sizeof(no), nullptr, 0, &dev, nullptr, nullptr);
#endif
    buffersGrandes(f);
    return f;
}

uint16_t puertoLocal(Fd f) {
    sockaddr_in sa{};
    socklen_t l = sizeof(sa);
    if (getsockname(f, reinterpret_cast<sockaddr*>(&sa), &l) != 0) return 0;
    return ntohs(sa.sin_port);
}

uint16_t leer16(const uint8_t* p) { return uint16_t((uint16_t(p[0]) << 8) | p[1]); }

bool esUsuarioEfimero(const std::string& u, int64_t& vence) {
    const size_t dos = u.find(':');
    const std::string num = u.substr(0, dos);
    if (num.empty() || num.size() > 12) return false;
    for (char c : num)
        if (c < '0' || c > '9') return false;
    vence = std::stoll(num);
    return true;
}

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

// ---------------------------------------------------------------------------
struct Servidor::Impl {
    // Quién mandó algo: una conexión TCP o una dirección UDP.
    struct Cliente {
        bool tcp = false;
        uint64_t conId = 0;
        uint32_t ip = 0;
        uint16_t puerto = 0;
        uint64_t clave() const { return tcp ? ((1ull << 63) | conId) : ((uint64_t(ip) << 16) | puerto); }
    };
    struct Conexion {
        Fd fd = FD_INVALIDO;
        uint32_t ip = 0;
        uint16_t puerto = 0;
        std::string entrada, salida;
        size_t salidaOff = 0;
        int64_t sinAsignacionDesde = 0; // 0 = tiene asignación
        bool cerrar = false;
    };
    struct Canal {
        uint32_t ip = 0;
        uint16_t puerto = 0;
        int64_t vence = 0;
    };
    struct Asignacion {
        Cliente cli;
        Fd relay = FD_INVALIDO;
        uint16_t puertoRelay = 0;
        std::string usuario, claveMI;
        int64_t vence = 0;
        std::map<uint32_t, int64_t> permisos; // ip -> vence
        std::map<uint16_t, Canal> canales;    // canal -> peer
        std::array<uint8_t, 12> tidAllocate{};
        std::string respuestaAllocate; // para retransmisiones por UDP
    };

    Config cfg;
    int maxAsig = 64, maxCon = 128, vidaMaxS = 3600, nonceS = 600;
    size_t colaMax = 1024 * 1024;
    Fd tcp = FD_INVALIDO, udp = FD_INVALIDO;
    uint32_t ipEscucha = 0, ipRelay = 0, ipBindRelay = 0;
    std::set<uint32_t> permitidas;
    std::string secretoNonce;

    std::map<uint64_t, Conexion> conexiones;
    uint64_t proxConexion = 1;
    std::map<uint64_t, Asignacion> asignaciones; // Cliente::clave() -> asignación
    std::map<uint16_t, uint64_t> porPuertoRelay;

    std::atomic<int> nAsig{0}, nCon{0};
    std::atomic<uint64_t> bytesHaciaPeers{0}, bytesDesdePeers{0}, rechazosPeer{0}, rechazosAuth{0}, descartesCola{0};
    int64_t ultimoBarrido = 0;

    // --- envío al cliente ---------------------------------------------------
    void vaciar(Conexion& c) {
        while (c.salidaOff < c.salida.size()) {
            const auto r = ::send(c.fd, c.salida.data() + c.salidaOff, int(c.salida.size() - c.salidaOff), MSG_NOSIGNAL);
            if (r > 0) {
                c.salidaOff += size_t(r);
                continue;
            }
            if (r < 0 && seguiriaBloqueado()) break;
            c.cerrar = true;
            return;
        }
        if (c.salidaOff == c.salida.size()) {
            c.salida.clear();
            c.salidaOff = 0;
        } else if (c.salidaOff > 64 * 1024) {
            c.salida.erase(0, c.salidaOff);
            c.salidaOff = 0;
        }
    }

    void enviar(const Cliente& cli, const std::string& datos, bool medio) {
        if (cli.tcp) {
            auto it = conexiones.find(cli.conId);
            if (it == conexiones.end() || it->second.cerrar) return;
            Conexion& c = it->second;
            const size_t pendiente = c.salida.size() - c.salidaOff;
            // El medio se descarta como en UDP si el TCP no da abasto; el
            // control nunca (pero con un tope, para no crecer sin fin).
            if (medio && pendiente > colaMax) {
                descartesCola++;
                return;
            }
            if (pendiente > colaMax * 2) {
                c.cerrar = true;
                return;
            }
            c.salida += datos;
            vaciar(c);
        } else if (udp != FD_INVALIDO) {
            sockaddr_in sa = sockDe(cli.ip, cli.puerto);
            ::sendto(udp, datos.data(), int(datos.size()), 0, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
        }
    }

    // --- política -----------------------------------------------------------
    bool ipPermitida(uint32_t ip) const { return ip != 0 && (permitidas.count(ip) != 0 || ip == ipRelay); }

    bool peerPermitido(uint32_t ip, uint16_t puerto) const {
        if (ip == 0 || puerto == 0) return false;
        if (permitidas.count(ip) != 0 && puerto >= cfg.rtcMin && puerto <= cfg.rtcMax) return true;
        // El relay de otra asignación de este servidor (navegador↔navegador).
        if (porPuertoRelay.count(puerto) != 0 && (ip == ipRelay || (ipBindRelay == 0 && permitidas.count(ip) != 0)))
            return true;
        return false;
    }

    // --- nonce sin estado: hex(vence) + hex(HMAC(secretoNonce, hex(vence)))[0..16]
    std::string nonceNuevo() const {
        char b[17];
        std::snprintf(b, sizeof(b), "%016llx", static_cast<unsigned long long>(ahoraUnix() + nonceS));
        const std::string t(b);
        const auto h = st::hmacSha1(secretoNonce, reinterpret_cast<const uint8_t*>(t.data()), t.size());
        return t + hex(h.data(), 8);
    }
    bool nonceValido(const std::string& n) const {
        if (n.size() != 32) return false;
        const std::string t = n.substr(0, 16);
        const auto h = st::hmacSha1(secretoNonce, reinterpret_cast<const uint8_t*>(t.data()), t.size());
        if (hex(h.data(), 8) != n.substr(16)) return false;
        const long long vence = std::strtoll(t.c_str(), nullptr, 16);
        return vence >= ahoraUnix();
    }

    // --- respuestas -----------------------------------------------------------
    template <typename F> void responder(const Cliente& cli, const st::Mensaje& m, uint16_t clase,
                                         const std::string* clave, F&& atributos) {
        st::Constructor b(st::tipo(m.metodo(), clase), m.tid);
        atributos(b);
        if (clave != nullptr) b.integridad(*clave);
        b.huella();
        enviar(cli, b.bytes(), false);
    }
    void error(const Cliente& cli, const st::Mensaje& m, int codigo, const char* razon,
               const std::string* clave = nullptr) {
        responder(cli, m, st::kError, clave, [&](st::Constructor& b) { b.agregarError(codigo, razon); });
    }
    void desafio(const Cliente& cli, const st::Mensaje& m, int codigo, const char* razon) {
        responder(cli, m, st::kError, nullptr, [&](st::Constructor& b) {
            b.agregarError(codigo, razon);
            b.agregar(st::kRealm, cfg.realm);
            b.agregar(st::kNonce, nonceNuevo());
        });
    }

    /// Credencial larga. true con `usuario` y `clave` (para firmar la respuesta).
    bool autenticar(const Cliente& cli, const st::Mensaje& m, const Asignacion* existente, std::string& usuario,
                    std::string& clave) {
        if (m.posIntegridad == std::string::npos) {
            desafio(cli, m, 401, "Unauthorized");
            return false;
        }
        usuario = m.texto(st::kUsername);
        const std::string realm = m.texto(st::kRealm), nonce = m.texto(st::kNonce);
        if (usuario.empty() || realm.empty() || nonce.empty()) {
            rechazosAuth++;
            error(cli, m, 400, "Bad Request");
            return false;
        }
        if (!nonceValido(nonce)) {
            desafio(cli, m, 438, "Stale Nonce");
            return false;
        }
        if (realm != cfg.realm) {
            rechazosAuth++;
            desafio(cli, m, 401, "Unauthorized");
            return false;
        }
        if (existente != nullptr) {
            // Una asignación viva sigue con su usuario aunque la credencial
            // haya vencido: el vencimiento cuenta para asignaciones NUEVAS.
            if (usuario != existente->usuario) {
                rechazosAuth++;
                error(cli, m, 441, "Wrong Credentials");
                return false;
            }
            clave = existente->claveMI;
        } else {
            int64_t vence = 0;
            if (!esUsuarioEfimero(usuario, vence) || vence < ahoraUnix()) {
                rechazosAuth++;
                desafio(cli, m, 401, "Unauthorized");
                return false;
            }
            clave = st::claveLarga(usuario, cfg.realm, claveDeUsuario(cfg.secreto, usuario));
        }
        if (!st::verificarIntegridad(m, clave)) {
            rechazosAuth++;
            desafio(cli, m, 401, "Unauthorized");
            return false;
        }
        return true;
    }

    static bool atributoConocido(uint16_t t) {
        switch (t) {
        case st::kMappedAddress: case st::kUsername: case st::kMessageIntegrity: case st::kErrorCode:
        case st::kUnknownAttributes: case st::kChannelNumber: case st::kLifetime: case st::kXorPeerAddress:
        case st::kDataAttr: case st::kRealm: case st::kNonce: case st::kXorRelayedAddress:
        case st::kRequestedAddressFamily: case st::kEvenPort: case st::kRequestedTransport:
        case st::kDontFragment: case st::kXorMappedAddress: case st::kReservationToken: case st::kPriority:
        case st::kUseCandidate:
            return true;
        default:
            return false;
        }
    }

    // --- asignaciones -----------------------------------------------------------
    Asignacion* asignacionDe(const Cliente& cli) {
        auto it = asignaciones.find(cli.clave());
        return it == asignaciones.end() ? nullptr : &it->second;
    }

    void borrarAsignacion(uint64_t clave) {
        auto it = asignaciones.find(clave);
        if (it == asignaciones.end()) return;
        porPuertoRelay.erase(it->second.puertoRelay);
        cerrarFd(it->second.relay);
        if (it->second.cli.tcp) {
            auto c = conexiones.find(it->second.cli.conId);
            if (c != conexiones.end()) c->second.sinAsignacionDesde = ahoraMs();
        }
        asignaciones.erase(it);
        nAsig = int(asignaciones.size());
    }

    int vidaPedida(const st::Mensaje& m) const {
        uint32_t v = kVidaDefS;
        if (!m.u32(st::kLifetime, v)) v = kVidaDefS;
        if (v == 0) return 0;
        return int(std::min<uint32_t>(std::max<uint32_t>(v, kVidaDefS), uint32_t(vidaMaxS)));
    }

    void allocate(const Cliente& cli, const st::Mensaje& m) {
        Asignacion* ya = asignacionDe(cli);
        if (ya != nullptr && ya->tidAllocate == m.tid && !ya->respuestaAllocate.empty()) {
            enviar(cli, ya->respuestaAllocate, false); // retransmisión (UDP)
            return;
        }
        std::string usuario, clave;
        if (!autenticar(cli, m, nullptr, usuario, clave)) return;
        if (ya != nullptr) return error(cli, m, 437, "Allocation Mismatch", &clave);
        const st::Atributo* rt = m.buscar(st::kRequestedTransport);
        if (rt == nullptr || rt->valor.empty()) return error(cli, m, 400, "Bad Request", &clave);
        if (uint8_t(rt->valor[0]) != 17) return error(cli, m, 442, "Unsupported Transport Protocol", &clave);
        const st::Atributo* fam = m.buscar(st::kRequestedAddressFamily);
        if (fam != nullptr && (fam->valor.empty() || fam->valor[0] != 0x01))
            return error(cli, m, 440, "Address Family not Supported", &clave);
        if (m.buscar(st::kReservationToken) != nullptr) return error(cli, m, 508, "Insufficient Capacity", &clave);
        if (int(asignaciones.size()) >= maxAsig) return error(cli, m, 486, "Allocation Quota Reached", &clave);
        const bool par = m.buscar(st::kEvenPort) != nullptr;
        Fd r = FD_INVALIDO;
        uint16_t puerto = 0;
        for (int intento = 0; intento < (par ? 32 : 1); ++intento) {
            r = udpEnlazado(ipBindRelay, 0);
            if (r == FD_INVALIDO) break;
            puerto = puertoLocal(r);
            if (!par || (puerto % 2) == 0) break;
            cerrarFd(r);
            r = FD_INVALIDO;
        }
        if (r == FD_INVALIDO || puerto == 0) {
            cerrarFd(r);
            return error(cli, m, 508, "Insufficient Capacity", &clave);
        }
        const int vida = std::max(vidaPedida(m), kVidaDefS);
        Asignacion a;
        a.cli = cli;
        a.relay = r;
        a.puertoRelay = puerto;
        a.usuario = usuario;
        a.claveMI = clave;
        a.vence = ahoraMs() + int64_t(vida) * 1000;
        a.tidAllocate = m.tid;
        st::Constructor b(st::tipo(st::kAllocate, st::kExito), m.tid);
        b.agregarDireccionXor(st::kXorRelayedAddress, ipRelay, puerto);
        b.agregarU32(st::kLifetime, uint32_t(vida));
        b.agregarDireccionXor(st::kXorMappedAddress, cli.ip, cli.puerto);
        b.integridad(clave);
        b.huella();
        a.respuestaAllocate = b.bytes();
        asignaciones[cli.clave()] = a;
        porPuertoRelay[puerto] = cli.clave();
        nAsig = int(asignaciones.size());
        if (cli.tcp) {
            auto c = conexiones.find(cli.conId);
            if (c != conexiones.end()) c->second.sinAsignacionDesde = 0;
        }
        enviar(cli, b.bytes(), false);
    }

    void refresh(const Cliente& cli, const st::Mensaje& m) {
        Asignacion* a = asignacionDe(cli);
        std::string usuario, clave;
        if (!autenticar(cli, m, a, usuario, clave)) return;
        if (a == nullptr) return error(cli, m, 437, "Allocation Mismatch", &clave);
        const int vida = vidaPedida(m);
        if (vida == 0) {
            borrarAsignacion(cli.clave());
        } else {
            a->vence = ahoraMs() + int64_t(vida) * 1000;
        }
        responder(cli, m, st::kExito, &clave, [&](st::Constructor& b) { b.agregarU32(st::kLifetime, uint32_t(vida)); });
    }

    void createPermission(const Cliente& cli, const st::Mensaje& m) {
        Asignacion* a = asignacionDe(cli);
        std::string usuario, clave;
        if (!autenticar(cli, m, a, usuario, clave)) return;
        if (a == nullptr) return error(cli, m, 437, "Allocation Mismatch", &clave);
        std::vector<uint32_t> ips;
        for (const auto& at : m.attrs) {
            if (at.tipo != st::kXorPeerAddress) continue;
            st::Mensaje uno;
            uno.attrs.push_back(at);
            uint32_t ip = 0;
            uint16_t p = 0;
            if (!uno.direccionXor(st::kXorPeerAddress, ip, p)) return error(cli, m, 443, "Peer Address Family Mismatch", &clave);
            ips.push_back(ip);
        }
        if (ips.empty()) return error(cli, m, 400, "Bad Request", &clave);
        for (uint32_t ip : ips)
            if (!ipPermitida(ip)) {
                rechazosPeer++;
                return error(cli, m, 403, "Forbidden", &clave);
            }
        const int64_t vence = ahoraMs() + kPermisoMs;
        for (uint32_t ip : ips) a->permisos[ip] = vence;
        responder(cli, m, st::kExito, &clave, [](st::Constructor&) {});
    }

    void channelBind(const Cliente& cli, const st::Mensaje& m) {
        Asignacion* a = asignacionDe(cli);
        std::string usuario, clave;
        if (!autenticar(cli, m, a, usuario, clave)) return;
        if (a == nullptr) return error(cli, m, 437, "Allocation Mismatch", &clave);
        const st::Atributo* cn = m.buscar(st::kChannelNumber);
        uint32_t ip = 0;
        uint16_t puerto = 0;
        if (cn == nullptr || cn->valor.size() != 4 || !m.direccionXor(st::kXorPeerAddress, ip, puerto))
            return error(cli, m, 400, "Bad Request", &clave);
        const uint16_t canal = leer16(reinterpret_cast<const uint8_t*>(cn->valor.data()));
        if (canal < 0x4000 || canal > 0x7FFF) return error(cli, m, 400, "Bad Request", &clave);
        if (!peerPermitido(ip, puerto)) {
            rechazosPeer++;
            return error(cli, m, 403, "Forbidden", &clave);
        }
        // Un canal ya atado a otro peer, o el peer atado a otro canal: 400.
        auto it = a->canales.find(canal);
        if (it != a->canales.end() && (it->second.ip != ip || it->second.puerto != puerto))
            return error(cli, m, 400, "Bad Request", &clave);
        for (const auto& [n, c] : a->canales)
            if (n != canal && c.ip == ip && c.puerto == puerto) return error(cli, m, 400, "Bad Request", &clave);
        const int64_t ahora = ahoraMs();
        a->canales[canal] = Canal{ip, puerto, ahora + kCanalMs};
        a->permisos[ip] = std::max(a->permisos[ip], ahora + kPermisoMs);
        responder(cli, m, st::kExito, &clave, [](st::Constructor&) {});
    }

    void haciaPeer(Asignacion& a, uint32_t ip, uint16_t puerto, const uint8_t* datos, size_t n) {
        auto p = a.permisos.find(ip);
        if (p == a.permisos.end() || p->second < ahoraMs() || !peerPermitido(ip, puerto)) {
            rechazosPeer++;
            return;
        }
        sockaddr_in sa = sockDe(ip, puerto);
        const auto r = ::sendto(a.relay, reinterpret_cast<const char*>(datos), int(n), 0,
                                reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
        if (r > 0) bytesHaciaPeers += uint64_t(r);
    }

    void sendIndicacion(const Cliente& cli, const st::Mensaje& m) {
        Asignacion* a = asignacionDe(cli);
        if (a == nullptr) return;
        uint32_t ip = 0;
        uint16_t puerto = 0;
        const st::Atributo* d = m.buscar(st::kDataAttr);
        if (d == nullptr || !m.direccionXor(st::kXorPeerAddress, ip, puerto)) return;
        haciaPeer(*a, ip, puerto, reinterpret_cast<const uint8_t*>(d->valor.data()), d->valor.size());
    }

    void manejarStun(const Cliente& cli, const uint8_t* p, size_t n) {
        st::Mensaje m;
        if (!st::parsear(p, n, m)) return;
        if (m.posHuella != std::string::npos && !st::verificarHuella(m)) return;
        if (m.clase() == st::kIndicacion) {
            if (m.metodo() == st::kSend) sendIndicacion(cli, m);
            return;
        }
        if (m.clase() != st::kPedido) return;
        // Atributos que exigen entenderse y no se entienden: 420.
        std::string desconocidos;
        for (const auto& a : m.attrs)
            if (a.tipo < 0x8000 && !atributoConocido(a.tipo)) {
                desconocidos.push_back(char(a.tipo >> 8));
                desconocidos.push_back(char(a.tipo & 0xFF));
            }
        if (!desconocidos.empty()) {
            responder(cli, m, st::kError, nullptr, [&](st::Constructor& b) {
                b.agregarError(420, "Unknown Attribute");
                b.agregar(st::kUnknownAttributes, desconocidos);
            });
            return;
        }
        switch (m.metodo()) {
        case st::kBinding:
            responder(cli, m, st::kExito, nullptr,
                      [&](st::Constructor& b) { b.agregarDireccionXor(st::kXorMappedAddress, cli.ip, cli.puerto); });
            break;
        case st::kAllocate: allocate(cli, m); break;
        case st::kRefresh: refresh(cli, m); break;
        case st::kCreatePermission: createPermission(cli, m); break;
        case st::kChannelBind: channelBind(cli, m); break;
        default: error(cli, m, 400, "Bad Request"); break;
        }
    }

    void manejarChannelData(const Cliente& cli, const uint8_t* p, size_t n) {
        if (n < 4) return;
        const uint16_t canal = leer16(p), largo = leer16(p + 2);
        if (size_t(largo) + 4 > n) return;
        Asignacion* a = asignacionDe(cli);
        if (a == nullptr) return;
        auto it = a->canales.find(canal);
        if (it == a->canales.end() || it->second.vence < ahoraMs()) return;
        haciaPeer(*a, it->second.ip, it->second.puerto, p + 4, largo);
    }

    // --- E/S ------------------------------------------------------------------------
    void aceptar() {
        for (;;) {
            sockaddr_in sa{};
            socklen_t l = sizeof(sa);
            Fd f = ::accept(tcp, reinterpret_cast<sockaddr*>(&sa), &l);
            if (f == FD_INVALIDO) return;
            if (int(conexiones.size()) >= maxCon || !noBloqueante(f)) {
                cerrarFd(f);
                continue;
            }
            int si = 1;
            setsockopt(f, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&si), sizeof(si));
            sinSigpipe(f);
            buffersGrandes(f);
            Conexion c;
            c.fd = f;
            c.ip = ntohl(sa.sin_addr.s_addr);
            c.puerto = ntohs(sa.sin_port);
            c.sinAsignacionDesde = ahoraMs();
            conexiones[proxConexion++] = std::move(c);
            nCon = int(conexiones.size());
        }
    }

    void leerConexion(uint64_t id, Conexion& c) {
        uint8_t buf[16384];
        for (;;) {
            const auto r = ::recv(c.fd, reinterpret_cast<char*>(buf), int(sizeof(buf)), 0);
            if (r > 0) {
                c.entrada.append(reinterpret_cast<const char*>(buf), size_t(r));
                if (c.entrada.size() > kEntradaMax * 4) break; // que el parseo alcance
                continue;
            }
            if (r < 0 && seguiriaBloqueado()) break;
            c.cerrar = true; // EOF o error
            break;
        }
        Cliente cli;
        cli.tcp = true;
        cli.conId = id;
        cli.ip = c.ip;
        cli.puerto = c.puerto;
        size_t i = 0;
        const auto* p = reinterpret_cast<const uint8_t*>(c.entrada.data());
        const size_t n = c.entrada.size();
        while (n - i >= 4 && !c.cerrar) {
            const uint8_t b0 = p[i];
            size_t total = 0;
            if ((b0 & 0xC0) == 0) {
                if (n - i < st::kCabecera) break;
                if (!st::pareceStun(p + i, n - i)) {
                    c.cerrar = true;
                    break;
                }
                total = st::largoStun(p + i, n - i);
                if (n - i < total) break;
                manejarStun(cli, p + i, total);
            } else if ((b0 & 0xC0) == 0x40) {
                const size_t largo = leer16(p + i + 2);
                total = (4 + largo + 3) & ~size_t(3); // con relleno, sobre TCP
                if (n - i < total) break;
                manejarChannelData(cli, p + i, 4 + largo);
            } else {
                c.cerrar = true; // basura: no es STUN ni ChannelData
                break;
            }
            i += total;
        }
        // Ojo: manejarStun pudo tocar `conexiones` (no inserta ni borra), c sigue válida.
        if (i > 0) c.entrada.erase(0, i);
        if (c.entrada.size() > kEntradaMax) c.cerrar = true;
    }

    void leerUdp() {
        uint8_t buf[65536];
        for (int k = 0; k < 256; ++k) {
            sockaddr_in sa{};
            socklen_t l = sizeof(sa);
            const auto r = ::recvfrom(udp, reinterpret_cast<char*>(buf), int(sizeof(buf)), 0,
                                      reinterpret_cast<sockaddr*>(&sa), &l);
            if (r < 0) {
#ifdef _WIN32
                if (WSAGetLastError() == WSAECONNRESET) continue;
#endif
                return;
            }
            Cliente cli;
            cli.ip = ntohl(sa.sin_addr.s_addr);
            cli.puerto = ntohs(sa.sin_port);
            const size_t n = size_t(r);
            if (n >= 4 && (buf[0] & 0xC0) == 0x40)
                manejarChannelData(cli, buf, n);
            else if (st::pareceStun(buf, n) && st::largoStun(buf, n) == n)
                manejarStun(cli, buf, n);
        }
    }

    void leerRelay(Asignacion& a) {
        uint8_t buf[65536];
        for (int k = 0; k < 256; ++k) {
            sockaddr_in sa{};
            socklen_t l = sizeof(sa);
            const auto r = ::recvfrom(a.relay, reinterpret_cast<char*>(buf), int(sizeof(buf)), 0,
                                      reinterpret_cast<sockaddr*>(&sa), &l);
            if (r < 0) {
#ifdef _WIN32
                if (WSAGetLastError() == WSAECONNRESET) continue;
#endif
                return;
            }
            const uint32_t ip = ntohl(sa.sin_addr.s_addr);
            const uint16_t puerto = ntohs(sa.sin_port);
            auto p = a.permisos.find(ip);
            if (p == a.permisos.end() || p->second < ahoraMs() || !peerPermitido(ip, puerto)) {
                rechazosPeer++;
                continue;
            }
            bytesDesdePeers += uint64_t(r);
            uint16_t canal = 0;
            for (const auto& [num, c] : a.canales)
                if (c.ip == ip && c.puerto == puerto && c.vence >= ahoraMs()) {
                    canal = num;
                    break;
                }
            if (canal != 0) {
                enviar(a.cli, st::channelData(canal, buf, size_t(r), a.cli.tcp), true);
            } else {
                std::array<uint8_t, 12> tid{};
                net::randomBytes(tid.data(), tid.size());
                st::Constructor b(st::tipo(st::kData, st::kIndicacion), tid);
                b.agregarDireccionXor(st::kXorPeerAddress, ip, puerto);
                b.agregar(st::kDataAttr, buf, size_t(r));
                enviar(a.cli, b.bytes(), true);
            }
        }
    }

    void barrer() {
        const int64_t ahora = ahoraMs();
        std::vector<uint64_t> vencidas;
        for (auto& [k, a] : asignaciones) {
            if (a.vence < ahora) {
                vencidas.push_back(k);
                continue;
            }
            for (auto it = a.permisos.begin(); it != a.permisos.end();)
                it = it->second < ahora ? a.permisos.erase(it) : std::next(it);
            for (auto it = a.canales.begin(); it != a.canales.end();)
                it = it->second.vence < ahora ? a.canales.erase(it) : std::next(it);
        }
        for (uint64_t k : vencidas) borrarAsignacion(k);
        for (auto& [id, c] : conexiones)
            if (c.sinAsignacionDesde != 0 && ahora - c.sinAsignacionDesde > kSinAsignacionMs) c.cerrar = true;
    }

    void cerrarConexiones() {
        for (auto it = conexiones.begin(); it != conexiones.end();) {
            if (!it->second.cerrar) {
                ++it;
                continue;
            }
            Cliente cli;
            cli.tcp = true;
            cli.conId = it->first;
            cerrarFd(it->second.fd);
            const uint64_t clave = cli.clave();
            it = conexiones.erase(it);
            borrarAsignacion(clave); // la asignación muere con su conexión
        }
        nCon = int(conexiones.size());
    }

    void unaVuelta() {
        std::vector<PollFd> fds;
        std::vector<std::pair<int, uint64_t>> que; // 0 tcp, 1 udp, 2 conexión, 3 relay
        auto sumar = [&](Fd f, short ev, int tipo, uint64_t id) {
            PollFd p{};
            p.fd = f;
            p.events = ev;
            fds.push_back(p);
            que.emplace_back(tipo, id);
        };
        if (tcp != FD_INVALIDO) sumar(tcp, POLLIN, 0, 0);
        if (udp != FD_INVALIDO) sumar(udp, POLLIN, 1, 0);
        for (auto& [id, c] : conexiones)
            sumar(c.fd, short(POLLIN | (c.salida.size() > c.salidaOff ? POLLOUT : 0)), 2, id);
        for (auto& [k, a] : asignaciones) sumar(a.relay, POLLIN, 3, k);
#ifdef _WIN32
        const int r = WSAPoll(fds.data(), ULONG(fds.size()), 100);
#else
        const int r = ::poll(fds.data(), nfds_t(fds.size()), 100);
#endif
        if (r > 0) {
            for (size_t i = 0; i < fds.size(); ++i) {
                const short re = fds[i].revents;
                if (re == 0) continue;
                const auto [tipo, id] = que[i];
                if (tipo == 0) {
                    aceptar();
                } else if (tipo == 1) {
                    leerUdp();
                } else if (tipo == 2) {
                    auto it = conexiones.find(id);
                    if (it == conexiones.end()) continue;
                    if (re & POLLOUT) vaciar(it->second);
                    if (re & (POLLIN | POLLHUP | POLLERR)) leerConexion(id, it->second);
                } else {
                    auto it = asignaciones.find(id);
                    if (it != asignaciones.end()) leerRelay(it->second);
                }
            }
        }
        cerrarConexiones();
        const int64_t ahora = ahoraMs();
        if (ahora - ultimoBarrido >= 1000) {
            ultimoBarrido = ahora;
            barrer();
            cerrarConexiones();
        }
    }

    void liberar() {
        for (auto& [k, a] : asignaciones) cerrarFd(a.relay);
        asignaciones.clear();
        porPuertoRelay.clear();
        for (auto& [id, c] : conexiones) cerrarFd(c.fd);
        conexiones.clear();
        cerrarFd(tcp);
        cerrarFd(udp);
        tcp = udp = FD_INVALIDO;
        nAsig = 0;
        nCon = 0;
    }
};

// ---------------------------------------------------------------------------
Servidor::Servidor(Config cfg) : cfg_(std::move(cfg)) {}

Servidor::~Servidor() { stop(); }

bool Servidor::start(std::string* err) {
    if (corriendo_.load()) return true;
    auto falla = [&](const std::string& e) {
        if (err != nullptr) *err = e;
        if (impl_) impl_->liberar();
        impl_.reset();
        return false;
    };
    if (cfg_.secreto.size() < 16) return falla("turn: falta el secreto (16 bytes o más)");
#ifdef _WIN32
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    impl_ = std::make_unique<Impl>();
    Impl& s = *impl_;
    s.cfg = cfg_;
    s.maxAsig = cfg_.maxAsignaciones > 0 ? cfg_.maxAsignaciones : ba::ajustes::entero("turn.maxAsignaciones", 64);
    s.maxCon = cfg_.maxConexiones > 0 ? cfg_.maxConexiones : ba::ajustes::entero("turn.maxConexiones", 128);
    s.vidaMaxS = cfg_.vidaMaxS > 0 ? cfg_.vidaMaxS : ba::ajustes::entero("turn.vidaMaxS", 3600);
    s.vidaMaxS = std::max(s.vidaMaxS, kVidaDefS);
    s.nonceS = cfg_.nonceS > 0 ? cfg_.nonceS : ba::ajustes::entero("turn.nonceS", 600);
    s.colaMax = size_t(std::max(64, ba::ajustes::entero("turn.colaTcpKB", 1024))) * 1024;

    if (!ipDeTexto(cfg_.direccion, s.ipEscucha)) return falla("turn: dirección inválida: " + cfg_.direccion);
    std::string relay = cfg_.direccionRelay;
    if (relay.empty()) relay = s.ipEscucha == 0 ? "127.0.0.1" : cfg_.direccion;
    if (!ipDeTexto(relay, s.ipRelay) || s.ipRelay == 0) return falla("turn: dirección de relay inválida: " + relay);
    const std::string dirBind = cfg_.bindRelay.empty() ? relay : cfg_.bindRelay;
    if (!ipDeTexto(dirBind, s.ipBindRelay)) return falla("turn: bindRelay inválido: " + dirBind);
    for (const auto& p : cfg_.peersPermitidos) {
        uint32_t ip = 0;
        if (ipDeTexto(p, ip) && ip != 0) s.permitidas.insert(ip);
    }
    if (s.permitidas.empty()) return falla("turn: sin peers permitidos");
    if (cfg_.rtcMin == 0 || cfg_.rtcMax < cfg_.rtcMin) return falla("turn: rango de puertos RTC inválido");
    std::array<uint8_t, 32> sn{};
    net::randomBytes(sn.data(), sn.size());
    s.secretoNonce.assign(reinterpret_cast<const char*>(sn.data()), sn.size());

    // TCP
    s.tcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s.tcp == FD_INVALIDO) return falla("turn: socket TCP");
    {
        int si = 1;
        setsockopt(s.tcp, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&si), sizeof(si));
        sockaddr_in sa = sockDe(s.ipEscucha, cfg_.puertoTcp);
        if (bind(s.tcp, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0 || listen(s.tcp, 64) != 0 ||
            !noBloqueante(s.tcp))
            return falla("turn: no se pudo escuchar en " + cfg_.direccion + ":" + std::to_string(cfg_.puertoTcp) +
                         " (TCP)");
    }
    puertoTcpReal_ = puertoLocal(s.tcp);
    // UDP (opcional)
    if (cfg_.puertoUdp != 0 || cfg_.conUdp) {
        s.udp = udpEnlazado(s.ipEscucha, cfg_.puertoUdp);
        if (s.udp == FD_INVALIDO)
            return falla("turn: no se pudo escuchar en " + cfg_.direccion + ":" + std::to_string(cfg_.puertoUdp) +
                         " (UDP)");
        puertoUdpReal_ = puertoLocal(s.udp);
    } else {
        puertoUdpReal_ = 0;
    }
    corriendo_ = true;
    hilo_ = std::thread([this] { bucle(); });
    return true;
}

void Servidor::bucle() {
    while (corriendo_.load()) impl_->unaVuelta();
}

void Servidor::stop() {
    if (!corriendo_.exchange(false)) return;
    if (hilo_.joinable()) hilo_.join();
    if (impl_) impl_->liberar();
#ifdef _WIN32
    WSACleanup();
#endif
}

json Servidor::estado() const {
    json j;
    j["activo"] = corriendo_.load();
    j["puertoTcp"] = puertoTcpReal_.load();
    j["puertoUdp"] = puertoUdpReal_.load();
    j["direccion"] = cfg_.direccion;
    j["realm"] = cfg_.realm;
    j["rtc"] = {cfg_.rtcMin, cfg_.rtcMax};
    j["peersPermitidos"] = cfg_.peersPermitidos;
    if (impl_) {
        j["direccionRelay"] = textoDeIp(impl_->ipRelay);
        j["asignaciones"] = impl_->nAsig.load();
        j["maxAsignaciones"] = impl_->maxAsig;
        j["conexiones"] = impl_->nCon.load();
        j["bytesHaciaPeers"] = impl_->bytesHaciaPeers.load();
        j["bytesDesdePeers"] = impl_->bytesDesdePeers.load();
        j["rechazosPeer"] = impl_->rechazosPeer.load();
        j["rechazosAuth"] = impl_->rechazosAuth.load();
        j["descartesCola"] = impl_->descartesCola.load();
    }
    return j;
}

} // namespace ba::gateway::turn
