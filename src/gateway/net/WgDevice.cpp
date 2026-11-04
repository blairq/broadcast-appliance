// ============================================================================
// src/gateway/net/WgDevice.cpp — ver WgDevice.hpp
// ============================================================================

#include "net/WgDevice.hpp"
#include "security/Comparar.hpp"
#include "i18n/I18nTexto.hpp"
#include "platform/Hilo.hpp"
#include "platform/Nucleos.hpp"

#include <chrono>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using SockT = SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
using SockT = int;
#endif

namespace ba::gateway::net {

namespace {

constexpr char kConstruction[] = "Noise_IKpsk2_25519_ChaChaPoly_BLAKE2s";
constexpr char kIdentifier[] = "WireGuard v1 zx2c4 Jason@zx2c4.com";
constexpr char kLabelMac1[] = "mac1----";
constexpr char kLabelCookie[] = "cookie--";

constexpr size_t kInitLen = 148;
constexpr size_t kRespLen = 92;
constexpr size_t kCookieLen = 64;
constexpr size_t kDataHeader = 16;

constexpr int64_t kRekeyAfterMs = 120000;
constexpr int64_t kRejectAfterMs = 180000;
constexpr int64_t kRekeyAttemptMs = 90000;
constexpr int64_t kRekeyTimeoutMs = 5000;
constexpr int64_t kKeepaliveMs = 10000;
constexpr int64_t kCookieValidMs = 120000;
constexpr uint64_t kRejectAfterMessages = UINT64_MAX - (1ULL << 13) - 1;
constexpr size_t kMaxQueue = 64;

int64_t monoMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
int64_t wallMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void put32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        p[i] = static_cast<uint8_t>(v >> (8 * i));
}
uint32_t get32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
void put64(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; ++i)
        p[i] = static_cast<uint8_t>(v >> (8 * i));
}
uint64_t get64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= static_cast<uint64_t>(p[i]) << (8 * i);
    return v;
}

/// H = HASH(H || data)
void mixHash(Key32& h, const uint8_t* data, size_t n) {
    Key32 out{};
    hash(out, h.data(), 32, data, n);
    h = out;
}

/// En tiempo constante (security/Comparar.hpp, la única comparación de claves).
bool ctEqual(const uint8_t* a, const uint8_t* b, size_t n) {
    return ba::security::igualesEnTiempoConstante(std::string_view(reinterpret_cast<const char*>(a), n),
                                                  std::string_view(reinterpret_cast<const char*>(b), n));
}

void closeSock(long long s) {
    if (s < 0) return;
#ifdef _WIN32
    closesocket(static_cast<SockT>(s));
#else
    ::close(static_cast<SockT>(s));
#endif
}

} // namespace

// --- Ventana anti-replay (RFC 6479, 2048 contadores) -------------------------

bool WgDevice::Replay::ok(uint64_t c) const {
    if (c >= kRejectAfterMessages) return false;
    if (!any || c > greatest) return true;
    if (greatest - c >= 2048) return false;
    return !bits.test(static_cast<size_t>(c % 2048));
}

void WgDevice::Replay::mark(uint64_t c) {
    if (!any) {
        bits.reset();
        any = true;
        greatest = c;
    } else if (c > greatest) {
        const uint64_t diff = c - greatest;
        if (diff >= 2048) {
            bits.reset();
        } else {
            for (uint64_t i = greatest + 1; i < c; ++i)
                bits.reset(static_cast<size_t>(i % 2048));
        }
        greatest = c;
    }
    bits.set(static_cast<size_t>(c % 2048));
}

// --- Construccion ----------------------------------------------------------

