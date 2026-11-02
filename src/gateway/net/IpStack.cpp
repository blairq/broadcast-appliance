// ============================================================================
// src/gateway/net/IpStack.cpp — ver IpStack.hpp
// ============================================================================

#include "net/IpStack.hpp"
#include "i18n/I18n.hpp"
#include "diag/Frontera.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <future>

extern "C" {
#include "lwip/init.h"
#include "lwip/ip4.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/timeouts.h"
#include "lwip/udp.h"
}

#include "ajustes/Ajustes.hpp"
#include "net/WgCrypto.hpp"
#include "net/WgDevice.hpp"
#include "platform/Hilo.hpp"
#include "platform/Nucleos.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using SockT = SOCKET;
using PollFd = WSAPOLLFD;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/eventfd.h>
#endif
using SockT = int;
using PollFd = pollfd;
#endif

// --- Enganches que pide lwIP (NO_SYS) ------------------------------------------
extern "C" {
u32_t sys_now(void) {
    return static_cast<u32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now().time_since_epoch())
                                  .count());
}
unsigned int ba_lwip_rand(void) {
    unsigned int v = 0;
    ba::gateway::net::randomBytes(reinterpret_cast<uint8_t*>(&v), sizeof(v));
    return v;
}
}

namespace ba::gateway::net {

namespace {

std::atomic<bool> g_inUse{false}; // lwIP es global: una sola pila por proceso

constexpr int64_t kUdpIdleMs = 120000;

bool sockValid(long long s) { return s >= 0; }

void closeSock(long long s) {
    if (s < 0) return;
#ifdef _WIN32
    closesocket(static_cast<SockT>(s));
#else
    ::close(static_cast<SockT>(s));
#endif
}

void setNonBlocking(long long s) {
#ifdef _WIN32
    u_long one = 1;
    ioctlsocket(static_cast<SockT>(s), FIONBIO, &one);
#else
    fcntl(static_cast<SockT>(s), F_SETFL, fcntl(static_cast<SockT>(s), F_GETFL, 0) | O_NONBLOCK);
#endif
}

bool wouldBlock() {
#ifdef _WIN32
    const int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS;
#endif
}

int64_t nowMs() { return static_cast<int64_t>(sys_now()); }

sockaddr_in loopback(uint16_t port) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(port);
    return a;
}

/// poll (WSAPoll en Windows): sin el tope de FD_SETSIZE de select (64 en Windows).
int esperar(std::vector<PollFd>& v, int ms) {
#ifdef _WIN32
    return WSAPoll(v.data(), static_cast<ULONG>(v.size()), ms);
#else
    return ::poll(v.data(), static_cast<nfds_t>(v.size()), ms);
#endif
}

PollFd pollDe(long long s, short eventos) {
    PollFd p{};
    p.fd = static_cast<SockT>(s);
    p.events = eventos;
    return p;
}

/// Para despertar al hilo de la pila desde otro: eventfd en Linux, un pipe en macOS y un
/// UDP local en Windows (WSAPoll solo espera sockets). Se escribe solo si la cola estaba
/// vacía: una despertada por tanda.
struct Despertador {
    long long lee = -1;
    long long escribe = -1;
    uint16_t puerto = 0; // Windows
    bool abrir() {
#if defined(_WIN32)
        const SockT ws = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (ws == INVALID_SOCKET) return false;
        sockaddr_in a = loopback(0);
        bind(ws, reinterpret_cast<const sockaddr*>(&a), sizeof(a));
        socklen_t len = sizeof(a);
        getsockname(ws, reinterpret_cast<sockaddr*>(&a), &len);
        lee = escribe = static_cast<long long>(ws);
        puerto = ntohs(a.sin_port);
        setNonBlocking(lee);
        return true;
#elif defined(__linux__)
        const int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (fd < 0) return false;
        lee = escribe = fd;
        return true;
#else
        int fds[2] = {-1, -1};
        if (pipe(fds) != 0) return false;
        lee = fds[0];
        escribe = fds[1];
        setNonBlocking(lee);
        setNonBlocking(escribe);
        return true;
#endif
    }
    void despertar() const {
        if (escribe < 0) return;
#if defined(_WIN32)
        const sockaddr_in a = loopback(puerto);
        const char b = 1;
        sendto(static_cast<SockT>(escribe), &b, 1, 0, reinterpret_cast<const sockaddr*>(&a), sizeof(a));
#elif defined(__linux__)
        const uint64_t uno = 1;
        (void)!write(static_cast<int>(escribe), &uno, sizeof uno);
#else
        const char b = 1;
        (void)!write(static_cast<int>(escribe), &b, 1);
#endif
    }
    void vaciar() const {
        if (lee < 0) return;
        char b[256];
#if defined(_WIN32)
        while (recv(static_cast<SockT>(lee), b, sizeof(b), 0) > 0) {
        }
#else
        while (read(static_cast<int>(lee), b, sizeof(b)) > 0) {
        }
#endif
    }
    void cerrar() {
#if defined(_WIN32)
        closeSock(lee);
#elif defined(__linux__)
        if (lee >= 0) ::close(static_cast<int>(lee));
#else
        if (lee >= 0) ::close(static_cast<int>(lee));
        if (escribe >= 0) ::close(static_cast<int>(escribe));
#endif
        lee = escribe = -1;
    }
};

} // namespace

std::string IpStack::ipToString(uint32_t ip) {
    return std::to_string((ip >> 24) & 255) + "." + std::to_string((ip >> 16) & 255) + "." +
           std::to_string((ip >> 8) & 255) + "." + std::to_string(ip & 255);
}

