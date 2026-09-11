// ============================================================================
// src/gateway/TokenAuth.hpp — Auth de invitado sobre ba_core::security::TokenService
//
// Proposito:
//   Envoltorio thread-safe del contrato `ba_core::security::TokenService`
//   (spec §8.3) para el servidor de señalización. El servidor despacha
//   peticiones de varios invitados (varias conexiones) desde distintos
//   threads, pero `TokenService` NO es thread-safe por si mismo (su docstring
//   lo dice: "una instancia NO es thread-safe por si misma"). Este wrapper
//   protege el acceso con un mutex y expone dos operaciones de alto nivel:
//     - issue(room, pin): emite un token corto firmado si el PIN es valido y no
//       hay lockout; cadena vacia si no.
//     - verify(token, room): verifica el token contra la sala; devuelve 0 si es
//       valido y un codigo negativo estable con el motivo del fallo si no.
//
//   El validador de PIN se inyecta en el TokenService (no se guarda el PIN en
//   claro en el servicio): compara contra el PIN de la sala, que puede cambiar
//   en caliente (ChangePin, spec §4.1) vía setPin(). El PIN vive en una
//   `shared_ptr<std::string>` compartida entre el wrapper y el callback, de
//   modo que setPin() muta el valor que el callback lee sin re-crear el
//   servicio.
//
// Modelo de threading:
//   Thread-safe: cada operacion toma el mutex. El owner (el servidor) no debe
//   sincronizar por su cuenta.
//
// Estrategia de errores:
//   No lanza. issue() devuelve "" en fallo; verify() devuelve un int (0 = ok,
//   negativo = motivo). Los codigos de verify mapean VerifyResult de forma
//   estable para que el servidor responda con el mensaje adecuado.
// ============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "security/TokenService.hpp"

namespace ba::gateway {

/// Wrapper thread-safe de `ba::security::TokenService` para el gateway.
class TokenAuth {
   public:
    /// Codigos de `verify()` (estables, para el log y la respuesta HTTP/WS).
    enum class VerifyCode : int {
        kOk = 0,
        kMalformed = -1,
        kBadSignature = -2,
        kExpired = -3,
        kWrongRoom = -4,
    };

    /// :param secret: clave HMAC que firma/verifica los tokens.
    /// :param pin: PIN inicial de la sala.
    /// :param maxAttempts: intentos de PIN antes del lockout (default 5).
    /// :param lockoutSec: duracion del lockout en segundos (default 60).
    /// :param ttlSec: vida util de los tokens emitidos (default 300).
    TokenAuth(std::string secret, std::string pin, int maxAttempts = 5, int lockoutSec = 60,
              int ttlSec = 300);

    /// Cambia el PIN de la sala en caliente (ChangePin, spec §4.1).
    ///
    /// :param pin: nuevo PIN.
    void setPin(const std::string& pin);

    /// Emite un token corto firmado para (room, pin) si el PIN es valido y no
    /// hay lockout.
    ///
    /// :param room: identificador de sala.
    /// :param pin: PIN presentado por el invitado.
    /// :returns: el token "b64url(payload).b64url(mac)"; cadena vacia si no se
    ///           emite (PIN invalido, lockout activo). Un fallo de PIN alimenta
    ///           el lockout.
    std::string issue(const std::string& room, const std::string& pin);

    /// Verifica un token contra la sala.
    ///
    /// :param token: token emitido por issue().
    /// :param room: sala esperada (debe coincidir con la del payload).
    /// :returns: VerifyCode::kOk (0) si todo esta bien; si no, el motivo
    ///           (kMalformed/kBadSignature/kExpired/kWrongRoom).
    int verify(const std::string& token, const std::string& room) const;

    /// Vida util configurada de los tokens (segundos).
    int ttlSec() const { return ttlSec_; }

   private:
    // `mutable`: `verify()` es const pero toma el lock (el PIN que lee el
    // callback es estado mutable compartido; se serializa todo).
    mutable std::mutex m_;
    // PIN actual de la sala, compartido con el callback inyectado en el
    // TokenService: el callback captura una copia de este shared_ptr, asi
    // setPin() (*pin_ = nuevo) muta el valor que el callback lee.
    // Se declara ANTES de `svc_` para que el ctor pueda inicializar `svc_`
    // capturando `pin_` ya construido (orden de inicializacion = declaracion).
    std::shared_ptr<std::string> pin_;
    // unique_ptr: `TokenService` NO tiene ctor por defecto (requiere secret +
    // validador), por lo que no puede ser miembro por valor default-constructed.
    std::unique_ptr<ba::security::TokenService> svc_;
    int ttlSec_;
};

} // namespace ba::gateway
