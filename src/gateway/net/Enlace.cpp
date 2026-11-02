// ============================================================================
// src/gateway/net/Enlace.cpp — ver Enlace.hpp
//
// Formato (todo en orden de red), después de "BAE1" + tipo:
//   HolaOk         u16 version, u32 kbps (tope), u32 ms (tope)
//   Ping/Pong/Dato u32 prueba, u32 seq, u64 tUs   (Dato: + relleno)
//   InformePedido  u32 prueba
//   Informe        u32 prueba, recibidos, enviados, seqMax, jitterUs, desordenados;
//                  u64 bytes, primeroUs, ultimoUs
//   BajadaPedido   u32 prueba, u32 kbps, u32 ms, u16 tamano
//   BajadaOk       u32 prueba, u32 kbps, u32 ms
//   Parar          u32 prueba
// ============================================================================

#include "net/Enlace.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ba::gateway::net::enlace {

namespace {

constexpr char kMagia[4] = {'B', 'A', 'E', '1'};

void pon16(std::string& s, uint16_t v) {
    s.push_back(static_cast<char>(v >> 8));
    s.push_back(static_cast<char>(v & 255));
}
void pon32(std::string& s, uint32_t v) {
    for (int i = 3; i >= 0; --i) s.push_back(static_cast<char>((v >> (8 * i)) & 255));
}
void pon64(std::string& s, uint64_t v) {
    for (int i = 7; i >= 0; --i) s.push_back(static_cast<char>((v >> (8 * i)) & 255));
}

/// Lector con tope: si falta algo, `ok` queda en false.
struct Lector {
    const uint8_t* d;
    size_t n;
    size_t i = kCabecera;
    bool ok = true;
    uint64_t leer(int bytes) {
        if (i + static_cast<size_t>(bytes) > n) {
            ok = false;
            return 0;
        }
        uint64_t v = 0;
        for (int k = 0; k < bytes; ++k) v = (v << 8) | d[i++];
        return v;
    }
    uint16_t u16() { return static_cast<uint16_t>(leer(2)); }
    uint32_t u32() { return static_cast<uint32_t>(leer(4)); }
    uint64_t u64() { return leer(8); }
};

} // namespace

std::string codificar(const Mensaje& m, size_t tamano) {
    std::string s(kMagia, 4);
    s.push_back(static_cast<char>(m.tipo));
    switch (m.tipo) {
    case Tipo::Hola:
        break;
    case Tipo::HolaOk:
        pon16(s, m.version);
        pon32(s, m.kbps);
        pon32(s, m.ms);
        break;
    case Tipo::Ping:
    case Tipo::Pong:
    case Tipo::Dato:
        pon32(s, m.prueba);
        pon32(s, m.seq);
        pon64(s, m.tUs);
        if (m.tipo == Tipo::Dato && tamano > s.size()) s.resize(tamano, '\0');
        break;
    case Tipo::InformePedido:
    case Tipo::Parar:
        pon32(s, m.prueba);
        break;
    case Tipo::Informe:
        pon32(s, m.prueba);
        pon32(s, m.recibidos);
        pon32(s, m.enviados);
        pon32(s, m.seqMax);
        pon32(s, m.jitterUs);
        pon32(s, m.desordenados);
        pon64(s, m.bytes);
        pon64(s, m.primeroUs);
        pon64(s, m.ultimoUs);
        break;
    case Tipo::BajadaPedido:
        pon32(s, m.prueba);
        pon32(s, m.kbps);
        pon32(s, m.ms);
        pon16(s, m.tamano);
        break;
    case Tipo::BajadaOk:
        pon32(s, m.prueba);
        pon32(s, m.kbps);
        pon32(s, m.ms);
        break;
    }
    return s;
}

