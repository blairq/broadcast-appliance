// src/app/audio/RingEstereo.hpp — un anillo de audio estéreo (sin Qt)
//
// La toma de una fuente, la salida de un bus de la Matriz, la tarjeta
// «Entrada» de un rack: alguien empuja audio (el callback de captura de una
// fuente) y otro lo saca (la mezcla, un dispositivo, el rack). Con colchón
// de arranque y descarte de lo viejo si se acumula (deriva entre relojes).
// Sin Qt: lo usa también el rack, que se prueba sin Qt.
//
// SIN CANDADO (F3-08; 03 P11): un solo escritor y un solo lector (SPSC), cada uno
// con su índice atómico. Antes era un deque con mutex: el hilo de audio del
// dispositivo esperaba al que empujaba y cada bloque reservaba memoria. El
// descarte por deriva lo hace el LECTOR (el escritor no puede mover el índice del
// otro): si al sacar hay más de kTope, salta lo viejo hasta quedar en el colchón;
// solo si lo último que llegó fue «ahora» (push): lo agendado a futuro (pushEn, la
// voz del sistema) no se recorta. Si el escritor llena la capacidad (el lector no
// lee), lo que no entra se tira y se cuenta (desbordes).
//
// Mientras la Matriz no termine de pasar al motor (docs/arquitectura/04-plan-unificado.md,
// F3-08), esto es lo que queda entre el motor y Qt Multimedia.

#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>

namespace ba::app::audio::anillo {

constexpr std::uint32_t kBloque = 480; // 10 ms a 48 kHz
constexpr std::uint32_t kColchon = 2 * kBloque;
constexpr std::uint32_t kTope = 8 * kBloque;
/// La capacidad de fábrica (cuadros por canal, potencia de 2): el tope y holgura de sobra.
constexpr std::uint32_t kCapacidad = 16384;
/// La de un anillo que recibe audio agendado (pushEn: la voz del sistema dice una frase
/// entera de una vez): ~43 s a 48 kHz (16 MB). Lo que no entra se tira (desbordes).
constexpr std::uint32_t kCapacidadAgendado = 1u << 21;

/// Ring estéreo planar, para la toma de una fuente o la salida de un bus. Un escritor
/// (push/pushEn) y un lector (pop), cada uno en su hilo.
struct Ring {
    /// `capacidad`: cuadros por canal (se redondea a potencia de 2). La de fábrica alcanza
    /// para lo que llega «ahora»; uno que recibe audio agendado (pushEn) pide más.
    explicit Ring(std::uint32_t capacidad = kCapacidad) {
        std::uint32_t c = 1;
        while (c < capacidad) c <<= 1;
        cap_ = c;
        izq_ = std::make_unique<float[]>(c);
        der_ = std::make_unique<float[]>(c);
    }
    Ring(const Ring&) = delete;
    Ring& operator=(const Ring&) = delete;

