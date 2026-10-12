// test_turn.cpp — el TURN de la caja (turn/Servidor, turn/Stun, turn/Ice)
//
// Un cliente TURN mínimo (acá mismo) contra el servidor en 127.0.0.1:
//   - vectores de RFC 5769 (MESSAGE-INTEGRITY corta y larga, FINGERPRINT) y
//     HMAC-SHA1 de RFC 2202;
//   - Binding sin credencial; Allocate sin credencial (401 + realm/nonce), con
//     clave mala, con usuario vencido, con nonce trucho (438), bien (y la
//     respuesta firmada), repetido (437);
//   - CreatePermission hacia 8.8.8.8 (403) y hacia 127.0.0.1;
//   - Send/Data ida y vuelta con un eco UDP en un puerto del rango RTC, y nada
//     hacia (ni desde) un puerto fuera del rango;
//   - ChannelBind (403 fuera del rango) y ChannelData con relleno sobre TCP;
//   - dos asignaciones que se hablan entre sí (navegador↔navegador);
//   - Allocate y Send/Data por UDP;
//   - Refresh con LIFETIME 0;
//   - iceServersPara / respuestaIce.
#include "turn/Ice.hpp"
#include "turn/Servidor.hpp"
#include "turn/Stun.hpp"

#include <array>
#include <atomic>
#include <functional>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET Fd;
#define FD_INVALIDO INVALID_SOCKET
static void cerrar(Fd f) { closesocket(f); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int Fd;
#define FD_INVALIDO (-1)
static void cerrar(Fd f) { ::close(f); }
#endif

namespace st = ba::gateway::turn::stun;
namespace tu = ba::gateway::turn;
using json = nlohmann::json;

static int fallas = 0;
#define CHECK(c, msg)                                                                                                  \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::printf("FALLA (línea %d): %s\n", __LINE__, msg);                                                      \
            ++fallas;                                                                                                  \
        }                                                                                                              \
    } while (0)

