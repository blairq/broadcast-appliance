// ============================================================================
// src/app/net/HttpsClient.cpp — ver HttpsClient.hpp
// ============================================================================

#include "net/HttpsClient.hpp"

#include <chrono>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <memory>
#include <mutex>

namespace ba::app::net {

namespace {

std::mutex g_caLock;
bool g_caLoaded = false;
mbedtls_x509_crt g_ca;
std::vector<std::string> g_caSearch = {
    // Debian/Ubuntu, Fedora/RHEL, openSUSE, Alpine, macOS: SOLO si falta el
    // del bundle (son datos del sistema, no codigo).
    "/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/ca-bundle.pem",
    "/etc/ssl/cert.pem"};

bool loadCaLocked(const std::string& path) {
    if (g_caLoaded) return true;
    mbedtls_x509_crt_init(&g_ca);
    if (mbedtls_x509_crt_parse_file(&g_ca, path.c_str()) < 0 || g_ca.version == 0) {
        mbedtls_x509_crt_free(&g_ca);
        return false;
    }
    g_caLoaded = true; // aunque algun certificado viejo no parsee (retorno > 0), el resto sirve
    return true;
}

bool ensureCa() {
    std::lock_guard<std::mutex> lock(g_caLock);
    if (g_caLoaded) return true;
    for (const std::string& p : g_caSearch)
        if (loadCaLocked(p)) return true;
    return false;
}

std::string lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

struct Url {
    bool tls = true;
    std::string host;
    std::string port;
    std::string target; // ruta + consulta
};

bool parseUrl(const std::string& u, Url& out) {
    size_t p = 0;
    if (u.rfind("https://", 0) == 0) {
        out.tls = true;
        p = 8;
    } else if (u.rfind("http://", 0) == 0) {
        out.tls = false;
        p = 7;
    } else {
        return false;
    }
    const size_t slash = u.find('/', p);
    const std::string hostport = u.substr(p, slash == std::string::npos ? std::string::npos : slash - p);
    out.target = slash == std::string::npos ? "/" : u.substr(slash);
    const size_t colon = hostport.rfind(':');
    if (colon != std::string::npos && hostport.find(']') == std::string::npos) {
        out.host = hostport.substr(0, colon);
        out.port = hostport.substr(colon + 1);
    } else {
        out.host = hostport;
        out.port = out.tls ? "443" : "80";
    }
    return !out.host.empty();
}

std::string tlsError(int code) {
    char buf[160] = {0};
    mbedtls_strerror(code, buf, sizeof(buf));
    return buf;
}

/// Conexion (TCP o TLS) con lectura/escritura "todo o error".
class Conn {
   public:
    Conn() { mbedtls_net_init(&net_); } // fd = -1: destruir sin abrir no cierra nada ajeno
    ~Conn() {
        if (tls_) {
            mbedtls_ssl_close_notify(&ssl_);
            mbedtls_ssl_free(&ssl_);
            mbedtls_ssl_config_free(&conf_);
            mbedtls_ctr_drbg_free(&drbg_);
            mbedtls_entropy_free(&entropy_);
        }
        mbedtls_net_free(&net_);
    }