WgDevice::WgDevice(WgConfig cfg, LogFn log) : cfg_(std::move(cfg)), log_(std::move(log)) {
    x25519Public(ourPub_, cfg_.privateKey);
    x25519(staticShared_, cfg_.privateKey, cfg_.peerPublicKey);
    hash(initChaining_, reinterpret_cast<const uint8_t*>(kConstruction), sizeof(kConstruction) - 1);
    hash(initHash_, initChaining_.data(), 32, reinterpret_cast<const uint8_t*>(kIdentifier),
         sizeof(kIdentifier) - 1);
    hash(mac1KeyPeer_, reinterpret_cast<const uint8_t*>(kLabelMac1), 8, cfg_.peerPublicKey.data(), 32);
    hash(mac1KeyOurs_, reinterpret_cast<const uint8_t*>(kLabelMac1), 8, ourPub_.data(), 32);
    hash(cookieKeyPeer_, reinterpret_cast<const uint8_t*>(kLabelCookie), 8, cfg_.peerPublicKey.data(), 32);
}

WgDevice::~WgDevice() {
    stop();
    wipe(cfg_.privateKey.data(), 32);
    wipe(staticShared_.data(), 32);
}

bool WgDevice::resolveEndpoint() {
    if (cfg_.endpointHost.empty()) return false;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(cfg_.endpointHost.c_str(), nullptr, &hints, &res) != 0 || res == nullptr) return false;
    epIp_ = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr.s_addr;
    epPort_ = cfg_.endpointPort;
    freeaddrinfo(res);
    return true;
}

bool WgDevice::start() {
#ifdef _WIN32
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    const SockT s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
    if (s == INVALID_SOCKET) return false;
#else
    if (s < 0) return false;
#endif
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(cfg_.listenPort);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        closeSock(static_cast<long long>(s));
        return false;
    }
    socklen_t len = sizeof(a);
    getsockname(s, reinterpret_cast<sockaddr*>(&a), &len);
    localPort_ = ntohs(a.sin_port);
    sock_ = static_cast<long long>(s);
    // Buffers grandes (rafagas de la pila IP) y no bloqueante (se vacia en lote).
    int big = 4 * 1024 * 1024;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&big), sizeof(big));
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&big), sizeof(big));
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    if (!cfg_.endpointHost.empty() && !resolveEndpoint() && log_) {
        log_(ba::i18n::llenar(ba::i18n::traducir("gateway", "wireguard: no se pudo resolver %1 (se reintenta)"),
                              {cfg_.endpointHost}));
    }
    stop_ = false;
    lastResolve_ = monoMs();
    thread_ = std::thread([this] {
        ba::platform::nombrarHilo("wireguard");
        ba::platform::nucleos::alGrupoGateway(); // F4: el bloque de caché del gateway
        loop();
    });
    if (log_)
        log_(ba::i18n::llenar(ba::i18n::traducir("gateway", "wireguard: escuchando UDP %1"), {std::to_string(localPort_)}));
    return true;
}

void WgDevice::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    std::lock_guard<std::mutex> lock(m_);
    closeSock(sock_);
    sock_ = -1;
    current_.reset();
    previous_.reset();
    next_.reset();
    hs_ = Handshake{};
    queue_.clear();
}

WgDevice::Stats WgDevice::stats() const {
    std::lock_guard<std::mutex> lock(m_);
    Stats s = stats_;
    const int64_t now = monoMs();
    s.up = current_ && now - current_->createdMs < kRejectAfterMs;
    if (epIp_ != 0) {
        char buf[INET_ADDRSTRLEN] = {0};
        in_addr ia{};
        ia.s_addr = epIp_;
        inet_ntop(AF_INET, &ia, buf, sizeof(buf));
        s.endpoint = std::string(buf) + ":" + std::to_string(epPort_);
    }
    return s;
}

// --- Bucle ------------------------------------------------------------------

void WgDevice::loop() {
    while (!stop_.load()) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(static_cast<SockT>(sock_), &rd);
        timeval tv{0, 50000};
        const int r = select(static_cast<int>(sock_) + 1, &rd, nullptr, nullptr, &tv);
        empezarTanda();
        servir(r > 0);
        terminarTanda();
    }
}

long long WgDevice::cederBucle() {
    if (thread_.joinable()) {
        stop_ = true;
        thread_.join();
        stop_ = false;
    }
    externo_ = true;
    return sock_;
}

