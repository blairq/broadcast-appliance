// ============================================================================
// src/gateway/net/WgDevice.cpp — ver WgDevice.hpp
// ============================================================================

#include "net/WgDevice.hpp"
#include "i18n/I18n.hpp"

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

bool ctEqual(const uint8_t* a, const uint8_t* b, size_t n) {
    uint8_t d = 0;
    for (size_t i = 0; i < n; ++i)
        d |= static_cast<uint8_t>(a[i] ^ b[i]);
    return d == 0;
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
    thread_ = std::thread(&WgDevice::loop, this);
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
    std::vector<uint8_t> buf(65536);
    int64_t lastResolve = monoMs();
    while (!stop_.load()) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(static_cast<SockT>(sock_), &rd);
        timeval tv{0, 50000};
        const int r = select(static_cast<int>(sock_) + 1, &rd, nullptr, nullptr, &tv);
        const int64_t now = monoMs();
        if (r > 0) {
            // Se vacia TODO lo que haya: de a un datagrama por vuelta, una
            // rafaga de TCP desborda el buffer del socket y se pierde.
            for (int i = 0; i < 256; ++i) {
                sockaddr_in from{};
                socklen_t fl = sizeof(from);
                const int n = static_cast<int>(
                    recvfrom(static_cast<SockT>(sock_), reinterpret_cast<char*>(buf.data()),
                             static_cast<int>(buf.size()), 0, reinterpret_cast<sockaddr*>(&from), &fl));
                if (n <= 0) break;
                handleDatagram(buf.data(), static_cast<size_t>(n), from.sin_addr.s_addr, ntohs(from.sin_port),
                               now);
            }
        }
        {
            std::lock_guard<std::mutex> lock(m_);
            if (epIp_ == 0 && !cfg_.endpointHost.empty() && now - lastResolve > 5000) {
                lastResolve = now;
                resolveEndpoint();
            }
            tick(now);
        }
    }
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
            if (log_) log_(ba::i18n::traducir("gateway", "wireguard: el peer no responde; se reintenta con el proximo trafico"));
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
    std::vector<uint8_t> plain(padded, 0);
    if (n > 0) std::memcpy(plain.data(), p, n);
    std::vector<uint8_t> msg(kDataHeader + padded + 16);
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

void WgDevice::handleDatagram(const uint8_t* d, size_t n, uint32_t ip, uint16_t port, int64_t now) {
    if (n < 4) return;
    std::unique_lock<std::mutex> lock(m_);
    stats_.rxBytes += n;
    switch (d[0]) {
        case 1: handleInitiation(d, n, ip, port, now); break;
        case 2: handleResponse(d, n, ip, port, now); break;
        case 3: handleCookie(d, n); break;
        case 4: {
            // El paquete descifrado se entrega FUERA del lock: la pila IP puede
            // querer contestar (send) desde el mismo callback.
            handleData(d, n, ip, port, now);
            break;
        }
        default: break;
    }
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
    if (log_) log_(ba::i18n::traducir("gateway", "wireguard: sesion establecida"));
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
    if (log_) log_(ba::i18n::traducir("gateway", "wireguard: el servidor pidio cookie (bajo carga); se reintenta con MAC2"));
}

void WgDevice::handleData(const uint8_t* d, size_t n, uint32_t ip, uint16_t port, int64_t now) {
    if (n < kDataHeader + 16) return;
    Keypair* kp = keypairFor(get32(d + 4));
    if (kp == nullptr || now - kp->createdMs >= kRejectAfterMs) return;
    const uint64_t counter = get64(d + 8);
    if (!kp->replay.ok(counter)) return;
    std::vector<uint8_t> plain(n - kDataHeader - 16);
    if (!aeadDecrypt(plain.data(), kp->recvKey, counter, d + kDataHeader, n - kDataHeader, nullptr, 0))
        return;
    kp->replay.mark(counter);
    // Respondedor: el primer dato con la sesion nueva la confirma.
    if (kp == next_.get()) {
        previous_ = std::move(current_);
        current_ = std::move(next_);
        flushQueueLocked(now);
    }
    epIp_ = ip; // roaming
    epPort_ = port;
    lastRecvMs_ = now;
    unansweredSinceMs_ = 0;
    if (plain.empty()) return; // keepalive
    lastDataRecvMs_ = now;
    // Largo real del paquete IP (el resto es relleno a 16).
    size_t len = plain.size();
    if (len >= 20 && (plain[0] >> 4) == 4) {
        const size_t total = (static_cast<size_t>(plain[2]) << 8) | plain[3];
        if (total >= 20 && total <= len) len = total;
    }
    PacketFn fn = onPacket_;
    m_.unlock();
    if (fn) fn(plain.data(), len);
    m_.lock();
}

} // namespace ba::gateway::net