struct IpStack::Impl {
    // --- Reenvios --------------------------------------------------------------
    struct TcpFwd {
        Impl* impl = nullptr;
        uint16_t tunnelPort = 0;
        uint16_t localPort = 0;
        bool proxyHeader = false;
        tcp_pcb* listen = nullptr;
    };
    /// Una conexion TCP del tunel puenteada a un socket local.
    struct Bridge {
        Impl* impl = nullptr;
        tcp_pcb* pcb = nullptr;
        long long sock = -1;
        bool connecting = true;
        std::string toSock; ///< datos del tunel pendientes de escribir al socket
        size_t unacked = 0; ///< bytes recibidos de lwIP aun no confirmados (tcp_recved)
        bool tunnelEof = false;
        bool sockEof = false;
        bool dead = false;
    };
    struct UdpFlowKey {
        uint32_t remoteIp;
        uint16_t remotePort;
        uint16_t tunnelPort;
        bool operator<(const UdpFlowKey& o) const {
            return std::tie(remoteIp, remotePort, tunnelPort) <
                   std::tie(o.remoteIp, o.remotePort, o.tunnelPort);
        }
    };
    struct UdpFlow {
        long long sock = -1;
        int64_t lastMs = 0;
    };
    struct ClientConn {
        tcp_pcb* pcb = nullptr;
        std::string request;
        size_t sent = 0; ///< bytes de la peticion ya entregados a lwIP
        std::string response;
        bool done = false;
        bool ok = false;
        std::promise<std::optional<std::string>> promise;
    };

    WgDevice& wg;
    Config cfg;
    LogFn log;
    netif nif{};
    std::vector<std::unique_ptr<TcpFwd>> tcpFwds;
    uint16_t udpFirst = 0;
    uint16_t udpLast = 0;
    std::map<uint16_t, udp_pcb*> udpPcbs;
    std::map<UdpFlowKey, UdpFlow> udpFlows;
    std::vector<std::unique_ptr<Bridge>> bridges;
    std::vector<std::shared_ptr<ClientConn>> clients;
    /// Los UDP propios (udpAbrir): el pcb y a quién darle lo que llega.
    struct UdpPropio {
        udp_pcb* pcb = nullptr;
        LlegadaFn alLlegar;
    };
    std::map<int, std::unique_ptr<UdpPropio>> udpPropios; ///< solo el hilo de la pila
    std::atomic<int> proximoUdp{1};

    std::mutex qLock;
    // Las fronteras de WireGuard (PLAN-GATEWAY-FRAGMENTOS.md #17-#20).
    ba::diag::Frontera& fDesdeWg = ba::diag::frontera("wireguard.hacia_ipstack", 17, "wireguard → lwIP (mismo hilo, sin cola)");
    ba::diag::Frontera& fHaciaRtc = ba::diag::frontera("ipstack.hacia_webrtc", 18, "ipstack → red de WebRTC (en memoria por tanda, o sendto)");
    ba::diag::Frontera& fDesdeRtc = ba::diag::frontera("ipstack.desde_webrtc", 19, "red de WebRTC → ipstack (en memoria, o poll + recvfrom)");
    ba::diag::Frontera& fHaciaWg = ba::diag::frontera("ipstack.hacia_wireguard", 20, "ipstack → cifrar → VPS (en tanda)");
    std::deque<std::function<void()>> tasks;
    Despertador despertador;
    long long wgSock = -1; ///< el socket de WireGuard (el bucle es de este hilo: cederBucle)

    // F5: la red de WebRTC en memoria.
    bool enMemoriaAjuste = true; ///< gateway.wgEnMemoria (se lee al arrancar)
    std::mutex sumideroMu;
    std::shared_ptr<SumideroRtc> sumidero;      ///< con sumideroMu
    std::shared_ptr<SumideroRtc> sumideroVuelta; ///< el de esta vuelta (solo el hilo de la pila)
    /// Lo que va a WebRTC en esta vuelta: los datos uno detrás del otro (se reusa).
    struct Pendiente {
        uint32_t ip;
        uint16_t puerto;
        uint16_t puertoRtc;
        size_t off, n;
    };
    std::vector<uint8_t> aRtcArena;
    std::vector<Pendiente> aRtc;
    std::vector<DatagramaTunel> aRtcLista;
    /// Lo que vuelve de WebRTC (enviarDesdeRtc): lo llenan otros hilos con qLock; el hilo de
    /// la pila lo cambia por el suyo (vacío, con lugar) y lo manda.
    std::vector<uint8_t> desdeRtcArena, desdeRtcArenaPila;
    std::vector<Pendiente> desdeRtc, desdeRtcPila;
    std::vector<uint8_t> salidaWg; ///< netifOutput: un paquete en varios pbuf (se reusa)
    std::vector<PollFd> fds;       ///< el poll de cada vuelta (se reusa)

    std::atomic<bool> stop{false};
    std::thread thread;
    mutable std::mutex statsLock;
    Stats stats;

    Impl(WgDevice& w, Config c, LogFn l) : wg(w), cfg(c), log(std::move(l)) {}

    void wake() { despertador.despertar(); }

    void post(std::function<void()> fn) {
        bool estaba = false;
        {
            std::lock_guard<std::mutex> lock(qLock);
            estaba = tasks.empty();
            tasks.push_back(std::move(fn));
        }
        if (estaba) wake();
    }

    bool enMemoria() const { return enMemoriaAjuste && sumideroVuelta != nullptr; }

    /// Un paquete IP que trajo el túnel: a lwIP ahora, en este hilo (#17).
    void entrada(const uint8_t* d, size_t n) {
        if (n == 0 || n > 0xFFFF) return;
        fDesdeWg.pase(n);
        pbuf* p = pbuf_alloc(PBUF_RAW, static_cast<u16_t>(n), PBUF_POOL);
        if (p == nullptr) return;
        pbuf_take(p, d, static_cast<u16_t>(n));
        if (nif.input(p, &nif) != ERR_OK) pbuf_free(p);
        ++entradasVuelta; // a stats al final de la vuelta (sin candado por paquete)
    }
    uint64_t entradasVuelta = 0;

