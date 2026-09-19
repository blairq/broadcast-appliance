// ============================================================================
// src/gateway/net/TlsTerminator.cpp — ver TlsTerminator.hpp
//
// TLS 1.2 (ECDHE + AES-GCM/ChaCha20): lo soporta cualquier navegador y no
// necesita PSA (que sin MBEDTLS_THREADING_C no es seguro entre threads). El
// unico estado compartido entre conexiones es el DRBG, detras de un mutex.
// ============================================================================

#include "net/TlsTerminator.hpp"

#include "security/CredentialStore.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <system_error>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h> // MBEDTLS_ERR_NET_SEND_FAILED
#include <mbedtls/pk.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/x509_csr.h>

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
#include <sys/stat.h>
#include <unistd.h>
using SockT = int;
#endif

namespace ba::gateway::net {

namespace fs = std::filesystem;

namespace {

void closeSock(long long s) {
    if (s < 0) return;
#ifdef _WIN32
    closesocket(static_cast<SockT>(s));
#else
    ::close(static_cast<SockT>(s));
#endif
}

void setRecvTimeout(long long s, int ms) {
#ifdef _WIN32
    DWORD v = static_cast<DWORD>(ms);
    setsockopt(static_cast<SockT>(s), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&v), sizeof(v));
#else
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    setsockopt(static_cast<SockT>(s), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

bool sendAllRaw(long long s, const char* p, size_t n) {
    while (n > 0) {
#ifdef MSG_NOSIGNAL
        const int r = static_cast<int>(::send(static_cast<SockT>(s), p, static_cast<int>(n), MSG_NOSIGNAL));
#else
        const int r = static_cast<int>(::send(static_cast<SockT>(s), p, static_cast<int>(n), 0));
#endif
        if (r <= 0) return false;
        p += r;
        n -= static_cast<size_t>(r);
    }
    return true;
}

std::string ipText(uint32_t netOrder) {
    char buf[INET_ADDRSTRLEN] = {0};
    in_addr a{};
    a.s_addr = netOrder;
    inet_ntop(AF_INET, &a, buf, sizeof(buf));
    return buf;
}

/// PEM de una credencial, con el NUL final que piden los parsers de mbedTLS.
bool readPem(const ba::security::CredentialStore& store, const char* name, std::string& out) {
    if (!store.read(name, out) || out.empty()) return false;
    out.push_back('\0');
    return true;
}

int bioSend(void* ctx, const unsigned char* buf, size_t len) {
    const long long fd = *static_cast<long long*>(ctx);
#ifdef MSG_NOSIGNAL
    const int r = static_cast<int>(::send(static_cast<SockT>(fd), reinterpret_cast<const char*>(buf),
                                          static_cast<int>(len), MSG_NOSIGNAL));
#else
    const int r = static_cast<int>(
        ::send(static_cast<SockT>(fd), reinterpret_cast<const char*>(buf), static_cast<int>(len), 0));
#endif
    return r > 0 ? r : MBEDTLS_ERR_NET_SEND_FAILED;
}

int bioRecv(void* ctx, unsigned char* buf, size_t len) {
    const long long fd = *static_cast<long long*>(ctx);
    const int r = static_cast<int>(
        ::recv(static_cast<SockT>(fd), reinterpret_cast<char*>(buf), static_cast<int>(len), 0));
    if (r > 0) return r;
    if (r == 0) return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
    return MBEDTLS_ERR_SSL_TIMEOUT; // timeout del handshake o error
}

} // namespace

struct TlsTerminator::Impl {
    std::mutex rngLock;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_x509_crt crt;
    mbedtls_pk_context key;
    mbedtls_ssl_config conf;
    const char* alpn[2] = {"http/1.1", nullptr};

