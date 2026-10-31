// ============================================================================
// src/gst-plugins/ipc/espera.hpp — dormir en el hilo de una fuente hasta un plazo
//
// baanillo y bavideoanillo sondean su canal (nadie avisa que hay algo nuevo) y
// duermen hasta la próxima mirada (ipc/Mirada.hpp). Mientras duermen, el pipeline
// puede pedirles que suelten (unlock: `parar`): se duerme en pasos de hasta 16 ms,
// así una espera corta es UNA despertada y parar se nota en a lo sumo 16 ms. Con
// platform::dormirHasta: en Windows, el temporizador fino (sleep_for redondea al
// tic de 15,6 ms y 4 ms eran 15,6).
// ============================================================================
#pragma once

#include "platform/Ritmo.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>

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

/// Duerme `ms`; false si mientras tanto se prendió `parar`.
inline bool esperar(const std::atomic<bool>& parar, int ms) {
    return esperarHasta(parar, std::chrono::steady_clock::now() + std::chrono::milliseconds(ms));
}

} // namespace ba::ipc_gst
