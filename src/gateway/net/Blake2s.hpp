// ============================================================================
// src/gateway/net/Blake2s.hpp — BLAKE2s (RFC 7693) y HMAC-BLAKE2s
//
// WireGuard usa BLAKE2s-256 como hash (Noise_...BLAKE2s), BLAKE2s con clave
// de 16 bytes para MAC1/MAC2, y HMAC-BLAKE2s dentro de su HKDF. mbedtls no
// trae BLAKE2s, asi que va aca (implementacion de referencia del RFC, sin
// dependencias). Verificado con los vectores del RFC 7693 (test_wireguard).
// ============================================================================
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ba::gateway::net {

class Blake2s {
   public:
    /// :param outLen: 1..32 bytes de salida.
    /// :param key/keyLen: clave opcional (0..32 bytes; BLAKE2s "keyed").
    explicit Blake2s(size_t outLen = 32, const uint8_t* key = nullptr, size_t keyLen = 0);
    void update(const uint8_t* data, size_t n);
    void final(uint8_t* out);

    /// Hash de una sola llamada.
    static void hash(uint8_t* out, size_t outLen, const uint8_t* data, size_t n, const uint8_t* key = nullptr,
                     size_t keyLen = 0);

   private:
    void compress(bool last);
    std::array<uint32_t, 8> h_{};
    std::array<uint32_t, 2> t_{};
    std::array<uint8_t, 64> buf_{};
    size_t bufLen_ = 0;
    size_t outLen_ = 32;
};

/// HMAC-BLAKE2s-256 (RFC 2104 sobre BLAKE2s, bloque de 64 bytes).
void hmacBlake2s(uint8_t out[32], const uint8_t* key, size_t keyLen, const uint8_t* data, size_t n);

} // namespace ba::gateway::net
