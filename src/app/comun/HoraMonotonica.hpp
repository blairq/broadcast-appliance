// ============================================================================
// src/app/comun/HoraMonotonica.hpp — el reloj monotónico de la app (nanosegundos)
//
// El que usan las horas que se pasan entre piezas (las voces, los rings de
// audio, lo que pinta la app): antes era os_gettime_ns de libobs. Cualquier pieza que
// traduce estas horas a otro reloj (GstVoz al del pipeline) lee los dos juntos,
// así que solo importa que todas usen ESTE.
// ============================================================================
#pragma once

#include <chrono>
#include <cstdint>

namespace ba::app {

inline std::uint64_t ahoraNs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace ba::app
