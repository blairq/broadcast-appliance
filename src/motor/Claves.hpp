// ============================================================================
// src/motor/Claves.hpp — en qué tramo de una animación por claves cae un momento
//
// Una sola cuenta para las dos puntas: el motor (Composicion: la capa animada que
// se mueve al aire) y el editor de gráficos (el recuadro de la vista previa donde
// va a ir el video). Si cada uno interpolara a su manera, lo que se ve en el
// editor no sería lo que sale.
//
// Sin GStreamer ni Qt: solo la cuenta, para que la use cualquiera.
// ============================================================================
#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace ba::motor {

/// Dónde cae un momento entre las claves: de cuál a cuál y cuánto avanzó (0…1, ya
/// suavizado si el tramo es suave). Antes de la primera, la primera; pasada la última,
/// la última (desde == hasta).
struct Tramo {
    std::size_t desde = 0, hasta = 0;
    double t = 0;
    /// Lo de en medio entre dos valores, en este tramo.
    double en(double a, double b) const { return a + (b - a) * t; }
};

/// `K` tiene `ms` (entero) y `suave` (el tramo que LLEGA a esa clave arranca y frena
/// despacio, como entra cualquier gráfico). Las claves, ordenadas por ms; no vacías.
template <class K>
Tramo tramoEn(const std::vector<K>& k, double ms) {
    Tramo r;
    if (k.empty()) return r;
    std::size_t i = 0;
    while (i + 1 < k.size() && ms >= k[i + 1].ms) ++i;
    r.desde = i;
    r.hasta = i + 1 < k.size() ? i + 1 : i;
    const auto& a = k[r.desde];
    const auto& b = k[r.hasta];
    if (ms <= k.front().ms || b.ms <= a.ms) return r; // t = 0
    double t = std::clamp((ms - a.ms) / static_cast<double>(b.ms - a.ms), 0.0, 1.0);
    if (b.suave) t = t * t * (3.0 - 2.0 * t);
    r.t = t;
    return r;
}

} // namespace ba::motor
