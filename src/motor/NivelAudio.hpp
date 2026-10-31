// ============================================================================
// src/motor/NivelAudio.hpp — un nivel de los vúmetros (Motor::niveles)
//
// Aparte de Motor.hpp para que lo lea quien no usa el motor (el canal de niveles de la
// app, emision/NivelesMotor, y la interfaz que lo lee) sin traer GStreamer.
// ============================================================================
#pragma once

#include <string>

namespace ba::motor {

/// Una entrada con audio (lo que entrega, después de su rack) o un bus (programa,
/// pista<n>, retorno<n>, matriz:<id>): por canal, pico y RMS en dB (-100 = nada).
struct NivelAudio {
    std::string id;
    bool bus = false;
    float pico[2] = {-100, -100};
    float rms[2] = {-100, -100};
};

} // namespace ba::motor