    Impl() {
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&drbg);
        mbedtls_x509_crt_init(&crt);
        mbedtls_pk_init(&key);
        mbedtls_ssl_config_init(&conf);
    }
    ~Impl() {
        mbedtls_ssl_config_free(&conf);
        mbedtls_pk_free(&key);
        mbedtls_x509_crt_free(&crt);
        mbedtls_ctr_drbg_free(&drbg);
        mbedtls_entropy_free(&entropy);
    }
    static int rng(void* p, unsigned char* out, size_t n) {
        auto* self = static_cast<Impl*>(p);
        std::lock_guard<std::mutex> lock(self->rngLock);
        return mbedtls_ctr_drbg_random(&self->drbg, out, n);
    }
};

TlsTerminator::TlsTerminator(Config cfg, LogFn log)
    : cfg_(std::move(cfg)), log_(std::move(log)), impl_(std::make_unique<Impl>()) {}

TlsTerminator::~TlsTerminator() { stop(); }

bool TlsTerminator::start() {
    Impl& d = *impl_;
    static const char kPers[] = "ba-tls";
    if (mbedtls_ctr_drbg_seed(&d.drbg, mbedtls_entropy_func, &d.entropy,
                              reinterpret_cast<const unsigned char*>(kPers), sizeof(kPers) - 1) != 0) {
        return false;
    }

    // --- Certificado ----------------------------------------------------------
    const ba::security::CredentialStore store{fs::path(cfg_.credentialsDir)};
    bool loaded = false;
    std::string crtPem;
    std::string keyPem;
    if (readPem(store, "tls-propio-cert", crtPem) && readPem(store, "tls-propio-clave", keyPem)) {
        loaded = mbedtls_x509_crt_parse(&d.crt, reinterpret_cast<const unsigned char*>(crtPem.data()),
                                        crtPem.size()) == 0 &&
                 mbedtls_pk_parse_key(&d.key, reinterpret_cast<const unsigned char*>(keyPem.data()),
                                      keyPem.size(), nullptr, 0, &Impl::rng, &d) == 0;
        if (loaded && log_) log_("https: certificado propio (Credenciales/tls-propio)");
        if (!loaded && log_) log_("https: el certificado propio es invalido; se usa uno autofirmado");
    }
    mbedtls_platform_zeroize(keyPem.data(), keyPem.size());
    if (!loaded) {
        selfSigned_ = true;
        std::string names;
        for (const auto& n : cfg_.names)
            names += n + "\n";
        // Se reutiliza mientras los nombres no cambien: huella estable, el
        // navegador no vuelve a preguntar.
        std::string oldNames;
        if (store.read("tls-autofirmado-nombres", oldNames) && oldNames == names &&
            readPem(store, "tls-autofirmado-cert", crtPem) &&
            readPem(store, "tls-autofirmado-clave", keyPem)) {
            mbedtls_x509_crt_free(&d.crt);
            mbedtls_x509_crt_init(&d.crt);
            mbedtls_pk_free(&d.key);
            mbedtls_pk_init(&d.key);
            loaded = mbedtls_x509_crt_parse(&d.crt, reinterpret_cast<const unsigned char*>(crtPem.data()),
                                            crtPem.size()) == 0 &&
                     mbedtls_pk_parse_key(&d.key, reinterpret_cast<const unsigned char*>(keyPem.data()),
                                          keyPem.size(), nullptr, 0, &Impl::rng, &d) == 0;
            mbedtls_platform_zeroize(keyPem.data(), keyPem.size());
        }
        if (!loaded) {
            mbedtls_x509_crt_free(&d.crt);
            mbedtls_x509_crt_init(&d.crt);
            mbedtls_pk_free(&d.key);
            mbedtls_pk_init(&d.key);
            bool ok =
                mbedtls_pk_setup(&d.key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) == 0 &&
                mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(d.key), &Impl::rng, &d) == 0;
            mbedtls_x509write_cert w;
            mbedtls_x509write_crt_init(&w);
            mbedtls_x509write_crt_set_version(&w, MBEDTLS_X509_CRT_VERSION_3);
            mbedtls_x509write_crt_set_md_alg(&w, MBEDTLS_MD_SHA256);
            mbedtls_x509write_crt_set_subject_key(&w, &d.key);
            mbedtls_x509write_crt_set_issuer_key(&w, &d.key);
            ok = ok && mbedtls_x509write_crt_set_subject_name(
                           &w, "CN=Broadcast Appliance,O=Broadcast Appliance") == 0;
            ok = ok && mbedtls_x509write_crt_set_issuer_name(
                           &w, "CN=Broadcast Appliance,O=Broadcast Appliance") == 0;
            unsigned char serial[16];
            Impl::rng(&d, serial, sizeof(serial));
            serial[0] &= 0x7F;
            ok = ok && mbedtls_x509write_crt_set_serial_raw(&w, serial, sizeof(serial)) == 0;
            ok = ok && mbedtls_x509write_crt_set_validity(&w, "20250101000000", "20351231235959") == 0;
            ok = ok && mbedtls_x509write_crt_set_basic_constraints(&w, 0, -1) == 0;
            ok = ok && mbedtls_x509write_crt_set_key_usage(&w, MBEDTLS_X509_KU_DIGITAL_SIGNATURE) == 0;
            // SAN: sin esto los navegadores rechazan el certificado aunque se
            // acepte la advertencia.
            std::vector<mbedtls_x509_san_list> san(cfg_.names.size() + 1);
            std::vector<std::array<unsigned char, 4>> ips(san.size());
            std::vector<std::string> keep(cfg_.names.begin(), cfg_.names.end());
            keep.insert(keep.begin(), "localhost");
            size_t used = 0;
            for (size_t i = 0; i < keep.size(); ++i) {
                in_addr a{};
                mbedtls_x509_san_list& node = san[used];
                std::memset(&node, 0, sizeof(node));
                if (inet_pton(AF_INET, keep[i].c_str(), &a) == 1) {
                    std::memcpy(ips[used].data(), &a, 4);
                    node.node.type = MBEDTLS_X509_SAN_IP_ADDRESS;
                    node.node.san.unstructured_name.p = ips[used].data();
                    node.node.san.unstructured_name.len = 4;
                } else {
                    node.node.type = MBEDTLS_X509_SAN_DNS_NAME;
                    node.node.san.unstructured_name.p =
                        reinterpret_cast<unsigned char*>(const_cast<char*>(keep[i].data()));
                    node.node.san.unstructured_name.len = keep[i].size();
                }
                if (used > 0) san[used - 1].next = &node;
                ++used;
            }
            ok = ok && mbedtls_x509write_crt_set_subject_alternative_name(&w, &san[0]) == 0;
            std::vector<unsigned char> pem(8192);
            ok = ok && mbedtls_x509write_crt_pem(&w, pem.data(), pem.size(), &Impl::rng, &d) == 0;
            crtPem = reinterpret_cast<char*>(pem.data());
            mbedtls_x509write_crt_free(&w);
            ok = ok && mbedtls_pk_write_key_pem(&d.key, pem.data(), pem.size()) == 0;
            keyPem = reinterpret_cast<char*>(pem.data());
            mbedtls_platform_zeroize(pem.data(), pem.size());
            ok = ok && mbedtls_x509_crt_parse(&d.crt, reinterpret_cast<const unsigned char*>(crtPem.c_str()),
                                              crtPem.size() + 1) == 0;
            if (!ok) {
                if (log_) log_("https: no se pudo generar el certificado autofirmado");
                return false;
            }
            const bool saved = store.write("tls-autofirmado-cert", crtPem) &&
                               store.write("tls-autofirmado-clave", keyPem) &&
                               store.write("tls-autofirmado-nombres", names);
            mbedtls_platform_zeroize(keyPem.data(), keyPem.size());
            loaded = true;
            if (log_)
                log_(saved ? "https: certificado autofirmado nuevo (Credenciales/tls-autofirmado)"
                           : "https: certificado autofirmado nuevo (no se pudo guardar: cambiara en el "
                             "proximo arranque)");
        }
    }
    unsigned char h[32];
    mbedtls_sha256(d.crt.raw.p, d.crt.raw.len, h, 0);
    static const char* hx = "0123456789ABCDEF";
    for (int i = 0; i < 32; ++i) {
        if (i) fingerprint_ += ':';
        fingerprint_ += hx[h[i] >> 4];
        fingerprint_ += hx[h[i] & 15];
    }

