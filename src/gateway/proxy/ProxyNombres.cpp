// ============================================================================
// src/gateway/proxy/ProxyNombres.cpp — ver ProxyNombres.hpp
// ============================================================================

#include "proxy/ProxyNombres.hpp"
#include "ajustes/Ajustes.hpp"
#include "i18n/I18n.hpp"

#include "proxy/Certificados.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <list>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/pk.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using SockT = SOCKET;
using pollfd_t = WSAPOLLFD;
#define BA_POLL WSAPoll
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using SockT = int;
using pollfd_t = struct pollfd;
#define BA_POLL ::poll
#endif

namespace ba::gateway::proxy {

using ba::i18n::llenar;
using ba::i18n::traducir;

namespace {

inline size_t kTope() { return static_cast<size_t>(ba::ajustes::entero("proxy.topeKB", 256)) * 1024; } // por dirección: más, y se deja de leer
inline int kHandshakeMs() { return ba::ajustes::entero("proxy.handshakeMs", 10000); }
inline int kOciosoMs() { return ba::ajustes::entero("proxy.ociosoMin", 15) * 60 * 1000; } // un WebSocket quieto (con su latido) dura más que esto
inline int kMaxConexiones() { return ba::ajustes::entero("proxy.maxConexiones", 4000); }

std::int64_t ahoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void cerrar(long long s) {
    if (s < 0) return;
#ifdef _WIN32
    closesocket(static_cast<SockT>(s));
#else
    ::close(static_cast<SockT>(s));
#endif
}

bool noBloqueante(long long s) {
#ifdef _WIN32
    u_long uno = 1;
    return ioctlsocket(static_cast<SockT>(s), FIONBIO, &uno) == 0;
#else
    const int f = fcntl(static_cast<int>(s), F_GETFL, 0);
    return f >= 0 && fcntl(static_cast<int>(s), F_SETFL, f | O_NONBLOCK) == 0;
#endif
}

bool tendria(int /*r*/) {
#ifdef _WIN32
    const int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EINTR;
#endif
}

long long escuchar(const std::string& bind, uint16_t puerto, std::string& error) {
    const SockT s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    if (s == INVALID_SOCKET) return error = "socket", -1;
#else
    if (s < 0) return error = std::strerror(errno), -1;
#endif
    int uno = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&uno), sizeof(uno));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(puerto);
    if (inet_pton(AF_INET, bind.c_str(), &a.sin_addr) != 1) a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || ::listen(s, 512) != 0) {
#ifdef _WIN32
        error = llenar(traducir("gateway", "no se pudo abrir el puerto %1 (error %2)"),
                       {std::to_string(puerto), std::to_string(WSAGetLastError())});
#else
        error = llenar(traducir("gateway", "no se pudo abrir el puerto %1 (%2)"), {std::to_string(puerto), std::strerror(errno)});
#endif
        cerrar(static_cast<long long>(s));
        return -1;
    }
    noBloqueante(static_cast<long long>(s));
    return static_cast<long long>(s);
}

std::string minus(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string ipTexto(uint32_t ipRed) {
    char b[INET_ADDRSTRLEN] = {0};
    in_addr a{};
    a.s_addr = ipRed;
    inet_ntop(AF_INET, &a, b, sizeof(b));
    return b;
}

int bioEnviar(void* ctx, const unsigned char* buf, size_t len) {
    const long long fd = *static_cast<long long*>(ctx);
#ifdef MSG_NOSIGNAL
    const int r = static_cast<int>(::send(static_cast<SockT>(fd), reinterpret_cast<const char*>(buf), static_cast<int>(len), MSG_NOSIGNAL));
#else
    const int r = static_cast<int>(::send(static_cast<SockT>(fd), reinterpret_cast<const char*>(buf), static_cast<int>(len), 0));
#endif
    if (r > 0) return r;
    return tendria(r) ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_NET_SEND_FAILED;
}

int bioRecibir(void* ctx, unsigned char* buf, size_t len) {
    const long long fd = *static_cast<long long*>(ctx);
    const int r = static_cast<int>(::recv(static_cast<SockT>(fd), reinterpret_cast<char*>(buf), static_cast<int>(len), 0));
    if (r > 0) return r;
    if (r == 0) return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
    return tendria(r) ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_RECV_FAILED;
}

} // namespace

