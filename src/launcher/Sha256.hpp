// Sha256.hpp
//
// Proposito:
//   Implementacion compacta y autocontenida de SHA-256 (FIPS 180-4) para la
//   verificacion de integridad del launcher (spec 3.1.2: verificacion de
//   archivos contra manifest.json con SHA-256). El launcher no enlaza
//   librerias externas, asi que el algoritmo vive aca, header-only.
//
// Modelo de threading:
//   Cada instancia es usada por UN thread a la vez (no es thread-safe). La
//   verificacion paralela se logra creando una instancia por worker
//   (Integrity.cpp), no compartiendo instancias.
//
// Estrategia de errores:
//   No lanza. hashFile() devuelve cadena vacia si el archivo no se puede
//   abrir o leer; el caller lo trata como "archivo ausente/corrupto".

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
// Convierte una ruta UTF-16 a UTF-8 para abrir con std::ifstream, que acepta
// string estrecho en ambas plataformas (MSVC acepta wstring, gcc/clang no).
// En no-Windows asume ASCII, suficiente para el stub de validacion de compilacion.
inline std::string toUtf8Path(const std::wstring& p) {
#ifdef _WIN32
    if (p.empty()) {
        return std::string();
    }
    // MinGW declara WideCharToMultiByte SIN valores por defecto para los dos
    // ultimos args (MSVC si los trae); se pasan explicitamente (nullptr).
    const int n =
        WideCharToMultiByte(CP_UTF8, 0, p.c_str(), static_cast<int>(p.size()), nullptr, 0, nullptr, nullptr);
    std::string out;
    if (n > 0) {
        out.resize(static_cast<std::size_t>(n));
        WideCharToMultiByte(CP_UTF8, 0, p.c_str(), static_cast<int>(p.size()), out.data(), n, nullptr,
                            nullptr);
    }
    return out;
#else
    std::string out;
    out.reserve(p.size());
    for (wchar_t c : p) {
        out.push_back(static_cast<char>(c));
    }
    return out;
#endif
}

// La inversa: una ruta en UTF-8 (el manifest, el caché) a la forma ancha que
// usan Integrity y hashFile. En Windows hay que DECODIFICAR el UTF-8: copiar
// byte por byte (lo que se hacía) deja «Sesión» como «SesiÃ³n» y el archivo
// «no existe»: una instalación nueva arrancaba con la pantalla de integridad.
// En el resto es la ida y vuelta de toUtf8Path (byte a byte).
inline std::wstring fromUtf8Path(const std::string& s) {
#ifdef _WIN32
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out;
    if (n > 0) {
        out.resize(static_cast<std::size_t>(n));
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    }
    return out;
#else
    return std::wstring(s.begin(), s.end());
#endif
}

/// Abre para leer una ruta ancha. En Windows con la ruta ancha: un char* se lee
/// en la página ANSI y una carpeta con tilde («C:\Users\Martín») no se encuentra.
inline std::ifstream abrirParaLeer(const std::wstring& p) {
#ifdef _WIN32
    return std::ifstream(std::filesystem::path(p), std::ios::binary);
#else
    return std::ifstream(toUtf8Path(p), std::ios::binary);
#endif
}
} // namespace

namespace ba::launcher {

/// SHA-256 incremental (FIPS 180-4).
class Sha256 {
   public:
    Sha256() { reset(); }

    /// Reinicia el estado para hashear un nuevo mensaje.
    void reset() {
        // Valores iniciales de FIPS 180-4 (5.3.3). h1, h5, h6 y h7 estaban mal
        // desde el principio: ningun hash coincidia con el manifest (hecho con
        // sha256sum) y la verificacion de integridad fallaba siempre.
        h_[0] = 0x6a09e667;
        h_[1] = 0xbb67ae85;
        h_[2] = 0x3c6ef372;
        h_[3] = 0xa54ff53a;
        h_[4] = 0x510e527f;
        h_[5] = 0x9b05688c;
        h_[6] = 0x1f83d9ab;
        h_[7] = 0x5be0cd19;
        bufLen_ = 0;
        totalLen_ = 0;
    }

    /// Absorbe `len` bytes del mensaje.
    void update(const void* data, std::size_t len) {
        const auto* p = static_cast<const unsigned char*>(data);
        totalLen_ += len;
        while (len > 0) {
            const std::size_t space = 64 - bufLen_;
            const std::size_t n = (len < space) ? len : space;
            std::memcpy(buf_ + bufLen_, p, n);
            bufLen_ += n;
            p += n;
            len -= n;
            if (bufLen_ == 64) {
                compress(buf_);
                bufLen_ = 0;
            }
        }
    }

    /// Finaliza y devuelve el digest de 32 bytes.
    std::array<std::uint8_t, 32> digest() const {
        // Copia el estado para no mutar el objeto original.
        Sha256 f = *this;
        // Longitud en bits, big-endian, al final del padding.
        const std::uint64_t bitLen = f.totalLen_ * 8ULL;
        unsigned char pad = 0x80;
        f.update(&pad, 1);
        const unsigned char zero = 0x00;
        while (f.bufLen_ % 64 != 56) {
            f.update(&zero, 1);
        }
        unsigned char lenBytes[8];
        for (int i = 0; i < 8; ++i) {
            lenBytes[i] = static_cast<unsigned char>((bitLen >> (56 - 8 * i)) & 0xFF);
        }
        f.update(lenBytes, 8);
        std::array<std::uint8_t, 32> out{};
        for (int i = 0; i < 8; ++i) {
            out[4 * i + 0] = static_cast<unsigned char>((f.h_[i] >> 24) & 0xFF);
            out[4 * i + 1] = static_cast<unsigned char>((f.h_[i] >> 16) & 0xFF);
            out[4 * i + 2] = static_cast<unsigned char>((f.h_[i] >> 8) & 0xFF);
            out[4 * i + 3] = static_cast<unsigned char>(f.h_[i] & 0xFF);
        }
        return out;
    }

    /// Hashea un archivo completo y devuelve el hex (64 chars).
    ///
    /// :param path: ruta del archivo (UTF-16).
    /// :returns: hex en minuscula; cadena vacia si el archivo no se pudo leer.
    static std::string hashFile(const std::wstring& path) {
        std::ifstream in = abrirParaLeer(path);
        if (!in) {
            return std::string();
        }
        Sha256 sh;
        char buf[1 << 16];
        while (in.read(buf, sizeof(buf)) || in.gcount() > 0) {
            sh.update(buf, static_cast<std::size_t>(in.gcount()));
            if (in.eof()) {
                break;
            }
        }
        const auto d = sh.digest();
        static const char* hexd = "0123456789abcdef";
        std::string out;
        out.reserve(64);
        for (unsigned char b : d) {
            out.push_back(hexd[b >> 4]);
            out.push_back(hexd[b & 0xF]);
        }
        return out;
    }

   private:
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
        std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
        std::uint32_t e = h_[4], f = h_[5], g = h_[6], h = h_[7];
        for (int t = 0; t < 64; ++t) {
            const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = h + S1 + ch + K[t] + w[t];
            const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = S0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h_[0] += a;
        h_[1] += b;
        h_[2] += c;
        h_[3] += d;
        h_[4] += e;
        h_[5] += f;
        h_[6] += g;
        h_[7] += h;
    }

    std::uint32_t h_[8]{};
    unsigned char buf_[64]{};
    std::size_t bufLen_ = 0;
    std::uint64_t totalLen_ = 0;
};

} // namespace ba::launcher
