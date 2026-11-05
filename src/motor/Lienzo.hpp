// ============================================================================
// src/motor/Lienzo.hpp — el lienzo del estudio (Motor::Config, las escenas internas)
//
// Aparte de Piezas.hpp para que quien solo lo configura (Motor.hpp, la app) no traiga
// GStreamer. Sus caps y su color, en Piezas.hpp.
// ============================================================================
#pragma once

#include <string>

namespace ba::motor {

/// El lienzo del estudio: a qué se normaliza cada entrada y a qué sale el programa.
struct Lienzo {
    int ancho = 1920, alto = 1080;
    int fpsN = 30, fpsD = 1;
    std::string color = "bt709";  ///< bt601, bt709, bt2100-pq, bt2100-hlg
    bool rangoCompleto = false;
    bool diezBits = false;        ///< P010 en vez de NV12 (también con HDR)
};

} // namespace ba::motor