// --- Certificados ----------------------------------------------------------------

struct ProxyNombres::Cert {
    mbedtls_x509_crt crt;
    mbedtls_pk_context pk;
    std::string origen; // acme | propio | casa
    std::int64_t vence = 0;
    bool ok = false;
    Cert() {
        mbedtls_x509_crt_init(&crt);
        mbedtls_pk_init(&pk);
    }
    ~Cert() {
        mbedtls_pk_free(&pk);
        mbedtls_x509_crt_free(&crt);
    }
};

struct ProxyNombres::Impl {
    mbedtls_entropy_context entropia;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_ssl_config conf;
    const char* alpn[2] = {"http/1.1", nullptr};
    long long tls = -1, http = -1;
#ifdef _WIN32
    bool wsa = false;
#endif
    Impl() {
        mbedtls_entropy_init(&entropia);
        mbedtls_ctr_drbg_init(&drbg);
        mbedtls_ssl_config_init(&conf);
    }
    ~Impl() {
        mbedtls_ssl_config_free(&conf);
        mbedtls_ctr_drbg_free(&drbg);
        mbedtls_entropy_free(&entropia);
    }
};

/// Una conexión: TLS (443) o HTTP plano (80).
struct ProxyNombres::Conn {
    enum class Estado { Handshake, Conectando, Pasando, Http, Cerrando };
    Estado estado = Estado::Handshake;
    bool esTls = true;
    long long cli = -1, back = -1;
    uint32_t ip = 0;
    mbedtls_ssl_context ssl;
    std::shared_ptr<Cert> cert; // el que presentó (vivo mientras dura el handshake)
    std::string host;
    const Servicio* servicio = nullptr;
    std::string haciaAdentro, haciaAfuera; // claro: cliente->servicio, servicio->cliente
    bool primerPedido = true;
    bool finCliente = false, finServicio = false;
    std::int64_t desde = 0, ultimo = 0;
    std::string http;     // 80: lo leído
    std::string respuesta; // 80: lo que falta mandar
    Conn() { mbedtls_ssl_init(&ssl); }
    ~Conn() {
        mbedtls_ssl_free(&ssl);
        cerrar(cli);
        cerrar(back);
    }
};

ProxyNombres::ProxyNombres(Config cfg, LogFn log) : cfg_(std::move(cfg)), log_(std::move(log)), impl_(std::make_unique<Impl>()) {}

ProxyNombres::~ProxyNombres() { stop(); }

bool ProxyNombres::ipLocal(uint32_t ipRed) {
    const uint32_t ip = ntohl(ipRed);
    return (ip >> 24) == 10 || (ip >> 20) == 0xAC1 || (ip >> 16) == 0xC0A8 || (ip >> 24) == 127 ||
           (ip >> 16) == 0xA9FE || (ip >> 22) == (100u << 2 | 1); // 100.64/10 (CGNAT, Tailscale/WireGuard)
}

std::vector<std::string> ProxyNombres::hosts() const {
    std::vector<std::string> out;
    for (const auto& s : cfg_.servicios) out.push_back(minus(s.nombre + "." + cfg_.dominio));
    return out;
}

const ProxyNombres::Servicio* ProxyNombres::servicioPara(const std::string& host) const {
    const std::string h = minus(host);
    for (const auto& s : cfg_.servicios)
        if (h == minus(s.nombre + "." + cfg_.dominio)) return &s;
    for (const auto& s : cfg_.servicios)
        if (s.nombre == cfg_.porDefecto) return &s;
    return cfg_.servicios.empty() ? nullptr : &cfg_.servicios.front();
}

std::shared_ptr<ProxyNombres::Cert> ProxyNombres::certPara(const std::string& host) const {
    std::lock_guard<std::mutex> l(certMu_);
    const auto it = certs_.find(minus(host));
    return it != certs_.end() ? it->second : casa_;
}