    /// Lo que juntó la vuelta para WebRTC, en una sola llamada al sumidero (#18).
    void entregarARtc() {
        if (aRtc.empty()) return;
        if (sumideroVuelta) {
            ba::diag::MedirPase medir(fHaciaRtc, aRtcArena.size());
            medir.pases(aRtc.size());
            fHaciaRtc.despertada();
            aRtcLista.clear();
            for (const Pendiente& x : aRtc) aRtcLista.push_back({x.ip, x.puerto, x.puertoRtc, aRtcArena.data() + x.off, x.n});
            sumideroVuelta->entregar(aRtcLista.data(), aRtcLista.size());
            std::lock_guard<std::mutex> lock(statsLock);
            stats.aRtc += aRtc.size();
        }
        aRtc.clear();
        aRtcArena.clear();
    }

    /// Lo que WebRTC mandó desde la vuelta anterior (#19): por lwIP, en este hilo.
    void mandarDesdeRtc() {
        {
            std::lock_guard<std::mutex> lock(qLock);
            if (desdeRtc.empty()) return;
            desdeRtc.swap(desdeRtcPila);
            desdeRtcArena.swap(desdeRtcArenaPila);
        }
        ba::diag::MedirPase medir(fDesdeRtc, desdeRtcArenaPila.size());
        medir.pases(desdeRtcPila.size());
        fDesdeRtc.despertada();
        for (const Pendiente& x : desdeRtcPila) {
            const auto pcb = udpPcbs.find(x.puertoRtc);
            if (pcb == udpPcbs.end() || x.n > 0xFFFF) continue;
            // Sin copiar: lwIP le pone la cabecera adelante y netifOutput lo cifra antes de volver.
            pbuf* p = pbuf_alloc(PBUF_TRANSPORT, static_cast<u16_t>(x.n), PBUF_REF);
            if (p == nullptr) continue;
            p->payload = desdeRtcArenaPila.data() + x.off;
            ip_addr_t dst;
            IP_ADDR4(&dst, (x.ip >> 24) & 255, (x.ip >> 16) & 255, (x.ip >> 8) & 255, x.ip & 255);
            udp_sendto(pcb->second, p, &dst, x.puerto);
            pbuf_free(p);
        }
        {
            std::lock_guard<std::mutex> lock(statsLock);
            stats.desdeRtc += desdeRtcPila.size();
        }
        desdeRtcPila.clear();
        desdeRtcArenaPila.clear();
    }

    // --- netif ----------------------------------------------------------------------
    static err_t netifOutput(netif* n, pbuf* p, const ip4_addr_t*) {
        auto* self = static_cast<Impl*>(n->state);
        ba::diag::MedirPase medir(self->fHaciaWg, p->tot_len);
        if (p->next == nullptr && p->len == p->tot_len) {
            self->wg.send(static_cast<const uint8_t*>(p->payload), p->len); // de una, sin copiar
        } else {
            self->salidaWg.resize(p->tot_len);
            pbuf_copy_partial(p, self->salidaWg.data(), p->tot_len, 0);
            self->wg.send(self->salidaWg.data(), self->salidaWg.size());
        }
        std::lock_guard<std::mutex> lock(self->statsLock);
        self->stats.packetsOut++;
        return ERR_OK;
    }

    static err_t netifInit(netif* n) {
        auto* self = static_cast<Impl*>(n->state);
        n->name[0] = 'w';
        n->name[1] = 'g';
        n->output = &Impl::netifOutput;
        n->mtu = static_cast<u16_t>(self->cfg.mtu);
        n->flags = 0; // punto a punto: sin ARP ni broadcast
        return ERR_OK;
    }

    // --- TCP: aceptar y puentear -----------------------------------------------------
    static err_t onAccept(void* arg, tcp_pcb* pcb, err_t err) {
        auto* fwd = static_cast<TcpFwd*>(arg);
        if (err != ERR_OK || pcb == nullptr) return ERR_VAL;
        Impl* self = fwd->impl;
        auto b = std::make_unique<Bridge>();
        b->impl = self;
        b->pcb = pcb;
        const SockT s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        b->sock = static_cast<long long>(s);
        if (!sockValid(b->sock)) {
            tcp_abort(pcb);
            return ERR_ABRT;
        }
        setNonBlocking(b->sock);
        int one = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
        const sockaddr_in a = loopback(fwd->localPort);
        if (connect(s, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) != 0 && !wouldBlock()) {
            closeSock(b->sock);
            tcp_abort(pcb);
            return ERR_ABRT;
        }
        if (fwd->proxyHeader) {
            // PROXY v1: el servicio local ve la IP real del invitado.
            b->toSock = "PROXY TCP4 " + ipToString(lwip_ntohl(ip_2_ip4(&pcb->remote_ip)->addr)) + " " +
                        ipToString(lwip_ntohl(ip_2_ip4(&pcb->local_ip)->addr)) + " " +
                        std::to_string(pcb->remote_port) + " " + std::to_string(pcb->local_port) + "\r\n";
        }
        tcp_arg(pcb, b.get());
        tcp_recv(pcb, &Impl::onRecv);
        tcp_sent(pcb, &Impl::onSent);
        tcp_err(pcb, &Impl::onErr);
        tcp_nagle_disable(pcb);
        self->bridges.push_back(std::move(b));
        return ERR_OK;
    }

    static err_t onRecv(void* arg, tcp_pcb*, pbuf* p, err_t) {
        auto* b = static_cast<Bridge*>(arg);
        if (b == nullptr) return ERR_OK;
        if (p == nullptr) { // el invitado cerro su lado
            b->tunnelEof = true;
            return ERR_OK;
        }
        const size_t base = b->toSock.size();
        b->toSock.resize(base + p->tot_len);
        pbuf_copy_partial(p, b->toSock.data() + base, p->tot_len, 0);
        b->unacked += p->tot_len;
        pbuf_free(p);
        return ERR_OK;
    }

