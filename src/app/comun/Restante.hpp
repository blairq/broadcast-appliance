// ============================================================================
// src/app/comun/Restante.hpp — lo que le queda a algo que termina (las cuentas regresivas)
//
// Un medio al aire, un cue del guion, lo que suena de la playlist, el paso de la
// conducción: el modelo da el estado de su cuenta y la vista lo pinta con
// Theme.colorDeEstado. UN umbral para todas (antes, «≤ 10 s» escrito en cada QML).
// Sin Qt: lo usa también la línea (evento/LineaVista).
// ============================================================================
#pragma once

namespace ba::app {

/// Los últimos segundos de lo que termina (un medio al aire, un cue del guion, lo que
/// suena de la playlist): se pintan en rojo. UN umbral para todas las cuentas regresivas.
inline constexpr double kUltimosSegundos = 10;

/// El estado de lo que le queda (para Theme.colorDeEstado): "excedido" en los últimos
/// segundos (o terminado), "ok" antes. Lo que `puedePasarse` (un paso del guion con su
/// tiempo previsto): "aviso" en los últimos segundos y "excedido" recién al pasarse.
inline const char* estadoRestante(double segundos, bool puedePasarse = false) {
    if (puedePasarse) return segundos < 0 ? "excedido" : segundos <= kUltimosSegundos ? "aviso" : "ok";
    return segundos <= kUltimosSegundos ? "excedido" : "ok";
}

} // namespace ba::app
