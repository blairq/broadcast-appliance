// Sha256.hpp
//
// Proposito:
//   SHA-256 (FIPS 180-4) para el nucleo ba_core, ahora RESPALDADO POR mbedTLS
//   (mbedtls_sha256_context). La consumen los modulos de seguridad
//   (HMAC-SHA256, TokenService, spec 8.3) y cualquier componente que necesite
//   un hash de integridad. No depende de Qt.
//
//   Se elige mbedTLS (libreria open-source, ya enlazada por ba_core) en lugar
//   de una implementacion propia: elimina el riesgo de bugs de baja capa (la
//   version anterior tenia un bug latente en la emision del digest) y hereda la
//   validacion exhaustiva de mbedTLS. El namespace sigue siendo ba::crypto,
//   independiente del launcher (ba::launcher::Sha256): no hay colision de
//   simbolos.
//
// Modelo de threading:
//   Cada instancia la usa UN thread a la vez (no es thread-safe). Para hashear
//   en paralelo se crea una instancia por worker, no se comparten instancias.
//   Las funciones comodin (sha256/sha256Hex) son puras y thread-safe.
//
// Estrategia de errores:
//   No lanza. update() con data==nullptr y len>0 viola una precondicion del
//   caller (se asume data valida); con len==0 no hace nada. digest() siempre
//   devuelve un array de 32 bytes valido y NO muta el objeto (es const): se
//   puede llamar varias veces y seguir hasheando el mismo mensaje. sha256/
//   sha256Hex con data==nullptr y len==0 devuelven el hash de cadena vacia.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <mbedtls/sha256.h>

namespace ba::crypto {

/// SHA-256 incremental (FIPS 180-4).
///
/// Uso tipico:
///   Sha256 h;
///   h.update(parte1, n1);
///   h.update(parte2, n2);
///   auto digest = h.digest(); // 32 bytes, no muta `h`
class Sha256 {
   public:
    /// Tamano del digest en bytes (32).
    static constexpr std::size_t kDigestSize = 32;

    Sha256();

    /// Reinicia el estado para hashear un mensaje nuevo desde cero.
    void reset();

    /// Absorbe `len` bytes del mensaje. Puede llamarse varias veces para
    /// hashear en trozos (el estado interno acumula).
    ///
    /// :param data: puntero a los bytes a hashear.
    /// :param len: cantidad de bytes. Si es 0, no hace nada.
    void update(const void* data, std::size_t len);

    /// Finaliza y devuelve el digest de 32 bytes.
    ///
    /// No muta el objeto: se trabaja sobre una copia interna, de modo que
    /// `digest()` es const y puede llamarse varias veces (o seguir hasheando
    /// con `update` despues).
    ///
    /// :returns: el hash de 32 bytes del mensaje absorbido hasta ahora.
    std::array<std::uint8_t, kDigestSize> digest() const;

   private:
    /// Contexto de mbedTLS que acumula el estado del hash incremental.
    /// Detalle de implementacion privada (no forma parte de la API publica).
    mbedtls_sha256_context ctx_{};
};

/// Hashea `data` de una vez y devuelve el digest de 32 bytes.
///
/// :param data: puntero a los bytes.
/// :param len: cantidad de bytes.
/// :returns: el hash de 32 bytes.
std::array<std::uint8_t, Sha256::kDigestSize> sha256(const void* data, std::size_t len);

/// Hashea `data` de una vez y devuelve el hex en minuscula (64 chars).
///
/// :param data: puntero a los bytes.
/// :param len: cantidad de bytes.
/// :returns: representacion hexadecimal en minuscula del digest.
std::string sha256Hex(const void* data, std::size_t len);

} // namespace ba::crypto
