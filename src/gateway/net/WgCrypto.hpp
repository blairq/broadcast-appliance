// ============================================================================
// src/gateway/net/WgCrypto.hpp — Primitivas de WireGuard
//
//   X25519            DH sobre Curve25519 (mbedtls)
//   AEAD              ChaCha20-Poly1305 IETF (mbedtls), nonce = 0^32 || ctr LE
//   XAEAD             XChaCha20-Poly1305 (HChaCha20 propio + mbedtls), para
//                     las cookies
//   HASH / MAC        BLAKE2s-256 / BLAKE2s keyed de 16 bytes
//   KDF1/2/3          HKDF sobre HMAC-BLAKE2s (whitepaper de WireGuard, §5.4)
//   TAI64N            marca de tiempo de la iniciacion
//
// Todo verificado con vectores publicos (RFC 7748, RFC 8439, RFC 7693,
// draft-irtf-cfrg-xchacha) en test_wireguard.
// ============================================================================
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ba::gateway::net {

using Key32 = std::array<uint8_t, 32>;

/// Bytes aleatorios criptograficos. false si la fuente de entropia falla.
bool randomBytes(uint8_t* out, size_t n);

/// Clave privada X25519 nueva (ya "clampeada").
bool x25519Generate(Key32& priv, Key32& pub);
/// Publica a partir de la privada.
bool x25519Public(Key32& pub, const Key32& priv);
/// Secreto compartido DH(priv, pub). false si el resultado es cero (punto de
/// orden bajo: WireGuard rechaza esas claves).
bool x25519(Key32& out, const Key32& priv, const Key32& pub);

/// ChaCha20-Poly1305 con nonce de WireGuard (4 ceros + contador LE de 64 bits).
/// `out` recibe n + 16 bytes (texto cifrado + tag).
bool aeadEncrypt(uint8_t* out, const Key32& key, uint64_t counter, const uint8_t* in, size_t n,
                 const uint8_t* aad, size_t aadLen);
/// Descifra n bytes (incluye los 16 de tag); `out` recibe n - 16.
bool aeadDecrypt(uint8_t* out, const Key32& key, uint64_t counter, const uint8_t* in, size_t n,
                 const uint8_t* aad, size_t aadLen);

/// XChaCha20-Poly1305 (nonce de 24 bytes).
bool xaeadEncrypt(uint8_t* out, const Key32& key, const uint8_t nonce[24], const uint8_t* in, size_t n,
                  const uint8_t* aad, size_t aadLen);
bool xaeadDecrypt(uint8_t* out, const Key32& key, const uint8_t nonce[24], const uint8_t* in, size_t n,
                  const uint8_t* aad, size_t aadLen);
/// HChaCha20 (subclave para XChaCha20).
void hchacha20(uint8_t out[32], const uint8_t key[32], const uint8_t nonce[16]);

/// HASH = BLAKE2s-256.
void hash(Key32& out, const uint8_t* a, size_t na, const uint8_t* b = nullptr, size_t nb = 0);
/// MAC = BLAKE2s keyed de 16 bytes.
void mac(uint8_t out[16], const uint8_t* key, size_t keyLen, const uint8_t* data, size_t n);
/// KDF1/2/3: HKDF de WireGuard sobre HMAC-BLAKE2s.
void kdf1(Key32& t1, const Key32& key, const uint8_t* in, size_t n);
void kdf2(Key32& t1, Key32& t2, const Key32& key, const uint8_t* in, size_t n);
void kdf3(Key32& t1, Key32& t2, Key32& t3, const Key32& key, const uint8_t* in, size_t n);

/// Marca TAI64N de "ahora" (12 bytes, big-endian).
void tai64n(uint8_t out[12]);

/// Claves en base64 (el formato de wg / wg-quick).
std::string toBase64(const Key32& k);
bool fromBase64(Key32& k, const std::string& b64);

/// Borra memoria sensible (que el compilador no puede quitar).
void wipe(void* p, size_t n);

} // namespace ba::gateway::net
