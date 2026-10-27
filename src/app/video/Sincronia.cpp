// src/app/video/Sincronia.cpp — ver Sincronia.hpp

#include "video/Sincronia.hpp"

#include "i18n/I18n.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace ba::app::video {

namespace {

constexpr size_t kLado = 144; // 16x9

/// Las diferencias de un cuadro al siguiente, sobre la grilla de cuadros del
/// lienzo: índice = (hora - hora0) / período. Falta (vacío) donde no hubo
/// cuadro o el anterior no era el inmediato.
struct Serie {
    std::vector<std::vector<float>> d; ///< por índice: el cambio espacial (144) o vacío
    std::vector<float> e;              ///< por índice: la energía del cambio (o NaN)
};

Serie serieDe(const std::vector<std::uint64_t>& t, const std::vector<std::uint8_t>& g, std::uint64_t t0,
              double periodo, size_t largo) {
    Serie s;
    s.d.assign(largo, {});
    s.e.assign(largo, std::nanf(""));
    std::map<long, size_t> porIndice;
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] < t0) continue;
        const long k = std::lround(static_cast<double>(t[i] - t0) / periodo);
        if (k >= 0 && static_cast<size_t>(k) < largo) porIndice[k] = i;
    }
    for (const auto& [k, i] : porIndice) {
        const auto prev = porIndice.find(k - 1);
        if (prev == porIndice.end()) continue;
        std::vector<float> v(kLado);
        double energia = 0;
        for (size_t j = 0; j < kLado; ++j) {
            v[j] = static_cast<float>(g[i * kLado + j]) - static_cast<float>(g[prev->second * kLado + j]);
            energia += std::fabs(v[j]);
        }
        s.d[static_cast<size_t>(k)] = std::move(v);
        s.e[static_cast<size_t>(k)] = static_cast<float>(energia / kLado);
    }
    return s;
}

struct Pico {
    int lag = 0;
    double valor = -2, segundo = -2;
    std::vector<double> curva; ///< por lag (de -L a L)
};

/// El lag de B respecto de A que mejor correlaciona; `corr(lag)` da la correlación.
template <typename F> Pico buscarPico(int L, F corr) {
    Pico p;
    for (int lag = -L; lag <= L; ++lag) {
        const double c = corr(lag);
        p.curva.push_back(c);
        if (c > p.valor) {
            p.valor = c;
            p.lag = lag;
        }
    }
    // El mejor fuera de ±2 cuadros del pico: si está cerca, el pico no es claro.
    for (int lag = -L; lag <= L; ++lag)
        if (std::abs(lag - p.lag) > 2) p.segundo = std::max(p.segundo, p.curva[static_cast<size_t>(lag + L)]);
    return p;
}

} // namespace