    // --- Configuracion TLS ------------------------------------------------------
    if (mbedtls_ssl_config_defaults(&d.conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0 ||
        mbedtls_ssl_conf_own_cert(&d.conf, &d.crt, &d.key) != 0) {
        return false;
    }
    mbedtls_ssl_conf_rng(&d.conf, &Impl::rng, &d);
    mbedtls_ssl_conf_min_tls_version(&d.conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(&d.conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_alpn_protocols(&d.conf, d.alpn);

    // --- Socket de escucha ------------------------------------------------------
    const SockT s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int one = 1;
#ifdef _WIN32
    // Exclusivo: con SO_REUSEADDR otro proceso podria tomar el mismo puerto.
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&one), sizeof(one));
#else
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
#endif
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(cfg_.port);
    inet_pton(AF_INET, cfg_.bindAddress.empty() ? "127.0.0.1" : cfg_.bindAddress.c_str(), &a.sin_addr);
    if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || listen(s, 32) != 0) {
#ifdef _WIN32
        const int code = WSAGetLastError();
        const std::string why =
            code == WSAEADDRINUSE ? "el puerto ya esta en uso" : "error " + std::to_string(code);
#else
        const int code = errno;
        const std::string why = code == EADDRINUSE ? "el puerto ya esta en uso" : std::strerror(code);
#endif
        closeSock(static_cast<long long>(s));
        if (log_)
            log_("https: no se pudo escuchar en " + cfg_.bindAddress + ":" + std::to_string(cfg_.port) +
                 " (" + why + ")");
        return false;
    }
    socklen_t len = sizeof(a);
    getsockname(s, reinterpret_cast<sockaddr*>(&a), &len);
    boundPort_ = ntohs(a.sin_port);
    listenFd_ = static_cast<long long>(s);
    stop_ = false;
    accept_ = std::thread(&TlsTerminator::acceptLoop, this);
    if (log_) {
        log_("https: escuchando en " + cfg_.bindAddress + ":" + std::to_string(boundPort_) +
             " -> 127.0.0.1:" + std::to_string(cfg_.backendPort) + " (huella " + fingerprint_.substr(0, 23) +
             "...)");
    }
    return true;
}