void WgDevice::retomarBucle() {
    if (!externo_) return;
    externo_ = false;
    if (sock_ >= 0 && !thread_.joinable()) {
        stop_ = false;
        thread_ = std::thread([this] {
            ba::platform::nombrarHilo("wireguard");
            ba::platform::nucleos::alGrupoGateway();
            loop();
        });
    }
}

void WgDevice::setDescifradorParalelo(std::shared_ptr<DescifradorParalelo> d) {
    std::lock_guard<std::mutex> lock(m_);
    paralelo_ = std::move(d);
}

void DescifradoWg::descifrar() { ok = largo >= 16 && aeadDecrypt(plano, clave, contador, cifrado, largo, nullptr, 0); }

int WgDevice::recibirTanda() {
    if (rxBuf_.size() < kTanda * kRxMax) rxBuf_.resize(kTanda * kRxMax);
    rx_.clear();
#if defined(__linux__)
    // #15: una llamada al sistema por tanda.
    mmsghdr msgs[kTanda];
    iovec iov[kTanda];
    sockaddr_in from[kTanda];
    std::memset(msgs, 0, sizeof msgs);
    for (size_t i = 0; i < kTanda; ++i) {
        iov[i].iov_base = rxBuf_.data() + i * kRxMax;
        iov[i].iov_len = kRxMax;
        msgs[i].msg_hdr.msg_iov = &iov[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
        msgs[i].msg_hdr.msg_name = &from[i];
        msgs[i].msg_hdr.msg_namelen = sizeof(from[i]);
    }
    const int n = recvmmsg(static_cast<SockT>(sock_), msgs, static_cast<unsigned>(kTanda), MSG_DONTWAIT, nullptr);
    for (int i = 0; i < n; ++i) {
        if (msgs[i].msg_len == 0 || (msgs[i].msg_hdr.msg_flags & MSG_TRUNC)) continue;
        rx_.push_back({static_cast<size_t>(i) * kRxMax, msgs[i].msg_len, from[i].sin_addr.s_addr, ntohs(from[i].sin_port)});
    }
#else
    for (size_t i = 0; i < kTanda; ++i) {
        sockaddr_in from{};
        socklen_t fl = sizeof(from);
        const int n = static_cast<int>(recvfrom(static_cast<SockT>(sock_), reinterpret_cast<char*>(rxBuf_.data() + i * kRxMax),
                                                static_cast<int>(kRxMax), 0, reinterpret_cast<sockaddr*>(&from), &fl));
        if (n <= 0) break;
        rx_.push_back({i * kRxMax, static_cast<size_t>(n), from.sin_addr.s_addr, ntohs(from.sin_port)});
    }
#endif
    return static_cast<int>(rx_.size());
}

void WgDevice::servir(bool legible) {
    const int64_t now = monoMs();
    std::unique_lock<std::mutex> lock(m_);
    if (legible && sock_ >= 0) {
        // Se vacía todo lo que haya, de a tandas: de a un datagrama por vuelta, una ráfaga de
        // TCP desborda el búfer del socket y se pierde. Tope de 8 tandas por vuelta (512):
        // los temporizadores y lo demás del hilo también tienen que correr.
        for (int vuelta = 0; vuelta < 8; ++vuelta) {
            lock.unlock();
            const int n = recibirTanda();
            lock.lock();
            for (int i = 0; i < n; ++i) {
                const Rx& r = rx_[static_cast<size_t>(i)];
                const uint8_t* d = rxBuf_.data() + r.off;
                if (r.n < 4) continue;
                stats_.rxBytes += r.n;
                if (d[0] == 4) {
                    prepararDatoLocked(d, r.n, r.ip, r.port, now);
                    continue;
                }
                // Un mensaje de handshake cambia las sesiones: los datos que llegaron antes
                // se entregan antes (el orden de la tanda se respeta).
                procesarTandaLocked(lock, now);
                switch (d[0]) {
                    case 1: handleInitiation(d, r.n, r.ip, r.port, now); break;
                    case 2: handleResponse(d, r.n, r.ip, r.port, now); break;
                    case 3: handleCookie(d, r.n); break;
                    default: break;
                }
            }
            procesarTandaLocked(lock, now);
            if (n < static_cast<int>(kTanda)) break;
        }
    }
    if (epIp_ == 0 && !cfg_.endpointHost.empty() && now - lastResolve_ > 5000) {
        lastResolve_ = now;
        resolveEndpoint();
    }
    tick(now);
}

void WgDevice::empezarTanda() {
    std::lock_guard<std::mutex> lock(m_);
    enTanda_ = true;
}

void WgDevice::terminarTanda() {
    std::lock_guard<std::mutex> lock(m_);
    enTanda_ = false;
    enviarSalidaLocked();
}

void WgDevice::enviarSalidaLocked() {
    if (salidaLargos_.empty()) return;
    if (sock_ >= 0 && epIp_ != 0) {
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_addr.s_addr = epIp_;
        to.sin_port = htons(epPort_);
#if defined(__linux__)
        // #20: una llamada al sistema por tanda.
        mmsghdr msgs[kTanda];
        iovec iov[kTanda];
        size_t off = 0, i = 0;
        while (i < salidaLargos_.size()) {
            size_t k = 0;
            for (; k < kTanda && i + k < salidaLargos_.size(); ++k) {
                std::memset(&msgs[k], 0, sizeof(mmsghdr));
                iov[k].iov_base = salida_.data() + off;
                iov[k].iov_len = salidaLargos_[i + k];
                off += salidaLargos_[i + k];
                msgs[k].msg_hdr.msg_iov = &iov[k];
                msgs[k].msg_hdr.msg_iovlen = 1;
                msgs[k].msg_hdr.msg_name = &to;
                msgs[k].msg_hdr.msg_namelen = sizeof(to);
            }
            size_t hecho = 0;
            while (hecho < k) {
                const int r = sendmmsg(static_cast<SockT>(sock_), msgs + hecho, static_cast<unsigned>(k - hecho), 0);
                if (r <= 0) break; // lleno o error: lo que queda se pierde (como en la red)
                hecho += static_cast<size_t>(r);
            }
            i += k;
        }
#else
        size_t off = 0;
        for (size_t n : salidaLargos_) {
            sendto(static_cast<SockT>(sock_), reinterpret_cast<const char*>(salida_.data() + off), static_cast<int>(n), 0,
                   reinterpret_cast<sockaddr*>(&to), sizeof(to));
            off += n;
        }
#endif
    }
    salida_.clear();
    salidaLargos_.clear();
}

void WgDevice::tick(int64_t now) {
    // Sesiones vencidas: afuera.
    if (current_ && now - current_->createdMs >= kRejectAfterMs) current_.reset();
    if (previous_ && now - previous_->createdMs >= kRejectAfterMs) previous_.reset();
    if (next_ && now - next_->createdMs >= kRejectAfterMs) next_.reset();

    // Handshake en curso: reintento cada 5 s (+jitter) hasta 90 s.
    if (hs_.active && now - hs_.sentMs >= kRekeyTimeoutMs) {
        if (now - hs_.startedMs >= kRekeyAttemptMs) {
            hs_.active = false;
            queue_.clear();
            if (log_) log_(ba::i18n::traducir("gateway", "wireguard: el peer no responde; se reintenta con el próximo tráfico"));
        } else {
            sendInitiationLocked(now);
        }
    }
    // Datos sin respuesta por 15 s (keepalive + rekey timeout): la sesion
    // puede estar muerta del otro lado; se renegocia.
    if (unansweredSinceMs_ != 0 && !hs_.active &&
        now - unansweredSinceMs_ >= kKeepaliveMs + kRekeyTimeoutMs) {
        unansweredSinceMs_ = 0;
        sendInitiationLocked(now);
    }
    // Keepalive persistente (mantener abierto el NAT) y pasivo (confirmar
    // que recibimos cuando no tenemos nada para mandar).
    const bool persistent = cfg_.persistentKeepaliveSec > 0 &&
                            now - lastSentMs_ >= static_cast<int64_t>(cfg_.persistentKeepaliveSec) * 1000;
    const bool passive = lastDataRecvMs_ > lastSentMs_ && now - lastDataRecvMs_ >= kKeepaliveMs;
    if ((persistent || passive) && epIp_ != 0) {
        sendDataLocked(nullptr, 0, now);
    }
}

// --- Envio ----------------------------------------------------------------------

void WgDevice::sendRawLocked(const uint8_t* d, size_t n) {
    if (sock_ < 0 || epIp_ == 0) return;
    if (enTanda_) {
        // #20: se junta y sale con la tanda (terminarTanda).
        salida_.insert(salida_.end(), d, d + n);
        salidaLargos_.push_back(n);
        lastSentMs_ = monoMs();
        stats_.txBytes += n;
        if (salidaLargos_.size() >= 4 * kTanda) enviarSalidaLocked();
        return;
    }
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = epIp_;
    to.sin_port = htons(epPort_);
    sendto(static_cast<SockT>(sock_), reinterpret_cast<const char*>(d), static_cast<int>(n), 0,
           reinterpret_cast<sockaddr*>(&to), sizeof(to));
    lastSentMs_ = monoMs();
    stats_.txBytes += n;
}

uint32_t WgDevice::newIndex() {
    uint32_t idx = 0;
    while (idx == 0)
        randomBytes(reinterpret_cast<uint8_t*>(&idx), sizeof(idx));
    return idx;
}

void WgDevice::sendInitiationLocked(int64_t now) {
    if (epIp_ == 0) return;
    uint8_t msg[kInitLen] = {0};
    msg[0] = 1;
    if (!hs_.active) hs_.startedMs = now;
    hs_.active = true;
    hs_.sentMs = now + static_cast<int64_t>((now * 7919) % 334); // jitter
    hs_.localIndex = newIndex();
    put32(msg + 4, hs_.localIndex);

    Key32 c = initChaining_;
    Key32 h = initHash_;
    mixHash(h, cfg_.peerPublicKey.data(), 32);
    Key32 ephPub{};
    x25519Generate(hs_.ephPriv, ephPub);
    Key32 t{};
    kdf1(t, c, ephPub.data(), 32);
    c = t;
    std::memcpy(msg + 8, ephPub.data(), 32);
    mixHash(h, ephPub.data(), 32);
    Key32 dh{};
    Key32 k{};
    x25519(dh, hs_.ephPriv, cfg_.peerPublicKey);
    kdf2(t, k, c, dh.data(), 32);
    c = t;
    aeadEncrypt(msg + 40, k, 0, ourPub_.data(), 32, h.data(), 32);
    mixHash(h, msg + 40, 48);
    kdf2(t, k, c, staticShared_.data(), 32);
    c = t;
    uint8_t ts[12];
    tai64n(ts);
    aeadEncrypt(msg + 88, k, 0, ts, 12, h.data(), 32);
    mixHash(h, msg + 88, 28);
    hs_.chaining = c;
    hs_.hash = h;

    mac(msg + 116, mac1KeyPeer_.data(), 32, msg, 116);
    std::memcpy(lastMac1_.data(), msg + 116, 16);
    if (cookieMs_ != 0 && now - cookieMs_ < kCookieValidMs) {
        mac(msg + 132, cookie_.data(), 16, msg, 132);
    }
    wipe(dh.data(), 32);
    wipe(k.data(), 32);
    sendRawLocked(msg, sizeof(msg));
}

bool WgDevice::sendDataLocked(const uint8_t* p, size_t n, int64_t now) {
    if (n > 65535) return false; // un paquete IP no puede ser mas grande
    Keypair* kp = current_.get();
    const bool usable =
        kp != nullptr && now - kp->createdMs < kRejectAfterMs && kp->sendCounter < kRejectAfterMessages;
    if (!usable) {
        if (n > 0) {
            if (queue_.size() >= kMaxQueue) queue_.pop_front();
            queue_.emplace_back(p, p + n);
        }
        if (!hs_.active) sendInitiationLocked(now);
        return n > 0;
    }
    // Rekey preventivo del lado iniciador (sin frenar este envio).
    if (kp->initiator && !hs_.active && now - kp->createdMs >= kRekeyAfterMs) sendInitiationLocked(now);

    const size_t padded = (n + 15) & ~static_cast<size_t>(15);
    // Búferes que se reusan (con m_): sin un std::vector nuevo por paquete.
    std::vector<uint8_t>& plain = planoEnvio_;
    plain.resize(padded);
    if (n > 0) std::memcpy(plain.data(), p, n);
    if (padded > n) std::memset(plain.data() + n, 0, padded - n);
    std::vector<uint8_t>& msg = msgEnvio_;
    msg.resize(kDataHeader + padded + 16);
    std::memset(msg.data(), 0, kDataHeader);
    msg[0] = 4;
    put32(msg.data() + 4, kp->remoteIndex);
    const uint64_t counter = kp->sendCounter++;
    put64(msg.data() + 8, counter);
    if (!aeadEncrypt(msg.data() + kDataHeader, kp->sendKey, counter, plain.data(), padded, nullptr, 0))
        return false;
    sendRawLocked(msg.data(), msg.size());
    if (n > 0) {
        lastDataSentMs_ = now;
        if (unansweredSinceMs_ == 0) unansweredSinceMs_ = now;
    }
    return true;
}

void WgDevice::flushQueueLocked(int64_t now) {
    while (!queue_.empty()) {
        std::vector<uint8_t> p = std::move(queue_.front());
        queue_.pop_front();
        sendDataLocked(p.data(), p.size(), now);
    }
}

bool WgDevice::send(const uint8_t* packet, size_t n) {
    if (packet == nullptr || n == 0) return false;
    std::lock_guard<std::mutex> lock(m_);
    return sendDataLocked(packet, n, monoMs());
}

// --- Recepcion ----------------------------------------------------------------

WgDevice::Keypair* WgDevice::keypairFor(uint32_t idx) {
    for (auto* kp : {current_.get(), previous_.get(), next_.get()}) {
        if (kp != nullptr && kp->localIndex == idx) return kp;
    }
    return nullptr;
}

void WgDevice::handleInitiation(const uint8_t* d, size_t n, uint32_t ip, uint16_t port, int64_t now) {
    if (n != kInitLen) return;
    uint8_t m1[16];
    mac(m1, mac1KeyOurs_.data(), 32, d, 116);
    if (!ctEqual(m1, d + 116, 16)) return;

    Key32 c = initChaining_;
    Key32 h = initHash_;
    mixHash(h, ourPub_.data(), 32);
    Key32 ei{};
    std::memcpy(ei.data(), d + 8, 32);
    Key32 t{};
    kdf1(t, c, ei.data(), 32);
    c = t;
    mixHash(h, ei.data(), 32);
    Key32 dh{};
    Key32 k{};
    if (!x25519(dh, cfg_.privateKey, ei)) return;
    kdf2(t, k, c, dh.data(), 32);
    c = t;
    Key32 si{};
    if (!aeadDecrypt(si.data(), k, 0, d + 40, 48, h.data(), 32)) return;
    // Un solo peer: la estatica del iniciador tiene que ser la configurada.
    if (!ctEqual(si.data(), cfg_.peerPublicKey.data(), 32)) return;
    mixHash(h, d + 40, 48);
    kdf2(t, k, c, staticShared_.data(), 32);
    c = t;
    uint8_t ts[12];
    if (!aeadDecrypt(ts, k, 0, d + 88, 28, h.data(), 32)) return;
    // Replay de iniciaciones: el TAI64N debe crecer.
    if (std::memcmp(ts, lastTimestamp_.data(), 12) <= 0) return;
    std::memcpy(lastTimestamp_.data(), ts, 12);
    mixHash(h, d + 88, 28);

    // --- Respuesta -------------------------------------------------------------
    uint8_t r[kRespLen] = {0};
    r[0] = 2;
    const uint32_t local = newIndex();
    put32(r + 4, local);
    std::memcpy(r + 8, d + 4, 4); // receiver = sender del iniciador
    Key32 ephPriv{};
    Key32 ephPub{};
    x25519Generate(ephPriv, ephPub);
    kdf1(t, c, ephPub.data(), 32);
    c = t;
    std::memcpy(r + 12, ephPub.data(), 32);
    mixHash(h, ephPub.data(), 32);
    x25519(dh, ephPriv, ei);
    kdf1(t, c, dh.data(), 32);
    c = t;
    x25519(dh, ephPriv, cfg_.peerPublicKey);
    kdf1(t, c, dh.data(), 32);
    c = t;
    Key32 tau{};
    kdf3(t, tau, k, c, cfg_.presharedKey.data(), 32);
    c = t;
    mixHash(h, tau.data(), 32);
    aeadEncrypt(r + 44, k, 0, nullptr, 0, h.data(), 32);
    mixHash(h, r + 44, 16);
    Key32 mac1Key{};
    hash(mac1Key, reinterpret_cast<const uint8_t*>(kLabelMac1), 8, cfg_.peerPublicKey.data(), 32);
    mac(r + 60, mac1Key.data(), 32, r, 60);

    auto kp = std::make_unique<Keypair>();
    kp->localIndex = local;
    kp->remoteIndex = get32(d + 4);
    kdf2(kp->recvKey, kp->sendKey, c, nullptr, 0); // respondedor: (recv, send)
    kp->createdMs = now;
    kp->initiator = false;
    next_ = std::move(kp);

    epIp_ = ip;
    epPort_ = port;
    wipe(ephPriv.data(), 32);
    wipe(dh.data(), 32);
    wipe(k.data(), 32);
    sendRawLocked(r, sizeof(r));
    stats_.handshakes++;
    stats_.lastHandshakeMs = wallMs();
}

void WgDevice::handleResponse(const uint8_t* d, size_t n, uint32_t ip, uint16_t port, int64_t now) {
    if (n != kRespLen || !hs_.active || get32(d + 8) != hs_.localIndex) return;
    uint8_t m1[16];
    mac(m1, mac1KeyOurs_.data(), 32, d, 60);
    if (!ctEqual(m1, d + 60, 16)) return;

    Key32 c = hs_.chaining;
    Key32 h = hs_.hash;
    Key32 er{};
    std::memcpy(er.data(), d + 12, 32);
    Key32 t{};
    kdf1(t, c, er.data(), 32);
    c = t;
    mixHash(h, er.data(), 32);
    Key32 dh{};
    if (!x25519(dh, hs_.ephPriv, er)) return;
    kdf1(t, c, dh.data(), 32);
    c = t;
    if (!x25519(dh, cfg_.privateKey, er)) return;
    kdf1(t, c, dh.data(), 32);
    c = t;
    Key32 tau{};
    Key32 k{};
    kdf3(t, tau, k, c, cfg_.presharedKey.data(), 32);
    c = t;
    mixHash(h, tau.data(), 32);
    uint8_t empty[1];
    if (!aeadDecrypt(empty, k, 0, d + 44, 16, h.data(), 32)) return;

    auto kp = std::make_unique<Keypair>();
    kp->localIndex = hs_.localIndex;
    kp->remoteIndex = get32(d + 4);
    kdf2(kp->sendKey, kp->recvKey, c, nullptr, 0); // iniciador: (send, recv)
    kp->createdMs = now;
    kp->initiator = true;
    previous_ = std::move(current_);
    current_ = std::move(kp);
    wipe(hs_.ephPriv.data(), 32);
    hs_ = Handshake{};
    wipe(dh.data(), 32);
    wipe(k.data(), 32);
    epIp_ = ip;
    epPort_ = port;
    stats_.handshakes++;
    stats_.lastHandshakeMs = wallMs();
    if (log_) log_(ba::i18n::traducir("gateway", "wireguard: sesión establecida"));
    // El respondedor confirma la sesion con el primer dato: si no hay nada
    // encolado, un keepalive.
    if (queue_.empty()) {
        sendDataLocked(nullptr, 0, now);
    } else {
        flushQueueLocked(now);
    }
}

void WgDevice::handleCookie(const uint8_t* d, size_t n) {
    if (n != kCookieLen || !hs_.active || get32(d + 4) != hs_.localIndex) return;
    uint8_t ck[16];
    if (!xaeadDecrypt(ck, cookieKeyPeer_, d + 8, d + 32, 32, lastMac1_.data(), 16)) return;
    std::memcpy(cookie_.data(), ck, 16);
    cookieMs_ = monoMs();
    if (log_) log_(ba::i18n::traducir("gateway", "wireguard: el servidor pidió cookie (bajo carga); se reintenta con MAC2"));
}

void WgDevice::prepararDatoLocked(const uint8_t* d, size_t n, uint32_t ip, uint16_t port, int64_t now) {
    if (n < kDataHeader + 16) return;
    const uint32_t idx = get32(d + 4);
    Keypair* kp = keypairFor(idx);
    if (kp == nullptr || now - kp->createdMs >= kRejectAfterMs) return;
    const uint64_t counter = get64(d + 8);
    if (!kp->replay.ok(counter)) return; // se vuelve a mirar al entregar (dos iguales en la tanda)
    DescifradoWg x;
    x.cifrado = d + kDataHeader;
    x.largo = n - kDataHeader;
    x.contador = counter;
    x.clave = kp->recvKey;
    descifrar_.push_back(x);
    // El lugar del descifrado: el puntero se pone al procesar (planoTanda_ puede crecer).
    DatoTanda t;
    t.localIndex = idx;
    t.ip = ip;
    t.port = port;
    t.planoOff = planoTanda_.size();
    planoTanda_.resize(planoTanda_.size() + (n - kDataHeader - 16));
    datos_.push_back(t);
}

void WgDevice::procesarTandaLocked(std::unique_lock<std::mutex>& lock, int64_t now) {
    if (datos_.empty()) return;
    for (size_t i = 0; i < descifrar_.size(); ++i) descifrar_[i].plano = planoTanda_.data() + datos_[i].planoOff;
    // #16: en este hilo, o repartido (cada uno es independiente); el orden se mantiene abajo.
    if (paralelo_) {
        const std::shared_ptr<DescifradorParalelo> p = paralelo_;
        lock.unlock();
        p->descifrar(descifrar_.data(), descifrar_.size());
        lock.lock();
    } else {
        for (DescifradoWg& x : descifrar_) x.descifrar();
    }
    // Se entrega en el orden en que llegó. Las sesiones se buscan de nuevo por su índice (un
    // dato puede confirmar la sesión nueva y soltar la vieja: nada de punteros guardados).
    const PacketFn fn = onPacket_;
    for (size_t i = 0; i < datos_.size(); ++i) {
        DescifradoWg& x = descifrar_[i];
        const DatoTanda& t = datos_[i];
        if (!x.ok) {
            wipe(x.clave.data(), 32);
            continue;
        }
        wipe(x.clave.data(), 32);
        Keypair* kp = keypairFor(t.localIndex);
        if (kp == nullptr || !kp->replay.ok(x.contador)) continue;
        kp->replay.mark(x.contador);
        // Respondedor: el primer dato con la sesion nueva la confirma.
        if (kp == next_.get()) {
            previous_ = std::move(current_);
            current_ = std::move(next_);
            flushQueueLocked(now);
        }
        epIp_ = t.ip; // roaming
        epPort_ = t.port;
        lastRecvMs_ = now;
        unansweredSinceMs_ = 0;
        const size_t plano = x.largo - 16;
        if (plano == 0) continue; // keepalive
        lastDataRecvMs_ = now;
        // Largo real del paquete IP (el resto es relleno a 16).
        const uint8_t* p = x.plano;
        size_t len = plano;
        if (len >= 20 && (p[0] >> 4) == 4) {
            const size_t total = (static_cast<size_t>(p[2]) << 8) | p[3];
            if (total >= 20 && total <= len) len = total;
        }
        // FUERA del candado: la pila IP puede contestar (send) desde el mismo callback.
        if (fn) {
            lock.unlock();
            fn(p, len);
            lock.lock();
        }
    }
    descifrar_.clear();
    datos_.clear();
    planoTanda_.clear();
}

} // namespace ba::gateway::net
