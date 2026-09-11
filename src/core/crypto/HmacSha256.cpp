// HmacSha256.cpp
//
// Implementacion de HMAC-SHA256 (RFC 2104) respaldada por mbedTLS: usa la API
// generica de message-digest (mbedtls_md) con MBEDTLS_MD_SHA256 y is_hmac=1.
// mbedTLS normaliza la clave segun el RFC (si es mas larga que el bloque se
// reemplaza por su SHA-256; si es mas corta se completa con ceros) y aplica
// ipad = 0x36 / opad = 0x5c internamente.
//
//   HMAC(K, m) = H((K' ^ opad) || H((K' ^ ipad) || m))
//
// Se elige mbedTLS (libreria open-source, ya enlazada por ba_core) en lugar de
// reimplementar el HMAC a mano: elimina el riesgo de bugs de baja capa y
// hereda la validacion exhaustiva de mbedTLS.

#include "HmacSha256.hpp"

#include <mbedtls/md.h>

namespace ba::crypto {

std::array<std::uint8_t, kHmacSha256Size> hmacSha256(const std::uint8_t* key, std::size_t keyLen,
                                                     const std::uint8_t* msg, std::size_t msgLen) {
    std::array<std::uint8_t, kHmacSha256Size> out{};

    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == nullptr) {
        // SHA-256 siempre esta disponible en mbedcrypto; ante lo imposible se
        // devuelve un MAC de 32 bytes valido (cero) para respetar el contrato.
        return out;
    }

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    if (mbedtls_md_setup(&ctx, info, /*hmac=*/1) != 0) {
        mbedtls_md_free(&ctx);
        return out;
    }

    const unsigned char* keyPtr =
        (key != nullptr && keyLen > 0) ? key : reinterpret_cast<const unsigned char*>("\0");
    const unsigned char* msgPtr =
        (msg != nullptr && msgLen > 0) ? msg : reinterpret_cast<const unsigned char*>("\0");

    if (mbedtls_md_hmac_starts(&ctx, keyPtr, keyLen) != 0 ||
        mbedtls_md_hmac_update(&ctx, msgPtr, msgLen) != 0 || mbedtls_md_hmac_finish(&ctx, out.data()) != 0) {
        // Fallo ante un contexto invalido (practicamente inalcanzable). Se
        // deja un MAC de 32 bytes valido (cero) para respetar el contrato.
        out.fill(0);
    }

    mbedtls_md_free(&ctx);
    return out;
}

std::string hmacSha256Hex(const std::string& key, const std::string& msg) {
    const auto mac = hmacSha256(reinterpret_cast<const std::uint8_t*>(key.data()), key.size(),
                                reinterpret_cast<const std::uint8_t*>(msg.data()), msg.size());
    static const char* hexd = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (std::uint8_t b : mac) {
        out.push_back(hexd[b >> 4]);
        out.push_back(hexd[b & 0xF]);
    }
    return out;
}

} // namespace ba::crypto
