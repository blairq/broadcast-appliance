// ============================================================================
// src/gateway/net/WgCrypto.cpp — ver WgCrypto.hpp
// ============================================================================

#include "net/WgCrypto.hpp"

#include <cstring>
#include <ctime>
#include <mutex>

#include <mbedtls/base64.h>
#include <mbedtls/chachapoly.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>

#include "net/Blake2s.hpp"

namespace ba::gateway::net {

namespace {

/// DRBG del proceso (mbedtls no es thread-safe sin MBEDTLS_THREADING_C: se
/// serializa con un mutex propio).
struct Drbg {
    std::mutex m;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr;
    bool ok = false;
    Drbg() {
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&ctr);
        static const char kPers[] = "ba-wireguard";
        ok = mbedtls_ctr_drbg_seed(&ctr, mbedtls_entropy_func, &entropy,
                                   reinterpret_cast<const unsigned char*>(kPers), sizeof(kPers) - 1) == 0;
    }
    ~Drbg() {
        mbedtls_ctr_drbg_free(&ctr);
        mbedtls_entropy_free(&entropy);
    }
};

Drbg& drbg() {
    static Drbg d;
    return d;
}

int rngCallback(void*, unsigned char* out, size_t n) { return randomBytes(out, n) ? 0 : -1; }

void clamp(uint8_t k[32]) {
    k[0] &= 248;
    k[31] &= 127;
    k[31] |= 64;
}

void nonceOf(uint8_t out[12], uint64_t counter) {
    std::memset(out, 0, 4);
    for (int i = 0; i < 8; ++i)
        out[4 + i] = static_cast<uint8_t>(counter >> (8 * i));
}

inline uint32_t rotl(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

inline uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

} // namespace

bool randomBytes(uint8_t* out, size_t n) {
    Drbg& d = drbg();
    std::lock_guard<std::mutex> lock(d.m);
    if (!d.ok) return false;
    while (n > 0) {
        const size_t chunk = n > MBEDTLS_CTR_DRBG_MAX_REQUEST ? MBEDTLS_CTR_DRBG_MAX_REQUEST : n;
        if (mbedtls_ctr_drbg_random(&d.ctr, out, chunk) != 0) return false;
        out += chunk;
        n -= chunk;
    }
    return true;
}

bool x25519(Key32& out, const Key32& priv, const Key32& pub) {
    uint8_t k[32];
    std::memcpy(k, priv.data(), 32);
    clamp(k);
    uint8_t u[32];
    std::memcpy(u, pub.data(), 32);
    u[31] &= 127; // RFC 7748: se ignora el bit alto de u

    mbedtls_ecp_group grp;
    mbedtls_mpi d;
    mbedtls_ecp_point q;
    mbedtls_ecp_point r;
    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&d);
    mbedtls_ecp_point_init(&q);
    mbedtls_ecp_point_init(&r);
    bool ok = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519) == 0 &&
              mbedtls_mpi_read_binary_le(&d, k, 32) == 0 &&
              mbedtls_ecp_point_read_binary(&grp, &q, u, 32) == 0 &&
              mbedtls_ecp_mul(&grp, &r, &d, &q, rngCallback, nullptr) == 0;
    size_t olen = 0;
    ok = ok &&
         mbedtls_ecp_point_write_binary(&grp, &r, MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, out.data(), 32) == 0 &&
         olen == 32;
    mbedtls_ecp_point_free(&r);
    mbedtls_ecp_point_free(&q);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_group_free(&grp);
    wipe(k, sizeof(k));
    if (!ok) return false;
    uint8_t acc = 0;
    for (uint8_t b : out)
        acc |= b;
    return acc != 0; // todo cero = clave publica de orden bajo
}

bool x25519Public(Key32& pub, const Key32& priv) {
    Key32 base{};
    base[0] = 9; // punto base de Curve25519 (u = 9)
    return x25519(pub, priv, base);
}

bool x25519Generate(Key32& priv, Key32& pub) {
    if (!randomBytes(priv.data(), 32)) return false;
    clamp(priv.data());
    return x25519Public(pub, priv);
}