Medicion medirDesfase(const std::vector<std::uint64_t>& ta, const std::vector<std::uint8_t>& ga,
                      const std::vector<std::uint64_t>& tb, const std::vector<std::uint8_t>& gb,
                      double maxMs) {
    Medicion m;
    if (ta.size() < 30 || tb.size() < 30 || ga.size() < ta.size() * kLado || gb.size() < tb.size() * kLado) {
        m.motivo = ba::i18n::traducir("sincronia", "hacen falta más cuadros de las dos entradas (al menos 30 de cada una)");
        return m;
    }
    // El período: la mediana de las diferencias entre cuadros seguidos.
    std::vector<std::uint64_t> difs;
    for (size_t i = 1; i < ta.size(); ++i)
        if (ta[i] > ta[i - 1]) difs.push_back(ta[i] - ta[i - 1]);
    if (difs.empty()) {
        m.motivo = ba::i18n::traducir("sincronia", "las horas de los cuadros no avanzan");
        return m;
    }
    std::nth_element(difs.begin(), difs.begin() + static_cast<long>(difs.size() / 2), difs.end());
    const double periodo = static_cast<double>(difs[difs.size() / 2]);
    m.cuadroMs = periodo / 1e6;
    const std::uint64_t t0 = std::min(ta.front(), tb.front());
    const std::uint64_t t1 = std::max(ta.back(), tb.back());
    const size_t largo = static_cast<size_t>(static_cast<double>(t1 - t0) / periodo) + 2;
    const Serie a = serieDe(ta, ga, t0, periodo, largo), b = serieDe(tb, gb, t0, periodo, largo);
    const int L = std::max(1, static_cast<int>(maxMs / m.cuadroMs));

    // ¿Hay algo que medir? Sin cambios no hay correlación posible.
    const auto actividad = [](const Serie& s) {
        double sum = 0, n = 0;
        for (float e : s.e)
            if (!std::isnan(e)) sum += e, ++n;
        return n > 0 ? sum / n : 0.0;
    };
    if (actividad(a) < 0.05 || actividad(b) < 0.05) {
        m.motivo = ba::i18n::traducir("sincronia",
                                      "una de las dos no cambia (imagen quieta): para medir, que las dos vean algo "
                                      "que se mueva (un aplauso, un flash, alguien caminando, la misma pantalla con "
                                      "video)");
        return m;
    }

    // 1) Espacial: correlación normalizada de los cambios de la grilla.
    int comunes = 0;
    const Pico esp = buscarPico(L, [&](int lag) {
        double ab = 0, aa = 0, bb = 0;
        int n = 0;
        for (size_t k = 0; k < largo; ++k) {
            const long kb = static_cast<long>(k) + lag;
            if (kb < 0 || static_cast<size_t>(kb) >= largo) continue;
            const auto& va = a.d[k];
            const auto& vb = b.d[static_cast<size_t>(kb)];
            if (va.empty() || vb.empty()) continue;
            for (size_t j = 0; j < kLado; ++j)
                ab += va[j] * vb[j], aa += va[j] * va[j], bb += vb[j] * vb[j];
            ++n;
        }
        if (lag == 0) comunes = n;
        return n < 10 || aa <= 0 || bb <= 0 ? -1.0 : ab / std::sqrt(aa * bb);
    });
    // 2) Energía: Pearson de cuánto cambia cada una.
    const Pico ene = buscarPico(L, [&](int lag) {
        std::vector<double> xa, xb;
        for (size_t k = 0; k < largo; ++k) {
            const long kb = static_cast<long>(k) + lag;
            if (kb < 0 || static_cast<size_t>(kb) >= largo) continue;
            if (std::isnan(a.e[k]) || std::isnan(b.e[static_cast<size_t>(kb)])) continue;
            xa.push_back(a.e[k]);
            xb.push_back(b.e[static_cast<size_t>(kb)]);
        }
        if (xa.size() < 10) return -1.0;
        double ma = 0, mb = 0;
        for (size_t i = 0; i < xa.size(); ++i)
            ma += xa[i], mb += xb[i];
        ma /= static_cast<double>(xa.size());
        mb /= static_cast<double>(xb.size());
        double ab = 0, aa = 0, bb = 0;
        for (size_t i = 0; i < xa.size(); ++i) {
            ab += (xa[i] - ma) * (xb[i] - mb);
            aa += (xa[i] - ma) * (xa[i] - ma);
            bb += (xb[i] - mb) * (xb[i] - mb);
        }
        return aa <= 0 || bb <= 0 ? -1.0 : ab / std::sqrt(aa * bb);
    });

    // La espacial si es clara (la misma imagen); si no, la energía.
    const bool espacial = esp.valor > 0.5 && esp.valor - esp.segundo > 0.15;
    const Pico& p = espacial ? esp : ene;
    m.metodo = espacial ? "espacial" : "energia";
    m.cuadros = comunes;
    m.confianza = std::clamp(espacial ? esp.valor : (ene.valor - std::max(0.0, ene.segundo)) * 2, 0.0, 1.0);
    if (p.valor < 0.3 || p.lag == -L || p.lag == L) {
        m.motivo = p.lag == -L || p.lag == L
                       ? ba::i18n::llenar(ba::i18n::traducir("sincronia", "el mejor ajuste quedó en el borde de lo "
                                                                         "buscado: el desfase es mayor que %1 ms, o "
                                                                         "no hay nada en común"),
                                          {std::to_string(static_cast<int>(maxMs))})
                       : ba::i18n::traducir("sincronia", "no se encontró un momento en común entre las dos (¿ven lo mismo?)");
        return m;
    }
    // Afinar entre cuadros: parábola por el pico y sus vecinos.
    double fino = p.lag;
    const size_t i = static_cast<size_t>(p.lag + L);
    if (i > 0 && i + 1 < p.curva.size()) {
        const double y0 = p.curva[i - 1], y1 = p.curva[i], y2 = p.curva[i + 1];
        const double den = y0 - 2 * y1 + y2;
        if (den < 0) fino += std::clamp(0.5 * (y0 - y2) / den, -0.5, 0.5);
    }
    m.desfaseMs = fino * m.cuadroMs;
    // Un pico apenas más alto que el resto no es una medición: mejor decir
    // que no se pudo que dar un número que no vale.
    if (m.confianza < 0.3) {
        m.motivo = ba::i18n::llenar(ba::i18n::traducir("sincronia", "no hay una coincidencia clara entre las dos "
                                                                    "(confianza %1): hace falta más movimiento en "
                                                                    "común, o que las dos vean lo mismo"),
                                    {std::to_string(m.confianza).substr(0, 4)});
        return m;
    }
    m.ok = true;
    return m;
}

} // namespace ba::app::video
