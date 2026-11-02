// ============================================================================
// src/gateway/AudioCompartido.cpp — agrupar a los que oyen lo mismo (AudioCompartido.hpp)
// ============================================================================
#include "AudioCompartido.hpp"

#include <cmath>

namespace ba::gateway {

bool parecidos(const float* a, const float* b, std::size_t n, float tope) {
    if (a == b) return true;
    if (a == nullptr || b == nullptr) return false;
    for (std::size_t i = 0; i < n; ++i)
        if (std::fabs(a[i] - b[i]) > tope) return false;
    return true;
}

namespace {
constexpr std::uint64_t kMagico = 0xBA5E;

std::uint8_t sumaControl(std::uint64_t v56) {
    std::uint8_t s = 0x5A;
    for (int i = 0; i < 7; ++i) s = static_cast<std::uint8_t>((s * 31u) ^ ((v56 >> (8 * i)) & 0xFFu));
    return s;
}
} // namespace

void marcarBloque(std::int16_t* pcm, std::size_t cuadros, int canales, std::uint16_t grupo, std::uint64_t bloque) {
    if (pcm == nullptr || canales <= 0 || cuadros < kCuadrosMarca) return;
    const std::uint64_t v56 = (kMagico << 40) | (static_cast<std::uint64_t>(grupo) << 24) | (bloque & 0xFFFFFFu);
    const std::uint64_t bits = (v56 << 8) | sumaControl(v56);
    for (std::size_t f = 0; f < kCuadrosMarca; ++f) {
        std::int16_t* c = pcm + f * static_cast<std::size_t>(canales);
        std::int32_t suma = 0;
        for (int k = 0; k < canales; ++k) suma += c[k];
        auto v = static_cast<std::int16_t>(suma / canales);
        const auto bit = static_cast<std::int16_t>((bits >> (63 - f)) & 1u);
        v = static_cast<std::int16_t>((v & ~1) | bit);
        for (int k = 0; k < canales; ++k) c[k] = v;
    }
}

bool leerMarca(const std::int16_t* pcm, std::size_t cuadros, int canales, std::uint16_t& grupo, std::uint32_t& bloque24) {
    if (pcm == nullptr || canales <= 0 || cuadros < kCuadrosMarca) return false;
    std::uint64_t bits = 0;
    for (std::size_t f = 0; f < kCuadrosMarca; ++f)
        bits = (bits << 1) | static_cast<std::uint64_t>(pcm[f * static_cast<std::size_t>(canales)] & 1);
    const std::uint64_t v56 = bits >> 8;
    if ((v56 >> 40) != kMagico || sumaControl(v56) != static_cast<std::uint8_t>(bits & 0xFFu)) return false;
    grupo = static_cast<std::uint16_t>((v56 >> 24) & 0xFFFFu);
    bloque24 = static_cast<std::uint32_t>(v56 & 0xFFFFFFu);
    return true;
}

std::uint64_t bloqueCompleto(std::uint32_t bloque24, std::uint64_t referencia) {
    constexpr std::uint64_t kVuelta = 1ull << 24;
    const std::uint64_t base = referencia & ~(kVuelta - 1);
    std::uint64_t mejor = base | bloque24;
    const auto lejos = [referencia](std::uint64_t x) { return x > referencia ? x - referencia : referencia - x; };
    if (base >= kVuelta && lejos((base - kVuelta) | bloque24) < lejos(mejor)) mejor = (base - kVuelta) | bloque24;
    if (lejos((base + kVuelta) | bloque24) < lejos(mejor)) mejor = (base + kVuelta) | bloque24;
    return mejor;
}

const float* AgrupadorRetorno::pcmDe(int etiqueta) const {
    return etiqueta >= 1 && etiqueta <= kGrupos ? pcm_[etiqueta] : nullptr;
}

void AgrupadorRetorno::agrupar(const std::vector<Entrada>& e, std::size_t n) {
    const std::size_t total = e.size();
    std::vector<int> parecido(total, 0); // a qué grupo se parece en este bloque
    std::vector<bool> libre(total);
    for (std::size_t i = 0; i < total; ++i) libre[i] = e[i].libre && e[i].pcm != nullptr;

    // Los miembros de un grupo con representante `c`: los libres que suenan como él.
    std::vector<std::size_t> miembros, mejores;
    const auto juntar = [&](std::size_t c, std::vector<std::size_t>& out) {
        out.clear();
        for (std::size_t i = 0; i < total; ++i)
            if (libre[i] && parecidos(e[i].pcm, e[c].pcm, n, kTope)) out.push_back(i);
    };
    // Primero los grupos que ya tenían representante (que sigan siendo los mismos), después
    // los vacíos.
    int orden[kGrupos];
    int k = 0;
    for (int g = 1; g <= kGrupos; ++g)
        if (rep_[g] != 0) orden[k++] = g;
    for (int g = 1; g <= kGrupos; ++g)
        if (rep_[g] == 0) orden[k++] = g;

    for (int g : orden) {
        pcm_[g] = nullptr;
        mejores.clear();
        std::size_t elegido = total;
        // El representante de antes, si sigue libre y con alguien que suena como él: gana
        // (el grupo no se mueve aunque otro sea más grande).
        if (rep_[g] != 0)
            for (std::size_t i = 0; i < total; ++i)
                if (libre[i] && e[i].sesion == rep_[g]) {
                    juntar(i, mejores);
                    if (mejores.size() >= 2) elegido = i;
                    break;
                }
        if (elegido == total) {
            // Uno nuevo: el más grande entre unos pocos candidatos (cada uno, uno que no
            // entró en los grupos de los anteriores).
            std::vector<bool> probado(total, false);
            for (int intento = 0; intento < kCandidatos; ++intento) {
                std::size_t c = total;
                for (std::size_t i = 0; i < total; ++i)
                    if (libre[i] && !probado[i]) {
                        c = i;
                        break;
                    }
                if (c == total) break;
                juntar(c, miembros);
                for (std::size_t m : miembros) probado[m] = true;
                if (miembros.size() >= 2 && miembros.size() > mejores.size()) {
                    mejores = miembros;
                    elegido = c;
                }
            }
        }
        if (elegido == total) {
            rep_[g] = 0;
            continue;
        }
        rep_[g] = e[elegido].sesion;
        pcm_[g] = e[elegido].pcm;
        for (std::size_t m : mejores) {
            parecido[m] = g;
            libre[m] = false;
        }
    }

    // La histéresis por sesión: entra después de kUnirseBloques seguidos en el mismo grupo;
    // sale en el acto.
    grupoDe_.assign(total, 0);
    std::unordered_map<int, Estado> nuevo;
    nuevo.reserve(total);
    for (std::size_t i = 0; i < total; ++i) {
        Estado s;
        const auto it = estado_.find(e[i].sesion);
        if (it != estado_.end()) s = it->second;
        if (parecido[i] == 0) {
            s = Estado{};
        } else if (parecido[i] == s.etiqueta) {
            if (s.racha < kUnirseBloques) ++s.racha;
        } else {
            s.etiqueta = parecido[i];
            s.racha = 1;
        }
        if (s.etiqueta != 0 && s.racha >= kUnirseBloques) grupoDe_[i] = s.etiqueta;
        nuevo[e[i].sesion] = s;
    }
    estado_.swap(nuevo); // las sesiones que no vinieron se olvidan
}

} // namespace ba::gateway
