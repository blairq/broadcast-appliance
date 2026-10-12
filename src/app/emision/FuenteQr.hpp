// ============================================================================
// src/app/emision/FuenteQr.hpp — la fuente «Código QR» de las escenas (ba_qr)
//
// Un QR en cualquier escena: uno de los del aparato (el de la página del
// Público con su clave, el de la Sala con el PIN, el del Prompter, el de los
// Rótulos: siempre al día, los da la sala), uno guardado en «Códigos QR» (con
// su estilo), o un texto escrito a mano. La pinta la app (ui/FuenteQrPintor).
//
//   ajustes: qr ("aparato:publico" | el id de uno guardado | ""), texto (si qr
//            está vacío), fondo (blanco alrededor, para que se escanee sobre
//            cualquier imagen), tamano (lado en píxeles)
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <functional>

namespace ba::app::fuentesqr {

constexpr const char* kTipo = "ba_qr";

/// Registra el tipo (antes de cargar las escenas).
void registrar();
/// De dónde salen las opciones de la lista (las pone el pintor: [{value, label}]).
void ponerOpciones(std::function<nlohmann::json()> f);

} // namespace ba::app::fuentesqr
