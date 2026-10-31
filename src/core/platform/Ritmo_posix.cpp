// ============================================================================
// src/core/platform/Ritmo_posix.cpp — ver Ritmo.hpp (macOS y Linux)
// ============================================================================
#include "platform/Ritmo.hpp"

#include <thread>

namespace ba::platform {

// Linux despierta un sleep con precisión de decenas de µs: no hace falta nada más.
//
// macOS le da a cada espera un margen proporcional a lo que dura (junta temporizadores para
// ahorrar energía): medido en una M1 Pro (2026-10-31), una espera de 4 ms despierta ~2 ms
// tarde, una de 10 ms ~4 y una de 25 ms ~7 (hasta 10), también con QoS interactiva. Para un
// hilo que duerme hasta el próximo cuadro (ipc/Mirada.hpp, ~30 ms) eso es la demora entera.
// Así que se duerme por tramos: el 70 % de lo que falta (el margen de ese tramo cae antes
// del plazo) hasta que quedan 2 ms, y eso de una. Son 2-3 despertadas en vez de una y el
// plazo se cumple con ~0,6 ms (medido: 25 ms → 0,64 ms tarde de media, contra 7,2).
void dormirHasta(std::chrono::steady_clock::time_point plazo) {
#if defined(__APPLE__)
    for (;;) {
        const auto falta = plazo - std::chrono::steady_clock::now();
        if (falta <= std::chrono::milliseconds(2)) break;
        std::this_thread::sleep_for(falta * 7 / 10);
    }
#endif
    std::this_thread::sleep_until(plazo);
}

} // namespace ba::platform
