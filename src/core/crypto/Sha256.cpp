// Sha256.cpp
//
// Implementacion de SHA-256 (FIPS 180-4) respaldada por mbedTLS
// (mbedtls_sha256_context). El estado del hash incremental vive en un
// mbedtls_sha256_context; update() delega en mbedtls_sha256_update() y
// digest() clona el contexto, lo finaliza con mbedtls_sha256_finish() y lee
// el digest de 32 bytes.
//
// Se elige mbedTLS (libreria open-source, ya enlazada por ba_core) en lugar de
// una implementacion propia: elimina el riesgo de bugs de baja capa (la version
// anterior tenia un bug latente en la emision del digest) y hereda la
// validacion exhaustiva de mbedTLS (self-tests, FIPS, portabilidad).
//
// Nota: la version del launcher (ba::launcher::Sha256) sigue siendo una
// implementacion independiente; esta del nucleo ahora usa mbedTLS.

#include "Sha256.hpp"

#include <mbedtls/sha256.h>

#include <new>
#include <type_traits>

namespace ba::crypto {

namespace {
// El contexto vive en los bytes opacos de Sha256 (ver el .hpp).
mbedtls_sha256_context* contexto(unsigned char* p) { return std::launder(reinterpret_cast<mbedtls_sha256_context*>(p)); }
const mbedtls_sha256_context* contexto(const unsigned char* p) {
    return std::launder(reinterpret_cast<const mbedtls_sha256_context*>(p));
}
} // namespace

// --- Constructor / reset ----------------------------------------------------

Sha256::Sha256() {
    static_assert(sizeof(mbedtls_sha256_context) <= kTamContexto, "Sha256: el contexto de mbedTLS no entra");
    static_assert(alignof(mbedtls_sha256_context) <= 8, "Sha256: el contexto de mbedTLS pide otra alineacion");
    static_assert(std::is_trivially_copyable_v<mbedtls_sha256_context>, "Sha256 se copia como bytes");
    // En cero, como el `ctx_{}` de antes (mbedtls_sha256_init hace eso mismo).
    mbedtls_sha256_init(new (ctx_) mbedtls_sha256_context);
    reset();
}

void Sha256::reset() {
    // is224 = 0 -> SHA-256. El contexto siempre esta inicializado (init en el
    // constructor), asi que el fallo es practicamente inalcanzable.
    if (mbedtls_sha256_starts(contexto(ctx_), 0) != 0) {
        // Fallo ante un contexto invalido; no se lanza (contrato del nucleo).
    }
}

// --- update -----------------------------------------------------------------

void Sha256::update(const void* data, std::size_t len) {
    if (data == nullptr || len == 0) {
        return;
    }
    const auto* p = static_cast<const std::uint8_t*>(data);
    if (mbedtls_sha256_update(contexto(ctx_), p, len) != 0) {
        // Fallo ante un contexto invalido (practicamente inalcanzable). Se
        // deja el objeto en un estado valido reiniciandolo.
        reset();
    }
}

// --- digest -----------------------------------------------------------------

std::array<std::uint8_t, Sha256::kDigestSize> Sha256::digest() const {
    // Clona el contexto para no mutar el objeto (digest es const).
    // mbedtls_sha256_finish() pone a cero el contexto al terminar, asi que se
    // trabaja sobre la copia y el original queda intacto para seguir hasheando.
    mbedtls_sha256_context clone = *contexto(ctx_);
    std::array<std::uint8_t, kDigestSize> out{};
    if (mbedtls_sha256_finish(&clone, out.data()) != 0) {
        // Fallo ante un contexto invalido (practicamente inalcanzable). Se
        // devuelve un array de 32 bytes valido (cero) para respetar el contrato
        // de que digest() siempre devuelve un array valido.
        out.fill(0);
    }
    return out;
}

// --- comodines ---------------------------------------------------------------

std::array<std::uint8_t, Sha256::kDigestSize> sha256(const void* data, std::size_t len) {
    Sha256 h;
    h.update(data, len);
    return h.digest();
}

std::string sha256Hex(const void* data, std::size_t len) {
    const auto d = sha256(data, len);
    static const char* hexd = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (std::uint8_t b : d) {
        out.push_back(hexd[b >> 4]);
        out.push_back(hexd[b & 0xF]);
    }
    return out;
}

} // namespace ba::crypto
