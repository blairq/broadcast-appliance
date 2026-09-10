// TokenService.cpp
//
// Implementacion del servicio de tokens de senalizacion (spec 8.3).
//
//   issue():  valida el PIN (pinValidator) y, si esta bien y no hay lockout,
//             arma el payload JSON, lo codifica en base64url, lo firma con
//             HMAC-SHA256(secret) y devuelve "b64url(payload).b64url(mac)".
//   verify(): re-codifica, re-firma y compara el MAC (comparacion en tiempo
//             constante), y luego valida la sala y la expiracion.
//
// El lockout es por ventana temporal: al acumular maxAttempts fallos se fija
// lockedUntil_ = now + lockoutSec; mientras now < lockedUntil_ no se permiten
// intentos. Cuando la ventana expira, el presupuesto de intentos se refresca.

#include "security/TokenService.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>

#include "crypto/HmacSha256.hpp"

namespace ba::security {

// --- reloj ------------------------------------------------------------------

int nowUnix() {
    using namespace std::chrono;
    return static_cast<int>(
        system_clock::now().time_since_epoch() / std::chrono::seconds(1));
}

// --- base64url (sin padding) ------------------------------------------------

namespace {
constexpr char kB64UrlAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
}

std::string base64UrlEncode(const std::uint8_t* data, std::size_t len) {
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    std::size_t i = 0;
    while (i + 3 <= len) {
        const std::uint32_t n = (std::uint32_t(data[i]) << 16) |
                                (std::uint32_t(data[i + 1]) << 8) |
                                std::uint32_t(data[i + 2]);
        out += char(kB64UrlAlphabet[(n >> 18) & 0x3F]);
        out += char(kB64UrlAlphabet[(n >> 12) & 0x3F]);
        out += char(kB64UrlAlphabet[(n >> 6) & 0x3F]);
        out += char(kB64UrlAlphabet[n & 0x3F]);
        i += 3;
    }
    const std::size_t rem = len - i;
    if (rem == 1) {
        const std::uint32_t n = std::uint32_t(data[i]) << 16;
        out += char(kB64UrlAlphabet[(n >> 18) & 0x3F]);
        out += char(kB64UrlAlphabet[(n >> 12) & 0x3F]);
    } else if (rem == 2) {
        const std::uint32_t n = (std::uint32_t(data[i]) << 16) |
                                (std::uint32_t(data[i + 1]) << 8);
        out += char(kB64UrlAlphabet[(n >> 18) & 0x3F]);
        out += char(kB64UrlAlphabet[(n >> 12) & 0x3F]);
        out += char(kB64UrlAlphabet[(n >> 6) & 0x3F]);
    }
    return out;
}

std::string base64UrlEncode(const std::string& s) {
    return base64UrlEncode(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}

bool base64UrlDecode(const std::string& in, std::vector<std::uint8_t>& out) {
    out.clear();
    int rev[256];
    for (int i = 0; i < 256; ++i) {
        rev[i] = -1;
    }
    for (int i = 0; i < 64; ++i) {
        rev[static_cast<unsigned char>(kB64UrlAlphabet[i])] = i;
    }
    // Tolerancia: aceptar tambien el alfabeto estandar.
    rev[static_cast<unsigned char>('+')] = 62;
    rev[static_cast<unsigned char>('/')] = 63;

    std::uint32_t buf = 0;
    int bits = 0;
    for (char c : in) {
        if (c == '=') {
            continue; // padding (no deberia estar, pero se tolera)
        }
        const int v = rev[static_cast<unsigned char>(c)];
        if (v < 0) {
            return false; // simbolo fuera del alfabeto
        }
        buf = (buf << 6) | std::uint32_t(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(std::uint8_t((buf >> bits) & 0xFF));
        }
    }
    return true;
}

// --- helpers de JSON (payload) ---------------------------------------------

namespace {

// Escapa un string para meterlo como valor JSON entre comillas.
std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                   static_cast<unsigned int>(static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
    return out;
}

// Extrae el valor de una clave de nivel superior en un JSON.
//
// `out` recibe el valor sin comillas (si es string, con los escapes resueltos)
// o como texto crudo (si es numero). Devuelve false si la clave no existe.
// Es un parser minimo a medida: el payload es auto-generado y protegido por
// HMAC, asi que solo se ejecuta sobre datos ya autenticados.
bool jsonValue(const std::string& json, const std::string& key, std::string& out) {
    const std::string needle = "\"" + key + "\"";
    const auto kpos = json.find(needle);
    if (kpos == std::string::npos) {
        return false;
    }
    std::size_t i = kpos + needle.size();
    while (i < json.size() && json[i] != ':') {
        ++i;
    }
    if (i >= json.size()) {
        return false;
    }
    ++i; // ':'
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) {
        ++i;
    }
    if (i >= json.size()) {
        return false;
    }

    if (json[i] == '"') {
        // String: leer hasta la comilla de cierre (con escapes).
        ++i;
        std::string v;
        while (i < json.size()) {
            const char c = json[i];
            if (c == '\\' && i + 1 < json.size()) {
                const char n = json[i + 1];
                switch (n) {
                    case '"':
                        v += '"';
                        break;
                    case '\\':
                        v += '\\';
                        break;
                    case '/':
                        v += '/';
                        break;
                    case 'b':
                        v += '\b';
                        break;
                    case 'f':
                        v += '\f';
                        break;
                    case 'n':
                        v += '\n';
                        break;
                    case 'r':
                        v += '\r';
                        break;
                    case 't':
                        v += '\t';
                        break;
                    case 'u': {
                        if (i + 5 < json.size()) {
                            const std::string hexd = json.substr(i + 2, 4);
                            try {
                                const unsigned code = std::stoul(hexd, nullptr, 16);
                                if (code < 0x80) {
                                    v += char(code);
                                } else if (code < 0x800) {
                                    v += char(0xC0 | (code >> 6));
                                    v += char(0x80 | (code & 0x3F));
                                } else {
                                    v += char(0xE0 | (code >> 12));
                                    v += char(0x80 | ((code >> 6) & 0x3F));
                                    v += char(0x80 | (code & 0x3F));
                                }
                            } catch (...) {
                                v += '?';
                            }
                            i += 5;
                            continue;
                        }
                        v += '?';
                        ++i;
                        continue;
                    }
                    default:
                        v += n;
                        break;
                }
                i += 2;
                continue;
            }
            if (c == '"') {
                ++i;
                break;
            }
            v += c;
            ++i;
        }
        out = std::move(v);
        return true;
    }

    // Numero u otro: leer hasta , } ] o fin.
    std::string v;
    while (i < json.size() && json[i] != ',' && json[i] != '}' && json[i] != ']') {
        v += json[i];
        ++i;
    }
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\n' ||
                          v.back() == '\r')) {
        v.pop_back();
    }
    out = std::move(v);
    return !out.empty();
}

