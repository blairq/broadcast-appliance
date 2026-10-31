// ============================================================================
// src/gateway/Ducking.hpp — el IFB: el programa baja mientras habla el operador
//
// docs/arquitectura/04-plan-unificado.md, F2-12 (03 §3.4). Cuando el operador le habla a
// un invitado al aire (IFB, «hablar»), lo que el invitado oye de fondo baja para que la voz
// se entienda. Antes era ×0,4 fijo (−8 dB), de golpe al llegar el primer bloque de voz y de
// golpe al irse: se oía el salto y, entre sílabas, el programa subía y bajaba (bombeo).
// Ahora, como una consola:
//
//   - un detector de la voz que la sostiene 150 ms después de la última muestra que pasó
//     el umbral: entre sílabas y palabras no se suelta;
//   - si la voz pasa el umbral (−45 dBFS), la ganancia del programa va hacia la
//     profundidad (de fábrica −15 dB; Configuración → avanzado «room.ifbProfundidadDb»,
//     lo usual en IFB es −12 a −20) con un ataque de 10 ms;
//   - cuando la voz se va, vuelve a 1 con una liberación de 300 ms.
//
// Una ganancia por cuadro (las dos muestras del estéreo igual), la misma para todas las
// sesiones de un retorno: la voz es una sola por salida (MediaPump::ColaIfb).
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace ba::gateway {

class Ducking {
   public:
    static constexpr double kAtaqueMs = 10.0;
    static constexpr double kLiberacionMs = 300.0;
    static constexpr double kSostenMs = 150.0; ///< lo que el detector sostiene la voz después de oírla
    static constexpr double kUmbralDb = -45.0;

    explicit Ducking(int frecuencia = 48000) : frecuencia_(std::max(1, frecuencia)) {
        ataque_ = coeficiente(kAtaqueMs);
        liberacion_ = coeficiente(kLiberacionMs);
        sosten_ = static_cast<long>(kSostenMs * static_cast<double>(frecuencia_) / 1000.0);
        umbral_ = std::pow(10.0, kUmbralDb / 20.0);
        ponerProfundidadDb(-15.0);
    }

    /// Cuánto baja el programa bajo la voz (dB, de −40 a 0).
    void ponerProfundidadDb(double db) { profundidad_ = std::pow(10.0, std::clamp(db, -40.0, 0.0) / 20.0); }
    double profundidad() const { return profundidad_; }

    /// La ganancia del programa para cada cuadro de un bloque de `n` floats estéreo
    /// intercalados; `voz` (los mismos `n`) o nullptr si en este bloque no hay voz. Deja en
    /// `ganancia` n/2 valores. Devuelve true si hay que aplicarla (hay voz o la ganancia
    /// todavía no volvió a 1): con false el programa pasa tal cual.
    bool procesar(const float* voz, std::size_t n, std::vector<float>& ganancia) {
        const std::size_t cuadros = n / 2;
        if (voz == nullptr && ganancia_ >= 1.0 && quedan_ == 0) return false;
        ganancia.resize(cuadros);
        for (std::size_t i = 0; i < cuadros; ++i) {
            const double x = voz != nullptr ? std::max(std::fabs(voz[2 * i]), std::fabs(voz[2 * i + 1])) : 0.0;
            if (x >= umbral_)
                quedan_ = sosten_;
            else if (quedan_ > 0)
                --quedan_;
            const double objetivo = quedan_ > 0 ? profundidad_ : 1.0;
            const double a = objetivo < ganancia_ ? ataque_ : liberacion_;
            ganancia_ = objetivo + (ganancia_ - objetivo) * a;
            if (ganancia_ > 0.9999 && objetivo >= 1.0) ganancia_ = 1.0;
            ganancia[i] = static_cast<float>(ganancia_);
        }
        return true;
    }

    double ganancia() const { return ganancia_; }

   private:
    /// El coeficiente de un filtro de un polo con constante de tiempo `ms`.
    double coeficiente(double ms) const { return std::exp(-1000.0 / (ms * static_cast<double>(frecuencia_))); }

    int frecuencia_;
    double ataque_ = 0, liberacion_ = 0, umbral_ = 0;
    double profundidad_ = 1.0;
    long sosten_ = 0, quedan_ = 0; ///< en cuadros: cuánto se sostiene la voz y cuánto le queda
    double ganancia_ = 1.0;
};

} // namespace ba::gateway