std::optional<Mensaje> decodificar(const uint8_t* d, size_t n) {
    if (d == nullptr || n < kCabecera) return std::nullopt;
    for (int i = 0; i < 4; ++i)
        if (d[i] != static_cast<uint8_t>(kMagia[i])) return std::nullopt;
    Mensaje m;
    const uint8_t t = d[4];
    if (t < 1 || t > 10) return std::nullopt;
    m.tipo = static_cast<Tipo>(t);
    Lector l{d, n};
    switch (m.tipo) {
    case Tipo::Hola:
        break;
    case Tipo::HolaOk:
        m.version = l.u16();
        m.kbps = l.u32();
        m.ms = l.u32();
        break;
    case Tipo::Ping:
    case Tipo::Pong:
    case Tipo::Dato:
        m.prueba = l.u32();
        m.seq = l.u32();
        m.tUs = l.u64();
        break;
    case Tipo::InformePedido:
    case Tipo::Parar:
        m.prueba = l.u32();
        break;
    case Tipo::Informe:
        m.prueba = l.u32();
        m.recibidos = l.u32();
        m.enviados = l.u32();
        m.seqMax = l.u32();
        m.jitterUs = l.u32();
        m.desordenados = l.u32();
        m.bytes = l.u64();
        m.primeroUs = l.u64();
        m.ultimoUs = l.u64();
        break;
    case Tipo::BajadaPedido:
        m.prueba = l.u32();
        m.kbps = l.u32();
        m.ms = l.u32();
        m.tamano = l.u16();
        break;
    case Tipo::BajadaOk:
        m.prueba = l.u32();
        m.kbps = l.u32();
        m.ms = l.u32();
        break;
    }
    if (!l.ok) return std::nullopt;
    return m;
}

// --- Cuentas -------------------------------------------------------------------

void Receptor::llegada(uint32_t seq, uint64_t envioUs, int64_t llegadaUs, size_t bytes) {
    ++recibidos_;
    bytes_ += bytes;
    if (!hay_) {
        primeroUs_ = llegadaUs;
        seqMax_ = seq;
    } else if (seq < seqMax_) {
        ++desordenados_;
    } else {
        seqMax_ = seq;
    }
    ultimoUs_ = std::max(ultimoUs_, llegadaUs);
    const int64_t transito = llegadaUs - static_cast<int64_t>(envioUs);
    if (hay_) {
        const double dif = std::fabs(static_cast<double>(transito - transitoAnt_));
        jitter_ += (dif - jitter_) / 16.0;
    }
    transitoAnt_ = transito;
    hay_ = true;
}

double kbpsEntregados(uint64_t bytesCarga, uint32_t paquetes, int64_t primeroUs, int64_t ultimoUs) {
    if (paquetes < 2 || ultimoUs <= primeroUs) return 0;
    // Entre el primero y el último pasaron (paquetes - 1) intervalos: se cuenta lo que
    // llegó después del primero, para no sobrestimar con pocos paquetes.
    const double porPaquete = static_cast<double>(bytesCarga) / paquetes + kCabeceraIpUdp;
    const double bits = porPaquete * (paquetes - 1) * 8.0;
    return bits / (static_cast<double>(ultimoUs - primeroUs) / 1000.0); // bits/ms = kbps
}

double perdida(uint32_t enviados, uint32_t recibidos) {
    if (enviados == 0) return 0;
    const double p = 1.0 - static_cast<double>(recibidos) / enviados;
    return std::clamp(p, 0.0, 1.0);
}

double percentil(std::vector<double> v, double p) {
    if (v.empty()) return -1;
    std::sort(v.begin(), v.end());
    const double pos = std::clamp(p, 0.0, 100.0) / 100.0 * static_cast<double>(v.size() - 1);
    const size_t i = static_cast<size_t>(pos);
    if (i + 1 >= v.size()) return v.back();
    const double f = pos - static_cast<double>(i);
    return v[i] + (v[i + 1] - v[i]) * f;
}

double variacionMedia(const std::vector<double>& v) {
    if (v.size() < 2) return 0;
    double s = 0;
    for (size_t i = 1; i < v.size(); ++i) s += std::fabs(v[i] - v[i - 1]);
    return s / static_cast<double>(v.size() - 1);
}

std::vector<int> rampa(int topeKbps) {
    static constexpr int kTasas[] = {2000, 5000, 10000, 20000, 30000, 40000, 60000, 80000, 100000, 150000, 200000};
    std::vector<int> r;
    if (topeKbps <= 0) return r;
    for (int t : kTasas) {
        if (t >= topeKbps) break;
        r.push_back(t);
    }
    r.push_back(topeKbps);
    return r;
}

bool saturado(const Escalon& e, double rttReposoMs) {
    if (e.perdida > 0.05) return true;
    if (e.kbpsEntregados < 0.85 * e.kbpsPedidos) return true;
    if (rttReposoMs >= 0 && e.rttMs >= 0 && e.rttMs - rttReposoMs > 300) return true;
    return false;
}

double capacidadKbps(const std::vector<Escalon>& es) {
    double m = 0;
    for (const Escalon& e : es) m = std::max(m, e.kbpsEntregados);
    return m;
}

} // namespace ba::gateway::net::enlace