bool ProxyNombres::ponerCertificado(const std::string& host, const std::string& certPem, const std::string& clavePem,
                                    const std::string& origen) {
    if (certPem.empty()) {
        std::lock_guard<std::mutex> l(certMu_);
        certs_.erase(minus(host));
        return true;
    }
    auto c = std::make_shared<Cert>();
    std::string cz = certPem, kz = clavePem;
    cz.push_back('\0');
    kz.push_back('\0');
    c->ok = mbedtls_x509_crt_parse(&c->crt, reinterpret_cast<const unsigned char*>(cz.data()), cz.size()) == 0 &&
            mbedtls_pk_parse_key(&c->pk, reinterpret_cast<const unsigned char*>(kz.data()), kz.size(), nullptr, 0,
                                 mbedtls_ctr_drbg_random, &impl_->drbg) == 0;
    mbedtls_platform_zeroize(kz.data(), kz.size());
    if (!c->ok) return false;
    c->origen = origen;
    c->vence = vence(certPem);
    std::lock_guard<std::mutex> l(certMu_);
    if (host.empty()) casa_ = c;
    else certs_[minus(host)] = c;
    return true;
}

void ProxyNombres::ponerDesafio(const std::string& token, const std::string& respuesta) {
    std::lock_guard<std::mutex> l(desafioMu_);
    desafios_[token] = respuesta;
}

void ProxyNombres::quitarDesafio(const std::string& token) {
    std::lock_guard<std::mutex> l(desafioMu_);
    desafios_.erase(token);
}

nlohmann::json ProxyNombres::estado() const {
    nlohmann::json servicios = nlohmann::json::array();
    for (const auto& s : cfg_.servicios) {
        const std::string host = minus(s.nombre + "." + cfg_.dominio);
        const auto c = certPara(host);
        servicios.push_back({{"nombre", s.nombre},
                             {"host", host},
                             {"destino", s.destino},
                             {"ruta", s.ruta},
                             {"soloLan", s.soloLan},
                             {"certificado", {{"origen", c ? c->origen : ""}, {"vence", c ? c->vence : 0}}}});
    }
    return {{"activo", impl_->tls >= 0},
            {"puertoTls", cfg_.puertoTls},
            {"puertoHttp", cfg_.puertoHttp},
            {"dominio", cfg_.dominio},
            {"servicios", servicios},
            {"conexiones", conexiones_.load()},
            {"total", total_.load()},
            {"rechazadas", rechazadas_.load()}};
}

