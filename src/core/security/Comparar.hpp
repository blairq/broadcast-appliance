// ============================================================================
// src/core/security/Comparar.hpp — comparar una clave sin filtrar cuánto acertó
//
// Una comparación común (`==`) corta en el primer carácter distinto: midiendo cuánto
// tarda, de afuera se adivina la clave de a una letra. Esta recorre siempre todo.
// Vacía nunca coincide: una superficie sin clave configurada no está abierta.
// ============================================================================
#pragma once

#include <string_view>

namespace ba::security {

inline bool igualesEnTiempoConstante(std::string_view dada, std::string_view esperada) {
    if (esperada.empty() || dada.size() != esperada.size()) return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < dada.size(); ++i)
        diff |= static_cast<unsigned char>(dada[i] ^ esperada[i]);
    return diff == 0;
}

} // namespace ba::security
