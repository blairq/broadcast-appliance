// ============================================================================
// src/core/diag/RitmoCuadros.hpp — cada cuánto llega un cuadro, siempre y barato
//
// La fluidez de un video no la dice «cuántos cuadros por segundo» (15 que llegan de a dos
// cada 133 ms son 15 por segundo y se ven a saltos): la dice el INTERVALO entre un cuadro
// y el siguiente. Esto lo anota en cada punto del camino de un video (lo que llega de la
// red, lo que sale del decodificador, lo que pinta la grilla, lo que dibuja la ventana) y
// da, de la ventana de los últimos kVentana intervalos: p50/p95/máx, los huecos (más de 1,5
// veces el período que debería tener: lo que se ve como «se pegó») y los cuadros por segundo.
//
// Barato: anotar es guardar un entero en un anillo fijo y sumar dos contadores (atómicos,
// relajados, sin candados ni memoria nueva): se puede llamar con cada cuadro desde el hilo
// que lo mueve. Un solo hilo anota en cada RitmoCuadros (el que mueve ese video); leer es
// de cualquier hilo y puede ver un intervalo a medio escribir de la vuelta anterior (un
// número de más o de menos en 128: para medir alcanza). El resumen ordena una copia de 128
// enteros: se lee cada segundo o menos, nunca por cuadro.
// ============================================================================
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

namespace ba::diag {

class RitmoCuadros {
   public:
    /// Cuántos intervalos guarda (a 30 cps, ~4 s; a 15, ~8 s).
    static constexpr int kVentana = 128;

    /// Llegó (o se dibujó) un cuadro a `ahoraUs` (reloj monotónico, µs).
    void cuadro(std::int64_t ahoraUs) {
        const std::int64_t antes = ultimoUs_.exchange(ahoraUs, std::memory_order_relaxed);
        cuadros_.fetch_add(1, std::memory_order_relaxed);
        if (antes == 0 || ahoraUs <= antes) return;
        // Más de 5 s sin cuadros no es un hueco del video: no estaba (no se lo miraba, se fue).
        const std::int64_t d = ahoraUs - antes;
        if (d > 5'000'000) return;
        const std::uint32_t i = pos_.fetch_add(1, std::memory_order_relaxed);
        intervalos_[i % kVentana].store(static_cast<std::uint32_t>(d), std::memory_order_relaxed);
    }

    /// Lo de la ventana, comparado con `esperadoUs` (el período que debería tener; 0 = no se sabe).
    struct Resumen {
        int n = 0;                ///< intervalos en la ventana
        double cps = 0;           ///< n / la suma de los intervalos
        double p50Ms = 0, p95Ms = 0, maxMs = 0;
        int huecos = 0;           ///< intervalos de más de 1,5 períodos (o de 100 ms sin período)
        double regularidad = 1;   ///< 1 - huecos/n (1 = parejo)
        std::uint64_t cuadros = 0; ///< desde que se creó
        std::int64_t ultimoUs = 0; ///< el último cuadro
    };
    Resumen resumen(std::int64_t esperadoUs = 0) const {
        Resumen r;
        r.cuadros = cuadros_.load(std::memory_order_relaxed);
        r.ultimoUs = ultimoUs_.load(std::memory_order_relaxed);
        const std::uint32_t pos = pos_.load(std::memory_order_relaxed);
        const int n = static_cast<int>(std::min<std::uint32_t>(pos, kVentana));
        if (n == 0) return r;
        std::array<std::uint32_t, kVentana> v{};
        std::uint64_t suma = 0;
        for (int k = 0; k < n; ++k) {
            v[k] = intervalos_[(pos - 1 - static_cast<std::uint32_t>(k)) % kVentana].load(std::memory_order_relaxed);
            suma += v[k];
        }
        std::sort(v.begin(), v.begin() + n);
        const auto pct = [&](double p) { return v[std::min(n - 1, static_cast<int>(p * (n - 1) + 0.5))] / 1000.0; };
        r.n = n;
        r.cps = suma > 0 ? n * 1e6 / static_cast<double>(suma) : 0;
        r.p50Ms = pct(0.5);
        r.p95Ms = pct(0.95);
        r.maxMs = v[n - 1] / 1000.0;
        const std::uint32_t umbral = esperadoUs > 0 ? static_cast<std::uint32_t>(esperadoUs * 3 / 2) : 100'000U;
        r.huecos = static_cast<int>(v.begin() + n - std::upper_bound(v.begin(), v.begin() + n, umbral));
        r.regularidad = 1.0 - static_cast<double>(r.huecos) / n;
        return r;
    }

    /// Los intervalos de la ventana en orden (el más viejo primero), en µs: para ver si los
    /// huecos de dos tramos son los mismos (diagnóstico; copia 128 enteros).
    std::array<std::uint32_t, kVentana> serie(int& n) const {
        std::array<std::uint32_t, kVentana> v{};
        const std::uint32_t pos = pos_.load(std::memory_order_relaxed);
        n = static_cast<int>(std::min<std::uint32_t>(pos, kVentana));
        for (int k = 0; k < n; ++k) v[k] = intervalos_[(pos - static_cast<std::uint32_t>(n) + static_cast<std::uint32_t>(k)) % kVentana].load(std::memory_order_relaxed);
        return v;
    }

    /// El último cuadro (0 = ninguno).
    std::int64_t ultimoUs() const { return ultimoUs_.load(std::memory_order_relaxed); }

    /// Vuelve a empezar la ventana (cambió lo que se mide: otra capa, otra fuente).
    void reiniciar() {
        pos_.store(0, std::memory_order_relaxed);
        ultimoUs_.store(0, std::memory_order_relaxed);
    }

   private:
    std::array<std::atomic<std::uint32_t>, kVentana> intervalos_{};
    std::atomic<std::uint32_t> pos_{0};
    std::atomic<std::int64_t> ultimoUs_{0};
    std::atomic<std::uint64_t> cuadros_{0};
};

} // namespace ba::diag
