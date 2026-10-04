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
    /// Empuja un bloque que trae HORA (la de libobs, os_gettime_ns): una
    /// fuente que entrega audio agendado a futuro (la voz del sistema dice una
    /// frase entera de una vez; el teleprompter la agenda segundos antes)
    /// suena a su hora y entera: si hay un hueco hasta esa hora se rellena con
    /// silencio, y lo que viene a futuro no se recorta. Lo que viene «ahora»
    /// (un micrófono) va como `push`, con el control de deriva.
    void pushEn(const float* a, const float* b, std::uint32_t n, std::uint64_t tsNs, std::uint64_t ahoraNs,
                std::uint32_t frecuencia) {
        if (frecuencia == 0) frecuencia = 48000;
        const std::uint64_t durNs = static_cast<std::uint64_t>(n) * 1'000'000'000ULL / frecuencia;
        const bool futuro = tsNs + durNs > ahoraNs + 200'000'000ULL;
        if (!futuro) {
            push(a, b, n);
            return;
        }
        std::lock_guard<std::mutex> g(m);
        // Hasta cuándo alcanza lo que ya hay (se saca a ritmo real desde ahora).
        const std::uint64_t fin =
            ahoraNs + static_cast<std::uint64_t>(l.size()) * 1'000'000'000ULL / frecuencia;
        if (tsNs > fin + 20'000'000ULL && tsNs - ahoraNs < 60'000'000'000ULL) {
            const std::size_t hueco = static_cast<std::size_t>((tsNs - fin) * frecuencia / 1'000'000'000ULL);
            l.insert(l.end(), hueco, 0.0f);
            r.insert(r.end(), hueco, 0.0f);
        }
        l.insert(l.end(), a, a + n);
        r.insert(r.end(), b, b + n);
        // Tope de seguridad (un minuto): nada agendado tan lejos.
        const std::size_t tope = static_cast<std::size_t>(frecuencia) * 60;
        while (l.size() > tope) {
            l.pop_front();
            r.pop_front();
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
