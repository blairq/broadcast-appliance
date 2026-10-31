// ============================================================================
// src/gst-plugins/ipc/espera.hpp — dormir en el hilo de una fuente hasta un plazo
//
// baanillo y bavideoanillo duermen hasta que el productor toca el timbre del canal
// (ipc/Timbre.hpp, F3-13; unlock los despierta con un toque) o, si no toca, hasta la
// próxima mirada (ipc/Mirada.hpp). Sin timbre, mientras duermen el pipeline puede
// pedirles que suelten (unlock: `parar`): se duerme en pasos de hasta 16 ms, así una
// espera corta es UNA despertada y parar se nota en a lo sumo 16 ms. Con
// platform::dormirHasta: en Windows, el temporizador fino (sleep_for redondea al
// tic de 15,6 ms y 4 ms eran 15,6).
// ============================================================================
#pragma once

#include "platform/Ritmo.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace ba::ipc_gst {

/// Duerme hasta `plazo`; false si mientras tanto se prendió `parar`.
inline bool esperarHasta(const std::atomic<bool>& parar, std::chrono::steady_clock::time_point plazo) {
    for (;;) {
        if (parar.load()) return false;
        const auto ahora = std::chrono::steady_clock::now();
        if (ahora >= plazo) return true;
        ba::platform::dormirHasta(std::min(plazo, ahora + std::chrono::milliseconds(16)));
    }
}

/// Sin nada nuevo en el canal: hasta que el productor toque el timbre (ipc/Timbre.hpp,
/// F3-13) o, si no toca (uno viejo, o un macOS anterior a 14.4), hasta su próxima mirada
/// (de a 16 ms, por `parar`). `visto`: el timbre del lector anotado ANTES de mirar. Con
/// timbre, quien prende `parar` despierta al lector (despertar()). false si se prendió `parar`.
template <typename Lector>
bool esperarNuevo(const std::atomic<bool>& parar, Lector& lector, std::uint32_t visto, std::chrono::steady_clock::duration pasoQuieto) {
    if (!lector.tocaTimbre()) return esperarHasta(parar, lector.proximaMirada(pasoQuieto));
    if (parar.load()) return false;
    lector.esperarNuevo(visto, pasoQuieto);
    return !parar.load();
}

/// Duerme `ms`; false si mientras tanto se prendió `parar`.
inline bool esperar(const std::atomic<bool>& parar, int ms) {
    return esperarHasta(parar, std::chrono::steady_clock::now() + std::chrono::milliseconds(ms));
}

} // namespace ba::ipc_gst
