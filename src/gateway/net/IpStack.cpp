// ============================================================================
// src/gateway/net/IpStack.cpp — ver IpStack.hpp
// ============================================================================

#include "net/IpStack.hpp"

#include <algorithm>
#include <chrono>
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

#include "net/WgCrypto.hpp"
#include "net/WgDevice.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using SockT = SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using SockT = int;
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

    std::mutex qLock;
    std::deque<std::vector<uint8_t>> inbound;
    std::deque<std::function<void()>> tasks;
    long long wakeSock = -1;
    uint16_t wakePort = 0;

    std::atomic<bool> stop{false};
    std::thread thread;
    mutable std::mutex statsLock;
    Stats stats;

    Impl(WgDevice& w, Config c, LogFn l) : wg(w), cfg(c), log(std::move(l)) {}

    void wake() {
        if (wakeSock < 0) return;
        const sockaddr_in a = loopback(wakePort);
        const char b = 1;
        sendto(static_cast<SockT>(wakeSock), &b, 1, 0, reinterpret_cast<const sockaddr*>(&a), sizeof(a));
    }

    void post(std::function<void()> fn) {
        {
            std::lock_guard<std::mutex> lock(qLock);
            tasks.push_back(std::move(fn));
        }
        wake();
    }

    // --- netif ----------------------------------------------------------------------
    static err_t netifOutput(netif* n, pbuf* p, const ip4_addr_t*) {
        auto* self = static_cast<Impl*>(n->state);
        std::vector<uint8_t> buf(p->tot_len);
        pbuf_copy_partial(p, buf.data(), p->tot_len, 0);
        self->wg.send(buf.data(), buf.size());
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
        sendto(static_cast<SockT>(it->second.sock), buf.data(), static_cast<int>(buf.size()), 0,
               reinterpret_cast<const sockaddr*>(&to), sizeof(to));
    }

    void serviceUdpFlow(const UdpFlowKey& key, UdpFlow& f) {
        char buf[65536];
        for (;;) {
            const int n =
                static_cast<int>(recvfrom(static_cast<SockT>(f.sock), buf, sizeof(buf), 0, nullptr, nullptr));
            if (n <= 0) break;
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
        std::vector<uint8_t> scratch(2048);
        while (!stop.load()) {
            fd_set rd;
            fd_set wr;
            FD_ZERO(&rd);
            FD_ZERO(&wr);
            long long maxFd = wakeSock;
            FD_SET(static_cast<SockT>(wakeSock), &rd);
            for (auto& b : bridges) {
                if (b->dead) continue;
                if (b->connecting || !b->toSock.empty()) FD_SET(static_cast<SockT>(b->sock), &wr);
                if (!b->connecting && b->pcb != nullptr && !b->sockEof && tcp_sndbuf(b->pcb) > 0) {
                    FD_SET(static_cast<SockT>(b->sock), &rd);
                }
                maxFd = std::max(maxFd, b->sock);
            }
            for (auto& [k, f] : udpFlows) {
                FD_SET(static_cast<SockT>(f.sock), &rd);
                maxFd = std::max(maxFd, f.sock);
            }
            u32_t sleep = sys_timeouts_sleeptime();
            if (sleep > 50) sleep = 50;
            // tv_usec es long en Linux/Windows e int en macOS.
            timeval tv{0, static_cast<decltype(timeval::tv_usec)>(sleep * 1000)};
            select(static_cast<int>(maxFd) + 1, &rd, &wr, nullptr, &tv);

            if (FD_ISSET(static_cast<SockT>(wakeSock), &rd)) {
                char b[256];
                while (recv(static_cast<SockT>(wakeSock), b, sizeof(b), 0) > 0) {
                }
            }
            // Paquetes que trajo el tunel y trabajo de otros threads.
            std::deque<std::vector<uint8_t>> in;
            std::deque<std::function<void()>> work;
            {
                std::lock_guard<std::mutex> lock(qLock);
                in.swap(inbound);
                work.swap(tasks);
            }
            for (auto& pkt : in) {
                pbuf* p = pbuf_alloc(PBUF_RAW, static_cast<u16_t>(pkt.size()), PBUF_POOL);
                if (p == nullptr) continue;
                pbuf_take(p, pkt.data(), static_cast<u16_t>(pkt.size()));
                if (nif.input(p, &nif) != ERR_OK) pbuf_free(p);
            }
            for (auto& fn : work)
                fn();
            for (auto& b : bridges) {
                const bool r = FD_ISSET(static_cast<SockT>(b->sock), &rd);
                const bool w = FD_ISSET(static_cast<SockT>(b->sock), &wr);
                // Mientras conecta, solo vale lo que dijo select(): con datos
                // pendientes (la cabecera PROXY) se daba por conectado un socket
                // que no lo estaba. Linux devuelve EAGAIN y lo tapaba; macOS da
                // ENOTCONN y el puente se caia.
                const bool writable = b->connecting ? w : (w || !b->toSock.empty());
                serviceBridge(*b, r || (!b->connecting && !b->sockEof), writable);
            }
            const int64_t now = nowMs();
            for (auto it = udpFlows.begin(); it != udpFlows.end();) {
                if (FD_ISSET(static_cast<SockT>(it->second.sock), &rd)) serviceUdpFlow(it->first, it->second);
                if (now - it->second.lastMs > kUdpIdleMs) {
                    closeSock(it->second.sock);
                    it = udpFlows.erase(it);
                } else {
                    ++it;
                }
            }
            sys_check_timeouts();
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
            stats.packetsIn += in.size();
            stats.tcpBridges = static_cast<int>(bridges.size());
            stats.udpFlows = static_cast<int>(udpFlows.size());
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
    // Socket para despertar al thread de la pila.
    const SockT ws = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in a = loopback(0);
    bind(ws, reinterpret_cast<const sockaddr*>(&a), sizeof(a));
    socklen_t len = sizeof(a);
    getsockname(ws, reinterpret_cast<sockaddr*>(&a), &len);
    d.wakeSock = static_cast<long long>(ws);
    d.wakePort = ntohs(a.sin_port);
    setNonBlocking(d.wakeSock);

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

    d.wg.setOnPacket([&d](const uint8_t* p, size_t n) {
        bool wasEmpty = false;
        {
            std::lock_guard<std::mutex> lock(d.qLock);
            wasEmpty = d.inbound.empty();
            if (d.inbound.size() < 8192) d.inbound.emplace_back(p, p + n);
        }
        if (wasEmpty) d.wake(); // un despertar por lote, no por paquete
    });
    d.stop = false;
    d.thread = std::thread([&d] { d.loop(); });
    if (d.log) {
        d.log("pila IP: " + ipToString(d.cfg.tunnelIp) + "/" + std::to_string(d.cfg.prefix) +
              " sobre WireGuard (" + std::to_string(d.tcpFwds.size()) + " reenvios TCP, UDP " +
              std::to_string(d.udpFirst) + "-" + std::to_string(d.udpLast) + ")");
    }
    return true;
}

void IpStack::stop() {
    Impl& d = *impl_;
    if (!d.thread.joinable()) return;
    d.wg.setOnPacket({});
    d.stop = true;
    d.wake();
    d.thread.join();
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
    for (auto& f : d.tcpFwds) {
        if (f->listen != nullptr) tcp_close(f->listen);
        f->listen = nullptr;
    }
    netif_remove(&d.nif);
    closeSock(d.wakeSock);
    d.wakeSock = -1;
    g_inUse = false;
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

} // namespace ba::gateway::net
