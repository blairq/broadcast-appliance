// ============================================================================
// src/app/emision/FuentePdf.hpp — la fuente «PDF» de las escenas (ba_pdf)
//
// Un PDF en una escena, una página por vez, para mostrarlo YA (sin desarmarlo en
// láminas): se elige de la documentación del evento (docs/) o se da su ruta, y se
// pasan las páginas desde la tarjeta «PDF», el MCP (pdf_escena_*) o la botonera.
// La pinta la app (ui/PdfEscena): la página entera, sin deformar, sobre el fondo.
//
//   ajustes: documento (el id de uno de docs/; vacío = `archivo`), archivo (ruta),
//            pagina (1 = la primera), zoom (1-8) y x, y (el centro de lo que se ve,
//            0 a 1 sobre la página), ancho y alto (el lienzo, en píxeles), fondo
//            (color 0xAABBGGRR)
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <functional>

namespace ba::app::fuentespdf {

constexpr const char* kTipo = "ba_pdf";

/// Registra el tipo (antes de cargar las escenas).
void registrar();
/// De dónde salen los documentos para la lista (los pone ui/PdfEscena: [{value, label}]).
void ponerOpciones(std::function<nlohmann::json()> f);

} // namespace ba::app::fuentespdf
