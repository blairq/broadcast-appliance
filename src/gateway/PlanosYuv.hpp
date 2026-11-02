// ============================================================================
// src/gateway/PlanosYuv.hpp — un cuadro I420 tal como sale del decodificador (sin copiar)
//
// Para quien lo va a ACHICAR (el retrato de la grilla): achicar en I420 (1,5 bytes por
// píxel, libyuv I420Scale) y convertir a BGRA lo chico cuesta menos de la mitad que
// convertir el cuadro entero y achicarlo en BGRA (banco del 2026-11-02, 1280x720 a
// 480x270: 103 µs contra 243 µs) cuando se achica a la mitad o menos (el «box» de libyuv).
// Entre la mitad y el tamaño entero libyuv interpola (bilineal) y en I420 es peor que en
// BGRA (600x338: 579 µs contra 369; 1100x619: 1558 contra 1128): ahí sirve el BGRA del
// mismo cuadro, si ya está convertido (`bgra`). Vale mientras dura la llamada.
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
    const std::uint8_t* bgra = nullptr; ///< el mismo cuadro en BGRA (ancho*4 por fila), si ya está
};

} // namespace ba::gateway
