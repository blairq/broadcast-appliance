// ============================================================================
// src/app/video/Scopes.hpp — los instrumentos de video: forma de onda, parade,
// vectorscopio e histograma (T39)
//
// Se dibujan a partir de una imagen RGB del programa o de una fuente (la que ya
// se renderiza para los monitores o para capture_source): no tocan la cadena
// de video ni agregan nada al camino al aire. Se achica la imagen a ~480 px de
// ancho antes de contar, así un cuadro cuesta unos pocos milisegundos y se
// pueden refrescar a 5-10 por segundo en la GUI.
//
// Escala: la imagen llega en RGB de rango completo (0..255 = 0..100 %). La
// forma de onda marca 0, 25, 50, 75 y 100 % y, en rojo, lo que se pasa del
// rango legal (por debajo de 16 o por encima de 235, en escala de 8 bits).
// El vectorscopio usa Cb/Cr BT.709 con las cajas de los colores al 75 % y la
// línea de piel.
// ============================================================================
#pragma once

#include <QImage>
#include <QString>

namespace ba::app::video {

enum class TipoScope { Onda, Parade, Vector, Histograma };

/// "onda", "parade", "vector", "histograma" (también "waveform", "vectorscope",
/// "histogram"). Devuelve false si no la conoce.
bool tipoScopeDeTexto(const QString& texto, TipoScope& tipo);

/// Qué tan fuera del rango legal está la imagen: porcentaje de píxeles con luma
/// por debajo de 16 o por encima de 235 (escala de 8 bits), y la croma pasada.
struct Legalidad {
    double lumaBajaPct = 0;
    double lumaAltaPct = 0;
    double cromaPct = 0; ///< luma ± croma fuera de la gama compuesta PAL (±133 %), como balegal
};

/// Dibuja el instrumento pedido. `ancho`×`alto` es el tamaño de la imagen de salida.
QImage dibujarScope(const QImage& imagen, TipoScope tipo, int ancho = 512, int alto = 288);

Legalidad medirLegalidad(const QImage& imagen);

} // namespace ba::app::video
