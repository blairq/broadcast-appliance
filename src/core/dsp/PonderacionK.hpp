// ============================================================================
// src/core/dsp/PonderacionK.hpp — la ponderación K de BS.1770 y sus bloques de 100 ms
//
// Lo común a todo lo que mide loudness: el estante alto (+4 dB arriba de ~1,7
// kHz, la cabeza) y el pasa altos (~38 Hz) de ITU-R BS.1770-4 para cualquier
// frecuencia, y la energía media de cada bloque de 100 ms (L + R, pesos 1).
// Con eso salen el momentáneo (4 bloques), el corto (30) y el integrado.
//
// Lo usan el medidor del programa (app/audio/LoudnessPrograma) y el control
// lento de ganancia hacia el objetivo (dsp/AgcLoudness, en balimitador).
// ============================================================================
#pragma once

#include <array>
#include <cstdint>

namespace ba::dsp {

class PonderacionK {
   public:
    explicit PonderacionK(double tasa = 48000) { preparar(tasa); }
    void preparar(double tasa);
    void reiniciar();
    /// Una muestra de cada canal (mono: la misma en los dos). true si con esta se
    /// cerró un bloque de 100 ms; `energia` es la de ese bloque.
    bool muestra(double l, double r, double& energia) {
        const double yl = pasaAltos_[0].paso(estante_[0].paso(l));
        const double yr = pasaAltos_[1].paso(estante_[1].paso(r));
        suma_ += yl * yl + yr * yr;
        if (++enBloque_ < porBloque_) return false;
        energia = suma_ / static_cast<double>(enBloque_);
        suma_ = 0;
        enBloque_ = 0;
        return true;
    }
    /// Energía media → LUFS (−120 si no hay nada).
    static double aLufs(double energia);
    /// LUFS → energía media.
    static double aEnergia(double lufs);

   private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double paso(double x) {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };
    std::array<Biquad, 2> estante_, pasaAltos_;
    std::uint32_t porBloque_ = 4800;
    std::uint32_t enBloque_ = 0;
    double suma_ = 0;
};

} // namespace ba::dsp