namespace {

std::string deHex(const char* h) {
    std::string s;
    int v = -1;
    for (const char* p = h; *p; ++p) {
        int d = -1;
        if (*p >= '0' && *p <= '9') d = *p - '0';
        else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
        if (d < 0) continue;
        if (v < 0) v = d;
        else {
            s.push_back(char(v * 16 + d));
            v = -1;
        }
    }
    return s;
}

const uint32_t kLocal = 0x7F000001;

sockaddr_in sockDe(uint32_t ip, uint16_t puerto) {
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(ip);
    sa.sin_port = htons(puerto);
    return sa;
}

bool legible(Fd f, int ms) {
    fd_set r;
    FD_ZERO(&r);
    FD_SET(f, &r);
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    return select(int(f) + 1, &r, nullptr, nullptr, &tv) > 0;
}

Fd udpLocal(uint16_t& puerto) {
    Fd f = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in sa = sockDe(kLocal, 0);
    bind(f, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
    socklen_t l = sizeof(sa);
    getsockname(f, reinterpret_cast<sockaddr*>(&sa), &l);
    puerto = ntohs(sa.sin_port);
    return f;
}

std::array<uint8_t, 12> tidNuevo() {
    static uint8_t n = 1;
    std::array<uint8_t, 12> t{};
    for (auto& b : t) b = n++;
    return t;
}

// --- cliente TURN mínimo -----------------------------------------------------
struct Cliente {
    bool tcp = true;
    Fd fd = FD_INVALIDO;
    uint16_t puertoServidor = 0;
    std::string buf;
    std::string usuario, claveMI, realm, nonce;

    bool conectar(uint16_t puerto, bool porTcp) {
        tcp = porTcp;
        puertoServidor = puerto;
        fd = socket(AF_INET, tcp ? SOCK_STREAM : SOCK_DGRAM, tcp ? IPPROTO_TCP : IPPROTO_UDP);
        sockaddr_in sa = sockDe(kLocal, puerto);
        return connect(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == 0;
    }
    ~Cliente() {
        if (fd != FD_INVALIDO) cerrar(fd);
    }
    void mandar(const std::string& s) { ::send(fd, s.data(), int(s.size()), 0); }

    /// Un marco (STUN o ChannelData, sin relleno). false si vence `ms`.
    bool marco(std::string& out, int ms = 1500) {
        const auto fin = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        for (;;) {
            if (!tcp && !buf.empty()) {
                out.swap(buf);
                buf.clear();
                return true;
            }
            if (tcp && buf.size() >= 4) {
                const auto* p = reinterpret_cast<const uint8_t*>(buf.data());
                size_t total = 0, util = 0;
                if ((p[0] & 0xC0) == 0) {
                    total = util = st::largoStun(p, buf.size());
                } else {
                    util = 4 + ((size_t(p[2]) << 8) | p[3]);
                    total = (util + 3) & ~size_t(3);
                }
                if (buf.size() >= total) {
                    out = buf.substr(0, util);
                    buf.erase(0, total);
                    return true;
                }
            }
            const int resta = int(std::chrono::duration_cast<std::chrono::milliseconds>(
                                      fin - std::chrono::steady_clock::now())
                                      .count());
            if (resta <= 0 || !legible(fd, resta)) return false;
            char b[70000];
            const auto r = ::recv(fd, b, int(sizeof(b)), 0);
            if (r <= 0) return false;
            buf.append(b, size_t(r));
        }
    }
    bool mensaje(st::Mensaje& m, int ms = 1500) {
        std::string f;
        while (marco(f, ms)) {
            const auto* p = reinterpret_cast<const uint8_t*>(f.data());
            if (st::parsear(p, f.size(), m)) return true;
        }
        return false;
    }
    /// Pedido con credencial (si hay nonce) y espera la respuesta.
    bool pedir(uint16_t metodo, const std::function<void(st::Constructor&)>& attrs, st::Mensaje& resp,
               bool firmar = true) {
        const auto tid = tidNuevo();
        st::Constructor b(st::tipo(metodo, st::kPedido), tid);
        attrs(b);
        if (firmar && !nonce.empty()) {
            b.agregar(st::kUsername, usuario);
            b.agregar(st::kRealm, realm);
            b.agregar(st::kNonce, nonce);
            b.integridad(claveMI);
        }
        b.huella();
        mandar(b.bytes());
        return mensaje(resp) && resp.tid == tid;
    }
    static int codigo(const st::Mensaje& m) {
        const st::Atributo* e = m.buscar(st::kErrorCode);
        if (e == nullptr || e->valor.size() < 4) return m.clase() == st::kExito ? 200 : 0;
        return (e->valor[2] & 7) * 100 + uint8_t(e->valor[3]);
    }
    /// Allocate con el ida y vuelta del 401. Devuelve el código final.
    int allocate(const std::string& u, const std::string& clave, uint32_t& ipRelay, uint16_t& puertoRelay) {
        usuario = u;
        nonce.clear();
        st::Mensaje r;
        auto attrs = [](st::Constructor& b) {
            const uint8_t udp[4] = {17, 0, 0, 0};
            b.agregar(st::kRequestedTransport, udp, 4);
        };
        if (!pedir(st::kAllocate, attrs, r)) return -1;
        if (codigo(r) != 401) return codigo(r);
        realm = r.texto(st::kRealm);
        nonce = r.texto(st::kNonce);
        claveMI = st::claveLarga(usuario, realm, clave);
        if (!pedir(st::kAllocate, attrs, r)) return -1;
        const int c = codigo(r);
        if (c == 200) {
            CHECK(st::verificarIntegridad(r, claveMI), "la respuesta del Allocate no viene firmada bien");
            CHECK(st::verificarHuella(r), "la respuesta del Allocate no trae una huella válida");
            r.direccionXor(st::kXorRelayedAddress, ipRelay, puertoRelay);
        }
        return c;
    }
    void sendInd(uint32_t ip, uint16_t puerto, const std::string& datos) {
        st::Constructor b(st::tipo(st::kSend, st::kIndicacion), tidNuevo());
        b.agregarDireccionXor(st::kXorPeerAddress, ip, puerto);
        b.agregar(st::kDataAttr, datos);
        b.huella();
        mandar(b.bytes());
    }
};

// Eco UDP en un hilo.
struct Eco {
    Fd fd = FD_INVALIDO;
    uint16_t puerto = 0;
    std::atomic<bool> seguir{true};
    std::thread t;
    Eco() {
        fd = udpLocal(puerto);
        t = std::thread([this] {
            char b[2000];
            while (seguir) {
                if (!legible(fd, 50)) continue;
                sockaddr_in de{};
                socklen_t l = sizeof(de);
                const auto r = recvfrom(fd, b, int(sizeof(b)), 0, reinterpret_cast<sockaddr*>(&de), &l);
                if (r > 0) sendto(fd, b, int(r), 0, reinterpret_cast<sockaddr*>(&de), l);
            }
        });
    }
    ~Eco() {
        seguir = false;
        t.join();
        cerrar(fd);
    }
};

void vectoresRfc() {
    // RFC 2202, caso 2.
    const std::string jefe = "what do ya want for nothing?";
    const auto h = st::hmacSha1("Jefe", reinterpret_cast<const uint8_t*>(jefe.data()), jefe.size());
    CHECK(std::string(reinterpret_cast<const char*>(h.data()), 20) == deHex("effcdf6ae5eb2fa2d27416d5f184df9c259a7c79"),
          "HMAC-SHA1 (RFC 2202)");
    CHECK(tu::claveDeUsuario("Jefe", jefe) == "7/zfauXrL6LSdBbV8YTfnCWafHk=", "claveDeUsuario = base64(HMAC-SHA1)");

    // RFC 5769 §2.1: pedido con credencial corta.
    const std::string pedido = deHex(
        "000100582112a442b7e7a701bc34d686fa87dfae802200105354554e207465737420636c69656e74"
        "002400046e0001ff80290008932ff9b151263b36000600096576746a3a68367659202020"
        "000800149aeaa70cbfd8cb56781ef2b5b2d3f249c1b571a280280004e57a3bcf");
    st::Mensaje m;
    CHECK(st::parsear(reinterpret_cast<const uint8_t*>(pedido.data()), pedido.size(), m), "parsear RFC 5769 §2.1");
    CHECK(m.texto(st::kUsername) == "evtj:h6vY", "USERNAME de §2.1");
    CHECK(st::verificarIntegridad(m, "VOkJxbRl1RmTxUk/WvJxBt"), "MESSAGE-INTEGRITY de §2.1");
    CHECK(!st::verificarIntegridad(m, "otra"), "MESSAGE-INTEGRITY con otra clave tiene que fallar");
    CHECK(st::verificarHuella(m), "FINGERPRINT de §2.1");

    // §2.2: respuesta IPv4.
    const std::string resp = deHex(
        "0101003c2112a442b7e7a701bc34d686fa87dfae8022000b7465737420766563746f7220"
        "002000080001a147e112a643000800142b91f599fd9e90c38c7489f92af9ba53f06be7d7"
        "80280004c07d4c96");
    CHECK(st::parsear(reinterpret_cast<const uint8_t*>(resp.data()), resp.size(), m), "parsear §2.2");
    uint32_t ip = 0;
    uint16_t puerto = 0;
    CHECK(m.direccionXor(st::kXorMappedAddress, ip, puerto) && ip == 0xC0000201 && puerto == 32853,
          "XOR-MAPPED-ADDRESS de §2.2 (192.0.2.1:32853)");
    CHECK(st::verificarIntegridad(m, "VOkJxbRl1RmTxUk/WvJxBt"), "MESSAGE-INTEGRITY de §2.2");
    CHECK(st::verificarHuella(m), "FINGERPRINT de §2.2");

    // §2.4: credencial larga (usuario y clave ya pasados por SASLprep).
    const std::string larga = deHex(
        "000100602112a44278ad3433c6ad72c029da412e00060012e3839ee38388e383aae38383e382afe382b90000"
        "0015001c662f2f3439396b3935346436" "4f4c33346f4c394653547679" "36347341"
        "0014000b6578616d706c652e6f726700"
        "00080014f67024656dd64a3e02b8e0712e85c9a28ca89666");
    if (st::parsear(reinterpret_cast<const uint8_t*>(larga.data()), larga.size(), m)) {
        const std::string clave = st::claveLarga(m.texto(st::kUsername), "example.org", "TheMatrIX");
        CHECK(st::verificarIntegridad(m, clave), "MESSAGE-INTEGRITY larga de §2.4");
    } else {
        CHECK(false, "parsear §2.4");
    }

    // Lo que arma el Constructor se verifica a sí mismo.
    st::Constructor b(st::tipo(st::kBinding, st::kPedido), tidNuevo());
    b.agregar(st::kUsername, std::string("a:b"));
    b.integridad("k");
    b.huella();
    CHECK(st::parsear(reinterpret_cast<const uint8_t*>(b.bytes().data()), b.bytes().size(), m) &&
              st::verificarIntegridad(m, "k") && st::verificarHuella(m),
          "Constructor: integridad + huella");
    CHECK(st::metodoDe(st::tipo(st::kChannelBind, st::kError)) == st::kChannelBind &&
              st::claseDe(st::tipo(st::kChannelBind, st::kError)) == st::kError && st::tipo(st::kAllocate, st::kExito) == 0x0103,
          "tipo/método/clase");
}

} // namespace

int main() {
#ifdef _WIN32
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    vectoresRfc();

    Eco eco;                  // el «gateway»: el único puerto del rango RTC
    uint16_t puertoFuera = 0; // un puerto fuera del rango
    Fd fuera = udpLocal(puertoFuera);

    tu::Servidor::Config cfg;
    cfg.puertoTcp = 0;
    cfg.conUdp = true;
    cfg.direccion = "127.0.0.1";
    cfg.secreto = tu::secretoAleatorio();
    cfg.peersPermitidos = {"127.0.0.1"};
    cfg.rtcMin = eco.puerto;
    cfg.rtcMax = eco.puerto;
    tu::Servidor srv(cfg);
    std::string err;
    if (!srv.start(&err)) {
        std::printf("FALLA: no arrancó: %s\n", err.c_str());
        return 1;
    }
    const uint16_t pt = srv.puertoTcp(), pu = srv.puertoUdp();
    CHECK(pt != 0 && pu != 0, "puertos efectivos");

    const auto pedirUdp = [](st::Constructor& b) {
        const uint8_t udp[4] = {17, 0, 0, 0};
        b.agregar(st::kRequestedTransport, udp, 4);
    };
    const tu::Credencial buena = tu::credencialEfimera(cfg.secreto, 600);
    st::Mensaje r;
    std::string f;
    {
        Cliente c;
        CHECK(c.conectar(pt, true), "conectar por TCP");
        // Binding sin credencial.
        CHECK(c.pedir(st::kBinding, [](st::Constructor&) {}, r, false) && Cliente::codigo(r) == 200, "Binding");
        uint32_t ip = 0;
        uint16_t p = 0;
        CHECK(r.direccionXor(st::kXorMappedAddress, ip, p) && ip == kLocal, "Binding: XOR-MAPPED-ADDRESS");

        // Sin credencial: 401 con realm y nonce.
        CHECK(c.pedir(st::kAllocate, pedirUdp, r, false) && Cliente::codigo(r) == 401 &&
                  r.texto(st::kRealm) == "ba" && !r.texto(st::kNonce).empty(),
              "Allocate sin credencial: 401 con REALM y NONCE");

        // Clave mala, usuario vencido, usuario que no es efímero, nonce trucho.
        uint32_t ipR = 0;
        uint16_t pR = 0;
        CHECK(c.allocate(buena.usuario, "clave-mala", ipR, pR) == 401, "Allocate con clave mala: 401");
        const tu::Credencial vieja = tu::credencialEfimera(cfg.secreto, 60, "x", 1000000);
        CHECK(c.allocate(vieja.usuario, vieja.clave, ipR, pR) == 401, "Allocate con usuario vencido: 401");
        CHECK(c.allocate("admin", tu::claveDeUsuario(cfg.secreto, "admin"), ipR, pR) == 401,
              "Allocate con usuario no efímero: 401");
        c.nonce = "00000000ffffffff0123456789abcdef";
        c.usuario = buena.usuario;
        c.claveMI = st::claveLarga(buena.usuario, c.realm, buena.clave);
        CHECK(c.pedir(st::kAllocate, pedirUdp, r) && Cliente::codigo(r) == 438, "Allocate con nonce trucho: 438");

        // Bien.
        CHECK(c.allocate(buena.usuario, buena.clave, ipR, pR) == 200, "Allocate bien");
        CHECK(ipR == kLocal && pR != 0, "XOR-RELAYED-ADDRESS");
        CHECK(srv.estado().value("asignaciones", 0) == 1, "estado: una asignación");
        CHECK(c.pedir(st::kAllocate, pedirUdp, r) && Cliente::codigo(r) == 437, "Allocate repetido: 437");

        // Permisos: afuera no; la propia máquina sí.
        CHECK(c.pedir(st::kCreatePermission,
                      [](st::Constructor& b) { b.agregarDireccionXor(st::kXorPeerAddress, 0x08080808, 40000); }, r) &&
                  Cliente::codigo(r) == 403,
              "CreatePermission hacia 8.8.8.8: 403");
        CHECK(st::verificarIntegridad(r, c.claveMI), "el 403 viene firmado");
        CHECK(c.pedir(st::kCreatePermission,
                      [](st::Constructor& b) { b.agregarDireccionXor(st::kXorPeerAddress, 0xC0A80001, 40000); }, r) &&
                  Cliente::codigo(r) == 403,
              "CreatePermission hacia 192.168.0.1 (LAN ajena): 403");
        const uint16_t pe = eco.puerto;
        CHECK(c.pedir(st::kCreatePermission,
                      [&](st::Constructor& b) { b.agregarDireccionXor(st::kXorPeerAddress, kLocal, pe); }, r) &&
                  Cliente::codigo(r) == 200,
              "CreatePermission hacia 127.0.0.1");

        // Send → eco → Data.
        c.sendInd(kLocal, eco.puerto, "hola");
        bool llego = false;
        while (c.mensaje(r)) {
            if (r.metodo() != st::kData || r.clase() != st::kIndicacion) continue;
            uint32_t di = 0;
            uint16_t dp = 0;
            llego = r.direccionXor(st::kXorPeerAddress, di, dp) && di == kLocal && dp == eco.puerto &&
                    r.texto(st::kDataAttr) == "hola";
            break;
        }
        CHECK(llego, "Send → eco → Data indication");

        // Hacia un puerto fuera del rango: no sale nada.
        c.sendInd(kLocal, puertoFuera, "no");
        CHECK(!legible(fuera, 300), "Send a un puerto fuera del rango: descartado");
        // Desde un puerto fuera del rango hacia el relay: no entra nada.
        {
            sockaddr_in sa = sockDe(kLocal, pR);
            sendto(fuera, "intruso", 7, 0, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
            CHECK(!c.marco(f, 300), "desde un puerto fuera del rango: descartado");
        }

        // ChannelBind.
        const uint16_t pf = puertoFuera;
        auto canal = [](uint16_t n) {
            return [n](st::Constructor& b) {
                const uint8_t v[4] = {uint8_t(n >> 8), uint8_t(n & 0xFF), 0, 0};
                b.agregar(st::kChannelNumber, v, 4);
            };
        };
        CHECK(c.pedir(st::kChannelBind,
                      [&](st::Constructor& b) {
                          canal(0x4001)(b);
                          b.agregarDireccionXor(st::kXorPeerAddress, kLocal, pf);
                      },
                      r) &&
                  Cliente::codigo(r) == 403,
              "ChannelBind fuera del rango: 403");
        CHECK(c.pedir(st::kChannelBind,
                      [&](st::Constructor& b) {
                          canal(0x3FFF)(b);
                          b.agregarDireccionXor(st::kXorPeerAddress, kLocal, pe);
                      },
                      r) &&
                  Cliente::codigo(r) == 400,
              "ChannelBind con número inválido: 400");
        CHECK(c.pedir(st::kChannelBind,
                      [&](st::Constructor& b) {
                          canal(0x4000)(b);
                          b.agregarDireccionXor(st::kXorPeerAddress, kLocal, pe);
                      },
                      r) &&
                  Cliente::codigo(r) == 200,
              "ChannelBind 0x4000 al eco");
        // ChannelData de 5 bytes (con relleno a 8) dos veces seguidas.
        const std::string cd = st::channelData(0x4000, reinterpret_cast<const uint8_t*>("chau!"), 5, true);
        CHECK(cd.size() == 12, "ChannelData con relleno");
        c.mandar(cd + cd);
        int vueltas = 0;
        while (vueltas < 2 && c.marco(f)) {
            if (f.size() == 9 && uint8_t(f[0]) == 0x40 && f[1] == 0 && f.substr(4) == "chau!") ++vueltas;
        }
        CHECK(vueltas == 2, "ChannelData → eco → ChannelData (dos, con relleno)");

        // Navegador↔navegador: otra asignación que le habla a esta.
        {
            Cliente d;
            CHECK(d.conectar(pt, true), "segunda conexión");
            uint32_t ipD = 0;
            uint16_t pD = 0;
            CHECK(d.allocate(buena.usuario, buena.clave, ipD, pD) == 200, "segunda asignación");
            CHECK(d.pedir(st::kCreatePermission,
                          [&](st::Constructor& b) { b.agregarDireccionXor(st::kXorPeerAddress, kLocal, pR); }, r) &&
                      Cliente::codigo(r) == 200,
                  "permiso hacia la otra asignación");
            d.sendInd(kLocal, pR, "entre relays");
            bool ok = false;
            while (c.mensaje(r))
                if (r.metodo() == st::kData) {
                    uint32_t di = 0;
                    uint16_t dp = 0;
                    ok = r.direccionXor(st::kXorPeerAddress, di, dp) && dp == pD &&
                         r.texto(st::kDataAttr) == "entre relays";
                    break;
                }
            CHECK(ok, "de una asignación a la otra (navegador↔navegador)");
            // Otro usuario no puede tocar la asignación ajena.
            d.usuario = tu::credencialEfimera(cfg.secreto, 600, "otro").usuario;
            CHECK(d.pedir(st::kRefresh, [](st::Constructor&) {}, r) && Cliente::codigo(r) == 441,
                  "Refresh con otro usuario: 441");
        }

        // Refresh con LIFETIME 0: se va.
        CHECK(c.pedir(st::kRefresh, [](st::Constructor& b) { b.agregarU32(st::kLifetime, 0); }, r) &&
                  Cliente::codigo(r) == 200,
              "Refresh 0");
        CHECK(c.pedir(st::kCreatePermission,
                      [&](st::Constructor& b) { b.agregarDireccionXor(st::kXorPeerAddress, kLocal, pe); }, r) &&
                  Cliente::codigo(r) == 437,
              "sin asignación: 437");
    }

    // Por UDP.
    {
        Cliente u;
        CHECK(u.conectar(pu, false), "UDP");
        uint32_t ipR = 0;
        uint16_t pR = 0;
        CHECK(u.allocate(buena.usuario, buena.clave, ipR, pR) == 200, "Allocate por UDP");
        const uint16_t pe = eco.puerto;
        CHECK(u.pedir(st::kCreatePermission,
                      [&](st::Constructor& b) { b.agregarDireccionXor(st::kXorPeerAddress, kLocal, pe); }, r) &&
                  Cliente::codigo(r) == 200,
              "CreatePermission por UDP");
        u.sendInd(kLocal, eco.puerto, "por udp");
        bool ok = false;
        while (u.mensaje(r))
            if (r.metodo() == st::kData) {
                ok = r.texto(st::kDataAttr) == "por udp";
                break;
            }
        CHECK(ok, "Send/Data por UDP");
    }

    // La conexión TCP cerrada se lleva su asignación.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(srv.estado().value("asignaciones", -1) == 1, "estado: queda solo la de UDP");
    CHECK(srv.estado().value("rechazosPeer", 0) >= 3, "estado: rechazos contados");

    // iceServers / /api/ice.
    {
        const json a = tu::iceServersPara(cfg, "sala.ejemplo.com", 443, 0, "", 600);
        CHECK(a.is_array() && a.size() == 1 && a[0]["urls"].size() == 1 &&
                  a[0]["urls"][0] == "turns:sala.ejemplo.com:443?transport=tcp",
              "iceServersPara: solo turns");
        const std::string usuario = a[0].value("username", "");
        CHECK(a[0].value("credential", "") == tu::claveDeUsuario(cfg.secreto, usuario), "iceServersPara: credencial");
        const json b = tu::iceServersPara(cfg, "h", 443, 3478, "u.ejemplo:4000", 600);
        CHECK(b[0]["urls"].size() == 3 && b[0]["urls"][1] == "turn:h:3478?transport=tcp" &&
                  b[0]["urls"][2] == "turn:u.ejemplo:4000?transport=udp",
              "iceServersPara: las tres");
        CHECK(tu::iceServersPara(cfg, "", 0, 0, "", 600).empty(), "iceServersPara: nada → []");

        tu::ExtremosIce ext;
        ext.hostPublico = "sala.ejemplo.com";
        ext.puertoTls = 443;
        ext.tcpPublico = "x.a.pinggy.link:41234";
        json e = json::parse(tu::respuestaIce(cfg, true, ext, true, 900));
        CHECK(e["politica"] == "relay" && e["ttl"] == 900 && e["iceServers"][0]["urls"].size() == 2 &&
                  e["iceServers"][0]["urls"][1] == "turn:x.a.pinggy.link:41234?transport=tcp",
              "respuestaIce externa: relay");
        json l = json::parse(tu::respuestaIce(cfg, true, ext, false, 900));
        CHECK(l["politica"] == "all" && l["iceServers"].empty(), "respuestaIce LAN sin TURN de LAN: []");
        ext.hostLan = "192.168.1.10";
        ext.puertoLanTcp = 3478;
        l = json::parse(tu::respuestaIce(cfg, true, ext, false, 900));
        CHECK(l["politica"] == "all" && l["iceServers"][0]["urls"][0] == "turn:192.168.1.10:3478?transport=tcp",
              "respuestaIce LAN con TURN de LAN");
        json apagado = json::parse(tu::respuestaIce(cfg, false, ext, true, 900));
        CHECK(apagado["politica"] == "all" && apagado["iceServers"].empty(), "respuestaIce sin TURN: []");
        tu::ExtremosIce vacio;
        json sin = json::parse(tu::respuestaIce(cfg, true, vacio, true, 900));
        CHECK(sin["politica"] == "all" && sin["iceServers"].empty(), "respuestaIce externa sin extremos: [] y all");
    }

    srv.stop();
    CHECK(!srv.activo(), "stop");
    cerrar(fuera);
    std::printf(fallas ? "test_turn: %d fallas\n" : "test_turn: todo ok\n", fallas);
    return fallas ? 1 : 0;
}
