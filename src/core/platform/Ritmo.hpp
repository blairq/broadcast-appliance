// ============================================================================
// src/core/platform/Ritmo.hpp — dormir hasta un plazo con la precisión que pide el audio
//
// Los hilos que marcan un ritmo (el retorno de audio del gateway cada 2 ms, sus bajadas de
// video cada 4 ms, la Matriz cada 10 ms) duermen entre vuelta y vuelta. En Windows, dormir
// con sleep_for/sleep_until redondea al tic del sistema (15,6 ms si nadie pidió otro):
// un hilo que quiere despertarse cada 2 ms se despierta cada 15,6 y el anillo de 160 ms se
// llena (docs/arquitectura/04-plan-unificado.md, F0-03). Acá:
//   Windows  un temporizador de alta resolución por hilo (CreateWaitableTimerExW con
//            CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, Windows 10 1803+), sin tocar la
//            resolución del sistema entero (nada de timeBeginPeriod); y el proceso fuera del
//            ahorro que ignora la resolución pedida (platform/Energia, sinAhorroDeEnergia).
//            Sin ese temporizador (un Windows viejo), sleep_until como antes.
//   macOS/Linux  sleep_until: el planificador ya despierta con precisión de decenas de µs.
//
// Ritmo lleva el plazo ABSOLUTO (próximo += período), así el trabajo de cada vuelta no se
// suma al período. Si se atrasó más de `atrasoMax` (el hilo estuvo trabado, la máquina
// suspendida), no corre para alcanzarlo: vuelve a contar desde ahora.
// ============================================================================
#pragma once

#include <chrono>

namespace ba::platform {

/// Duerme el hilo que llama hasta `plazo` (steady_clock), con la mejor precisión del sistema.
void dormirHasta(std::chrono::steady_clock::time_point plazo);

class Ritmo {
   public:
    using Reloj = std::chrono::steady_clock;

    /// `atrasoMax`: cuánto atraso se recupera corriendo (vueltas sin dormir) antes de
    /// volver a contar desde ahora. Un bucle que solo mira si hay algo nuevo no recupera
    /// nada (atrasoMax = período); uno que produce un bloque por vuelta, sí.
    explicit Ritmo(Reloj::duration periodo, Reloj::duration atrasoMax = Reloj::duration::zero());

    /// Duerme hasta el próximo plazo.
    void esperar();
    /// El próximo plazo cuenta desde ahora (después de una espera larga de otra cosa).
    void reiniciar();

   private:
    Reloj::duration periodo_;
    Reloj::duration atrasoMax_;
    Reloj::time_point proximo_;
};

} // namespace ba::platform