    bool open(const Url& u, int timeoutMs, std::string& err) {
        mbedtls_net_init(&net_);
        timeout_ = static_cast<uint32_t>(timeoutMs);
        const int r = mbedtls_net_connect(&net_, u.host.c_str(), u.port.c_str(), MBEDTLS_NET_PROTO_TCP);
        if (r != 0) {
            err = "no se pudo conectar a " + u.host + ":" + u.port + " (" + tlsError(r) + ")";
            return false;
        }
        if (!u.tls) return true;
        if (!ensureCa()) {
            err = "sin raices de certificados (falta data/cacert.pem: make deps-cacert)";
            return false;
        }
        static std::once_flag psa;
        std::call_once(psa, []() { psa_crypto_init(); }); // TLS 1.3 de mbedtls usa PSA
        tls_ = true;
        mbedtls_ssl_init(&ssl_);
        mbedtls_ssl_config_init(&conf_);
        mbedtls_ctr_drbg_init(&drbg_);
        mbedtls_entropy_init(&entropy_);
        const char* pers = "ba-https";
        if (mbedtls_ctr_drbg_seed(&drbg_, mbedtls_entropy_func, &entropy_,
                                  reinterpret_cast<const unsigned char*>(pers), 8) != 0 ||
            mbedtls_ssl_config_defaults(&conf_, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                        MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
            err = "no se pudo preparar TLS";
            return false;
        }
        mbedtls_ssl_conf_authmode(&conf_, MBEDTLS_SSL_VERIFY_REQUIRED);
        {
            std::lock_guard<std::mutex> lock(g_caLock);
            mbedtls_ssl_conf_ca_chain(&conf_, &g_ca, nullptr);
        }
        mbedtls_ssl_conf_rng(&conf_, mbedtls_ctr_drbg_random, &drbg_);
        mbedtls_ssl_conf_read_timeout(&conf_, timeout_);
        if (mbedtls_ssl_setup(&ssl_, &conf_) != 0 || mbedtls_ssl_set_hostname(&ssl_, u.host.c_str()) != 0) {
            err = "no se pudo preparar TLS";
            return false;
        }
        mbedtls_ssl_set_bio(&ssl_, &net_, mbedtls_net_send, nullptr, mbedtls_net_recv_timeout);
        int h = 0;
        while ((h = mbedtls_ssl_handshake(&ssl_)) != 0) {
            if (h == MBEDTLS_ERR_SSL_WANT_READ || h == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            const uint32_t flags = mbedtls_ssl_get_verify_result(&ssl_);
            if (flags != 0) {
                char info[256] = {0};
                mbedtls_x509_crt_verify_info(info, sizeof(info), "", flags);
                err = "certificado de " + u.host + " no valido: " + info;
            } else {
                err = "TLS con " + u.host + " fallo: " + tlsError(h);
            }
            return false;
        }
        return true;
    }

    bool write(const char* data, size_t n) {
        size_t sent = 0;
        while (sent < n) {
            int r = 0;
            if (tls_) {
                r = mbedtls_ssl_write(&ssl_, reinterpret_cast<const unsigned char*>(data + sent), n - sent);
                if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            } else {
                r = mbedtls_net_send(&net_, reinterpret_cast<const unsigned char*>(data + sent), n - sent);
            }
            if (r <= 0) return false;
            sent += static_cast<size_t>(r);
        }
        return true;
    }

    /// >0 bytes leidos, 0 = fin, <0 error.
    int read(char* buf, size_t n) {
        for (;;) {
            int r = 0;
            if (tls_) {
                r = mbedtls_ssl_read(&ssl_, reinterpret_cast<unsigned char*>(buf), n);
                if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
                if (r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue; // TLS 1.3
#endif
                if (r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
            } else {
                r = mbedtls_net_recv_timeout(&net_, reinterpret_cast<unsigned char*>(buf), n, timeout_);
            }
            return r;
        }
    }

   private:
    mbedtls_net_context net_{};
    bool tls_ = false;
    uint32_t timeout_ = 30000;
    mbedtls_ssl_context ssl_{};
    mbedtls_ssl_config conf_{};
    mbedtls_ctr_drbg_context drbg_{};
    mbedtls_entropy_context entropy_{};
};

} // namespace

struct LineSocket::Impl {
    Conn conn;
    std::string buf;
    bool open = false;
};

LineSocket::LineSocket() : impl_(new Impl()) {}
LineSocket::~LineSocket() { delete impl_; }

bool LineSocket::open(const std::string& host, const std::string& port, bool tls, int readTimeoutMs,
                      std::string& err) {
    delete impl_;
    impl_ = new Impl();
    Url u;
    u.tls = tls;
    u.host = host;
    u.port = port;
    impl_->open = impl_->conn.open(u, readTimeoutMs, err);
    return impl_->open;
}

bool LineSocket::write(const std::string& data) {
    return impl_->open && impl_->conn.write(data.data(), data.size());
}

int LineSocket::readLine(std::string& line) {
    if (!impl_->open) return -1;
    for (;;) {
        const size_t eol = impl_->buf.find('\n');
        if (eol != std::string::npos) {
            line = impl_->buf.substr(0, eol);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            impl_->buf.erase(0, eol + 1);
            return 1;
        }
        char tmp[4096];
        const int r = impl_->conn.read(tmp, sizeof(tmp));
        if (r == MBEDTLS_ERR_SSL_TIMEOUT) return 0;
        if (r <= 0) {
            impl_->open = false;
            return -1;
        }
        impl_->buf.append(tmp, static_cast<size_t>(r));
        if (impl_->buf.size() > 1024 * 1024) impl_->buf.clear(); // linea absurda: se descarta
    }
}

void LineSocket::close() {
    delete impl_;
    impl_ = new Impl();
}

std::string HttpResponse::header(const std::string& name) const {
    const std::string n = lower(name);
    for (const auto& [k, v] : headers)
        if (lower(k) == n) return v;
    return {};
}

bool HttpsClient::setCaFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_caLock);
    return loadCaLocked(path);
}

void HttpsClient::setDefaultCaSearch(std::vector<std::string> paths) {
    std::lock_guard<std::mutex> lock(g_caLock);
    paths.insert(paths.end(), g_caSearch.begin(), g_caSearch.end());
    g_caSearch = std::move(paths);
}

std::string urlEncode(const std::string& s) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 15]);
        }
    }
    return out;
}