// Comparacion de MAC en tiempo constante (mitiga timing attacks).
bool constantTimeEqual(const std::uint8_t* a, const std::uint8_t* b, std::size_t n) {
    volatile unsigned char diff = 0;
    for (std::size_t i = 0; i < n; ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}

} // namespace

// --- TokenService -----------------------------------------------------------

TokenService::TokenService(std::string secret, int maxAttempts, int lockoutSec,
                           PinValidator pinValidator)
    : secret_(std::move(secret)),
      maxAttempts_(maxAttempts > 0 ? maxAttempts : 1),
      lockoutSec_(lockoutSec > 0 ? lockoutSec : 1),
      pinValidator_(std::move(pinValidator)) {}

void TokenService::setPinValidator(PinValidator pinValidator) {
    pinValidator_ = std::move(pinValidator);
}

std::string TokenService::issue(const std::string& room, const std::string& pin,
                                int ttlSec, int nowUnix) {
    if (ttlSec <= 0) {
        ttlSec = 1;
    }
    // Con lockout activo no se emiten tokens nuevos.
    if (!attemptAllowed()) {
        return {};
    }
    // Validar el PIN. Sin validator no se emite (no se adivina el PIN).
    const bool pinOk = pinValidator_ ? pinValidator_(pin) : false;
    if (!pinOk) {
        recordFailure();
        return {};
    }

    // Nonce unico (anti-replay dentro del TTL). No es secreto.
    const std::uint64_t n = nonceCounter_++;
    const std::string nonce = std::to_string(nowUnix) + "-" + std::to_string(n);

    const int exp = nowUnix + ttlSec;
    const std::string payload = "{\"room\":" + jsonEscape(room) +
                                ",\"exp\":" + std::to_string(exp) +
                                ",\"nonce\":" + jsonEscape(nonce) + "}";

    const std::string b64payload = base64UrlEncode(payload);
    const auto mac = ba::crypto::hmacSha256(
        reinterpret_cast<const std::uint8_t*>(secret_.data()), secret_.size(),
        reinterpret_cast<const std::uint8_t*>(b64payload.data()), b64payload.size());
    const std::string b64mac = base64UrlEncode(mac.data(), mac.size());

    return b64payload + "." + b64mac;
}

