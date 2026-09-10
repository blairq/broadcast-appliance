// test_token_service.cpp
//
// Pruebas unitarias de la seguridad de senalizacion (spec 8.3):
//   1. HMAC-SHA256 contra un vector de prueba conocido (KAT).
//   2. issue() (PIN valido) y luego verify() -> Ok.
//   3. Token alterado (cambiar un char del mac) -> BadSignature.
//   4. verify() con nowUnix posterior a exp -> Expired.
//   5. verify() con una sala distinta -> WrongRoom.
//   6. Token mal formado (basura / sin punto) -> Malformed.
//   7. Rate-limit / lockout de intentos de PIN.
//
// Sin framework externo: es un main() que devuelve 0 si todo pasa y 1 si algo
// falla (lo registra CTest). El reloj se inyecta (nowUnix) para que la
// expiracion y el lockout sean deterministas.

#include "crypto/HmacSha256.hpp"
#include "security/TokenService.hpp"

#include <cstddef>
#include <cstdio>
#include <string>

using ba::crypto::hmacSha256Hex;
using ba::security::TokenService;
using VerifyResult = ba::security::TokenService::VerifyResult;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FALLO: %s (linea %d)\n", #cond, __LINE__);            \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

// 1) KAT de HMAC-SHA256 (vector conocido).
void testHmacKAT() {
    // RFC 4231, Test Case 2 (HMAC-SHA-256): key="Jefe",
    // data="what do ya want for nothing?". (El vector anterior era el KAT de
    // SHA-256 plano, no el de HMAC: por eso no coincidía.)
    const std::string expected =
        "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843";
    const std::string got =
        hmacSha256Hex("Jefe", "what do ya want for nothing?");
    CHECK(got == expected);
}

// 2) issue (PIN valido) y luego verify -> Ok.
void testIssueThenVerifyOk() {
    TokenService svc("secret-para-pruebas", /*maxAttempts=*/5, /*lockoutSec=*/60,
                     [](const std::string& pin) { return pin == "1234"; });
    const int now0 = 1'700'000'000;
    const std::string token = svc.issue("sala-1", "1234", /*ttlSec=*/300, now0);
    CHECK(!token.empty());
    CHECK(svc.verify(token, "sala-1", now0) == VerifyResult::Ok);
}

// 3) Alterar el token (cambiar un char del mac) -> BadSignature.
void testTamperedMacBadSignature() {
    TokenService svc("secret-para-pruebas", 5, 60,
                     [](const std::string& pin) { return pin == "1234"; });
    const int now0 = 1'700'000'000;
    const std::string token = svc.issue("sala-1", "1234", 300, now0);
    CHECK(!token.empty());

    // Cambia un char de la parte del mac por otro simbolo base64url valido.
    std::string tampered = token;
    const auto dot = tampered.find('.');
    CHECK(dot != std::string::npos);
    if (dot != std::string::npos && dot + 1 < tampered.size()) {
        const std::size_t pos = dot + 1;
        const char repl = (tampered[pos] == 'A') ? 'B' : 'A';
        tampered[pos] = repl;
    }
    CHECK(svc.verify(tampered, "sala-1", now0) == VerifyResult::BadSignature);
}

// 4) verify con nowUnix posterior a exp -> Expired.
void testExpired() {
    TokenService svc("secret-para-pruebas", 5, 60,
                     [](const std::string& pin) { return pin == "1234"; });
    const int now0 = 1'700'000'000;
    const std::string token = svc.issue("sala-1", "1234", /*ttlSec=*/300, now0);
    CHECK(!token.empty());
    // Justo en el limite (now == exp) -> expirado (>=).
    CHECK(svc.verify(token, "sala-1", now0 + 300) == VerifyResult::Expired);
    // Despues del limite -> expirado.
    CHECK(svc.verify(token, "sala-1", now0 + 301) == VerifyResult::Expired);
}

// 5) verify con una sala distinta -> WrongRoom.
void testWrongRoom() {
    TokenService svc("secret-para-pruebas", 5, 60,
                     [](const std::string& pin) { return pin == "1234"; });
    const int now0 = 1'700'000'000;
    const std::string token = svc.issue("sala-1", "1234", 300, now0);
    CHECK(!token.empty());
    CHECK(svc.verify(token, "sala-2", now0) == VerifyResult::WrongRoom);
}

// 6) Token mal formado (basura / sin punto) -> Malformed.
void testMalformed() {
    TokenService svc("secret-para-pruebas", 5, 60,
                     [](const std::string& pin) { return pin == "1234"; });
    const int now0 = 1'700'000'000;
    CHECK(svc.verify("", "sala-1", now0) == VerifyResult::Malformed);
    CHECK(svc.verify("sin-punto", "sala-1", now0) == VerifyResult::Malformed);
    CHECK(svc.verify("!!!.!!!", "sala-1", now0) == VerifyResult::Malformed);
    CHECK(svc.verify("a.b", "sala-1", now0) == VerifyResult::Malformed);
}

// 7) Rate-limit / lockout.
void testRateLimitLockout() {
    TokenService svc("secret-para-pruebas", /*maxAttempts=*/3, /*lockoutSec=*/60,
                     [](const std::string& pin) { return pin == "1234"; });
    // 3 fallos -> lockout.
    svc.recordFailure();
    svc.recordFailure();
    svc.recordFailure();
    CHECK(svc.attemptAllowed() == false);
    // Un exito limpia el lockout.
    svc.recordSuccess();
    CHECK(svc.attemptAllowed() == true);
    // Volver a agotar y limpiar con resetLockout.
    svc.recordFailure();
    svc.recordFailure();
    svc.recordFailure();
    CHECK(svc.attemptAllowed() == false);
    svc.resetLockout(1'700'000'000);
    CHECK(svc.attemptAllowed() == true);
}

} // namespace

int main() {
    testHmacKAT();
    testIssueThenVerifyOk();
    testTamperedMacBadSignature();
    testExpired();
    testWrongRoom();
    testMalformed();
    testRateLimitLockout();

    if (g_failures == 0) {
        std::printf("test_token_service: OK\n");
        return 0;
    }
    std::printf("test_token_service: %d fallo(s)\n", g_failures);
    return 1;
}