    static err_t onSent(void*, tcp_pcb*, u16_t) { return ERR_OK; }

    static void onErr(void* arg, err_t) {
        auto* b = static_cast<Bridge*>(arg);
        if (b == nullptr) return;
        b->pcb = nullptr; // lwIP ya libero el pcb
        b->dead = true;
    }

    void serviceBridge(Bridge& b, bool readable, bool writable) {
        if (b.dead) return;
        const SockT s = static_cast<SockT>(b.sock);
        if (b.connecting && writable) {
            int err = 0;
            socklen_t len = sizeof(err);
            getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
            if (err != 0) {
                if (b.pcb) tcp_abort(b.pcb);
                b.pcb = nullptr;
                b.dead = true;
                return;
            }
            b.connecting = false;
        }
        if (b.connecting) return;
        // tunel -> socket local
        if (!b.toSock.empty()) {
            const int n = static_cast<int>(::send(s, b.toSock.data(), static_cast<int>(b.toSock.size()), 0));
            if (n > 0) {
                b.toSock.erase(0, static_cast<size_t>(n));
                // Confirmar a lwIP solo lo que ya salio: contrapresion real.
                size_t credit = std::min(b.unacked, static_cast<size_t>(n));
                if (b.pcb) {
                    while (credit > 0) {
                        const u16_t c = static_cast<u16_t>(std::min<size_t>(credit, 0xFFFF));
                        tcp_recved(b.pcb, c);
                        credit -= c;
                        b.unacked -= c;
                    }
                }
            } else if (n < 0 && !wouldBlock()) {
                b.dead = true;
            }
        }
        if (b.tunnelEof && b.toSock.empty() && !b.sockEof) {
#ifdef _WIN32
            shutdown(s, SD_SEND);
#else
            shutdown(s, SHUT_WR);
#endif
        }
        // socket local -> tunel (solo si lwIP tiene lugar)
        if (readable && b.pcb != nullptr && !b.sockEof) {
            const size_t room = tcp_sndbuf(b.pcb);
            if (room > 0) {
                // tcp_write toma u16: como mucho 65535 por llamada.
                std::vector<char> buf(std::min<size_t>(room, 65535));
                const int n = static_cast<int>(::recv(s, buf.data(), static_cast<int>(buf.size()), 0));
                if (n > 0) {
                    tcp_write(b.pcb, buf.data(), static_cast<u16_t>(n), TCP_WRITE_FLAG_COPY);
                    tcp_output(b.pcb);
                } else if (n == 0) {
                    b.sockEof = true;
                } else if (!wouldBlock()) {
                    b.sockEof = true;
                }
            }
        }
        if (b.sockEof && b.pcb != nullptr && tcp_sndqueuelen(b.pcb) == 0) {
            tcp_arg(b.pcb, nullptr);
            if (tcp_close(b.pcb) != ERR_OK) tcp_abort(b.pcb);
            b.pcb = nullptr;
            b.dead = true;
        }
        if (b.tunnelEof && b.sockEof) b.dead = true;
    }

    // --- UDP: rango de WebRTC ----------------------------------------------------------
    static void onUdp(void* arg, udp_pcb* pcb, pbuf* p, const ip_addr_t* addr, u16_t port) {
        auto* self = static_cast<Impl*>(arg);
        const uint16_t tunnelPort = pcb->local_port;
        const UdpFlowKey key{lwip_ntohl(ip_2_ip4(addr)->addr), port, tunnelPort};
        if (self->enMemoria()) {
            // F5 (#18): se junta para la tanda; entregarARtc lo pasa entero al final de la vuelta.
            const size_t off = self->aRtcArena.size();
            self->aRtcArena.resize(off + p->tot_len);
            pbuf_copy_partial(p, self->aRtcArena.data() + off, p->tot_len, 0);
            self->aRtc.push_back({key.remoteIp, key.remotePort, tunnelPort, off, p->tot_len});
            pbuf_free(p);
            return;
        }
        auto it = self->udpFlows.find(key);
        if (it == self->udpFlows.end()) {
            const SockT s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (!sockValid(static_cast<long long>(s))) {
                pbuf_free(p);
                return;
            }
            // Cada invitado sale por su propio socket local: para el servicio
            // (libwebrtc) es un par distinto, y la respuesta vuelve a el.
            sockaddr_in any = loopback(0);
            bind(s, reinterpret_cast<const sockaddr*>(&any), sizeof(any));
            setNonBlocking(static_cast<long long>(s));
            it = self->udpFlows.emplace(key, UdpFlow{static_cast<long long>(s), 0}).first;
        }
        it->second.lastMs = nowMs();
        std::vector<char> buf(p->tot_len);
        pbuf_copy_partial(p, buf.data(), p->tot_len, 0);
        pbuf_free(p);
        const sockaddr_in to = loopback(tunnelPort);
        ba::diag::MedirPase medir(self->fHaciaRtc, buf.size());
        sendto(static_cast<SockT>(it->second.sock), buf.data(), static_cast<int>(buf.size()), 0,
               reinterpret_cast<const sockaddr*>(&to), sizeof(to));
    }

    void serviceUdpFlow(const UdpFlowKey& key, UdpFlow& f) {
        char buf[65536];
        for (;;) {
            const int n =
                static_cast<int>(recvfrom(static_cast<SockT>(f.sock), buf, sizeof(buf), 0, nullptr, nullptr));
            if (n <= 0) break;
            fDesdeRtc.pase(static_cast<uint64_t>(n));
            f.lastMs = nowMs();
            auto pcbIt = udpPcbs.find(key.tunnelPort);
            if (pcbIt == udpPcbs.end()) break;
            pbuf* p = pbuf_alloc(PBUF_TRANSPORT, static_cast<u16_t>(n), PBUF_RAM);
            if (p == nullptr) break;
            std::memcpy(p->payload, buf, static_cast<size_t>(n));
            ip_addr_t dst;
            IP_ADDR4(&dst, (key.remoteIp >> 24) & 255, (key.remoteIp >> 16) & 255, (key.remoteIp >> 8) & 255,
                     key.remoteIp & 255);
            udp_sendto(pcbIt->second, p, &dst, key.remotePort);
            pbuf_free(p);
        }
    }

