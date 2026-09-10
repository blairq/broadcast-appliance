// TokenService.hpp
//
// Proposito:
//   Seguridad de la senalizacion (spec 8.3): emite y verifica tokens cortos
//   firmados con HMAC a partir de "sala + PIN", y aplica el limite de
//   intentos de PIN con bloqueo (rate-limit / lockout). Es testeable por
//   unidad: el reloj ("now") es inyectable y la validacion del PIN se inyecta
//   como callback, de modo que el PIN no se guarda en claro y los tests son
//   deterministas. No depende de Qt ni de libobs.
//
//   Formato del token:  base64url(payload) + "." + base64url(mac)
//     - payload = JSON compacto {"room":..., "exp":<unix-sec>, "nonce":...}
//     - mac     = HMAC-SHA256(key=secret, msg=base64url(payload))
//   El token es autocontenido: el verificador solo necesita el mismo `secret`.
//   La firma cubre el payload codificado, asi que cualquier alteracion del
//   payload o del mac se detecta como BadSignature.
//
// Modelo de threading:
//   Una instancia NO es thread-safe por si misma: la usa un solo owner (p. ej.
//   el thread del servidor de senalizacion que despacha las peticiones de
//   entrada a sala). El contador de intentos, el lockout y el nonce son estado
//   mutable sin lock. Para usarla desde varios threads, el owner debe
//   sincronizar el acceso.
//
// Estrategia de errores:
//   No lanza. issue() devuelve cadena vacia cuando no emite (PIN invalido,
//   lockout activo, o sin validator configurado); un fallo de PIN alimenta el
//   lockout. verify() devuelve un VerifyResult que distingue el motivo del
//   fallo (Malformed / BadSignature / Expired / WrongRoom) para que el
//   servidor responda con el mensaje adecuado.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ba::security {

/// Reloj de referencia en segundos desde la epoca Unix.
///
/// Es el "now" por defecto de issue()/verify() y de la ventana de lockout. En
/// los tests se inyecta un valor propio para que la expiracion y el bloqueo
/// sean deterministas.
///
/// :returns: segundos Unix actuales.
int nowUnix();

/// Codifica `data` en base64url (RFC 4648, variante URL) SIN padding.
///
/// :param data: puntero a los bytes.
/// :param len: cantidad de bytes.
/// :returns: cadena base64url (solo A-Z a-z 0-9 - _).
std::string base64UrlEncode(const std::uint8_t* data, std::size_t len);

/// Sobrecarga de `base64UrlEncode` para std::string.
std::string base64UrlEncode(const std::string& s);

/// Decodifica una cadena base64url (sin padding) a bytes.
///
/// Acepta tanto los simbolos base64url ('-' y '_') como los estandar ('+' y
/// '/'), y tolera (ignora) un padding '=' si estuviera presente.
///
/// :param in: cadena base64url a decodificar.
/// :param out: recibe los bytes decodificados (se limpia antes).
/// :returns: true si todos los simbolos son validos; false si hay un simbolo
///           fuera del alfabeto.
bool base64UrlDecode(const std::string& in, std::vector<std::uint8_t>& out);

/// Servicio de tokens de senalizacion (spec 8.3).
///
/// Combina la emision/verificacion de tokens firmados con el rate-limit de
/// intentos de PIN. El `secret` es la clave HMAC compartida entre quien emite
/// (el servidor) y quien verifica (el mismo servidor al admitir al invitado).
class TokenService {
public:
    /// Motivo por el que un token no verifica.
    enum class VerifyResult {
        Ok,           // firma valida, sala coincide, no expirado.
        Malformed,    // no es un token bien formado (sin punto, base64 invalido,
                      // mac de tamano distinto, payload sin room/exp).
        BadSignature, // el MAC no coincide (token alterado o secret distinto).
        Expired,      // `nowUnix` >= exp.
        WrongRoom,    // firma valida pero la sala del payload no coincide.
    };