bool aeadEncrypt(uint8_t* out, const Key32& key, uint64_t counter, const uint8_t* in, size_t n,
                 const uint8_t* aad, size_t aadLen) {
    uint8_t nonce[12];
    nonceOf(nonce, counter);
    mbedtls_chachapoly_context ctx;
    mbedtls_chachapoly_init(&ctx);
    const bool ok = mbedtls_chachapoly_setkey(&ctx, key.data()) == 0 &&
                    mbedtls_chachapoly_encrypt_and_tag(&ctx, n, nonce, aad, aadLen, in, out, out + n) == 0;
    mbedtls_chachapoly_free(&ctx);
    return ok;
}

bool aeadDecrypt(uint8_t* out, const Key32& key, uint64_t counter, const uint8_t* in, size_t n,
                 const uint8_t* aad, size_t aadLen) {
    if (n < 16) return false;
    uint8_t nonce[12];
    nonceOf(nonce, counter);
    mbedtls_chachapoly_context ctx;
    mbedtls_chachapoly_init(&ctx);
    const bool ok =
        mbedtls_chachapoly_setkey(&ctx, key.data()) == 0 &&
        mbedtls_chachapoly_auth_decrypt(&ctx, n - 16, nonce, aad, aadLen, in + n - 16, in, out) == 0;
    mbedtls_chachapoly_free(&ctx);
    return ok;
}

void hchacha20(uint8_t out[32], const uint8_t key[32], const uint8_t nonce[16]) {
    uint32_t s[16] = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574};
    for (int i = 0; i < 8; ++i)
        s[4 + i] = le32(key + i * 4);
    for (int i = 0; i < 4; ++i)
        s[12 + i] = le32(nonce + i * 4);
    const auto qr = [&s](int a, int b, int c, int d) {
        s[a] += s[b];
        s[d] = rotl(s[d] ^ s[a], 16);
        s[c] += s[d];
        s[b] = rotl(s[b] ^ s[c], 12);
        s[a] += s[b];
        s[d] = rotl(s[d] ^ s[a], 8);
        s[c] += s[d];
        s[b] = rotl(s[b] ^ s[c], 7);
    };
    for (int i = 0; i < 10; ++i) {
        qr(0, 4, 8, 12);
        qr(1, 5, 9, 13);
        qr(2, 6, 10, 14);
        qr(3, 7, 11, 15);
        qr(0, 5, 10, 15);
        qr(1, 6, 11, 12);
        qr(2, 7, 8, 13);
        qr(3, 4, 9, 14);
    }
    // Salida: palabras 0..3 y 12..15 (sin sumar el estado inicial).
    const int idx[8] = {0, 1, 2, 3, 12, 13, 14, 15};
    for (int i = 0; i < 8; ++i) {
        const uint32_t w = s[idx[i]];
        out[i * 4] = static_cast<uint8_t>(w);
        out[i * 4 + 1] = static_cast<uint8_t>(w >> 8);
        out[i * 4 + 2] = static_cast<uint8_t>(w >> 16);
        out[i * 4 + 3] = static_cast<uint8_t>(w >> 24);
    }
    wipe(s, sizeof(s));
}

namespace {
bool xaead(bool enc, uint8_t* out, const Key32& key, const uint8_t nonce[24], const uint8_t* in, size_t n,
           const uint8_t* aad, size_t aadLen) {
    Key32 sub{};
    hchacha20(sub.data(), key.data(), nonce);
    uint64_t ctr = 0;
    for (int i = 0; i < 8; ++i)
        ctr |= static_cast<uint64_t>(nonce[16 + i]) << (8 * i);
    const bool ok =
        enc ? aeadEncrypt(out, sub, ctr, in, n, aad, aadLen) : aeadDecrypt(out, sub, ctr, in, n, aad, aadLen);
    wipe(sub.data(), sub.size());
    return ok;
}
} // namespace