    // --- Cliente (pruebas/diagnostico) ---------------------------------------------------
    /// Entrega la peticion por tramos segun el lugar que haya (tcp_write toma
    /// u16) y, al terminar, cierra el lado de escritura (el otro ve EOF).
    static void clientPump(ClientConn* c, tcp_pcb* pcb) {
        while (c->sent < c->request.size()) {
            const size_t room = tcp_sndbuf(pcb);
            if (room == 0) break;
            const size_t n = std::min({room, c->request.size() - c->sent, static_cast<size_t>(65535)});
            if (tcp_write(pcb, c->request.data() + c->sent, static_cast<u16_t>(n), TCP_WRITE_FLAG_COPY) !=
                ERR_OK)
                break;
            c->sent += n;
        }
        if (c->sent == c->request.size() && c->sent != SIZE_MAX) {
            tcp_shutdown(pcb, 0, 1);
            c->sent = SIZE_MAX; // ya cerrado
            return;
        }
        tcp_output(pcb);
    }
    static err_t clientConnected(void* arg, tcp_pcb* pcb, err_t) {
        clientPump(static_cast<ClientConn*>(arg), pcb);
        return ERR_OK;
    }
    static err_t clientSent(void* arg, tcp_pcb* pcb, u16_t) {
        auto* c = static_cast<ClientConn*>(arg);
        if (c != nullptr && c->sent != SIZE_MAX) clientPump(c, pcb);
        return ERR_OK;
    }
    static err_t clientRecv(void* arg, tcp_pcb* pcb, pbuf* p, err_t) {
        auto* c = static_cast<ClientConn*>(arg);
        if (p == nullptr) {
            c->done = true;
            c->ok = true;
            tcp_arg(pcb, nullptr);
            if (tcp_close(pcb) != ERR_OK) {
                tcp_abort(pcb);
                c->pcb = nullptr;
                c->promise.set_value(c->response);
                return ERR_ABRT;
            }
            c->pcb = nullptr;
            c->promise.set_value(c->response);
            return ERR_OK;
        }
        const size_t base = c->response.size();
        c->response.resize(base + p->tot_len);
        pbuf_copy_partial(p, c->response.data() + base, p->tot_len, 0);
        tcp_recved(pcb, p->tot_len);
        pbuf_free(p);
        return ERR_OK;
    }
    static void clientErr(void* arg, err_t) {
        auto* c = static_cast<ClientConn*>(arg);
        if (c == nullptr || c->done) return;
        c->done = true;
        c->pcb = nullptr;
        c->promise.set_value(c->response.empty() ? std::nullopt : std::optional<std::string>(c->response));
    }

    // --- Bucle de la pila ----------------------------------------------------------------
    void loop() {
        // Índices en `fds`: 0 = WireGuard, 1 = despertador, después los puentes TCP y los
        // flujos UDP por el kernel (solo sin sumidero), en el orden en que se agregaron.
        std::vector<size_t> idxPuente;
        std::vector<std::pair<UdpFlowKey, size_t>> idxFlujo;
        while (!stop.load()) {
            fds.clear();
            fds.push_back(pollDe(wgSock, POLLIN));
            fds.push_back(pollDe(despertador.lee, POLLIN));
            idxPuente.assign(bridges.size(), SIZE_MAX);
            for (size_t i = 0; i < bridges.size(); ++i) {
                const Bridge& b = *bridges[i];
                if (b.dead) continue;
                short ev = 0;
                if (b.connecting || !b.toSock.empty()) ev |= POLLOUT;
                if (!b.connecting && b.pcb != nullptr && !b.sockEof && tcp_sndbuf(b.pcb) > 0) ev |= POLLIN;
                if (ev == 0 && !b.connecting) continue;
                idxPuente[i] = fds.size();
                fds.push_back(pollDe(b.sock, ev == 0 ? static_cast<short>(POLLOUT) : ev));
            }
            idxFlujo.clear();
            for (auto& [k, f] : udpFlows) {
                idxFlujo.emplace_back(k, fds.size());
                fds.push_back(pollDe(f.sock, POLLIN));
            }
            u32_t sleep = sys_timeouts_sleeptime();
            if (sleep > 50) sleep = 50;
            esperar(fds, static_cast<int>(sleep));
            const auto revents = [&](size_t i) -> short { return i < fds.size() ? fds[i].revents : static_cast<short>(0); };

            if (revents(1) & POLLIN) despertador.vaciar();
            {
                std::lock_guard<std::mutex> lock(sumideroMu);
                sumideroVuelta = sumidero;
            }
            // Todo lo que se cifre en esta vuelta sale junto al final (#20).
            wg.empezarTanda();
            // #15-#17: WireGuard recibe en tanda y descifra, y cada paquete entra a lwIP
            // ahora mismo (entrada), en este hilo.
            const bool legible = (revents(0) & (POLLIN | POLLERR | POLLHUP)) != 0;
            if (legible) fDesdeWg.despertada();
            wg.servir(legible);
            // #18: lo que llegó para WebRTC, de una.
            entregarARtc();
            // #19: lo que WebRTC mandó.
            mandarDesdeRtc();
            // Trabajo de otros hilos.
            std::deque<std::function<void()>> work;
            {
                std::lock_guard<std::mutex> lock(qLock);
                work.swap(tasks);
            }
            for (auto& fn : work)
                fn();
            for (size_t i = 0; i < bridges.size(); ++i) {
                Bridge& b = *bridges[i];
                // Los que aceptó lwIP en esta vuelta no estaban en el poll: se atienden igual.
                const size_t k = i < idxPuente.size() ? idxPuente[i] : SIZE_MAX;
                const short re = k == SIZE_MAX ? static_cast<short>(0) : revents(k);
                const bool r = (re & POLLIN) != 0;
                const bool w = (re & POLLOUT) != 0;
                // Un connect() fallido: Windows lo avisa con POLLERR/POLLHUP (serviceBridge mira
                // SO_ERROR). Mientras conecta, solo vale lo que dijo poll(): con datos pendientes
                // (la cabecera PROXY) se daba por conectado un socket que no lo estaba. Linux
                // devuelve EAGAIN y lo tapaba; macOS da ENOTCONN y el puente se caia.
                const bool e = (re & (POLLERR | POLLHUP)) != 0;
                const bool writable = b.connecting ? (w || e) : (w || !b.toSock.empty());
                serviceBridge(b, r || (!b.connecting && !b.sockEof), writable);
            }
            const int64_t now = nowMs();
            // Por clave (en esta vuelta pudieron aparecer flujos nuevos y correr las posiciones).
            for (const auto& [clave, i] : idxFlujo)
                if (revents(i) & POLLIN)
                    if (auto it = udpFlows.find(clave); it != udpFlows.end()) serviceUdpFlow(it->first, it->second);
            for (auto it = udpFlows.begin(); it != udpFlows.end();) {
                if (now - it->second.lastMs > kUdpIdleMs) {
                    closeSock(it->second.sock);
                    it = udpFlows.erase(it);
                } else {
                    ++it;
                }
            }
            sys_check_timeouts();
            // Lo que lwIP mandó por sus temporizadores o por los puentes también va en la tanda.
            wg.terminarTanda();
            // Limpieza de puentes muertos.
            for (auto it = bridges.begin(); it != bridges.end();) {
                Bridge& b = **it;
                if (b.dead) {
                    if (b.pcb != nullptr) {
                        tcp_arg(b.pcb, nullptr);
                        tcp_abort(b.pcb);
                    }
                    closeSock(b.sock);
                    it = bridges.erase(it);
                } else {
                    ++it;
                }
            }
            clients.erase(std::remove_if(clients.begin(), clients.end(),
                                         [](const std::shared_ptr<ClientConn>& c) { return c->done; }),
                          clients.end());
            std::lock_guard<std::mutex> lock(statsLock);
            stats.packetsIn += entradasVuelta;
            entradasVuelta = 0;
            stats.tcpBridges = static_cast<int>(bridges.size());
            stats.udpFlows = static_cast<int>(udpFlows.size());
            stats.enMemoria = enMemoria();
        }
    }
};