    void push(const float* a, const float* b, std::uint32_t n) {
        escribir(a, b, n);
        ahora_.store(true, std::memory_order_relaxed);
    }
    /// Empuja un bloque que trae HORA (la monotónica de la app, ba::app::ahoraNs): una
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
        ahora_.store(false, std::memory_order_relaxed);
        // Hasta cuándo alcanza lo que ya hay (se saca a ritmo real desde ahora).
        const std::uint64_t fin = ahoraNs + static_cast<std::uint64_t>(disponibles()) * 1'000'000'000ULL / frecuencia;
        if (tsNs > fin + 20'000'000ULL && tsNs - ahoraNs < 60'000'000'000ULL) {
            std::uint64_t hueco = (tsNs - fin) * frecuencia / 1'000'000'000ULL;
            static const float ceros[kBloque] = {};
            while (hueco > 0) {
                const auto k = static_cast<std::uint32_t>(std::min<std::uint64_t>(hueco, kBloque));
                if (escribir(ceros, ceros, k) < k) return; // no entra: lo agendado no tiene lugar
                hueco -= k;
            }
        }
        escribir(a, b, n);
    }
    /// Saca n frames (con ceros si falta). Hasta tener el colchón no entrega.
    bool pop(float* a, float* b, std::uint32_t n) {
        const std::uint64_t w = w_.load(std::memory_order_acquire);
        std::uint64_t r = r_.load(std::memory_order_relaxed);
        std::uint64_t hay = w - r;
        if (!primado_) {
            if (hay < kColchon) {
                std::fill(a, a + n, 0.0f);
                std::fill(b, b + n, 0.0f);
                return false;
            }
            primado_ = true;
        }
        // Deriva: si se acumula (el que empuja va más rápido), se tira lo viejo y vuelve al
        // colchón. Lo agendado a futuro no.
        if (hay > kTope && ahora_.load(std::memory_order_relaxed)) {
            r = w - kColchon;
            hay = kColchon;
            recortes.fetch_add(1, std::memory_order_relaxed);
        }
        const auto saca = static_cast<std::uint32_t>(std::min<std::uint64_t>(n, hay));
        copiarDesde(r, a, izq_.get(), saca);
        copiarDesde(r, b, der_.get(), saca);
        r_.store(r + saca, std::memory_order_release);
        std::fill(a + saca, a + n, 0.0f);
        std::fill(b + saca, b + n, 0.0f);
        // Se quedó corto (el que empuja va más lento): vuelve a juntar colchón. Antes solo
        // con el anillo vacío del todo: con un escritor apenas más lento, cada bloque salía
        // con un cuadro de menos (un click cada 10 ms, para siempre); ahora es UN hueco.
        if (saca < n) primado_ = false;
        return saca > 0;
    }
    /// Cuadros que hay para sacar (desde cualquiera de los dos lados).
    std::uint32_t disponibles() const {
        return static_cast<std::uint32_t>(w_.load(std::memory_order_acquire) - r_.load(std::memory_order_acquire));
    }

    /// Diagnóstico: las veces que el lector tiró lo viejo por deriva, y los cuadros que el
    /// escritor no pudo dejar (el anillo lleno: el lector no lee).
    std::atomic<std::uint64_t> recortes{0}, desbordes{0};

   private:
    /// Escribe lo que entra (lo que no, se tira y se cuenta); devuelve cuántos entraron.
    std::uint32_t escribir(const float* a, const float* b, std::uint32_t n) {
        const std::uint64_t w = w_.load(std::memory_order_relaxed);
        const std::uint64_t r = r_.load(std::memory_order_acquire);
        const auto libres = static_cast<std::uint32_t>(cap_ - (w - r));
        const std::uint32_t entra = std::min(n, libres);
        if (entra < n) desbordes.fetch_add(n - entra, std::memory_order_relaxed);
        copiarHacia(w, izq_.get(), a, entra);
        copiarHacia(w, der_.get(), b, entra);
        w_.store(w + entra, std::memory_order_release);
        return entra;
    }
    void copiarHacia(std::uint64_t pos, float* dst, const float* src, std::uint32_t n) const {
        const auto i = static_cast<std::uint32_t>(pos & (cap_ - 1));
        const std::uint32_t primero = std::min(n, cap_ - i);
        std::copy(src, src + primero, dst + i);
        std::copy(src + primero, src + n, dst);
    }
    void copiarDesde(std::uint64_t pos, float* dst, const float* src, std::uint32_t n) const {
        const auto i = static_cast<std::uint32_t>(pos & (cap_ - 1));
        const std::uint32_t primero = std::min(n, cap_ - i);
        std::copy(src + i, src + i + primero, dst);
        std::copy(src, src + (n - primero), dst + primero);
    }

    std::uint32_t cap_ = 0;
    std::unique_ptr<float[]> izq_, der_;
    alignas(64) std::atomic<std::uint64_t> w_{0}; ///< lo escrito (solo el escritor)
    alignas(64) std::atomic<std::uint64_t> r_{0}; ///< lo leído (solo el lector)
    alignas(64) bool primado_ = false;            ///< del lector
    std::atomic<bool> ahora_{true};               ///< lo último fue «ahora» (push): se puede recortar
};

} // namespace ba::app::audio::anillo
