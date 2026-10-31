// src/app/video/Sincronia.hpp — cuánto atrasa una entrada de video respecto de otra
//
// Con las HUELLAS de dos entradas (la tarjeta `sincro` del grafo: por cada
// cuadro, su hora y una grilla de luma de 16x9) se busca el
// corrimiento, en cuadros, que mejor las alinea. Dos señales:
//
//   - ESPACIAL: el cambio de la grilla de un cuadro al siguiente. Si las dos
//     entradas son la MISMA imagen por caminos distintos (un stream que llega
//     por SRT y por RTMP, una cámara por SDI y por red) coinciden casi
//     exacto: es la medición más precisa.
//   - ENERGÍA: cuánto cambia la imagen en total. Sirve para dos cámaras
//     distintas mirando lo mismo (un aplauso, un flash, alguien que entra):
//     el momento del cambio es el mismo aunque el encuadre no.
//
// Se usa la espacial si es clara; si no, la energía. Sin movimiento en común
// no hay nada que medir, y se dice.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ba::app::video {

struct Medicion {
    bool ok = false;
    /// > 0: B atrasa respecto de A (lo mismo pasa en B más tarde), en ms.
    double desfaseMs = 0;
    /// 0..1: qué tan claro es el pico de la correlación.
    double confianza = 0;
    std::string metodo;  ///< "espacial" o "energia"
    double cuadroMs = 0; ///< la duración de un cuadro (la resolución de la medición)
    int cuadros = 0;     ///< cuántos cuadros en común se usaron
    std::string motivo;  ///< por qué no se pudo (si !ok)
};

/// :param ta, ga: horas (ns) y grillas (144 bytes por cuadro) de A.
/// :param maxMs: hasta cuánto desfase se busca, para un lado y para el otro.
Medicion medirDesfase(const std::vector<std::uint64_t>& ta, const std::vector<std::uint8_t>& ga,
                      const std::vector<std::uint64_t>& tb, const std::vector<std::uint8_t>& gb,
                      double maxMs = 1500);

} // namespace ba::app::video