bool ProxyNombres::start(const std::string& autofirmadoPem, const std::string& clavePem) {
    Impl& d = *impl_;
    static const char kPers[] = "ba-proxy-nombres";
    if (mbedtls_ctr_drbg_seed(&d.drbg, mbedtls_entropy_func, &d.entropia, reinterpret_cast<const unsigned char*>(kPers),
                              sizeof(kPers) - 1) != 0)
        return false;
    if (!ponerCertificado("", autofirmadoPem, clavePem, "casa")) {
        if (log_) log_(traducir("gateway", "proxy: el certificado de la casa es inválido"));
        return false;
    }
    if (mbedtls_ssl_config_defaults(&d.conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0)
        return false;
    mbedtls_ssl_conf_rng(&d.conf, mbedtls_ctr_drbg_random, &d.drbg);
    mbedtls_ssl_conf_min_tls_version(&d.conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(&d.conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_alpn_protocols(&d.conf, d.alpn);
    {
        std::lock_guard<std::mutex> l(certMu_);
        mbedtls_ssl_conf_own_cert(&d.conf, &casa_->crt, &casa_->pk);
    }
    // SNI: el certificado del nombre que pide (o el de la casa) y se anota el host
    // en la conexión (que viaja como user_data del contexto TLS).
    mbedtls_ssl_conf_sni(
        &d.conf,
        [](void* p, mbedtls_ssl_context* ssl, const unsigned char* nombre, size_t largo) -> int {
            auto* self = static_cast<ProxyNombres*>(p);
            const std::string host(reinterpret_cast<const char*>(nombre), largo);
            auto* c = static_cast<Conn*>(mbedtls_ssl_get_user_data_p(ssl));
            if (c == nullptr) return 0;
            c->host = minus(host);
            c->cert = self->certPara(c->host);
            if (c->cert && c->cert->ok) return mbedtls_ssl_set_hs_own_cert(ssl, &c->cert->crt, &c->cert->pk);
            return 0;
        },
        this);
#ifdef _WIN32
    WSADATA wsa{};
    d.wsa = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#endif
    std::string e;
    d.tls = escuchar(cfg_.bind, cfg_.puertoTls, e);
    if (d.tls < 0) {
        if (log_)
            log_(llenar(traducir("gateway", "proxy: %1 (en Linux, los puertos de menos de 1024 piden permiso: usá otro, p. "
                                            "ej. 8443)"),
                        {e}));
        return false;
    }
    if (cfg_.puertoHttp != 0) {
        d.http = escuchar(cfg_.bind, cfg_.puertoHttp, e);
        if (d.http < 0 && log_)
            log_(llenar(traducir("gateway", "proxy: %1 — sin el 80 no hay desafío HTTP-01 de ACME ni redirección a https"),
                        {e}));
    }
    stop_ = false;
    hilo_ = std::thread([this]() { bucle(); });
    if (log_)
        log_(d.http >= 0
                 ? llenar(traducir("gateway", "proxy: atendiendo %1 y %2 para *.%3 (%4 servicios)"),
                          {std::to_string(cfg_.puertoTls), std::to_string(cfg_.puertoHttp), cfg_.dominio,
                           std::to_string(cfg_.servicios.size())})
                 : llenar(traducir("gateway", "proxy: atendiendo %1 para *.%2 (%3 servicios)"),
                          {std::to_string(cfg_.puertoTls), cfg_.dominio, std::to_string(cfg_.servicios.size())}));
    return true;
}

void ProxyNombres::stop() {
    stop_ = true;
    if (hilo_.joinable()) hilo_.join();
    cerrar(impl_->tls);
    cerrar(impl_->http);
    impl_->tls = impl_->http = -1;
#ifdef _WIN32
    if (impl_->wsa) WSACleanup();
    impl_->wsa = false;
#endif
}

void ProxyNombres::bucle() {
    Impl& d = *impl_;
    std::list<std::unique_ptr<Conn>> conns;
    std::vector<pollfd_t> fds;
    std::vector<Conn*> deFd; // por índice de fds: qué conexión (nullptr = escucha)
    std::vector<bool> esBack;

    const auto aceptar = [&](long long l, bool tls) {
        for (int i = 0; i < 64; ++i) {
            sockaddr_in a{};
            socklen_t n = sizeof(a);
            const SockT s = ::accept(static_cast<SockT>(l), reinterpret_cast<sockaddr*>(&a), &n);
#ifdef _WIN32
            if (s == INVALID_SOCKET) return;
#else
            if (s < 0) return;
#endif
            if (static_cast<int>(conns.size()) >= kMaxConexiones()) {
                cerrar(static_cast<long long>(s));
                ++rechazadas_;
                continue;
            }
            noBloqueante(static_cast<long long>(s));
            int uno = 1;
            setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&uno), sizeof(uno));
            auto c = std::make_unique<Conn>();
            c->cli = static_cast<long long>(s);
            c->ip = a.sin_addr.s_addr;
            c->esTls = tls;
            c->estado = tls ? Conn::Estado::Handshake : Conn::Estado::Http;
            c->desde = c->ultimo = ahoraMs();
            if (tls) {
                if (mbedtls_ssl_setup(&c->ssl, &d.conf) != 0) continue;
                mbedtls_ssl_set_user_data_p(&c->ssl, c.get());
                mbedtls_ssl_set_bio(&c->ssl, &c->cli, bioEnviar, bioRecibir, nullptr);
            }
            ++total_;
            conns.push_back(std::move(c));
        }
    };

    // El servicio de adentro: conectar sin bloquear (la cabecera PROXY va primero).
    const auto conectar = [&](Conn& c) -> bool {
        c.servicio = servicioPara(c.host);
        if (c.servicio == nullptr) return false;
        if (c.servicio->soloLan && !ipLocal(c.ip)) {
            ++rechazadas_;
            const std::string cuerpo = traducir("gateway", "solo desde la red local") + "\r\n";
            c.haciaAfuera = "HTTP/1.1 403 Forbidden\r\nContent-Type: text/plain; charset=utf-8\r\nConnection: close\r\n"
                            "Content-Length: " +
                            std::to_string(cuerpo.size()) + "\r\n\r\n" + cuerpo;
            c.finServicio = true;
            c.estado = Conn::Estado::Cerrando;
            return true;
        }
        const SockT s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        noBloqueante(static_cast<long long>(s));
        int uno = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&uno), sizeof(uno));
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(c.servicio->destino);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        const int r = ::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
        if (r != 0 && !tendria(r)) {
            cerrar(static_cast<long long>(s));
            return false;
        }
        c.back = static_cast<long long>(s);
        if (c.servicio->cabeceraProxy)
            c.haciaAdentro = "PROXY TCP4 " + ipTexto(c.ip) + " 127.0.0.1 1 " + std::to_string(c.servicio->destino) + "\r\n";
        c.estado = Conn::Estado::Conectando;
        return true;
    };

    // El 80: desafío ACME o redirección a https.
    const auto atenderHttp = [&](Conn& c) {
        const auto fin = c.http.find("\r\n\r\n");
        if (fin == std::string::npos) return;
        const auto sp1 = c.http.find(' ');
        const auto sp2 = c.http.find(' ', sp1 + 1);
        const std::string ruta = sp1 != std::string::npos && sp2 != std::string::npos ? c.http.substr(sp1 + 1, sp2 - sp1 - 1) : "/";
        std::string host;
        for (size_t p = 0; (p = c.http.find("\r\n", p)) != std::string::npos && p < fin;) {
            p += 2;
            if (c.http.size() > p + 5 && minus(c.http.substr(p, 5)) == "host:") {
                const auto e = c.http.find("\r\n", p);
                host = c.http.substr(p + 5, e - p - 5);
                host.erase(0, host.find_first_not_of(' '));
                if (const auto dp = host.find(':'); dp != std::string::npos) host.resize(dp);
                break;
            }
        }
        static const std::string kAcme = "/.well-known/acme-challenge/";
        if (ruta.rfind(kAcme, 0) == 0) {
            std::string resp;
            {
                std::lock_guard<std::mutex> l(desafioMu_);
                const auto it = desafios_.find(ruta.substr(kAcme.size()));
                if (it != desafios_.end()) resp = it->second;
            }
            c.respuesta = resp.empty() ? "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
                                       : "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: " +
                                             std::to_string(resp.size()) + "\r\nConnection: close\r\n\r\n" + resp;
        } else {
            const std::string destino = "https://" + (host.empty() ? cfg_.porDefecto + "." + cfg_.dominio : host) +
                                        (cfg_.puertoTls == 443 ? "" : ":" + std::to_string(cfg_.puertoTls)) + ruta;
            c.respuesta = "HTTP/1.1 301 Moved Permanently\r\nLocation: " + destino +
                          "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        }
        c.estado = Conn::Estado::Cerrando;
    };

    // El primer pedido a «/» de un servicio con su ruta: se redirige (sesion.x → /sesion/).
    const auto redirigirRaiz = [&](Conn& c) -> bool {
        if (!c.primerPedido || c.servicio == nullptr || c.servicio->ruta == "/" || c.servicio->ruta.empty()) return false;
        // El pedido empieza después de la cabecera PROXY (si la hay).
        size_t inicio = 0;
        if (c.haciaAdentro.rfind("PROXY ", 0) == 0) inicio = c.haciaAdentro.find("\r\n") + 2;
        const auto finLinea = c.haciaAdentro.find("\r\n", inicio);
        if (finLinea == std::string::npos) return false; // todavía no llegó la línea
        c.primerPedido = false;
        const std::string linea = c.haciaAdentro.substr(inicio, finLinea - inicio);
        if (linea.rfind("GET / ", 0) != 0) return false;
        c.haciaAdentro.clear();
        c.haciaAfuera = "HTTP/1.1 302 Found\r\nLocation: " + c.servicio->ruta + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        c.finServicio = true;
        c.estado = Conn::Estado::Cerrando;
        return true;
    };

    std::vector<char> buf(16384);
    while (!stop_.load()) {
        fds.clear();
        deFd.clear();
        esBack.clear();
        const auto agregar = [&](long long fd, short ev, Conn* c, bool back) {
            pollfd_t p{};
            p.fd = static_cast<SockT>(fd);
            p.events = ev;
            fds.push_back(p);
            deFd.push_back(c);
            esBack.push_back(back);
        };
        if (d.tls >= 0) agregar(d.tls, POLLIN, nullptr, false);
        if (d.http >= 0) agregar(d.http, POLLIN, nullptr, true);
        bool hayPendienteTls = false;
        for (auto& cp : conns) {
            Conn& c = *cp;
            short evCli = 0, evBack = 0;
            switch (c.estado) {
                case Conn::Estado::Handshake: evCli = POLLIN | POLLOUT; break;
                case Conn::Estado::Http: evCli = POLLIN; break;
                case Conn::Estado::Conectando: evBack = POLLOUT; break;
                case Conn::Estado::Pasando:
                    if (c.haciaAdentro.size() < kTope() && !c.finCliente) evCli |= POLLIN;
                    if (!c.haciaAfuera.empty()) evCli |= POLLOUT;
                    if (c.haciaAfuera.size() < kTope() && !c.finServicio) evBack |= POLLIN;
                    if (!c.haciaAdentro.empty()) evBack |= POLLOUT;
                    if (c.esTls && mbedtls_ssl_get_bytes_avail(&c.ssl) > 0) hayPendienteTls = true;
                    break;
                case Conn::Estado::Cerrando:
                    evCli = POLLOUT;
                    break;
            }
            if (evCli != 0) agregar(c.cli, evCli, &c, false);
            if (evBack != 0 && c.back >= 0) agregar(c.back, evBack, &c, true);
        }
        const int n = BA_POLL(fds.data(), static_cast<unsigned long>(fds.size()), hayPendienteTls ? 0 : 200);
        if (n < 0) continue;
        const std::int64_t ahora = ahoraMs();
        for (size_t i = 0; i < fds.size(); ++i) {
            const short re = fds[i].revents;
            Conn* cp = deFd[i];
            if (cp == nullptr) {
                if (re & POLLIN) aceptar(static_cast<long long>(fds[i].fd), !esBack[i]);
                continue;
            }
            Conn& c = *cp;
            const bool lee = (re & (POLLIN | POLLHUP | POLLERR)) != 0;
            const bool escribe = (re & POLLOUT) != 0;
            if (re != 0) c.ultimo = ahora;
            if (!esBack[i]) {
                // --- lado del cliente ---
                if (c.estado == Conn::Estado::Handshake) {
                    const int r = mbedtls_ssl_handshake(&c.ssl);
                    if (r == 0) {
                        if (!conectar(c)) c.estado = Conn::Estado::Cerrando, c.finServicio = true;
                    } else if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
                        c.estado = Conn::Estado::Cerrando;
                        c.finServicio = true;
                        c.haciaAfuera.clear();
                        c.finCliente = true;
                    }
                } else if (c.estado == Conn::Estado::Http && lee) {
                    const int r = static_cast<int>(::recv(static_cast<SockT>(c.cli), buf.data(), static_cast<int>(buf.size()), 0));
                    if (r > 0) {
                        c.http.append(buf.data(), static_cast<size_t>(r));
                        if (c.http.size() > 16384) c.estado = Conn::Estado::Cerrando;
                        else atenderHttp(c);
                    } else if (r == 0 || !tendria(r)) {
                        c.estado = Conn::Estado::Cerrando;
                        c.finCliente = true;
                    }
                } else if (c.estado == Conn::Estado::Pasando) {
                    if (lee || mbedtls_ssl_get_bytes_avail(&c.ssl) > 0) {
                        while (c.haciaAdentro.size() < kTope()) {
                            const int r = mbedtls_ssl_read(&c.ssl, reinterpret_cast<unsigned char*>(buf.data()), buf.size());
                            if (r > 0) {
                                c.haciaAdentro.append(buf.data(), static_cast<size_t>(r));
                                continue;
                            }
                            if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) break;
                            c.finCliente = true; // cerró o falló
                            break;
                        }
                        redirigirRaiz(c);
                    }
                    if (escribe && !c.haciaAfuera.empty()) {
                        const int r = mbedtls_ssl_write(&c.ssl, reinterpret_cast<const unsigned char*>(c.haciaAfuera.data()),
                                                        std::min<size_t>(c.haciaAfuera.size(), 16384));
                        if (r > 0) c.haciaAfuera.erase(0, static_cast<size_t>(r));
                        else if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) c.finCliente = true, c.haciaAfuera.clear();
                    }
                }
                if (c.estado == Conn::Estado::Cerrando && escribe) {
                    // Lo último (un 302/403 o la respuesta del 80) y cerrar.
                    std::string& pend = c.esTls ? c.haciaAfuera : c.respuesta;
                    if (!pend.empty()) {
                        const int r = c.esTls ? mbedtls_ssl_write(&c.ssl, reinterpret_cast<const unsigned char*>(pend.data()), pend.size())
                                              : static_cast<int>(::send(static_cast<SockT>(c.cli), pend.data(), static_cast<int>(pend.size()), 0));
                        if (r > 0) pend.erase(0, static_cast<size_t>(r));
                        else if (!(c.esTls && (r == MBEDTLS_ERR_SSL_WANT_WRITE || r == MBEDTLS_ERR_SSL_WANT_READ)) && !(!c.esTls && tendria(r)))
                            pend.clear();
                    }
                    if (pend.empty()) {
                        if (c.esTls) mbedtls_ssl_close_notify(&c.ssl);
                        c.finCliente = c.finServicio = true;
                    }
                }
            } else {
                // --- lado del servicio ---
                if (c.estado == Conn::Estado::Conectando && (escribe || (re & (POLLERR | POLLHUP)))) {
                    int err = 0;
                    socklen_t l = sizeof(err);
                    getsockopt(static_cast<SockT>(c.back), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &l);
                    if (err != 0 || (re & POLLERR)) {
                        const std::string cuerpo = traducir("gateway", "el servicio no esta andando") + "\n";
                        c.haciaAfuera = "HTTP/1.1 502 Bad Gateway\r\nContent-Length: " + std::to_string(cuerpo.size()) +
                                        "\r\nConnection: close\r\n\r\n" + cuerpo;
                        c.estado = Conn::Estado::Cerrando;
                        c.finServicio = true;
                    } else {
                        c.estado = Conn::Estado::Pasando;
                    }
                } else if (c.estado == Conn::Estado::Pasando) {
                    if (escribe && !c.haciaAdentro.empty() && !(c.primerPedido && c.servicio && c.servicio->ruta != "/")) {
                        const int r = static_cast<int>(::send(static_cast<SockT>(c.back), c.haciaAdentro.data(),
                                                              static_cast<int>(c.haciaAdentro.size()), 0));
                        if (r > 0) c.haciaAdentro.erase(0, static_cast<size_t>(r));
                        else if (!tendria(r)) c.finServicio = true;
                    }
                    if (lee) {
                        const int r = static_cast<int>(::recv(static_cast<SockT>(c.back), buf.data(), static_cast<int>(buf.size()), 0));
                        if (r > 0) c.haciaAfuera.append(buf.data(), static_cast<size_t>(r));
                        else if (r == 0 || !tendria(r)) c.finServicio = true;
                    }
                }
            }
        }
        // Cerrar lo terminado: un lado se fue y no queda nada para el otro; o
        // un handshake que no termina; o una conexión quieta hace mucho.
        for (auto it = conns.begin(); it != conns.end();) {
            Conn& c = **it;
            bool fin = false;
            if (c.estado == Conn::Estado::Handshake && ahora - c.desde > kHandshakeMs()) fin = true;
            if (ahora - c.ultimo > kOciosoMs()) fin = true;
            if (c.estado == Conn::Estado::Pasando) {
                if (c.finServicio && c.haciaAfuera.empty()) fin = true;
                if (c.finCliente && c.haciaAdentro.empty()) fin = true;
            }
            if ((c.estado == Conn::Estado::Cerrando) && c.finCliente && c.finServicio) fin = true;
            if (c.estado == Conn::Estado::Cerrando && c.finCliente && !c.esTls && c.respuesta.empty()) fin = true;
            it = fin ? conns.erase(it) : std::next(it);
        }
        conexiones_ = static_cast<int>(conns.size());
    }
    conns.clear();
}

} // namespace ba::gateway::proxy
