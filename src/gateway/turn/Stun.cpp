// ============================================================================
// src/gateway/turn/Stun.cpp — implementación de Stun.hpp (ver el header)
// ============================================================================

#include "turn/Stun.hpp"

#include <cstring>

#include <mbedtls/md.h>

namespace ba::gateway::turn::stun {

namespace {

uint16_t leer16(const uint8_t* p) { return uint16_t((uint16_t(p[0]) << 8) | p[1]); }
uint32_t leer32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
void poner16(std::string& s, uint16_t v) {
    s.push_back(char(v >> 8));
    s.push_back(char(v & 0xFF));
}
void poner32(std::string& s, uint32_t v) {
    for (int i = 3; i >= 0; --i) s.push_back(char((v >> (i * 8)) & 0xFF));
}
void escribir16(std::string& s, size_t pos, uint16_t v) {
    s[pos] = char(v >> 8);
    s[pos + 1] = char(v & 0xFF);
}

// Tabla de CRC-32 (polinomio 0xEDB88320, el de ISO 3309 que pide STUN).
struct TablaCrc {
    uint32_t t[256];
    TablaCrc() {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
    }
};

} // namespace

uint16_t tipo(uint16_t m, uint16_t c) {
    return uint16_t((m & 0x000F) | ((m & 0x0070) << 1) | ((m & 0x0F80) << 2) | ((c & 1) << 4) | ((c & 2) << 7));
}
uint16_t metodoDe(uint16_t t) { return uint16_t((t & 0x000F) | ((t >> 1) & 0x0070) | ((t >> 2) & 0x0F80)); }
uint16_t claseDe(uint16_t t) { return uint16_t(((t >> 4) & 1) | ((t >> 7) & 2)); }

const Atributo* Mensaje::buscar(uint16_t t) const {
    for (const auto& a : attrs)
        if (a.tipo == t) return &a;
    return nullptr;
}

bool Mensaje::direccionXor(uint16_t t, uint32_t& ip, uint16_t& puerto) const {
    const Atributo* a = buscar(t);
    if (a == nullptr || a->valor.size() != 8) return false;
    const auto* p = reinterpret_cast<const uint8_t*>(a->valor.data());
    if (p[1] != 0x01) return false; // solo IPv4
    puerto = uint16_t(leer16(p + 2) ^ (kMagic >> 16));
    ip = leer32(p + 4) ^ kMagic;
    return true;
}

bool Mensaje::u32(uint16_t t, uint32_t& v) const {
    const Atributo* a = buscar(t);
    if (a == nullptr || a->valor.size() != 4) return false;
    v = leer32(reinterpret_cast<const uint8_t*>(a->valor.data()));
    return true;
}

std::string Mensaje::texto(uint16_t t) const {
    const Atributo* a = buscar(t);
    return a ? a->valor : std::string();
}

bool pareceStun(const uint8_t* p, size_t n) {
    return n >= kCabecera && (p[0] & 0xC0) == 0 && leer32(p + 4) == kMagic;
}

size_t largoStun(const uint8_t* p, size_t n) {
    if (n < 4) return 0;
    return kCabecera + leer16(p + 2);
}

bool parsear(const uint8_t* p, size_t n, Mensaje& m) {
    if (!pareceStun(p, n)) return false;
    const size_t largo = leer16(p + 2);
    if ((largo & 3) != 0 || kCabecera + largo != n) return false;
    m = Mensaje{};
    m.tipo = leer16(p);
    std::memcpy(m.tid.data(), p + 8, 12);
    m.crudo.assign(reinterpret_cast<const char*>(p), n);
    size_t i = kCabecera;
    while (i + 4 <= n) {
        const uint16_t t = leer16(p + i);
        const uint16_t l = leer16(p + i + 2);
        if (i + 4 + l > n) return false;
        const size_t pos = i;
        i += 4 + ((l + 3u) & ~3u);
        if (m.posHuella != std::string::npos) break; // nada vale después de la huella
        if (m.posIntegridad != std::string::npos && t != kFingerprint) continue;
        if (t == kMessageIntegrity) {
            if (l != 20) return false;
            m.posIntegridad = pos;
        } else if (t == kFingerprint) {
            if (l != 4) return false;
            m.posHuella = pos;
        }
        m.attrs.push_back(Atributo{t, std::string(reinterpret_cast<const char*>(p + pos + 4), l)});
    }
    return true;
}

std::array<uint8_t, 20> hmacSha1(const std::string& clave, const uint8_t* datos, size_t n) {
    std::array<uint8_t, 20> out{};
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    if (info != nullptr)
        mbedtls_md_hmac(info, reinterpret_cast<const unsigned char*>(clave.data()), clave.size(), datos, n,
                        out.data());
    return out;
}

std::array<uint8_t, 16> md5(const std::string& s) {
    std::array<uint8_t, 16> out{};
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_MD5);
    if (info != nullptr) mbedtls_md(info, reinterpret_cast<const unsigned char*>(s.data()), s.size(), out.data());
    return out;
}