IpStack::IpStack(WgDevice& wg, Config cfg, LogFn log)
    : impl_(std::make_unique<Impl>(wg, cfg, std::move(log))) {}

IpStack::~IpStack() { stop(); }

void IpStack::forwardTcp(uint16_t tunnelPort, uint16_t localPort, bool proxyHeader) {
    auto f = std::make_unique<Impl::TcpFwd>();
    f->impl = impl_.get();
    f->tunnelPort = tunnelPort;
    f->localPort = localPort;
    f->proxyHeader = proxyHeader;
    impl_->tcpFwds.push_back(std::move(f));
}

void IpStack::forwardUdpRange(uint16_t first, uint16_t last) {
    impl_->udpFirst = first;
    impl_->udpLast = last;
}

bool IpStack::start() {
    if (g_inUse.exchange(true)) return false; // lwIP es global
    Impl& d = *impl_;
    // Para despertar al hilo de la pila (tareas de otros hilos, la vuelta de WebRTC).
    if (!d.despertador.abrir()) {
        g_inUse = false;
        return false;
    }
    // F5: el bucle de WireGuard pasa a este hilo (recibir, descifrar y lwIP juntos).
    d.wgSock = d.wg.cederBucle();
    if (d.wgSock < 0) {
        d.wg.retomarBucle();
        d.despertador.cerrar();
        g_inUse = false;
        return false;
    }
    d.enMemoriaAjuste = ba::ajustes::siNo("gateway.wgEnMemoria", true);

    lwip_init();
    ip4_addr_t ip;
    ip4_addr_t mask;
    ip4_addr_t gw;
    ip4_addr_set_u32(&ip, lwip_htonl(d.cfg.tunnelIp));
    const uint32_t m = d.cfg.prefix <= 0 ? 0 : 0xFFFFFFFFu << (32 - d.cfg.prefix);
    ip4_addr_set_u32(&mask, lwip_htonl(m));
    ip4_addr_set_zero(&gw);
    netif_add(&d.nif, &ip, &mask, &gw, &d, &Impl::netifInit, ip4_input);
    netif_set_default(&d.nif);
    netif_set_up(&d.nif);
    netif_set_link_up(&d.nif);

    for (auto& f : d.tcpFwds) {
        tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
        if (pcb == nullptr || tcp_bind(pcb, IP_ADDR_ANY, f->tunnelPort) != ERR_OK) continue;
        f->listen = tcp_listen(pcb);
        tcp_arg(f->listen, f.get());
        tcp_accept(f->listen, &Impl::onAccept);
    }
    for (uint32_t p = d.udpFirst; d.udpFirst != 0 && p <= d.udpLast; ++p) {
        udp_pcb* pcb = udp_new_ip_type(IPADDR_TYPE_V4);
        if (pcb == nullptr || udp_bind(pcb, IP_ADDR_ANY, static_cast<u16_t>(p)) != ERR_OK) continue;
        udp_recv(pcb, &Impl::onUdp, &d);
        d.udpPcbs[static_cast<uint16_t>(p)] = pcb;
    }

    // Se llama en este mismo hilo (WgDevice::servir desde el bucle de la pila): sin cola.
    d.wg.setOnPacket([&d](const uint8_t* p, size_t n) { d.entrada(p, n); });
    d.stop = false;
    d.thread = std::thread([&d] {
        ba::platform::nombrarHilo("wireguard");
        ba::platform::nucleos::alGrupoGateway(); // F4: el bloque de caché del gateway
        d.loop();
    });
    if (d.log) {
        d.log(ba::i18n::llenar(ba::i18n::traducir("gateway", "pila IP: %1/%2 sobre WireGuard (%3 reenvios TCP, UDP %4-%5)"),
                               {ipToString(d.cfg.tunnelIp), std::to_string(d.cfg.prefix), std::to_string(d.tcpFwds.size()),
                                std::to_string(d.udpFirst), std::to_string(d.udpLast)}));
    }
    return true;
}