bool xaeadEncrypt(uint8_t* out, const Key32& key, const uint8_t nonce[24], const uint8_t* in, size_t n,
                  const uint8_t* aad, size_t aadLen) {
    return xaead(true, out, key, nonce, in, n, aad, aadLen);
}

bool xaeadDecrypt(uint8_t* out, const Key32& key, const uint8_t nonce[24], const uint8_t* in, size_t n,
                  const uint8_t* aad, size_t aadLen) {
    return xaead(false, out, key, nonce, in, n, aad, aadLen);
}

void hash(Key32& out, const uint8_t* a, size_t na, const uint8_t* b, size_t nb) {
    Blake2s h(32);
    h.update(a, na);
    if (b != nullptr && nb > 0) h.update(b, nb);
    h.final(out.data());
}

void mac(uint8_t out[16], const uint8_t* key, size_t keyLen, const uint8_t* data, size_t n) {
    Blake2s::hash(out, 16, data, n, key, keyLen);
}

void kdf1(Key32& t1, const Key32& key, const uint8_t* in, size_t n) {
    uint8_t t0[32];
    hmacBlake2s(t0, key.data(), 32, in, n);
    const uint8_t one = 1;
    hmacBlake2s(t1.data(), t0, 32, &one, 1);
    wipe(t0, sizeof(t0));
}

void kdf2(Key32& t1, Key32& t2, const Key32& key, const uint8_t* in, size_t n) {
    uint8_t t0[32];
    hmacBlake2s(t0, key.data(), 32, in, n);
    const uint8_t one = 1;
    hmacBlake2s(t1.data(), t0, 32, &one, 1);
    uint8_t buf[33];
    std::memcpy(buf, t1.data(), 32);
    buf[32] = 2;
    hmacBlake2s(t2.data(), t0, 32, buf, 33);
    wipe(t0, sizeof(t0));
    wipe(buf, sizeof(buf));
}

void kdf3(Key32& t1, Key32& t2, Key32& t3, const Key32& key, const uint8_t* in, size_t n) {
    uint8_t t0[32];
    hmacBlake2s(t0, key.data(), 32, in, n);
    const uint8_t one = 1;
    hmacBlake2s(t1.data(), t0, 32, &one, 1);
    uint8_t buf[33];
    std::memcpy(buf, t1.data(), 32);
    buf[32] = 2;
    hmacBlake2s(t2.data(), t0, 32, buf, 33);
    std::memcpy(buf, t2.data(), 32);
    buf[32] = 3;
    hmacBlake2s(t3.data(), t0, 32, buf, 33);
    wipe(t0, sizeof(t0));
    wipe(buf, sizeof(buf));
}

void tai64n(uint8_t out[12]) {
    timespec ts{};
    timespec_get(&ts, TIME_UTC);
    const uint64_t secs = 0x400000000000000aULL + static_cast<uint64_t>(ts.tv_sec);
    const uint32_t nanos = static_cast<uint32_t>(ts.tv_nsec);
    for (int i = 0; i < 8; ++i)
        out[i] = static_cast<uint8_t>(secs >> (56 - 8 * i));
    for (int i = 0; i < 4; ++i)
        out[8 + i] = static_cast<uint8_t>(nanos >> (24 - 8 * i));
}

std::string toBase64(const Key32& k) {
    unsigned char buf[64];
    size_t olen = 0;
    if (mbedtls_base64_encode(buf, sizeof(buf), &olen, k.data(), 32) != 0) return {};
    return std::string(reinterpret_cast<char*>(buf), olen);
}

bool fromBase64(Key32& k, const std::string& b64) {
    unsigned char buf[48];
    size_t olen = 0;
    if (mbedtls_base64_decode(buf, sizeof(buf), &olen, reinterpret_cast<const unsigned char*>(b64.data()),
                              b64.size()) != 0 ||
        olen != 32) {
        return false;
    }
    std::memcpy(k.data(), buf, 32);
    return true;
}

void wipe(void* p, size_t n) {
    volatile uint8_t* v = static_cast<volatile uint8_t*>(p);
    while (n-- > 0)
        *v++ = 0;
}

} // namespace ba::gateway::net
