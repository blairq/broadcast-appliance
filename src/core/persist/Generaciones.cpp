// src/core/persist/Generaciones.cpp — ver el .hpp

#include "persist/Generaciones.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <system_error>

namespace ba::persist {

namespace fs = std::filesystem;

namespace {

// --- SHA-256 (FIPS 180-4), autocontenido ------------------------------------
//
// Compacto y sin dependencias: es el checksum de las generaciones (y de
// SnapshotStore), no criptografía. Una instancia por hash.

struct Sha256 {
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    unsigned char buf[64]{};
    std::size_t bufLen = 0;
    std::uint64_t totalLen = 0;

    static std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void compress(const unsigned char* chunk) {
        static const std::uint32_t K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        std::uint32_t w[64];
        for (int t = 0; t < 16; ++t) {
            w[t] = (static_cast<std::uint32_t>(chunk[t * 4]) << 24) |
                   (static_cast<std::uint32_t>(chunk[t * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(chunk[t * 4 + 2]) << 8) |
                   (static_cast<std::uint32_t>(chunk[t * 4 + 3]));
        }
        for (int t = 16; t < 64; ++t) {
            const std::uint32_t s0 = rotr(w[t - 15], 7) ^ rotr(w[t - 15], 18) ^ (w[t - 15] >> 3);
            const std::uint32_t s1 = rotr(w[t - 2], 17) ^ rotr(w[t - 2], 19) ^ (w[t - 2] >> 10);
            w[t] = w[t - 16] + s0 + w[t - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int t = 0; t < 64; ++t) {
            const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = hh + S1 + ch + K[t] + w[t];
            const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = S0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }

    void update(const void* data, std::size_t len) {
        const auto* p = static_cast<const unsigned char*>(data);
        totalLen += len;
        while (len > 0) {
            const std::size_t space = 64 - bufLen;
            const std::size_t n = (len < space) ? len : space;
            std::memcpy(buf + bufLen, p, n);
            bufLen += n;
            p += n;
            len -= n;
            if (bufLen == 64) {
                compress(buf);
                bufLen = 0;
            }
        }
    }

    std::array<std::uint8_t, 32> digest() const {
        Sha256 f = *this;
        const std::uint64_t bitLen = f.totalLen * 8ULL;
        unsigned char pad = 0x80;
        f.update(&pad, 1);
        const unsigned char zero = 0x00;
        while (f.bufLen % 64 != 56) {
            f.update(&zero, 1);
        }
        unsigned char lenBytes[8];
        for (int i = 0; i < 8; ++i) {
            lenBytes[i] = static_cast<unsigned char>((bitLen >> (56 - 8 * i)) & 0xFF);
        }
        f.update(lenBytes, 8);
        std::array<std::uint8_t, 32> out{};
        for (int i = 0; i < 8; ++i) {
            out[4 * i + 0] = static_cast<unsigned char>((f.h[i] >> 24) & 0xFF);
            out[4 * i + 1] = static_cast<unsigned char>((f.h[i] >> 16) & 0xFF);
            out[4 * i + 2] = static_cast<unsigned char>((f.h[i] >> 8) & 0xFF);
            out[4 * i + 3] = static_cast<unsigned char>(f.h[i] & 0xFF);
        }
        return out;
    }
};

constexpr std::string_view kCabecera = "#BA-SNAP v1 seq=";

bool aNumero(std::string_view s, std::uint64_t& out) {
    if (s.empty() || s.size() > 20) return false;
    std::uint64_t v = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<std::uint64_t>(c - '0');
    }
    out = v;
    return true;
}

/// Una generación: true si está entera (y deja su secuencia y su cuerpo).
bool leerGeneracion(const fs::path& ruta, bool& existe, std::uint64_t& secuencia, std::string& cuerpo) {
    std::string contenido;
    existe = leerArchivo(ruta, contenido);
    return existe && sinCabecera(contenido, secuencia, cuerpo);
}

} // namespace

std::string sha256Hex(std::string_view datos) {
    Sha256 sh;
    sh.update(datos.data(), datos.size());
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (const std::uint8_t b : sh.digest()) {
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0xF]);
    }
    return out;
}

std::string conCabecera(std::uint64_t secuencia, std::string_view cuerpo) {
    std::string out;
    out.reserve(cuerpo.size() + 96);
    out += kCabecera;
    out += std::to_string(secuencia);
    out += " sha256=";
    out += sha256Hex(cuerpo);
    out += '\n';
    out += cuerpo;
    out += '\n';
    return out;
}

bool sinCabecera(const std::string& contenido, std::uint64_t& secuencia, std::string& cuerpo) {
    if (contenido.rfind(kCabecera, 0) != 0) return false;
    const std::size_t nl = contenido.find('\n');
    if (nl == std::string::npos) return false;
    const std::string_view linea(contenido.data(), nl);
    const std::size_t espacio = linea.find(' ', kCabecera.size());
    if (espacio == std::string_view::npos) return false;
    if (!aNumero(linea.substr(kCabecera.size(), espacio - kCabecera.size()), secuencia)) return false;
    const std::string_view resto = linea.substr(espacio + 1);
    constexpr std::string_view kSha = "sha256=";
    if (resto.substr(0, kSha.size()) != kSha || resto.size() != kSha.size() + 64) return false;
    // El cuerpo termina en el '\n' que agrega conCabecera: sin él, el archivo quedó cortado.
    if (contenido.size() < nl + 2 || contenido.back() != '\n') return false;
    cuerpo.assign(contenido, nl + 1, contenido.size() - nl - 2);
    return sha256Hex(cuerpo) == resto.substr(kSha.size());
}

DosGeneraciones::DosGeneraciones(fs::path archivo) : archivo_(std::move(archivo)) {}

fs::path DosGeneraciones::rutaA() const {
    return archivo_.parent_path() / (archivo_.stem().string() + ".A" + archivo_.extension().string());
}

fs::path DosGeneraciones::rutaB() const {
    return archivo_.parent_path() / (archivo_.stem().string() + ".B" + archivo_.extension().string());
}

Lectura DosGeneraciones::leer() const {
    Lectura l;
    bool hayA = false, hayB = false;
    std::uint64_t sa = 0, sb = 0;
    std::string ca, cb;
    const bool okA = leerGeneracion(rutaA(), hayA, sa, ca);
    const bool okB = leerGeneracion(rutaB(), hayB, sb, cb);
    l.descartadas = (hayA && !okA ? 1 : 0) + (hayB && !okB ? 1 : 0);
    if (okA || okB) {
        const bool tomarA = okA && (!okB || sa >= sb);
        l.estado = Lectura::Estado::Valida;
        l.secuencia = tomarA ? sa : sb;
        l.cuerpo = std::move(tomarA ? ca : cb);
        return l;
    }
    if (hayA || hayB) {
        l.estado = Lectura::Estado::Danada;
        return l;
    }
    if (leerArchivo(archivo_, l.cuerpo)) l.estado = Lectura::Estado::Vieja;
    return l;
}

bool DosGeneraciones::escribir(std::string_view cuerpo, Durabilidad d, std::string* error) {
    if (!conocido_) {
        // La primera vez se mira el disco: la secuencia sigue y se pisa la generación
        // más vieja (o la rota), nunca la última buena.
        bool hayA = false, hayB = false;
        std::uint64_t sa = 0, sb = 0;
        std::string c;
        const bool okA = leerGeneracion(rutaA(), hayA, sa, c);
        const bool okB = leerGeneracion(rutaB(), hayB, sb, c);
        secuencia_ = std::max<std::uint64_t>(okA ? sa : 0, okB ? sb : 0);
        siguienteA_ = !okA || (okB && sa < sb);
        conocido_ = true;
    }
    const std::uint64_t seq = secuencia_ + 1;
    if (!persist::escribir(siguienteA_ ? rutaA() : rutaB(), conCabecera(seq, cuerpo), d, Permisos::Comunes, error))
        return false; // el turno no cambia: la próxima vuelve a pisar la misma (la vieja)
    secuencia_ = seq;
    siguienteA_ = !siguienteA_;
    // El de antes de las generaciones ya no dice lo último.
    std::error_code ec;
    if (fs::exists(archivo_, ec)) persist::borrar(archivo_);
    return true;
}

bool DosGeneraciones::borrar() {
    conocido_ = false;
    const bool a = persist::borrar(rutaA());
    const bool b = persist::borrar(rutaB());
    const bool v = persist::borrar(archivo_);
    return a && b && v;
}

} // namespace ba::persist
