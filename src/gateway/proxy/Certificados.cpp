// ============================================================================
// src/gateway/proxy/Certificados.cpp — ver Certificados.hpp
// ============================================================================

#include "proxy/Certificados.hpp"

#include <array>
#include <cstring>
#include <ctime>
#include <mutex>

#include <mbedtls/base64.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/x509_csr.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

namespace ba::gateway::proxy {

namespace {

/// Un generador compartido (detrás de un mutex): sacar claves y firmar es raro.
struct Azar {
    std::mutex mu;
    mbedtls_entropy_context entropia;
    mbedtls_ctr_drbg_context drbg;
    bool listo = false;
    Azar() {
        mbedtls_entropy_init(&entropia);
        mbedtls_ctr_drbg_init(&drbg);
        static const char kPers[] = "ba-proxy";
        listo = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropia,
                                      reinterpret_cast<const unsigned char*>(kPers), sizeof(kPers) - 1) == 0;
    }
    static int rng(void* p, unsigned char* out, size_t n) {
        auto* a = static_cast<Azar*>(p);
        std::lock_guard<std::mutex> l(a->mu);
        return mbedtls_ctr_drbg_random(&a->drbg, out, n);
    }
};

Azar& azar() {
    static Azar a;
    return a;
}

bool leerClave(const std::string& pem, mbedtls_pk_context& pk) {
    std::string z = pem;
    z.push_back('\0');
    return mbedtls_pk_parse_key(&pk, reinterpret_cast<const unsigned char*>(z.data()), z.size(), nullptr, 0,
                                &Azar::rng, &azar()) == 0;
}

/// La lista SAN de mbedtls para `nombres` (los punteros apuntan a `guarda`).
struct ListaSan {
    std::vector<mbedtls_x509_san_list> nodos;
    std::vector<std::array<unsigned char, 4>> ips;
    std::vector<std::string> guarda;
    explicit ListaSan(const std::vector<std::string>& nombres) : nodos(nombres.size()), ips(nombres.size()), guarda(nombres) {
        for (size_t i = 0; i < guarda.size(); ++i) {
            mbedtls_x509_san_list& n = nodos[i];
            std::memset(&n, 0, sizeof(n));
            in_addr a{};
            if (inet_pton(AF_INET, guarda[i].c_str(), &a) == 1) {
                std::memcpy(ips[i].data(), &a, 4);
                n.node.type = MBEDTLS_X509_SAN_IP_ADDRESS;
                n.node.san.unstructured_name.p = ips[i].data();
                n.node.san.unstructured_name.len = 4;
            } else {
                n.node.type = MBEDTLS_X509_SAN_DNS_NAME;
                n.node.san.unstructured_name.p = reinterpret_cast<unsigned char*>(guarda[i].data());
                n.node.san.unstructured_name.len = guarda[i].size();
            }
            if (i > 0) nodos[i - 1].next = &n;
        }
    }
    const mbedtls_x509_san_list* primero() const { return nodos.empty() ? nullptr : &nodos[0]; }
};

std::string sujeto(const std::vector<std::string>& nombres) {
    // El CN no admite comas ni '=' sin escapar; los nombres de servicio no los tienen.
    return "CN=" + (nombres.empty() ? std::string("Broadcast Appliance") : nombres.front()) + ",O=Broadcast Appliance";
}

} // namespace

std::string claveNueva() {
    if (!azar().listo) return {};
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    std::string out;
    if (mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) == 0 &&
        mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(pk), &Azar::rng, &azar()) == 0) {
        std::vector<unsigned char> b(2048);
        if (mbedtls_pk_write_key_pem(&pk, b.data(), b.size()) == 0) out = reinterpret_cast<char*>(b.data());
        mbedtls_platform_zeroize(b.data(), b.size());
    }
    mbedtls_pk_free(&pk);
    return out;
}

