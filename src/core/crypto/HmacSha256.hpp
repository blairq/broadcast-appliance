// HmacSha256.hpp
//
// Proposito:
//   HMAC-SHA256 (RFC 2104) para el nucleo ba_core, ahora RESPALDADO POR
//   mbedTLS (API generica mbedtls_md con MBEDTLS_MD_SHA256 e is_hmac=1). Es la
//   primitiva de mensaje autenticado (MAC) que firma los tokens de senalizacion
//   (spec 8.3) y que podra usar cualquier modulo que necesite integridad +
//   autenticacion. No depende de Qt ni de libobs.
//
//   Se elige mbedTLS (libreria open-source, ya enlazada por ba_core) en lugar
//   de reimplementar el HMAC a mano: elimina el riesgo de bugs de baja capa y
//   hereda la validacion exhaustiva de mbedTLS.
//
// Modelo de threading:
//   Funciones puras y sin estado: thread-safe, pueden llamarse desde cualquier
//   thread a la vez.
//
// Estrategia de errores:
//   No lanza. key==nullptr con keyLen>0 y msg==nullptr con msgLen>0 violan una
//   precondicion del caller (se asumen punteros validos); con len==0 no se
//   lee memoria. La clave se normaliza segun el RFC: si es mas larga que el
//   bloque (64 bytes) se reemplaza por su SHA-256; si es mas corta se completa
//   con ceros.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ba::crypto {

/// Tamano del bloque de SHA-256 en bytes (64), usado por el HMAC.
inline constexpr std::size_t kHmacSha256BlockSize = 64;
/// Tamano de la salida del HMAC-SHA256 en bytes (32).
inline constexpr std::size_t kHmacSha256Size = 32;

/// Calcula HMAC-SHA256 (RFC 2104) de `msg` con `key`.
///
///   HMAC(K, m) = H((K' ^ opad) || H((K' ^ ipad) || m))
///
/// donde K' es la clave normalizada a 64 bytes (SHA-256 de la clave si es mas
/// larga; la clave completada con ceros si es mas corta), ipad = 0x36 y
/// opad = 0x5c.
///
/// :param key: clave (secret).
/// :param keyLen: longitud de la clave en bytes.
/// :param msg: mensaje a firmar.
/// :param msgLen: longitud del mensaje en bytes.
/// :returns: el MAC de 32 bytes.
std::array<std::uint8_t, kHmacSha256Size> hmacSha256(const std::uint8_t* key, std::size_t keyLen,
                                                     const std::uint8_t* msg, std::size_t msgLen);

/// Comodino de `hmacSha256` con clave/mensaje como std::string, devolviendo el
/// MAC en hex en minuscula (64 chars).
///
/// :param key: clave (secret) como string.
/// :param msg: mensaje a firmar como string.
/// :returns: representacion hexadecimal en minuscula del MAC.
std::string hmacSha256Hex(const std::string& key, const std::string& msg);

} // namespace ba::crypto