uint32_t crc32(const uint8_t* p, size_t n) {
    static const TablaCrc tabla;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = tabla.t[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::string claveLarga(const std::string& usuario, const std::string& realm, const std::string& clave) {
    const auto h = md5(usuario + ":" + realm + ":" + clave);
    return std::string(reinterpret_cast<const char*>(h.data()), h.size());
}

bool verificarIntegridad(const Mensaje& m, const std::string& clave) {
    if (m.posIntegridad == std::string::npos || m.posIntegridad + 24 > m.crudo.size()) return false;
    std::string copia = m.crudo.substr(0, m.posIntegridad);
    escribir16(copia, 2, uint16_t(m.posIntegridad + 24 - kCabecera));
    const auto h = hmacSha1(clave, reinterpret_cast<const uint8_t*>(copia.data()), copia.size());
    // Comparación sin cortocircuito (que no se pueda medir cuánto coincidió).
    uint8_t dif = 0;
    for (size_t i = 0; i < 20; ++i) dif |= uint8_t(h[i] ^ uint8_t(m.crudo[m.posIntegridad + 4 + i]));
    return dif == 0;
}

bool verificarHuella(const Mensaje& m) {
    if (m.posHuella == std::string::npos || m.posHuella + 8 > m.crudo.size()) return false;
    std::string copia = m.crudo.substr(0, m.posHuella);
    escribir16(copia, 2, uint16_t(m.posHuella + 8 - kCabecera));
    const uint32_t c = crc32(reinterpret_cast<const uint8_t*>(copia.data()), copia.size()) ^ 0x5354554eu;
    return c == leer32(reinterpret_cast<const uint8_t*>(m.crudo.data() + m.posHuella + 4));
}

Constructor::Constructor(uint16_t t, const std::array<uint8_t, 12>& tid) {
    b_.reserve(128);
    poner16(b_, t);
    poner16(b_, 0);
    poner32(b_, kMagic);
    b_.append(reinterpret_cast<const char*>(tid.data()), tid.size());
}

void Constructor::fijarLargo(size_t largo) { escribir16(b_, 2, uint16_t(largo - kCabecera)); }

void Constructor::agregar(uint16_t t, const void* datos, size_t n) {
    poner16(b_, t);
    poner16(b_, uint16_t(n));
    b_.append(static_cast<const char*>(datos), n);
    while (b_.size() % 4 != 0) b_.push_back('\0');
    fijarLargo(b_.size());
}

void Constructor::agregarU32(uint16_t t, uint32_t v) {
    std::string s;
    poner32(s, v);
    agregar(t, s);
}

void Constructor::agregarDireccionXor(uint16_t t, uint32_t ip, uint16_t puerto) {
    std::string s;
    s.push_back('\0');
    s.push_back('\x01');
    poner16(s, uint16_t(puerto ^ (kMagic >> 16)));
    poner32(s, ip ^ kMagic);
    agregar(t, s);
}

void Constructor::agregarError(int codigo, const std::string& razon) {
    std::string s;
    s.push_back('\0');
    s.push_back('\0');
    s.push_back(char((codigo / 100) & 0x07));
    s.push_back(char(codigo % 100));
    s += razon;
    agregar(kErrorCode, s);
}

void Constructor::integridad(const std::string& clave) {
    fijarLargo(b_.size() + 24);
    const auto h = hmacSha1(clave, reinterpret_cast<const uint8_t*>(b_.data()), b_.size());
    agregar(kMessageIntegrity, h.data(), h.size());
}

void Constructor::huella() {
    fijarLargo(b_.size() + 8);
    const uint32_t c = crc32(reinterpret_cast<const uint8_t*>(b_.data()), b_.size()) ^ 0x5354554eu;
    agregarU32(kFingerprint, c);
}

std::string channelData(uint16_t canal, const uint8_t* datos, size_t n, bool rellenar) {
    std::string s;
    s.reserve(n + 8);
    poner16(s, canal);
    poner16(s, uint16_t(n));
    s.append(reinterpret_cast<const char*>(datos), n);
    if (rellenar)
        while (s.size() % 4 != 0) s.push_back('\0');
    return s;
}

} // namespace ba::gateway::turn::stun