std::string autofirmado(const std::string& clavePem, const std::vector<std::string>& nombres) {
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    std::string out;
    if (leerClave(clavePem, pk)) {
        mbedtls_x509write_cert w;
        mbedtls_x509write_crt_init(&w);
        mbedtls_x509write_crt_set_version(&w, MBEDTLS_X509_CRT_VERSION_3);
        mbedtls_x509write_crt_set_md_alg(&w, MBEDTLS_MD_SHA256);
        mbedtls_x509write_crt_set_subject_key(&w, &pk);
        mbedtls_x509write_crt_set_issuer_key(&w, &pk);
        const std::string s = sujeto(nombres);
        unsigned char serie[16];
        Azar::rng(&azar(), serie, sizeof(serie));
        serie[0] &= 0x7F;
        const ListaSan san(nombres);
        bool ok = mbedtls_x509write_crt_set_subject_name(&w, s.c_str()) == 0 &&
                  mbedtls_x509write_crt_set_issuer_name(&w, s.c_str()) == 0 &&
                  mbedtls_x509write_crt_set_serial_raw(&w, serie, sizeof(serie)) == 0 &&
                  mbedtls_x509write_crt_set_validity(&w, "20250101000000", "20351231235959") == 0 &&
                  mbedtls_x509write_crt_set_basic_constraints(&w, 0, -1) == 0 &&
                  mbedtls_x509write_crt_set_key_usage(&w, MBEDTLS_X509_KU_DIGITAL_SIGNATURE) == 0;
        if (ok && san.primero() != nullptr) ok = mbedtls_x509write_crt_set_subject_alternative_name(&w, san.primero()) == 0;
        std::vector<unsigned char> b(8192);
        if (ok && mbedtls_x509write_crt_pem(&w, b.data(), b.size(), &Azar::rng, &azar()) == 0)
            out = reinterpret_cast<char*>(b.data());
        mbedtls_x509write_crt_free(&w);
    }
    mbedtls_pk_free(&pk);
    return out;
}

std::vector<unsigned char> csrDer(const std::string& clavePem, const std::vector<std::string>& nombres) {
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    std::vector<unsigned char> out;
    if (leerClave(clavePem, pk)) {
        mbedtls_x509write_csr w;
        mbedtls_x509write_csr_init(&w);
        mbedtls_x509write_csr_set_md_alg(&w, MBEDTLS_MD_SHA256);
        mbedtls_x509write_csr_set_key(&w, &pk);
        const std::string s = sujeto(nombres);
        const ListaSan san(nombres);
        bool ok = mbedtls_x509write_csr_set_subject_name(&w, s.c_str()) == 0;
        if (ok && san.primero() != nullptr) ok = mbedtls_x509write_csr_set_subject_alternative_name(&w, san.primero()) == 0;
        std::vector<unsigned char> b(4096);
        // mbedtls escribe el DER al FINAL del buffer y devuelve el largo.
        const int n = ok ? mbedtls_x509write_csr_der(&w, b.data(), b.size(), &Azar::rng, &azar()) : -1;
        if (n > 0) out.assign(b.end() - n, b.end());
        mbedtls_x509write_csr_free(&w);
    }
    mbedtls_pk_free(&pk);
    return out;
}

std::int64_t vence(const std::string& certPem) {
    mbedtls_x509_crt c;
    mbedtls_x509_crt_init(&c);
    std::string z = certPem;
    z.push_back('\0');
    std::int64_t out = 0;
    if (mbedtls_x509_crt_parse(&c, reinterpret_cast<const unsigned char*>(z.data()), z.size()) == 0) {
        std::tm t{};
        t.tm_year = c.valid_to.year - 1900;
        t.tm_mon = c.valid_to.mon - 1;
        t.tm_mday = c.valid_to.day;
        t.tm_hour = c.valid_to.hour;
        t.tm_min = c.valid_to.min;
        t.tm_sec = c.valid_to.sec;
#ifdef _WIN32
        out = static_cast<std::int64_t>(_mkgmtime(&t));
#else
        out = static_cast<std::int64_t>(timegm(&t));
#endif
    }
    mbedtls_x509_crt_free(&c);
    return out;
}

