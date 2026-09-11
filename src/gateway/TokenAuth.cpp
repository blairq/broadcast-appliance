// ============================================================================
// src/gateway/TokenAuth.cpp — implementacion del wrapper de TokenService
//
// Proposito:
//   Implementa el wrapper thread-safe descrito en TokenAuth.hpp. La clave de
//   diseno es el callback de validacion de PIN inyectado en el TokenService:
//   captura una copia del `shared_ptr<std::string>` que el wrapper guarda en
//   `pin_`, de modo que `setPin()` (que hace `*pin_ = nuevo`) muta el valor
//   que el callback lee sin re-crear el servicio (ChangePin, spec §4.1).
//
// Modelo de threading:
//   issue()/setPin() toman el mutex (mutan el servicio / el PIN). verify() es
//   const pero TAMBIEN toma el mutex: aunque TokenService::verify sea const,
//   el PIN que lee el callback es estado mutable compartido, y para que la
//   semantica de "no dos threads a la vez" sea estricta se serializa todo.
//
// Estrategia de errores:
//   No lanza. issue() devuelve "" en fallo; verify() mapea VerifyResult a un
//   int estable.
// ============================================================================

#include "TokenAuth.hpp"

namespace ba::gateway {

TokenAuth::TokenAuth(std::string secret, std::string pin, int maxAttempts, int lockoutSec, int ttlSec)
    : pin_(std::make_shared<std::string>(std::move(pin))), ttlSec_(ttlSec) {
    // El TokenService se construye DESPUES de pin_ para que el callback pueda
    // capturar el shared_ptr ya inicializado. El callback compara el PIN
    // presentado contra *pin_ (el PIN actual de la sala, mutable en caliente).
    // unique_ptr: TokenService no tiene ctor por defecto (requiere secret +
    // validador), asi que no puede ser miembro por valor default-constructed.
    svc_ = std::make_unique<ba::security::TokenService>(
        std::move(secret), maxAttempts, lockoutSec,
        [ref = pin_](const std::string& presented) { return !presented.empty() && *ref == presented; });
}

void TokenAuth::setPin(const std::string& pin) {
    std::lock_guard<std::mutex> lock(m_);
    // Muta el valor compartido que el callback del TokenService lee. No se
    // re-crea el servicio: el callback captura el mismo shared_ptr.
    *pin_ = pin;
}

std::string TokenAuth::issue(const std::string& room, const std::string& pin) {
    std::lock_guard<std::mutex> lock(m_);
    // El PIN presentado se valida contra el PIN de la sala dentro del
    // TokenService (via el callback inyectado). Si no coincide, issue()
    // devuelve "" y registra el fallo (lockout).
    return svc_->issue(room, pin, ttlSec_);
}

int TokenAuth::verify(const std::string& token, const std::string& room) const {
    std::lock_guard<std::mutex> lock(m_);
    const auto r = svc_->verify(token, room);
    switch (r) {
        case ba::security::TokenService::VerifyResult::Ok: return static_cast<int>(VerifyCode::kOk);
        case ba::security::TokenService::VerifyResult::Malformed:
            return static_cast<int>(VerifyCode::kMalformed);
        case ba::security::TokenService::VerifyResult::BadSignature:
            return static_cast<int>(VerifyCode::kBadSignature);
        case ba::security::TokenService::VerifyResult::Expired: return static_cast<int>(VerifyCode::kExpired);
        case ba::security::TokenService::VerifyResult::WrongRoom:
            return static_cast<int>(VerifyCode::kWrongRoom);
    }
    return static_cast<int>(VerifyCode::kMalformed);
}

} // namespace ba::gateway
