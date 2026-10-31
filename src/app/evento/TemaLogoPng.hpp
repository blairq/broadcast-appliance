// ============================================================================
// src/app/evento/TemaLogoPng.hpp — el PNG de un logo SVG del evento
//
// El QR que dibuja la app es un SVG con el logo del evento al centro en un <image>, y
// QtSvg no pinta un SVG adentro de otro (solo imágenes de mapa de bits). Por eso, al
// poner un logo SVG, se dibuja una vez su PNG al lado en Tema/ (ba::tema::pngDeLogo:
// mismo nombre, .png) y el QR usa ese (ba::tema::logoParaQr). El PNG se va con su logo
// (limpiarRecursos).
//
// Es lo único del tema que necesita Qt (QtSvg): el resto del modelo (TemaEvento) es puro.
// ============================================================================
#pragma once

#include <filesystem>

#include "evento/TemaEvento.hpp"

namespace ba::evento {

/// El lado mayor del PNG: sobra para el centro de un QR impreso grande.
constexpr int kLadoPngDeLogo = 512;

/// Dibuja el SVG `svg` como PNG en `png` (fondo transparente, `lado` px el lado mayor, la
/// proporción del SVG). false si QtSvg no lo pudo leer o no se pudo escribir.
bool dibujarPngDeLogo(const std::filesystem::path& svg, const std::filesystem::path& png, int lado = kLadoPngDeLogo);

/// Para cada logo SVG que nombra el tema (logo, logoClaro, favicon), su PNG en Tema/ si
/// todavía no está (un tema de antes, uno copiado a mano). Devuelve cuántos dibujó.
int asegurarPngDeLogos(const std::filesystem::path& carpetaEvento, const TemaEvento& t);

} // namespace ba::evento
