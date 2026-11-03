// ============================================================================
// src/gateway/turn/Servidor.cpp — implementación de Servidor.hpp (ver el header
// y docs/funciones/TURN.md)
//
// Cada fragmento es lo que antes era el servidor entero (conexiones, asignaciones, la
// política), pero en el hilo de su Bucle y sin poll() propio. Lo común (configuración,
// topes, escuchas, contadores) está en Comun.
// ============================================================================

#include "turn/Servidor.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>

#include "ajustes/Ajustes.hpp"
#include "diag/Frontera.hpp"
#include "i18n/I18n.hpp"
#include "net/WgCrypto.hpp" // randomBytes
#include "rtc/Conmutador.hpp"
#include "turn/BuclePoll.hpp"
#include "turn/Ice.hpp" // claveDeUsuario
#include "turn/Stun.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#define FD_INVALIDO static_cast<Fd>(INVALID_SOCKET)
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#define FD_INVALIDO (-1)
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace ba::gateway::turn {

namespace st = stun;
using ba::i18n::llenar;
using ba::i18n::traducir;
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
    Fd f = static_cast<Fd>(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
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
// Lo de todos los fragmentos.
struct Servidor::Comun {
    Config cfg;
    int maxAsig = 64, maxCon = 128, vidaMaxS = 3600, nonceS = 600;
    size_t colaMax = 1024 * 1024;
    Fd tcp = FD_INVALIDO, udp = FD_INVALIDO;
    uint32_t ipEscucha = 0, ipRelay = 0, ipBindRelay = 0;
    std::set<uint32_t> permitidas;
    std::string secretoNonce;
    memoria::Conmutador* memoria = nullptr;
    std::vector<Bucle*> bucles;
    std::vector<std::unique_ptr<Fragmento>> frags;
    std::atomic<bool> corriendo{false};

    // Los puertos de relay vivos de TODOS los fragmentos (navegador↔navegador entre
    // fragmentos): un bit por puerto, sin candado.
    std::array<std::atomic<uint64_t>, 1024> puertosRelay{};
    void anotarRelay(uint16_t p, bool si) {
        auto& w = puertosRelay[p >> 6];
        const uint64_t b = uint64_t(1) << (p & 63);
        if (si)
            w.fetch_or(b, std::memory_order_acq_rel);
        else
            w.fetch_and(~b, std::memory_order_acq_rel);
    }
    bool esRelay(uint16_t p) const { return (puertosRelay[p >> 6].load(std::memory_order_acquire) >> (p & 63)) & 1; }

    // Las fronteras del TURN (PLAN-GATEWAY-FRAGMENTOS.md #1, #2, #3 y #14; diag/Frontera.hpp).
    ba::diag::Frontera& fEntraUdp = ba::diag::frontera("turn.entra.udp", 1, "pinggy UDP → TURN (hilo de red del fragmento 0)");
    ba::diag::Frontera& fEntraTcp = ba::diag::frontera("turn.entra.tcp", 2, "pinggy TCP → TURN (hilo de red de su fragmento)");
    ba::diag::Frontera& fHaciaRtc =
        ba::diag::frontera("turn.hacia_webrtc", 3, "TURN → red de WebRTC (en memoria; sendto si no es un socket nuestro)");
    ba::diag::Frontera& fDesdeRtc = ba::diag::frontera("turn.desde_webrtc", 14, "red de WebRTC → TURN → cliente");
    ba::diag::Frontera& fRepartoUdp =
        ba::diag::frontera("turn.reparto.udp", 1, "UDP de pinggy → otro fragmento (una tanda por fragmento)");
    ba::diag::Frontera& fEntregaTcp =
        ba::diag::frontera("turn.entrega.tcp", 2, "conexión TCP nueva → el hilo de su fragmento (una vez)");

    std::atomic<int> nAsig{0}, nCon{0};
    std::atomic<uint64_t> bytesHaciaPeers{0}, bytesDesdePeers{0}, rechazosPeer{0}, rechazosAuth{0}, descartesCola{0};
    std::atomic<uint64_t> enMemoriaHacia{0}, enMemoriaDesde{0};

    bool ipPermitida(uint32_t ip) const { return ip != 0 && (permitidas.count(ip) != 0 || ip == ipRelay); }

    bool peerPermitido(uint32_t ip, uint16_t puerto) const {
        if (ip == 0 || puerto == 0) return false;
        if (permitidas.count(ip) != 0 && puerto >= cfg.rtcMin && puerto <= cfg.rtcMax) return true;
        // El relay de otra asignación de este servidor (navegador↔navegador), de cualquier fragmento.
        if (esRelay(puerto) && (ip == ipRelay || (ipBindRelay == 0 && permitidas.count(ip) != 0))) return true;
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

    /// El fragmento menos cargado (para una conexión o un origen UDP nuevos).
    int menosCargado() const;
};

// ---------------------------------------------------------------------------
// Un fragmento: sus conexiones y asignaciones, en el hilo de su bucle.
struct Servidor::Fragmento {
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
        bool vigilaEscritura = false;
    };
    struct Canal {
        uint32_t ip = 0;
        uint16_t puerto = 0;
        int64_t vence = 0;
        // El socket de WebRTC del otro lado (en memoria); `gen` dice hasta cuándo vale un «no hay».
        memoria::PuntoPtr punto;
        uint64_t gen = 0;
    };
    struct ReceptorRelay final : memoria::Receptor {
        Fragmento* f = nullptr;
        uint64_t clave = 0;
        void recibirEnMemoria(const uint8_t* d, size_t n, uint32_t ip, uint16_t puerto, memoria::Via) override {
            f->desdeMemoria(clave, d, n, ip, puerto);
        }
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
        memoria::PuntoPtr punto;       // el relay anotado en el conmutador
        std::unique_ptr<ReceptorRelay> receptor;
    };

    Fragmento(Comun& c, int i, Bucle* b) : c(c), indice(i), bucle(b) {}

    Comun& c;
    const int indice;
    Bucle* const bucle;

    std::map<uint64_t, Conexion> conexiones;
    uint64_t proxConexion = 1;
    std::map<uint64_t, Asignacion> asignaciones; // Cliente::clave() -> asignación
    std::vector<uint64_t> porCerrar;
    int profundidad = 0; // > 0: adentro de un manejador de este fragmento
    std::atomic<int> nAsigF{0}, nConF{0}, nRutasF{0};

    // Solo el fragmento 0: a qué fragmento va cada origen UDP (clave → fragmento, último uso).
    struct Ruta {
        int frag = 0;
        int64_t visto = 0;
    };
    std::unordered_map<uint64_t, Ruta> rutasUdp;
    struct Datagrama {
        uint32_t ip = 0;
        uint16_t puerto = 0;
        size_t off = 0, n = 0;
    };
    struct Lote {
        std::string bytes;
        std::vector<Datagrama> d;
    };

    // --- el marco de cada manejador ---------------------------------------------------
    /// Corre `f` como manejador del fragmento: lo que se entregue en memoria en el mismo hilo
    /// sale al terminar (sin anidar WebRTC adentro del TURN) y después se cierra lo marcado.
    template <typename F> void manejar(F&& f) {
        ++profundidad;
        {
            std::optional<memoria::Conmutador::Ambito> a;
            if (c.memoria != nullptr) a.emplace(*c.memoria);
            f();
        }
        --profundidad;
        if (profundidad == 0) cerrarPendientes();
    }

    void marcarCierre(uint64_t id, Conexion& con) {
        if (con.cerrar) return;
        con.cerrar = true;
        porCerrar.push_back(id);
    }

    // --- envío al cliente ---------------------------------------------------
    void vigilarEscritura(Conexion& con, bool si) {
        if (con.vigilaEscritura == si || con.fd == FD_INVALIDO) return;
        con.vigilaEscritura = si;
        bucle->cambiar(con.fd, Bucle::kLeer | (si ? Bucle::kEscribir : 0));
    }

    void vaciar(uint64_t id, Conexion& con) {
        while (con.salidaOff < con.salida.size()) {
            const auto r =
                ::send(con.fd, con.salida.data() + con.salidaOff, int(con.salida.size() - con.salidaOff), MSG_NOSIGNAL);
            if (r > 0) {
                con.salidaOff += size_t(r);
                continue;
            }
            if (r < 0 && seguiriaBloqueado()) break;
            marcarCierre(id, con);
            return;
        }
        if (con.salidaOff == con.salida.size()) {
            con.salida.clear();
            con.salidaOff = 0;
        } else if (con.salidaOff > 64 * 1024) {
            con.salida.erase(0, con.salidaOff);
            con.salidaOff = 0;
        }
        vigilarEscritura(con, con.salidaOff < con.salida.size());
    }

    void enviar(const Cliente& cli, const std::string& datos, bool medio) {
        if (cli.tcp) {
            auto it = conexiones.find(cli.conId);
            if (it == conexiones.end() || it->second.cerrar) return;
            Conexion& con = it->second;
            const size_t pendiente = con.salida.size() - con.salidaOff;
            // El medio se descarta como en UDP si el TCP no da abasto; el
            // control nunca (pero con un tope, para no crecer sin fin).
            if (medio && pendiente > c.colaMax) {
                c.descartesCola++;
                return;
            }
            if (pendiente > c.colaMax * 2) {
                marcarCierre(it->first, con);
                return;
            }
            con.salida += datos;
            vaciar(it->first, con);
        } else if (c.udp != FD_INVALIDO) {
            // El socket UDP es uno solo (el de la escucha): sendto se puede llamar desde el
            // hilo de cualquier fragmento (un datagrama es atómico).
            sockaddr_in sa = sockDe(cli.ip, cli.puerto);
            ::sendto(c.udp, datos.data(), int(datos.size()), 0, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
        }
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
            b.agregar(st::kRealm, c.cfg.realm);
            b.agregar(st::kNonce, c.nonceNuevo());
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
            c.rechazosAuth++;
            error(cli, m, 400, "Bad Request");
            return false;
        }
        if (!c.nonceValido(nonce)) {
            desafio(cli, m, 438, "Stale Nonce");
            return false;
        }
        if (realm != c.cfg.realm) {
            c.rechazosAuth++;
            desafio(cli, m, 401, "Unauthorized");
            return false;
        }
        if (existente != nullptr) {
            // Una asignación viva sigue con su usuario aunque la credencial
            // haya vencido: el vencimiento cuenta para asignaciones NUEVAS.
            if (usuario != existente->usuario) {
                c.rechazosAuth++;
                error(cli, m, 441, "Wrong Credentials");
                return false;
            }
            clave = existente->claveMI;
        } else {
            int64_t vence = 0;
            if (!esUsuarioEfimero(usuario, vence) || vence < ahoraUnix()) {
                c.rechazosAuth++;
                desafio(cli, m, 401, "Unauthorized");
                return false;
            }
            clave = st::claveLarga(usuario, c.cfg.realm, claveDeUsuario(c.cfg.secreto, usuario));
        }
        if (!st::verificarIntegridad(m, clave)) {
            c.rechazosAuth++;
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
        Asignacion& a = it->second;
        if (c.memoria != nullptr && a.punto) c.memoria->cerrar(a.punto);
        c.anotarRelay(a.puertoRelay, false);
        if (a.relay != FD_INVALIDO) {
            bucle->dejar(a.relay);
            cerrarFd(a.relay);
        }
        if (a.cli.tcp) {
            auto con = conexiones.find(a.cli.conId);
            if (con != conexiones.end()) con->second.sinAsignacionDesde = ahoraMs();
        }
        asignaciones.erase(it);
        nAsigF = int(asignaciones.size());
        c.nAsig.fetch_sub(1, std::memory_order_acq_rel);
    }

    int vidaPedida(const st::Mensaje& m) const {
        uint32_t v = kVidaDefS;
        if (!m.u32(st::kLifetime, v)) v = kVidaDefS;
        if (v == 0) return 0;
        return int(std::min<uint32_t>(std::max<uint32_t>(v, kVidaDefS), uint32_t(c.vidaMaxS)));
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
        // El tope es de todo el servidor (todos los fragmentos).
        if (c.nAsig.fetch_add(1, std::memory_order_acq_rel) >= c.maxAsig) {
            c.nAsig.fetch_sub(1, std::memory_order_acq_rel);
            return error(cli, m, 486, "Allocation Quota Reached", &clave);
        }
        const bool par = m.buscar(st::kEvenPort) != nullptr;
        Fd r = FD_INVALIDO;
        uint16_t puerto = 0;
        for (int intento = 0; intento < (par ? 32 : 1); ++intento) {
            r = udpEnlazado(c.ipBindRelay, 0);
            if (r == FD_INVALIDO) break;
            puerto = puertoLocal(r);
            if (!par || (puerto % 2) == 0) break;
            cerrarFd(r);
            r = FD_INVALIDO;
        }
        if (r == FD_INVALIDO || puerto == 0) {
            cerrarFd(r);
            c.nAsig.fetch_sub(1, std::memory_order_acq_rel);
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
        b.agregarDireccionXor(st::kXorRelayedAddress, c.ipRelay, puerto);
        b.agregarU32(st::kLifetime, uint32_t(vida));
        b.agregarDireccionXor(st::kXorMappedAddress, cli.ip, cli.puerto);
        b.integridad(clave);
        b.huella();
        a.respuestaAllocate = b.bytes();
        const uint64_t k = cli.clave();
        Asignacion& viva = asignaciones.emplace(k, std::move(a)).first->second;
        // Lo que llegue al relay por el kernel (algo que no es un socket nuestro).
        bucle->vigilar(r, Bucle::kLeer, [this, k](int) {
            manejar([&] {
                auto it = asignaciones.find(k);
                if (it != asignaciones.end()) leerRelay(it->second);
            });
        });
        // Lo que le mande la red de WebRTC, en memoria.
        if (c.memoria != nullptr) {
            viva.receptor = std::make_unique<ReceptorRelay>();
            viva.receptor->f = this;
            viva.receptor->clave = k;
            viva.punto = c.memoria->abrir(c.ipRelay, puerto, indice, memoria::Clase::RelayTurn, viva.receptor.get());
        }
        c.anotarRelay(puerto, true);
        nAsigF = int(asignaciones.size());
        if (cli.tcp) {
            auto con = conexiones.find(cli.conId);
            if (con != conexiones.end()) con->second.sinAsignacionDesde = 0;
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
            if (!c.ipPermitida(ip)) {
                c.rechazosPeer++;
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
        if (!c.peerPermitido(ip, puerto)) {
            c.rechazosPeer++;
            return error(cli, m, 403, "Forbidden", &clave);
        }
        // Un canal ya atado a otro peer, o el peer atado a otro canal: 400.
        auto it = a->canales.find(canal);
        if (it != a->canales.end() && (it->second.ip != ip || it->second.puerto != puerto))
            return error(cli, m, 400, "Bad Request", &clave);
        for (const auto& [n, cn2] : a->canales)
            if (n != canal && cn2.ip == ip && cn2.puerto == puerto) return error(cli, m, 400, "Bad Request", &clave);
        const int64_t ahora = ahoraMs();
        Canal& nuevo = a->canales[canal];
        nuevo.ip = ip;
        nuevo.puerto = puerto;
        nuevo.vence = ahora + kCanalMs;
        a->permisos[ip] = std::max(a->permisos[ip], ahora + kPermisoMs);
        responder(cli, m, st::kExito, &clave, [](st::Constructor&) {});
    }

    /// El socket de WebRTC (u otro relay) en ip:puerto, si está en memoria. Con `canal`, se
    /// recuerda (y un «no hay» vale hasta que se anote otro punto).
    memoria::PuntoPtr destinoEnMemoria(uint32_t ip, uint16_t puerto, Canal* canal) {
        if (c.memoria == nullptr) return nullptr;
        if (canal != nullptr) {
            if (canal->punto && canal->punto->vivo.load(std::memory_order_acquire)) return canal->punto;
            const uint64_t gen = c.memoria->generacion();
            if (!canal->punto && canal->gen == gen) return nullptr;
            canal->punto = c.memoria->buscar(ip, puerto);
            canal->gen = gen;
            return canal->punto;
        }
        return c.memoria->buscar(ip, puerto);
    }

    void haciaPeer(Asignacion& a, uint32_t ip, uint16_t puerto, const uint8_t* datos, size_t n, Canal* canal) {
        auto p = a.permisos.find(ip);
        if (p == a.permisos.end() || p->second < ahoraMs() || !c.peerPermitido(ip, puerto)) {
            c.rechazosPeer++;
            return;
        }
        const uint64_t t0 = ba::diag::relojNs();
        // #3: en memoria, desde la dirección del relay (la que anunció el Allocate).
        if (memoria::PuntoPtr destino = destinoEnMemoria(ip, puerto, canal)) {
            if (c.memoria->mandar(indice, destino, datos, n, c.ipRelay, a.puertoRelay)) {
                c.bytesHaciaPeers += uint64_t(n);
                c.enMemoriaHacia++;
            }
            c.fHaciaRtc.pase(n, ba::diag::relojNs() - t0 + 1);
            return;
        }
        sockaddr_in sa = sockDe(ip, puerto);
        const auto r = ::sendto(a.relay, reinterpret_cast<const char*>(datos), int(n), 0,
                                reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
        if (r > 0) c.bytesHaciaPeers += uint64_t(r);
        c.fHaciaRtc.pase(n, ba::diag::relojNs() - t0 + 1);
    }

    void sendIndicacion(const Cliente& cli, const st::Mensaje& m) {
        Asignacion* a = asignacionDe(cli);
        if (a == nullptr) return;
        uint32_t ip = 0;
        uint16_t puerto = 0;
        const st::Atributo* d = m.buscar(st::kDataAttr);
        if (d == nullptr || !m.direccionXor(st::kXorPeerAddress, ip, puerto)) return;
        haciaPeer(*a, ip, puerto, reinterpret_cast<const uint8_t*>(d->valor.data()), d->valor.size(), nullptr);
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
        haciaPeer(*a, it->second.ip, it->second.puerto, p + 4, largo, &it->second);
    }

    /// Un datagrama de un cliente UDP (del fragmento 0, directo o en una tanda).
    void procesarUdp(uint32_t ip, uint16_t puerto, const uint8_t* buf, size_t n) {
        Cliente cli;
        cli.ip = ip;
        cli.puerto = puerto;
        if (n >= 4 && (buf[0] & 0xC0) == 0x40)
            manejarChannelData(cli, buf, n);
        else if (st::pareceStun(buf, n) && st::largoStun(buf, n) == n)
            manejarStun(cli, buf, n);
    }

    // --- lo que vuelve del peer (red de WebRTC u otro relay) al cliente ---------------
    void desdePeer(Asignacion& a, uint32_t ip, uint16_t puerto, const uint8_t* buf, size_t r) {
        auto p = a.permisos.find(ip);
        if (p == a.permisos.end() || p->second < ahoraMs() || !c.peerPermitido(ip, puerto)) {
            c.rechazosPeer++;
            return;
        }
        c.bytesDesdePeers += uint64_t(r);
        uint16_t canal = 0;
        const int64_t ahora = ahoraMs();
        for (const auto& [num, cn] : a.canales)
            if (cn.ip == ip && cn.puerto == puerto && cn.vence >= ahora) {
                canal = num;
                break;
            }
        if (canal != 0) {
            enviar(a.cli, st::channelData(canal, buf, r, a.cli.tcp), true);
        } else {
            std::array<uint8_t, 12> tid{};
            net::randomBytes(tid.data(), tid.size());
            st::Constructor b(st::tipo(st::kData, st::kIndicacion), tid);
            b.agregarDireccionXor(st::kXorPeerAddress, ip, puerto);
            b.agregar(st::kDataAttr, buf, r);
            enviar(a.cli, b.bytes(), true);
        }
    }

    /// #14 en memoria: lo que la red de WebRTC le mandó al relay `clave`.
    void desdeMemoria(uint64_t clave, const uint8_t* d, size_t n, uint32_t ip, uint16_t puerto) {
        const uint64_t t0 = ba::diag::relojNs();
        auto it = asignaciones.find(clave);
        if (it != asignaciones.end()) {
            c.enMemoriaDesde++;
            desdePeer(it->second, ip, puerto, d, n);
        }
        c.fDesdeRtc.pase(n, ba::diag::relojNs() - t0 + 1);
        // Si el TURN no estaba en la pila (llegó derecho desde WebRTC), cerrar lo marcado.
        if (profundidad == 0) cerrarPendientes();
    }

    // --- E/S ------------------------------------------------------------------------
    /// Una conexión aceptada (en el fragmento 0) que ahora es de este fragmento.
    void adoptar(Fd f, uint32_t ip, uint16_t puerto) {
        Conexion con;
        con.fd = f;
        con.ip = ip;
        con.puerto = puerto;
        con.sinAsignacionDesde = ahoraMs();
        const uint64_t id = proxConexion++;
        conexiones[id] = std::move(con);
        nConF = int(conexiones.size());
        bucle->vigilar(f, Bucle::kLeer, [this, id](int ev) { manejar([&] { eventoConexion(id, ev); }); });
    }

    void eventoConexion(uint64_t id, int ev) {
        auto it = conexiones.find(id);
        if (it == conexiones.end() || it->second.cerrar) return;
        if (ev & Bucle::kEscribir) vaciar(id, it->second);
        if (!it->second.cerrar && (ev & Bucle::kLeer)) leerConexion(id, it->second);
    }

    void leerConexion(uint64_t id, Conexion& con) {
        const uint64_t t0 = ba::diag::relojNs();
        uint64_t marcos = 0, bytesMarcos = 0;
        struct AlSalir {
            ba::diag::Frontera& f;
            const uint64_t& t0;
            const uint64_t& marcos;
            const uint64_t& bytes;
            ~AlSalir() {
                f.despertada();
                f.lote(marcos, bytes, marcos ? ba::diag::relojNs() - t0 + 1 : 0);
            }
        } alSalir{c.fEntraTcp, t0, marcos, bytesMarcos};
        uint8_t buf[16384];
        bool fin = false;
        for (;;) {
            const auto r = ::recv(con.fd, reinterpret_cast<char*>(buf), int(sizeof(buf)), 0);
            if (r > 0) {
                con.entrada.append(reinterpret_cast<const char*>(buf), size_t(r));
                if (con.entrada.size() > kEntradaMax * 4) break; // que el parseo alcance
                continue;
            }
            if (r < 0 && seguiriaBloqueado()) break;
            fin = true; // EOF o error
            break;
        }
        Cliente cli;
        cli.tcp = true;
        cli.conId = id;
        cli.ip = con.ip;
        cli.puerto = con.puerto;
        size_t i = 0;
        const auto* p = reinterpret_cast<const uint8_t*>(con.entrada.data());
        const size_t n = con.entrada.size();
        while (n - i >= 4 && !con.cerrar) {
            const uint8_t b0 = p[i];
            size_t total = 0;
            if ((b0 & 0xC0) == 0) {
                if (n - i < st::kCabecera) break;
                if (!st::pareceStun(p + i, n - i)) {
                    marcarCierre(id, con);
                    break;
                }
                total = st::largoStun(p + i, n - i);
                if (n - i < total) break;
                manejarStun(cli, p + i, total);
                ++marcos;
                bytesMarcos += total;
            } else if ((b0 & 0xC0) == 0x40) {
                const size_t largo = leer16(p + i + 2);
                total = (4 + largo + 3) & ~size_t(3); // con relleno, sobre TCP
                if (n - i < total) break;
                manejarChannelData(cli, p + i, 4 + largo);
                ++marcos;
                bytesMarcos += total;
            } else {
                marcarCierre(id, con); // basura: no es STUN ni ChannelData
                break;
            }
            i += total;
        }
        // manejarStun/ChannelData no insertan ni borran conexiones (los cierres se marcan):
        // `con` sigue válida y nadie tocó su entrada.
        if (i > 0) con.entrada.erase(0, i);
        if (con.entrada.size() > kEntradaMax || fin) marcarCierre(id, con);
    }

    /// El fragmento 0: conexiones nuevas, cada una a su fragmento (una vez).
    void aceptar() {
        for (;;) {
            sockaddr_in sa{};
            socklen_t l = sizeof(sa);
            Fd f = static_cast<Fd>(::accept(c.tcp, reinterpret_cast<sockaddr*>(&sa), &l));
            if (f == FD_INVALIDO) return;
            if (c.nCon.load(std::memory_order_acquire) >= c.maxCon || !noBloqueante(f)) {
                cerrarFd(f);
                continue;
            }
            int si = 1;
            setsockopt(f, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&si), sizeof(si));
            sinSigpipe(f);
            buffersGrandes(f);
            c.nCon.fetch_add(1, std::memory_order_acq_rel);
            const uint32_t ip = ntohl(sa.sin_addr.s_addr);
            const uint16_t puerto = ntohs(sa.sin_port);
            const int k = c.menosCargado();
            if (k == indice) {
                adoptar(f, ip, puerto);
                continue;
            }
            Fragmento* destino = c.frags[size_t(k)].get();
            destino->nConF.fetch_add(1, std::memory_order_relaxed); // que el próximo reparto lo cuente ya
            c.fEntregaTcp.pase(0);
            Comun* comun = &c;
            destino->bucle->enHilo([comun, destino, f, ip, puerto] {
                if (!comun->corriendo.load()) {
                    cerrarFd(f);
                    comun->nCon.fetch_sub(1, std::memory_order_acq_rel);
                    return;
                }
                comun->fEntregaTcp.despertada();
                destino->adoptar(f, ip, puerto);
            });
        }
    }

    /// El fragmento 0: vacía el UDP de pinggy de a tandas y reparte por origen.
    void leerUdp() {
        const uint64_t t0 = ba::diag::relojNs();
        uint64_t paquetes = 0, bytes = 0;
        struct AlSalir {
            ba::diag::Frontera& f;
            const uint64_t& t0;
            const uint64_t& paquetes;
            const uint64_t& bytes;
            ~AlSalir() {
                f.despertada();
                f.lote(paquetes, bytes, paquetes ? ba::diag::relojNs() - t0 + 1 : 0);
            }
        } alSalir{c.fEntraUdp, t0, paquetes, bytes};
        const size_t nf = c.frags.size();
        std::vector<Lote> lotes(nf > 1 ? nf : 0);
        const int64_t ahora = ahoraMs();
        uint8_t buf[65536];
        for (int k = 0; k < 256; ++k) {
            sockaddr_in sa{};
            socklen_t l = sizeof(sa);
            const auto r = ::recvfrom(c.udp, reinterpret_cast<char*>(buf), int(sizeof(buf)), 0,
                                      reinterpret_cast<sockaddr*>(&sa), &l);
            if (r < 0) {
#ifdef _WIN32
                if (WSAGetLastError() == WSAECONNRESET) continue;
#endif
                break;
            }
            const uint32_t ip = ntohl(sa.sin_addr.s_addr);
            const uint16_t puerto = ntohs(sa.sin_port);
            const size_t n = size_t(r);
            ++paquetes;
            bytes += n;
            int destino = indice;
            if (nf > 1) {
                const uint64_t clave = (uint64_t(ip) << 16) | puerto;
                auto it = rutasUdp.find(clave);
                if (it == rutasUdp.end()) {
                    Ruta ru;
                    ru.frag = c.menosCargado();
                    c.frags[size_t(ru.frag)]->nRutasF.fetch_add(1, std::memory_order_relaxed);
                    it = rutasUdp.emplace(clave, ru).first;
                }
                it->second.visto = ahora;
                destino = it->second.frag;
            }
            if (destino == indice) {
                procesarUdp(ip, puerto, buf, n);
                continue;
            }
            Lote& lo = lotes[size_t(destino)];
            Datagrama d;
            d.ip = ip;
            d.puerto = puerto;
            d.off = lo.bytes.size();
            d.n = n;
            lo.bytes.append(reinterpret_cast<const char*>(buf), n);
            lo.d.push_back(d);
        }
        // Una entrega por fragmento y por tanda.
        for (size_t k = 0; k < lotes.size(); ++k) {
            if (lotes[k].d.empty()) continue;
            auto lote = std::make_shared<Lote>(std::move(lotes[k]));
            Fragmento* f = c.frags[k].get();
            Comun* comun = &c;
            const uint64_t puesto = ba::diag::relojNs();
            f->bucle->enHilo([comun, f, lote, puesto] {
                if (!comun->corriendo.load()) return;
                const uint64_t t1 = ba::diag::relojNs();
                f->manejar([&] {
                    for (const Datagrama& d : lote->d)
                        f->procesarUdp(d.ip, d.puerto, reinterpret_cast<const uint8_t*>(lote->bytes.data()) + d.off, d.n);
                });
                comun->fRepartoUdp.despertada();
                comun->fRepartoUdp.lote(lote->d.size(), lote->bytes.size(), ba::diag::relojNs() - t1 + 1,
                                        t1 > puesto ? t1 - puesto : 0);
            });
        }
    }

    /// Lo que llegó al relay por el kernel (no un socket de WebRTC en memoria).
    void leerRelay(Asignacion& a) {
        const uint64_t t0 = ba::diag::relojNs();
        uint64_t paquetes = 0, bytes = 0;
        struct AlSalir {
            ba::diag::Frontera& f;
            const uint64_t& t0;
            const uint64_t& paquetes;
            const uint64_t& bytes;
            ~AlSalir() {
                f.despertada();
                f.lote(paquetes, bytes, paquetes ? ba::diag::relojNs() - t0 + 1 : 0);
            }
        } alSalir{c.fDesdeRtc, t0, paquetes, bytes};
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
            ++paquetes;
            bytes += uint64_t(r);
            desdePeer(a, ntohl(sa.sin_addr.s_addr), ntohs(sa.sin_port), buf, size_t(r));
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
        for (auto& [id, con] : conexiones)
            if (con.sinAsignacionDesde != 0 && ahora - con.sinAsignacionDesde > kSinAsignacionMs) marcarCierre(id, con);
        // Las rutas UDP sin uso (más que la vida máxima de una asignación): la asignación ya no está.
        const int64_t vidaRuta = int64_t(c.vidaMaxS + 60) * 1000;
        for (auto it = rutasUdp.begin(); it != rutasUdp.end();) {
            if (ahora - it->second.visto > vidaRuta) {
                c.frags[size_t(it->second.frag)]->nRutasF.fetch_sub(1, std::memory_order_relaxed);
                it = rutasUdp.erase(it);
            } else {
                ++it;
            }
        }
    }

    void cerrarPendientes() {
        while (!porCerrar.empty()) {
            std::vector<uint64_t> ids;
            ids.swap(porCerrar);
            for (uint64_t id : ids) {
                auto it = conexiones.find(id);
                if (it == conexiones.end()) continue;
                Cliente cli;
                cli.tcp = true;
                cli.conId = id;
                bucle->dejar(it->second.fd);
                cerrarFd(it->second.fd);
                conexiones.erase(it);
                c.nCon.fetch_sub(1, std::memory_order_acq_rel);
                borrarAsignacion(cli.clave()); // la asignación muere con su conexión
            }
        }
        nConF = int(conexiones.size());
    }

    void programarBarrido(std::shared_ptr<Comun> comun) {
        bucle->enHiloLuego(1000, [this, comun] {
            if (!comun->corriendo.load()) return;
            manejar([&] { barrer(); });
            programarBarrido(comun);
        });
    }

    /// En el hilo del fragmento: suelta todo.
    void liberar() {
        for (auto& [k, a] : asignaciones) {
            if (c.memoria != nullptr && a.punto) c.memoria->cerrar(a.punto);
            c.anotarRelay(a.puertoRelay, false);
            if (a.relay != FD_INVALIDO) {
                bucle->dejar(a.relay);
                cerrarFd(a.relay);
            }
        }
        c.nAsig.fetch_sub(int(asignaciones.size()), std::memory_order_acq_rel);
        asignaciones.clear();
        for (auto& [id, con] : conexiones) {
            bucle->dejar(con.fd);
            cerrarFd(con.fd);
        }
        c.nCon.fetch_sub(int(conexiones.size()), std::memory_order_acq_rel);
        conexiones.clear();
        porCerrar.clear();
        for (const auto& [k, r] : rutasUdp) c.frags[size_t(r.frag)]->nRutasF.fetch_sub(1, std::memory_order_relaxed);
        rutasUdp.clear();
        nAsigF = 0;
        nConF = 0;
    }
};

int Servidor::Comun::menosCargado() const {
    int mejor = 0;
    int carga = -1;
    for (size_t i = 0; i < frags.size(); ++i) {
        const Fragmento& f = *frags[i];
        const int x = f.nConF.load(std::memory_order_relaxed) + f.nRutasF.load(std::memory_order_relaxed);
        if (carga < 0 || x < carga) {
            carga = x;
            mejor = int(i);
        }
    }
    return mejor;
}

// ---------------------------------------------------------------------------
Servidor::Servidor(Config cfg) : cfg_(std::move(cfg)) {}

Servidor::~Servidor() { stop(); }

bool Servidor::start(std::string* err) {
    if (corriendo_.load()) return true;
    std::shared_ptr<Comun> comun = std::make_shared<Comun>();
    auto falla = [&](const std::string& e) {
        if (err != nullptr) *err = e;
        cerrarFd(comun->tcp);
        cerrarFd(comun->udp);
        propios_.clear();
        return false;
    };
    if (cfg_.secreto.size() < 16) return falla(traducir("gateway", "turn: falta el secreto (16 bytes o más)"));
#ifdef _WIN32
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    Comun& s = *comun;
    s.cfg = cfg_;
    s.memoria = cfg_.memoria;
    s.maxAsig = cfg_.maxAsignaciones > 0 ? cfg_.maxAsignaciones : ba::ajustes::entero("turn.maxAsignaciones", 640);
    s.maxCon = cfg_.maxConexiones > 0 ? cfg_.maxConexiones : ba::ajustes::entero("turn.maxConexiones", 1280);
    s.vidaMaxS = cfg_.vidaMaxS > 0 ? cfg_.vidaMaxS : ba::ajustes::entero("turn.vidaMaxS", 3600);
    s.vidaMaxS = std::max(s.vidaMaxS, kVidaDefS);
    s.nonceS = cfg_.nonceS > 0 ? cfg_.nonceS : ba::ajustes::entero("turn.nonceS", 600);
    s.colaMax = size_t(std::max(64, ba::ajustes::entero("turn.colaTcpKB", 1024))) * 1024;

    if (!ipDeTexto(cfg_.direccion, s.ipEscucha))
        return falla(llenar(traducir("gateway", "turn: dirección inválida: %1"), {cfg_.direccion}));
    std::string relay = cfg_.direccionRelay;
    if (relay.empty()) relay = s.ipEscucha == 0 ? "127.0.0.1" : cfg_.direccion;
    if (!ipDeTexto(relay, s.ipRelay) || s.ipRelay == 0)
        return falla(llenar(traducir("gateway", "turn: dirección de relay inválida: %1"), {relay}));
    const std::string dirBind = cfg_.bindRelay.empty() ? relay : cfg_.bindRelay;
    if (!ipDeTexto(dirBind, s.ipBindRelay)) return falla(llenar(traducir("gateway", "turn: bindRelay inválido: %1"), {dirBind}));
    for (const auto& p : cfg_.peersPermitidos) {
        uint32_t ip = 0;
        if (ipDeTexto(p, ip) && ip != 0) s.permitidas.insert(ip);
    }
    if (s.permitidas.empty()) return falla(traducir("gateway", "turn: sin peers permitidos"));
    if (cfg_.rtcMin == 0 || cfg_.rtcMax < cfg_.rtcMin) return falla(traducir("gateway", "turn: rango de puertos RTC inválido"));
    std::array<uint8_t, 32> sn{};
    net::randomBytes(sn.data(), sn.size());
    s.secretoNonce.assign(reinterpret_cast<const char*>(sn.data()), sn.size());

    // TCP
    s.tcp = static_cast<Fd>(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (s.tcp == FD_INVALIDO) return falla("turn: socket TCP");
    {
        int si = 1;
        setsockopt(s.tcp, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&si), sizeof(si));
        sockaddr_in sa = sockDe(s.ipEscucha, cfg_.puertoTcp);
        if (bind(s.tcp, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0 || listen(s.tcp, 128) != 0 ||
            !noBloqueante(s.tcp))
            return falla(llenar(traducir("gateway", "turn: no se pudo escuchar en %1:%2 (TCP)"),
                                {cfg_.direccion, std::to_string(cfg_.puertoTcp)}));
    }
    puertoTcpReal_ = puertoLocal(s.tcp);
    // UDP (opcional)
    if (cfg_.puertoUdp != 0 || cfg_.conUdp) {
        s.udp = udpEnlazado(s.ipEscucha, cfg_.puertoUdp);
        if (s.udp == FD_INVALIDO)
            return falla(llenar(traducir("gateway", "turn: no se pudo escuchar en %1:%2 (UDP)"),
                                {cfg_.direccion, std::to_string(cfg_.puertoUdp)}));
        puertoUdpReal_ = puertoLocal(s.udp);
    } else {
        puertoUdpReal_ = 0;
    }

    // Los hilos: los de red que nos dieron o unos propios.
    s.bucles = cfg_.bucles;
    if (s.bucles.empty()) {
        const int n = std::max(1, cfg_.fragmentosPropios);
        for (int i = 0; i < n; ++i) {
            propios_.push_back(std::make_unique<BuclePoll>(n == 1 ? std::string("turn") : "turn-" + std::to_string(i)));
            s.bucles.push_back(propios_.back().get());
        }
    }
    // La entrega en memoria supone que el fragmento i corre en el hilo i del conmutador: sin
    // eso (hilos propios, otra cantidad), por el kernel.
    if (s.memoria != nullptr && (cfg_.bucles.empty() || int(cfg_.bucles.size()) != s.memoria->hilos())) s.memoria = nullptr;
    if (s.memoria != nullptr) s.memoria->ponerIpRelay(s.ipRelay);
    for (size_t i = 0; i < s.bucles.size(); ++i) s.frags.push_back(std::make_unique<Fragmento>(s, int(i), s.bucles[i]));
    s.corriendo = true;
    {
        std::lock_guard<std::mutex> l(comunM_);
        comun_ = comun;
    }
    corriendo_ = true;
    // Las escuchas en el fragmento 0; el barrido en cada uno.
    Fragmento* cero = s.frags[0].get();
    cero->bucle->enHiloEsperando([&s, cero] {
        cero->bucle->vigilar(s.tcp, Bucle::kLeer, [cero](int) { cero->manejar([&] { cero->aceptar(); }); });
        if (s.udp != FD_INVALIDO)
            cero->bucle->vigilar(s.udp, Bucle::kLeer, [cero](int) { cero->manejar([&] { cero->leerUdp(); }); });
    });
    for (auto& f : s.frags) {
        Fragmento* fp = f.get();
        fp->bucle->enHilo([fp, comun] { fp->programarBarrido(comun); });
    }
    return true;
}

void Servidor::stop() {
    if (!corriendo_.exchange(false)) return;
    std::shared_ptr<Comun> comun;
    {
        std::lock_guard<std::mutex> l(comunM_);
        comun = comun_;
    }
    if (comun) {
        comun->corriendo = false;
        if (comun->memoria != nullptr) comun->memoria->ponerIpRelay(0); // los sockets de WebRTC dejan de buscar relays
        Fragmento* cero = comun->frags.empty() ? nullptr : comun->frags[0].get();
        if (cero != nullptr)
            cero->bucle->enHiloEsperando([&comun, cero] {
                cero->bucle->dejar(comun->tcp);
                if (comun->udp != FD_INVALIDO) cero->bucle->dejar(comun->udp);
            });
        for (auto& f : comun->frags) {
            Fragmento* fp = f.get();
            fp->bucle->enHiloEsperando([fp] { fp->liberar(); });
        }
        cerrarFd(comun->tcp);
        cerrarFd(comun->udp);
        comun->tcp = comun->udp = FD_INVALIDO;
        // El estado sigue mirando los contadores (activo = false); los fragmentos se van con
        // la última tarea que los nombre.
    }
    // Los hilos propios terminan acá (lo que quedaba en sus colas ve corriendo = false).
    propios_.clear();
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
    std::shared_ptr<Comun> s;
    {
        std::lock_guard<std::mutex> l(comunM_);
        s = comun_;
    }
    if (s) {
        j["direccionRelay"] = textoDeIp(s->ipRelay);
        j["asignaciones"] = s->nAsig.load();
        j["maxAsignaciones"] = s->maxAsig;
        j["conexiones"] = s->nCon.load();
        j["bytesHaciaPeers"] = s->bytesHaciaPeers.load();
        j["bytesDesdePeers"] = s->bytesDesdePeers.load();
        j["rechazosPeer"] = s->rechazosPeer.load();
        j["rechazosAuth"] = s->rechazosAuth.load();
        j["descartesCola"] = s->descartesCola.load();
        j["enMemoria"] = s->memoria != nullptr;
        j["paquetesEnMemoriaHacia"] = s->enMemoriaHacia.load();
        j["paquetesEnMemoriaDesde"] = s->enMemoriaDesde.load();
        json fr = json::array();
        for (const auto& f : s->frags)
            fr.push_back({{"fragmento", f->indice},
                          {"conexiones", f->nConF.load()},
                          {"asignaciones", f->nAsigF.load()},
                          {"rutasUdp", f->nRutasF.load()}});
        j["fragmentos"] = fr;
    }
    return j;
}

} // namespace ba::gateway::turn
