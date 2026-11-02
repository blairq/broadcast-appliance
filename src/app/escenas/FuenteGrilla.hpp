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

#include <functional>
#include <string>

namespace ba::app::grilla {

/// Registra el tipo (antes de cargar las escenas).
void registrarTipo();
/// Los ajustes de la entrada del motor para estos ajustes de la fuente.
nlohmann::json ajustesMotor(const nlohmann::json& ajustes);
/// Quién arma la escena del orador de una grilla (el botón de Propiedades): lo pone
/// escenas::ControlGrillas al arrancar.
void ponerArmarEscenaOrador(std::function<void(const std::string& fuente)> fn);

} // namespace ba::app::grilla
