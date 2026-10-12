// ============================================================================
// src/gst-plugins/rostros/vision.hpp — el detector de caras de Apple (Vision)
//
// En la Mac, VNDetectFaceRectanglesRequest corre en el Neural Engine: casi no
// cuesta CPU. Solo detecta (dónde hay caras); reconocer sigue siendo YuNet +
// SFace, que dan los cinco puntos con los que se endereza la cara.
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>

namespace ba::rostros {

/// Una cara de Vision, en fracciones del cuadro (origen arriba a la izquierda).
struct CajaVision {
    float x, y, ancho, alto, confianza;
};

/// true si hay Vision en este sistema (macOS); false en las otras plataformas.
bool hayVision();
/// Las caras de un cuadro BGRA contiguo (ancho * 4 bytes por fila).
bool detectarConVision(const std::uint8_t* bgra, int ancho, int alto, std::vector<CajaVision>& caras);

} // namespace ba::rostros
