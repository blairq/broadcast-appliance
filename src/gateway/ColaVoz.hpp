// ============================================================================
// src/gateway/ColaVoz.hpp — una voz que espera para mezclarse en otra bajada
//
// La atención remota del registro (docs/funciones/REGISTRO-TOTEM.md): quien
// atiende por web oye al tótem (la voz de la persona MÁS la del asistente) y,
// mientras atiende, su voz baja al tótem (mezclada con la del asistente). Las
// voces llegan por hilos distintos y a su propio ritmo (la de la persona y la de
// quien atiende, desde libwebrtc; la del asistente, del anillo de la app), y un
// track de WebRTC tiene UNA fuente: empujar dos en el mismo track las intercala
// y suena al doble de velocidad. Así que una se encola acá y la otra, la que
// marca el reloj (el anillo del asistente, que la app llena cada 10 ms con voz o
// con silencio), se la suma al salir.
//
// La cola tiene tope: si el reloj se para (la app cerró la voz) no se acumula
// una frase vieja que sonaría después, fuera de lugar. Se queda lo último.
// ============================================================================
#pragma once

#include <algorithm>
#include <cstddef>
#include <deque>
#include <vector>

namespace ba::gateway::colavoz {

/// Agrega `n` muestras (intercaladas) a la cola, y si pasa de `tope` tira lo más viejo.
inline void encolar(std::deque<float>& cola, const float* m, std::size_t n, std::size_t tope) {
    if (m == nullptr || n == 0) return;
    cola.insert(cola.end(), m, m + n);
    if (cola.size() > tope) cola.erase(cola.begin(), cola.begin() + static_cast<long>(cola.size() - tope));
}

/// Pasa `m` (de `de` Hz a `a` Hz, estéreo intercalado) por interpolación lineal. Una voz
/// para el oído de una persona: no hace falta más (y libwebrtc casi siempre entrega a 48 kHz).
inline std::vector<float> remuestrear(const float* m, std::size_t n, int de, int a) {
    if (m == nullptr || n < 2 || de <= 0 || a <= 0) return {};
    if (de == a) return std::vector<float>(m, m + n);
    const std::size_t cuadros = n / 2;
    const std::size_t salen = static_cast<std::size_t>(static_cast<double>(cuadros) * a / de);
    std::vector<float> out(salen * 2);
    for (std::size_t i = 0; i < salen; ++i) {
        const double pos = static_cast<double>(i) * de / a;
        const std::size_t j = std::min(static_cast<std::size_t>(pos), cuadros - 1);
        const std::size_t k = std::min(j + 1, cuadros - 1);
        const float f = static_cast<float>(pos - static_cast<double>(j));
        out[2 * i] = m[2 * j] * (1.0f - f) + m[2 * k] * f;
        out[2 * i + 1] = m[2 * j + 1] * (1.0f - f) + m[2 * k + 1] * f;
    }
    return out;
}

/// Suma a `dst` (n muestras) lo que haya en la cola, hasta n, y lo saca de la cola. Recortado
/// a [-1, 1]. Devuelve cuántas muestras sumó (0 = la cola estaba vacía y `dst` quedó igual).
inline std::size_t mezclar(std::deque<float>& cola, float* dst, std::size_t n) {
    const std::size_t k = std::min(n, cola.size());
    for (std::size_t i = 0; i < k; ++i) dst[i] = std::clamp(dst[i] + cola[i], -1.0f, 1.0f);
    cola.erase(cola.begin(), cola.begin() + static_cast<long>(k));
    return k;
}

} // namespace ba::gateway::colavoz