void IpStack::stop() {
    Impl& d = *impl_;
    if (!d.thread.joinable()) return;
    d.stop = true;
    d.wake();
    d.thread.join();
    // El bucle de WireGuard vuelve a su hilo propio, ya sin la pila.
    d.wg.setOnPacket({});
    d.wg.retomarBucle();
    d.wgSock = -1;
    {
        std::lock_guard<std::mutex> lock(d.sumideroMu);
        d.sumidero.reset();
    }
    d.sumideroVuelta.reset();
    for (auto& b : d.bridges) {
        if (b->pcb != nullptr) {
            tcp_arg(b->pcb, nullptr);
            tcp_abort(b->pcb);
        }
        closeSock(b->sock);
    }
    d.bridges.clear();
    for (auto& [k, f] : d.udpFlows)
        closeSock(f.sock);
    d.udpFlows.clear();
    for (auto& [p, pcb] : d.udpPcbs)
        udp_remove(pcb);
    d.udpPcbs.clear();
    for (auto& [id, u] : d.udpPropios)
        if (u->pcb != nullptr) udp_remove(u->pcb);
    d.udpPropios.clear();
    for (auto& f : d.tcpFwds) {
        if (f->listen != nullptr) tcp_close(f->listen);
        f->listen = nullptr;
    }
    netif_remove(&d.nif);
    d.despertador.cerrar();
    g_inUse = false;
}

void IpStack::setSumidero(std::shared_ptr<SumideroRtc> s) {
    Impl& d = *impl_;
    const bool hay = s != nullptr;
    {
        std::lock_guard<std::mutex> lock(d.sumideroMu);
        d.sumidero = std::move(s);
    }
    if (d.log)
        d.log(hay ?ba::i18n::traducir("gateway", "pila IP: lo del túnel va a WebRTC en memoria")
                         : ba::i18n::traducir("gateway", "pila IP: lo del túnel va a WebRTC por la red local"));
}

void IpStack::enviarDesdeRtc(uint16_t puertoRtc, uint32_t ipRemota, uint16_t puertoRemoto, const uint8_t* datos, size_t n) {
    if (datos == nullptr || n == 0 || n > 0xFFFF) return;
    Impl& d = *impl_;
    bool estaba = false;
    {
        std::lock_guard<std::mutex> lock(d.qLock);
        // Un tope: si la pila no da abasto, se pierde como en la red (no se acumula sin fin).
        if (d.desdeRtcArena.size() > 8u * 1024 * 1024) return;
        estaba = d.desdeRtc.empty();
        const size_t off = d.desdeRtcArena.size();
        d.desdeRtcArena.insert(d.desdeRtcArena.end(), datos, datos + n);
        d.desdeRtc.push_back({ipRemota, puertoRemoto, puertoRtc, off, n});
    }
    if (estaba) d.wake(); // una despertada por tanda
}

IpStack::Stats IpStack::stats() const {
    std::lock_guard<std::mutex> lock(impl_->statsLock);
    return impl_->stats;
}