void TlsTerminator::stop() {
    if (!accept_.joinable()) return;
    stop_ = true;
    accept_.join();
    closeSock(listenFd_);
    listenFd_ = -1;
    for (int i = 0; i < 150 && active_.load() > 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

void TlsTerminator::acceptLoop() {
    while (!stop_.load()) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(static_cast<SockT>(listenFd_), &rd);
        timeval tv{0, 200000};
        if (select(static_cast<int>(listenFd_) + 1, &rd, nullptr, nullptr, &tv) <= 0) continue;
        sockaddr_in peer{};
        socklen_t pl = sizeof(peer);
        const SockT c = accept(static_cast<SockT>(listenFd_), reinterpret_cast<sockaddr*>(&peer), &pl);
#ifdef _WIN32
        if (c == INVALID_SOCKET) continue;
#else
        if (c < 0) continue;
#endif
        if (active_.load() >= 64) {
            closeSock(static_cast<long long>(c));
            continue;
        }
        active_++;
        std::thread([this, c, peer]() {
            serve(static_cast<long long>(c), peer.sin_addr.s_addr, ntohs(peer.sin_port));
            active_--;
        }).detach();
    }
}

void TlsTerminator::serve(long long fd, uint32_t peerIp, uint16_t peerPort) {
    std::string clientIp = ipText(peerIp);
    uint16_t clientPort = peerPort;
    setRecvTimeout(fd, 10000);

    // Del puente del tunel (loopback) puede venir la IP original en PROXY v1.
    if (clientIp == "127.0.0.1") {
        char peek[6];
        if (recv(static_cast<SockT>(fd), peek, sizeof(peek), MSG_PEEK) == 6 &&
            std::memcmp(peek, "PROXY ", 6) == 0) {
            std::string line;
            char ch = 0;
            while (line.size() < 120 && recv(static_cast<SockT>(fd), &ch, 1, 0) == 1) {
                line.push_back(ch);
                if (line.size() >= 2 && line.compare(line.size() - 2, 2, "\r\n") == 0) break;
            }
            char src[64] = {0};
            char dst[64] = {0};
            unsigned sp = 0;
            unsigned dp = 0;
            if (std::sscanf(line.c_str(), "PROXY TCP4 %63s %63s %u %u", src, dst, &sp, &dp) == 4) {
                clientIp = src;
                clientPort = static_cast<uint16_t>(sp);
            }
        }
    }

    mbedtls_ssl_context ssl;
    mbedtls_ssl_init(&ssl);
    long long ioFd = fd;
    long long back = -1;
    bool ok = mbedtls_ssl_setup(&ssl, &impl_->conf) == 0;
    if (ok) {
        mbedtls_ssl_set_bio(&ssl, &ioFd, bioSend, bioRecv, nullptr);
        int r = 0;
        while ((r = mbedtls_ssl_handshake(&ssl)) != 0) {
            if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
                ok = false;
                break;
            }
        }
    }
    if (ok) {
        setRecvTimeout(fd, 0);
        const SockT b = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        back = static_cast<long long>(b);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons(cfg_.backendPort);
        int one = 1;
        setsockopt(b, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
        const std::string proxy = "PROXY TCP4 " + clientIp + " 127.0.0.1 " + std::to_string(clientPort) +
                                  " " + std::to_string(cfg_.backendPort) + "\r\n";
        ok = connect(b, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0 &&
             sendAllRaw(back, proxy.data(), proxy.size());
    }
    // --- Bombeo en los dos sentidos -------------------------------------------------
    std::vector<char> buf(16384);
    while (ok && !stop_.load()) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(static_cast<SockT>(fd), &rd);
        FD_SET(static_cast<SockT>(back), &rd);
        timeval tv{0, 200000};
        const bool pending = mbedtls_ssl_get_bytes_avail(&ssl) > 0;
        if (!pending) {
            const int s = select(static_cast<int>(std::max(fd, back)) + 1, &rd, nullptr, nullptr, &tv);
            if (s < 0) break;
            if (s == 0) continue;
        }
        if (pending || FD_ISSET(static_cast<SockT>(fd), &rd)) {
            const int n = mbedtls_ssl_read(&ssl, reinterpret_cast<unsigned char*>(buf.data()), buf.size());
            if (n > 0) {
                if (!sendAllRaw(back, buf.data(), static_cast<size_t>(n))) break;
            } else if (n != MBEDTLS_ERR_SSL_WANT_READ && n != MBEDTLS_ERR_SSL_WANT_WRITE) {
                break; // cierre del navegador o error
            }
        }
        if (!pending && FD_ISSET(static_cast<SockT>(back), &rd)) {
            const int n = static_cast<int>(
                ::recv(static_cast<SockT>(back), buf.data(), static_cast<int>(buf.size()), 0));
            if (n <= 0) break;
            size_t off = 0;
            while (off < static_cast<size_t>(n)) {
                const int w = mbedtls_ssl_write(&ssl, reinterpret_cast<unsigned char*>(buf.data()) + off,
                                                static_cast<size_t>(n) - off);
                if (w > 0) {
                    off += static_cast<size_t>(w);
                } else if (w != MBEDTLS_ERR_SSL_WANT_WRITE && w != MBEDTLS_ERR_SSL_WANT_READ) {
                    ok = false;
                    break;
                }
            }
        }
    }
    mbedtls_ssl_close_notify(&ssl);
    mbedtls_ssl_free(&ssl);
    closeSock(back);
    closeSock(fd);
}

} // namespace ba::gateway::net
