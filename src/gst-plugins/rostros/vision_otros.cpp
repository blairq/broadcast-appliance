// ============================================================================
// src/gst-plugins/rostros/vision_otros.cpp — sin Vision (Windows, Linux): YuNet siempre
// ============================================================================
#include "vision.hpp"

namespace ba::rostros {

bool hayVision() { return false; }

bool detectarConVision(const std::uint8_t*, int, int, std::vector<CajaVision>& caras) {
    caras.clear();
    return false;
}

} // namespace ba::rostros