TokenService::VerifyResult
TokenService::verify(const std::string& token, const std::string& room,
                     int nowUnix) const {
    // 1) Forma: "b64url(payload).b64url(mac)".
    const auto dot = token.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= token.size()) {
        return VerifyResult::Malformed;
    }
    const std::string b64payload = token.substr(0, dot);
    const std::string b64mac = token.substr(dot + 1);

    std::vector<std::uint8_t> payloadBytes;
    if (!base64UrlDecode(b64payload, payloadBytes)) {
        return VerifyResult::Malformed;
    }
    std::vector<std::uint8_t> macBytes;
    if (!base64UrlDecode(b64mac, macBytes)) {
        return VerifyResult::Malformed;
    }
    if (macBytes.size() != ba::crypto::kHmacSha256Size) {
        return VerifyResult::Malformed;
    }

    // 2) Firma: re-firmar el payload y comparar el MAC (tiempo constante).
    const auto expected = ba::crypto::hmacSha256(
        reinterpret_cast<const std::uint8_t*>(secret_.data()), secret_.size(),
        reinterpret_cast<const std::uint8_t*>(b64payload.data()), b64payload.size());
    if (!constantTimeEqual(expected.data(), macBytes.data(), macBytes.size())) {
        return VerifyResult::BadSignature;
    }

    // 3) Payload: extraer room y exp.
    const std::string payload(payloadBytes.begin(), payloadBytes.end());
    std::string pRoom;
    std::string pExp;
    if (!jsonValue(payload, "room", pRoom) || !jsonValue(payload, "exp", pExp)) {
        return VerifyResult::Malformed;
    }

    // 4) Sala.
    if (pRoom != room) {
        return VerifyResult::WrongRoom;
    }

    // 5) Expiracion.
    long long exp = 0;
    try {
        exp = std::stoll(pExp);
    } catch (...) {
        return VerifyResult::Malformed;
    }
    if (nowUnix >= exp) {
        return VerifyResult::Expired;
    }

    return VerifyResult::Ok;
}

// --- rate-limit / lockout ---------------------------------------------------

bool TokenService::attemptAllowed() {
    const int now = nowUnix();
    if (lockedUntil_ != 0) {
        if (now < lockedUntil_) {
            return false; // en ventana de lockout
        }
        // La ventana expiro: presupuesto fresco.
        failures_ = 0;
        lockedUntil_ = 0;
    }
    return true;
}

void TokenService::recordFailure() {
    const int now = nowUnix();
    if (lockedUntil_ != 0 && now < lockedUntil_) {
        return; // ya bloqueado
    }
    ++failures_;
    if (failures_ >= maxAttempts_) {
        lockedUntil_ = now + lockoutSec_;
    }
}

void TokenService::recordSuccess() {
    failures_ = 0;
    lockedUntil_ = 0;
}

void TokenService::resetLockout(int nowUnix) {
    (void)nowUnix; // aceptado para pruebas deterministas / registro del caller
    failures_ = 0;
    lockedUntil_ = 0;
}

} // namespace ba::security