HttpResponse HttpsClient::request(const HttpRequest& req) {
    HttpResponse res;
    Url u;
    if (!parseUrl(req.url, u)) {
        res.error = "URL invalida: " + req.url;
        return res;
    }
    std::ifstream file;
    std::uint64_t bodySize = req.body.size();
    if (!req.bodyFile.empty()) {
        file.open(req.bodyFile, std::ios::binary | std::ios::ate);
        if (!file) {
            res.error = "no se pudo abrir " + req.bodyFile;
            return res;
        }
        bodySize = static_cast<std::uint64_t>(file.tellg());
        file.seekg(0);
    }
    Conn c;
    if (!c.open(u, req.timeoutMs, res.error)) return res;

    std::string head =
        req.method + " " + u.target + " HTTP/1.1\r\nHost: " + u.host +
        ((u.tls && u.port != "443") || (!u.tls && u.port != "80") ? ":" + u.port : "") +
        "\r\nUser-Agent: BroadcastAppliance/1.0\r\nAccept-Encoding: identity\r\nConnection: close\r\n";
    bool hasLength = false;
    for (const auto& [k, v] : req.headers) {
        head += k + ": " + v + "\r\n";
        hasLength = hasLength || lower(k) == "content-length";
    }
    if (!hasLength && (bodySize > 0 || req.method == "POST" || req.method == "PUT"))
        head += "Content-Length: " + std::to_string(bodySize) + "\r\n";
    head += "\r\n";
    if (!c.write(head.data(), head.size())) {
        res.error = "se corto al enviar";
        return res;
    }
    if (!req.bodyFile.empty()) {
        std::vector<char> chunk(256 * 1024);
        std::uint64_t sent = 0;
        while (sent < bodySize) {
            file.read(chunk.data(),
                      static_cast<std::streamsize>(std::min<std::uint64_t>(chunk.size(), bodySize - sent)));
            const std::streamsize got = file.gcount();
            if (got <= 0 || !c.write(chunk.data(), static_cast<size_t>(got))) {
                res.error = "se corto la subida en " + std::to_string(sent) + " de " +
                            std::to_string(bodySize) + " bytes";
                return res;
            }
            sent += static_cast<std::uint64_t>(got);
            if (req.progress) req.progress(sent, bodySize);
        }
    } else if (!req.body.empty()) {
        if (!c.write(req.body.data(), req.body.size())) {
            res.error = "se corto al enviar";
            return res;
        }
        if (req.progress) req.progress(bodySize, bodySize);
    }

    // Respuesta: cabeceras, despues el cuerpo segun como venga.
    std::string raw;
    char buf[16384];
    size_t headEnd = std::string::npos;
    while ((headEnd = raw.find("\r\n\r\n")) == std::string::npos) {
        const int r = c.read(buf, sizeof(buf));
        if (r <= 0) {
            res.error = raw.empty() ? "el servidor no respondio" : "respuesta cortada";
            return res;
        }
        raw.append(buf, static_cast<size_t>(r));
        if (raw.size() > 256 * 1024) {
            res.error = "cabeceras demasiado grandes";
            return res;
        }
    }
    const std::string headers = raw.substr(0, headEnd);
    std::string body = raw.substr(headEnd + 4);
    size_t pos = headers.find("\r\n");
    const std::string status = headers.substr(0, pos);
    if (std::sscanf(status.c_str(), "HTTP/%*d.%*d %d", &res.status) != 1) {
        res.error = "respuesta HTTP invalida";
        return res;
    }
    while (pos != std::string::npos) {
        const size_t next = headers.find("\r\n", pos + 2);
        const std::string line =
            headers.substr(pos + 2, next == std::string::npos ? std::string::npos : next - pos - 2);
        const size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string v = line.substr(colon + 1);
            while (!v.empty() && v.front() == ' ')
                v.erase(0, 1);
            res.headers.emplace_back(line.substr(0, colon), v);
        }
        pos = next;
    }
    const bool chunked = lower(res.header("Transfer-Encoding")).find("chunked") != std::string::npos;
    const std::string cl = res.header("Content-Length");
    const auto readMore = [&]() {
        const int r = c.read(buf, sizeof(buf));
        if (r > 0) body.append(buf, static_cast<size_t>(r));
        return r > 0;
    };
    if (!req.responseFile.empty() && res.status >= 200 && res.status < 300 && !chunked) {
        // Descarga a archivo, en streaming (agregando si el servidor siguió desde
        // donde se cortó: 206 a un pedido con rango).
        const bool sigue = req.resumeFrom > 0 && res.status == 206;
        std::ofstream out(req.responseFile, std::ios::binary | (sigue ? std::ios::app : std::ios::trunc));
        if (!out) {
            res.error = "no se pudo escribir " + req.responseFile;
            return res;
        }
        const std::uint64_t ya = sigue ? req.resumeFrom : 0;
        const std::uint64_t total = cl.empty() ? 0 : ya + std::strtoull(cl.c_str(), nullptr, 10);
        std::uint64_t got = ya + body.size();
        out.write(body.data(), static_cast<std::streamsize>(body.size()));
        body.clear();
        auto lastReport = std::chrono::steady_clock::now();
        while (total == 0 || got < total) {
            if (req.cancel != nullptr && req.cancel->load()) {
                res.error = "cancelado";
                return res;
            }
            const int r = c.read(buf, sizeof(buf));
            if (r <= 0) break;
            out.write(buf, r);
            got += static_cast<std::uint64_t>(r);
            const auto now = std::chrono::steady_clock::now();
            if (req.downloadProgress && now - lastReport > std::chrono::milliseconds(200)) {
                lastReport = now;
                req.downloadProgress(got, total);
            }
        }
        out.flush();
        if (req.downloadProgress) req.downloadProgress(got, total);
        if ((total != 0 && got < total) || !out) res.error = "descarga incompleta";
        return res;
    }
    if (res.status == 204 || res.status == 304 || req.method == "HEAD") {
        body.clear();
    } else if (chunked) {
        std::string out;
        size_t p = 0;
        for (;;) {
            size_t eol;
            while ((eol = body.find("\r\n", p)) == std::string::npos)
                if (!readMore()) break;
            eol = body.find("\r\n", p);
            if (eol == std::string::npos) break;
            const size_t n = std::strtoul(body.substr(p, eol - p).c_str(), nullptr, 16);
            if (n == 0) break;
            while (body.size() < eol + 2 + n + 2)
                if (!readMore()) break;
            if (body.size() < eol + 2 + n) break;
            out.append(body, eol + 2, n);
            p = eol + 2 + n + 2;
        }
        body = std::move(out);
    } else if (!cl.empty()) {
        const size_t want = std::strtoull(cl.c_str(), nullptr, 10);
        while (body.size() < want)
            if (!readMore()) break;
        if (body.size() > want) body.resize(want);
    } else {
        while (readMore()) {
        }
    }
    res.body = std::move(body);
    return res;
}

HttpResponse HttpsClient::download(HttpRequest req) {
    req.method = "GET";
    if (req.resumeFrom > 0) req.headers.emplace_back("Range", "bytes=" + std::to_string(req.resumeFrom) + "-");
    HttpResponse res;
    for (int hop = 0; hop < 6; ++hop) {
        res = request(req);
        if (res.status >= 300 && res.status < 400) {
            std::string loc = res.header("Location");
            if (loc.empty()) break;
            if (loc.rfind("http", 0) != 0) { // relativa
                const auto scheme = req.url.find("://");
                const auto slash = req.url.find('/', scheme == std::string::npos ? 0 : scheme + 3);
                loc = req.url.substr(0, slash) + (loc.front() == '/' ? "" : "/") + loc;
            }
            req.url = loc;
            continue;
        }
        break;
    }
    if (res.error.empty() && (res.status < 200 || res.status >= 300))
        res.error = "HTTP " + std::to_string(res.status);
    return res;
}

} // namespace ba::app::net
