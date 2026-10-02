// src/app/audio/RingEstereo.hpp — un anillo de audio estéreo (sin Qt)
//
// La toma de una fuente, la salida de un bus de la Matriz, la tarjeta
// «Entrada» de un rack: alguien empuja audio (el callback de captura de una
// fuente) y otro lo saca (la mezcla, un dispositivo, el rack). Con colchón
// de arranque y descarte de lo viejo si se acumula (deriva entre relojes).
// Sin Qt: lo usa también el rack, que se prueba sin Qt.

#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <mutex>

namespace ba::app::audio::anillo {

constexpr std::uint32_t kBloque = 480; // 10 ms a 48 kHz
constexpr std::uint32_t kColchon = 2 * kBloque;
constexpr std::uint32_t kTope = 8 * kBloque;

/// Ring estéreo planar, para la toma de una fuente o la salida de un bus.
struct Ring {
    std::mutex m;
    std::deque<float> l, r;
    bool primado = false;
    void push(const float* a, const float* b, std::uint32_t n) {
        std::lock_guard<std::mutex> g(m);
        l.insert(l.end(), a, a + n);
        r.insert(r.end(), b, b + n);
        // Deriva: si se acumula, se tira lo viejo (vuelve al colchón).
        while (l.size() > kTope) {
            l.erase(l.begin(), l.begin() + static_cast<std::ptrdiff_t>(l.size() - kColchon));
            r.erase(r.begin(), r.begin() + static_cast<std::ptrdiff_t>(r.size() - kColchon));
        }
    }
    /// Saca n frames (con ceros si falta). Hasta tener el colchón no entrega.
    bool pop(float* a, float* b, std::uint32_t n) {
        std::lock_guard<std::mutex> g(m);
        if (!primado) {
            if (l.size() < kColchon) return false;
            primado = true;
        }
        const std::uint32_t hay = static_cast<std::uint32_t>(std::min<std::size_t>(n, l.size()));
        std::copy(l.begin(), l.begin() + hay, a);
        std::copy(r.begin(), r.begin() + hay, b);
        l.erase(l.begin(), l.begin() + hay);
        r.erase(r.begin(), r.begin() + hay);
        std::fill(a + hay, a + n, 0.0f);
        std::fill(b + hay, b + n, 0.0f);
        if (hay == 0) primado = false; // se cortó: vuelve a juntar colchón
        return hay > 0;
    }
};

} // namespace ba::app::audio::anillo
