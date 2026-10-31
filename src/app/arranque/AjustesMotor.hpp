// ============================================================================
// src/app/arranque/AjustesMotor.hpp — los ajustes de Configuración que van al motor
//
// Un solo lugar para pasarle al motor (en caliente) lo que se ajusta en la
// página: lo llaman el arranque (OperateMode) y Configuración al aplicar.
// ============================================================================
#pragma once

namespace ba::app {

struct AppConfig;
class BroadcastEngine;

/// En el dueño del motor (los umbrales del disco son de `motor`).
void aplicarAjustesMotor(const AppConfig& c, BroadcastEngine& motor);

} // namespace ba::app