std::vector<std::string> nombresDe(const std::string& certPem) {
    mbedtls_x509_crt c;
    mbedtls_x509_crt_init(&c);
    std::string z = certPem;
    z.push_back('\0');
    std::vector<std::string> out;
    if (mbedtls_x509_crt_parse(&c, reinterpret_cast<const unsigned char*>(z.data()), z.size()) == 0)
        for (const mbedtls_x509_sequence* s = &c.subject_alt_names; s != nullptr && s->buf.p != nullptr; s = s->next)
            if ((s->buf.tag & MBEDTLS_ASN1_TAG_VALUE_MASK) == MBEDTLS_X509_SAN_DNS_NAME)
                out.emplace_back(reinterpret_cast<const char*>(s->buf.p), s->buf.len);
    mbedtls_x509_crt_free(&c);
    return out;
}

std::string sha256(const std::string& datos) {
    unsigned char h[32];
    mbedtls_sha256(reinterpret_cast<const unsigned char*>(datos.data()), datos.size(), h, 0);
    return std::string(reinterpret_cast<char*>(h), 32);
}

bool coordenadas(const std::string& clavePem, std::string& x, std::string& y) {
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    bool ok = false;
    if (leerClave(clavePem, pk) && mbedtls_pk_get_type(&pk) == MBEDTLS_PK_ECKEY) {
        // 0x04 ‖ X ‖ Y (punto sin comprimir de P-256).
        unsigned char b[65];
        size_t n = 0;
        mbedtls_ecp_keypair* kp = mbedtls_pk_ec(pk);
        mbedtls_ecp_group grp;
        mbedtls_ecp_point q;
        mbedtls_mpi d;
        mbedtls_ecp_group_init(&grp);
        mbedtls_ecp_point_init(&q);
        mbedtls_mpi_init(&d);
        if (mbedtls_ecp_export(kp, &grp, &d, &q) == 0 &&
            mbedtls_ecp_point_write_binary(&grp, &q, MBEDTLS_ECP_PF_UNCOMPRESSED, &n, b, sizeof b) == 0 && n == 65) {
            x.assign(reinterpret_cast<char*>(b + 1), 32);
            y.assign(reinterpret_cast<char*>(b + 33), 32);
            ok = true;
        }
        mbedtls_mpi_free(&d);
        mbedtls_ecp_point_free(&q);
        mbedtls_ecp_group_free(&grp);
    }
    mbedtls_pk_free(&pk);
    return ok;
}

bool firmarEs256(const std::string& clavePem, const std::string& datos, std::string& firma) {
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    bool ok = false;
    if (leerClave(clavePem, pk)) {
        const std::string h = sha256(datos);
        unsigned char der[MBEDTLS_PK_SIGNATURE_MAX_SIZE];
        size_t n = 0;
        if (mbedtls_pk_sign(&pk, MBEDTLS_MD_SHA256, reinterpret_cast<const unsigned char*>(h.data()), h.size(), der,
                            sizeof der, &n, &Azar::rng, &azar()) == 0) {
            // DER: SEQUENCE { INTEGER r, INTEGER s } -> r y s de 32 bytes cada uno.
            size_t i = 0;
            const auto largo = [&](size_t& l) {
                l = der[i++];
                if (l & 0x80) {
                    const size_t k = l & 0x7F;
                    l = 0;
                    for (size_t j = 0; j < k; ++j) l = (l << 8) | der[i++];
                }
            };
            const auto entero = [&](std::string& out) -> bool {
                if (der[i++] != 0x02) return false;
                size_t l = 0;
                largo(l);
                const unsigned char* p = der + i;
                i += l;
                while (l > 32 && *p == 0) ++p, --l;
                if (l > 32) return false;
                out.assign(32 - l, '\0');
                out.append(reinterpret_cast<const char*>(p), l);
                return true;
            };
            size_t l = 0;
            std::string r, s;
            if (n > 8 && der[i++] == 0x30) {
                largo(l);
                ok = entero(r) && entero(s);
            }
            if (ok) firma = r + s;
        }
    }
    mbedtls_pk_free(&pk);
    return ok;
}

std::string base64url(const unsigned char* p, size_t n) {
    std::vector<unsigned char> b(4 * ((n + 2) / 3) + 4);
    size_t largo = 0;
    if (mbedtls_base64_encode(b.data(), b.size(), &largo, p, n) != 0) return {};
    std::string s(reinterpret_cast<char*>(b.data()), largo);
    for (char& c : s) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    while (!s.empty() && s.back() == '=') s.pop_back();
    return s;
}

} // namespace ba::gateway::proxy
