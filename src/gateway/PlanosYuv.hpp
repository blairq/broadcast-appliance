// ============================================================================
// src/gateway/PlanosYuv.hpp — un cuadro I420 tal como sale del decodificador (sin copiar)
//
// Para quien lo va a ACHICAR (el retrato de la grilla): achicar en I420 (1,5 bytes por
// píxel, libyuv I420Scale) y convertir a BGRA lo chico cuesta menos de la mitad que
// convertir el cuadro entero y achicarlo en BGRA (banco del 2026-11-02, 1280x720 a
// 480x270: 103 µs contra 243 µs más la conversión). Vale mientras dura la llamada.
// ============================================================================
#pragma once

#include <cstdint>

namespace ba::gateway {

struct PlanosYuv {
    const std::uint8_t* y = nullptr;
    const std::uint8_t* u = nullptr;
    const std::uint8_t* v = nullptr;
    int strideY = 0, strideU = 0, strideV = 0;
    int ancho = 0, alto = 0;
};

} // namespace ba::gateway
