// ============================================================================
// src/app/escenas/FuenteGrilla.hpp — el tipo de fuente «Grilla de personas» (ba_grilla)
//
// Lo que ve la ventana Propiedades (las filas y los valores de fábrica, los
// mismos nombres que lee grilla::Config) y la entrada del motor: el canal
// BA.Grilla.<canal> que pinta el gateway, tomado con bavideoanillo como un
// invitado. El canal lo asigna escenas/ControlGrillas (el primero libre).
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

namespace ba::app::grilla {

/// Registra el tipo (antes de cargar las escenas).
void registrarTipo();
/// Los ajustes de la entrada del motor para estos ajustes de la fuente.
nlohmann::json ajustesMotor(const nlohmann::json& ajustes);

} // namespace ba::app::grilla
