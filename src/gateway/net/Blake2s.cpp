// ============================================================================
// src/gateway/net/Blake2s.cpp — ver Blake2s.hpp (RFC 7693, seccion 3)
// ============================================================================

#include "net/Blake2s.hpp"

#include <cstring>

namespace ba::gateway::net {

namespace {

constexpr uint32_t kIv[8] = {0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
                             0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19};

constexpr uint8_t kSigma[10][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
    {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4},
    {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
    {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13},
    {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
    {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11},
    {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
    {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5},
    {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0},
};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

inline uint32_t load32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline void store32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

} // namespace

Blake2s::Blake2s(size_t outLen, const uint8_t* key, size_t keyLen) : outLen_(outLen) {
    for (int i = 0; i < 8; ++i)
        h_[static_cast<size_t>(i)] = kIv[i];
    // Parametros: digest length, key length, fanout = depth = 1.
    h_[0] ^= 0x01010000u ^ (static_cast<uint32_t>(keyLen) << 8) ^ static_cast<uint32_t>(outLen);
    if (keyLen > 0) {
        uint8_t block[64] = {0};
        std::memcpy(block, key, keyLen);
        update(block, 64);
        std::memset(block, 0, sizeof(block));
    }
}

void Blake2s::compress(bool last) {
    uint32_t v[16];
    uint32_t m[16];
    for (int i = 0; i < 8; ++i) {
        v[i] = h_[static_cast<size_t>(i)];
        v[i + 8] = kIv[i];
    }
    v[12] ^= t_[0];
    v[13] ^= t_[1];
    if (last) v[14] = ~v[14];
    for (int i = 0; i < 16; ++i)
        m[i] = load32(buf_.data() + i * 4);
    const auto g = [&](int a, int b, int c, int d, uint32_t x, uint32_t y) {
        v[a] = v[a] + v[b] + x;
        v[d] = rotr(v[d] ^ v[a], 16);
        v[c] = v[c] + v[d];
        v[b] = rotr(v[b] ^ v[c], 12);
        v[a] = v[a] + v[b] + y;
        v[d] = rotr(v[d] ^ v[a], 8);
        v[c] = v[c] + v[d];
        v[b] = rotr(v[b] ^ v[c], 7);
    };
    for (int r = 0; r < 10; ++r) {
        const uint8_t* s = kSigma[r];
        g(0, 4, 8, 12, m[s[0]], m[s[1]]);
        g(1, 5, 9, 13, m[s[2]], m[s[3]]);
        g(2, 6, 10, 14, m[s[4]], m[s[5]]);
        g(3, 7, 11, 15, m[s[6]], m[s[7]]);
        g(0, 5, 10, 15, m[s[8]], m[s[9]]);
        g(1, 6, 11, 12, m[s[10]], m[s[11]]);
        g(2, 7, 8, 13, m[s[12]], m[s[13]]);
        g(3, 4, 9, 14, m[s[14]], m[s[15]]);
    }
    for (int i = 0; i < 8; ++i)
        h_[static_cast<size_t>(i)] ^= v[i] ^ v[i + 8];
}

void Blake2s::update(const uint8_t* data, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        // El ultimo bloque se comprime en final() (con la marca de "ultimo"),
        // asi que un bloque lleno solo se procesa cuando llega mas entrada.
        if (bufLen_ == 64) {
            t_[0] += 64;
            if (t_[0] < 64) ++t_[1];
            compress(false);
            bufLen_ = 0;
        }
        buf_[bufLen_++] = data[i];
    }
}

void Blake2s::final(uint8_t* out) {
    t_[0] += static_cast<uint32_t>(bufLen_);
    if (t_[0] < bufLen_) ++t_[1];
    std::memset(buf_.data() + bufLen_, 0, 64 - bufLen_);
    compress(true);
    uint8_t full[32];
    for (int i = 0; i < 8; ++i)
        store32(full + i * 4, h_[static_cast<size_t>(i)]);
    std::memcpy(out, full, outLen_);
}

void Blake2s::hash(uint8_t* out, size_t outLen, const uint8_t* data, size_t n, const uint8_t* key,
                   size_t keyLen) {
    Blake2s b(outLen, key, keyLen);
    b.update(data, n);
    b.final(out);
}

void hmacBlake2s(uint8_t out[32], const uint8_t* key, size_t keyLen, const uint8_t* data, size_t n) {
    uint8_t k[64] = {0};
    if (keyLen > 64) {
        Blake2s::hash(k, 32, key, keyLen);
    } else {
        std::memcpy(k, key, keyLen);
    }
    uint8_t ipad[64];
    uint8_t opad[64];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = static_cast<uint8_t>(k[i] ^ 0x36);
        opad[i] = static_cast<uint8_t>(k[i] ^ 0x5c);
    }
    uint8_t inner[32];
    Blake2s a(32);
    a.update(ipad, 64);
    a.update(data, n);
    a.final(inner);
    Blake2s b(32);
    b.update(opad, 64);
    b.update(inner, 32);
    b.final(out);
    std::memset(k, 0, sizeof(k));
}

} // namespace ba::gateway::net