    /// Callback que valida el PIN. Recibe el PIN en claro y devuelve true si es
    /// correcto. Se inyecta para no guardar el PIN en claro en el servicio: el
    /// caller puede compararlo contra un hash, un KDF, etc.
    using PinValidator = std::function<bool(const std::string&)>;

    /// :param secret: clave HMAC (secret) que firma/verifica los tokens.
    /// :param maxAttempts: intentos de PIN permitidos antes del bloqueo
    ///                     (por defecto 5).
    /// :param lockoutSec: duracion del bloqueo, en segundos (por defecto 60).
    /// :param pinValidator: validador de PIN (por defecto: ninguno; issue() no
    ///                      emitira hasta que se setee uno).
    TokenService(std::string secret, int maxAttempts = 5, int lockoutSec = 60,
                 PinValidator pinValidator = {});

    /// Cambia el validador de PIN.
    ///
    /// :param pinValidator: nuevo callback de validacion.
    void setPinValidator(PinValidator pinValidator);

    /// Emite un token corto firmado para (room, pin) si el PIN es valido y no
    /// hay lockout activo.
    ///
    /// :param room: identificador de sala (queda en el payload).
    /// :param pin: PIN de la sala (se valida con el pinValidator inyectado).
    /// :param ttlSec: vida util del token en segundos (exp = now + ttlSec);
    ///                por defecto 300.
    /// :param nowUnix: "ahora" en segundos Unix (por defecto nowUnix()).
    /// :returns: el token "b64url(payload).b64url(mac)"; cadena vacia si no se
    ///           emite (PIN invalido, lockout activo, o sin validator). Un
    ///           fallo de PIN alimenta el lockout (recordFailure).
    std::string issue(const std::string& room, const std::string& pin,
                      int ttlSec = 300, int nowUnix = ba::security::nowUnix());

    /// Verifica un token contra (room, nowUnix).
    ///
    /// Orden de chequeos: forma (Malformed) -> firma (BadSignature) -> sala
    /// (WrongRoom) -> expiracion (Expired). Asi un token alterado se reporta
    /// como BadSignature aunque la sala no coincida, y un token valido de otra
    /// sala se reporta como WrongRoom.
    ///
    /// :param token: token emitido por issue().
    /// :param room: sala esperada (debe coincidir con la del payload).
    /// :param nowUnix: "ahora" en segundos Unix (por defecto nowUnix()).
    /// :returns: VerifyResult::Ok si todo esta bien; si no, el motivo.
    VerifyResult verify(const std::string& token, const std::string& room,
                        int nowUnix = ba::security::nowUnix()) const;

    /// Indica si se permite un nuevo intento de PIN.
    ///
    /// Devuelve false cuando hay un lockout activo (now < lockedUntil_). Cuando
    /// la ventana de lockout expira, refresca el presupuesto de intentos y
    /// vuelve a permitir. No consume presupuesto por si mismo: el presupuesto
    /// lo agota recordFailure() al alcanzar maxAttempts.
    ///
    /// :returns: true si se permite un intento; false si hay lockout.
    bool attemptAllowed();

    /// Registra un intento de PIN fallido. Al alcanzar maxAttempts fallos abre
    /// la ventana de lockout (lockedUntil_ = nowUnix() + lockoutSec).
    void recordFailure();

    /// Registra un intento exitoso: limpia el contador de fallos y el lockout.
    void recordSuccess();

    /// Limpia el lockout y el contador de fallos.
    ///
    /// :param nowUnix: "ahora" en segundos Unix; se acepta para pruebas
    ///                 deterministas y registro del caller (el reset es
    ///                 inmediato).
    void resetLockout(int nowUnix);

private:
    std::string secret_;
    int maxAttempts_;
    int lockoutSec_;
    PinValidator pinValidator_;

    int failures_ = 0;    // fallos de PIN en la ventana actual
    int lockedUntil_ = 0; // hasta cuando (seg Unix) dura el lockout; 0 = sin lockout
    std::uint64_t nonceCounter_ = 0; // para generar nonces unicos (no son secreto)
};

} // namespace ba::security