std::optional<std::string> IpStack::tcpRequest(uint32_t ip, uint16_t port, const std::string& request,
                                               int timeoutMs) {
    auto c = std::make_shared<Impl::ClientConn>();
    c->request = request;
    auto fut = c->promise.get_future();
    Impl* d = impl_.get();
    d->post([d, c, ip, port]() {
        tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
        if (pcb == nullptr) {
            c->done = true;
            c->promise.set_value(std::nullopt);
            return;
        }
        c->pcb = pcb;
        tcp_arg(pcb, c.get());
        tcp_recv(pcb, &Impl::clientRecv);
        tcp_sent(pcb, &Impl::clientSent);
        tcp_err(pcb, &Impl::clientErr);
        ip_addr_t dst;
        IP_ADDR4(&dst, (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
        tcp_connect(pcb, &dst, port, &Impl::clientConnected);
        d->clients.push_back(c);
    });
    if (fut.wait_for(std::chrono::milliseconds(timeoutMs)) != std::future_status::ready) {
        d->post([c]() {
            if (!c->done && c->pcb != nullptr) {
                tcp_arg(c->pcb, nullptr);
                tcp_abort(c->pcb);
                c->pcb = nullptr;
                c->done = true;
                c->promise.set_value(std::nullopt);
            }
        });
        return std::nullopt;
    }
    return fut.get();
}

std::optional<std::string> IpStack::udpRequest(uint32_t ip, uint16_t port, const std::string& payload,
                                               int timeoutMs) {
    struct Req {
        udp_pcb* pcb = nullptr;
        std::promise<std::string> promise;
        bool done = false;
    };
    auto r = std::make_shared<Req>();
    auto fut = r->promise.get_future();
    Impl* d = impl_.get();
    d->post([r, ip, port, payload]() {
        r->pcb = udp_new_ip_type(IPADDR_TYPE_V4);
        udp_bind(r->pcb, IP_ADDR_ANY, 0);
        udp_recv(
            r->pcb,
            [](void* arg, udp_pcb*, pbuf* p, const ip_addr_t*, u16_t) {
                auto* rq = static_cast<Req*>(arg);
                std::string s(p->tot_len, '\0');
                pbuf_copy_partial(p, s.data(), p->tot_len, 0);
                pbuf_free(p);
                if (!rq->done) {
                    rq->done = true;
                    rq->promise.set_value(s);
                }
            },
            r.get());
        pbuf* p = pbuf_alloc(PBUF_TRANSPORT, static_cast<u16_t>(payload.size()), PBUF_RAM);
        std::memcpy(p->payload, payload.data(), payload.size());
        ip_addr_t dst;
        IP_ADDR4(&dst, (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
        udp_sendto(r->pcb, p, &dst, port);
        pbuf_free(p);
    });
    const bool ok = fut.wait_for(std::chrono::milliseconds(timeoutMs)) == std::future_status::ready;
    std::optional<std::string> out = ok ? std::optional<std::string>(fut.get()) : std::nullopt;
    d->post([r]() {
        if (r->pcb != nullptr) udp_remove(r->pcb);
        r->pcb = nullptr;
    });
    return out;
}

// --- UDP propio por el túnel (el medidor del enlace) ------------------------------------

int IpStack::udpAbrir(uint32_t ip, uint16_t port, LlegadaFn alLlegar) {
    Impl* d = impl_.get();
    if (!d->thread.joinable()) return 0;
    const int id = d->proximoUdp++;
    auto listo = std::make_shared<std::promise<bool>>();
    auto fut = listo->get_future();
    d->post([d, id, ip, port, listo, cb = std::move(alLlegar)]() mutable {
        auto u = std::make_unique<Impl::UdpPropio>();
        u->alLlegar = std::move(cb);
        u->pcb = udp_new_ip_type(IPADDR_TYPE_V4);
        ip_addr_t dst;
        IP_ADDR4(&dst, (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
        if (u->pcb == nullptr || udp_bind(u->pcb, IP_ADDR_ANY, 0) != ERR_OK || udp_connect(u->pcb, &dst, port) != ERR_OK) {
            if (u->pcb != nullptr) udp_remove(u->pcb);
            listo->set_value(false);
            return;
        }
        udp_recv(
            u->pcb,
            [](void* arg, udp_pcb*, pbuf* p, const ip_addr_t*, u16_t) {
                auto* up = static_cast<Impl::UdpPropio*>(arg);
                if (p->tot_len <= 2048) {
                    uint8_t buf[2048];
                    pbuf_copy_partial(p, buf, p->tot_len, 0);
                    if (up->alLlegar) up->alLlegar(buf, p->tot_len);
                }
                pbuf_free(p);
            },
            u.get());
        d->udpPropios[id] = std::move(u);
        listo->set_value(true);
    });
    if (fut.wait_for(std::chrono::seconds(2)) != std::future_status::ready || !fut.get()) {
        udpCerrar(id);
        return 0;
    }
    return id;
}

void IpStack::udpEnviar(int id, std::vector<std::string>&& lote) {
    Impl* d = impl_.get();
    d->post([d, id, l = std::move(lote)]() {
        const auto it = d->udpPropios.find(id);
        if (it == d->udpPropios.end() || it->second->pcb == nullptr) return;
        for (const std::string& s : l) {
            pbuf* p = pbuf_alloc(PBUF_TRANSPORT, static_cast<u16_t>(s.size()), PBUF_RAM);
            if (p == nullptr) continue; // sin memoria: se pierde (como en la red)
            std::memcpy(p->payload, s.data(), s.size());
            udp_send(it->second->pcb, p);
            pbuf_free(p);
        }
    });
}

void IpStack::udpCerrar(int id) {
    Impl* d = impl_.get();
    d->post([d, id]() {
        const auto it = d->udpPropios.find(id);
        if (it == d->udpPropios.end()) return;
        if (it->second->pcb != nullptr) udp_remove(it->second->pcb);
        d->udpPropios.erase(it);
    });
}

namespace {
/// El transporte del medidor por el túnel: lo que llega se anota con la hora en el hilo
/// de la pila y espera en una cola a que el medidor lo lea.
class TransporteTunel : public enlace::Transporte {
   public:
    struct Cola {
        std::mutex m;
        std::condition_variable cv;
        std::vector<enlace::Datagrama> v;
    };
    TransporteTunel(IpStack& pila, int id, std::shared_ptr<Cola> cola) : pila_(pila), id_(id), cola_(std::move(cola)) {}
    ~TransporteTunel() override { pila_.udpCerrar(id_); }
    bool enviar(std::vector<std::string>&& lote) override {
        pila_.udpEnviar(id_, std::move(lote));
        return true;
    }
    std::vector<enlace::Datagrama> recibir(int esperaMs) override {
        std::unique_lock<std::mutex> l(cola_->m);
        if (cola_->v.empty()) cola_->cv.wait_for(l, std::chrono::milliseconds(esperaMs), [this] { return !cola_->v.empty(); });
        std::vector<enlace::Datagrama> out;
        out.swap(cola_->v);
        return out;
    }

   private:
    IpStack& pila_;
    int id_;
    std::shared_ptr<Cola> cola_;
};
} // namespace

std::unique_ptr<enlace::Transporte> IpStack::transporteUdp(uint32_t ip, uint16_t port) {
    auto cola = std::make_shared<TransporteTunel::Cola>();
    const int id = udpAbrir(ip, port, [cola](const uint8_t* d, size_t n) {
        const int64_t t = enlace::ahoraUs();
        {
            std::lock_guard<std::mutex> l(cola->m);
            if (cola->v.size() < 65536) cola->v.push_back({std::string(reinterpret_cast<const char*>(d), n), t});
        }
        cola->cv.notify_one();
    });
    if (id == 0) return nullptr;
    return std::make_unique<TransporteTunel>(*this, id, cola);
}

} // namespace ba::gateway::net
