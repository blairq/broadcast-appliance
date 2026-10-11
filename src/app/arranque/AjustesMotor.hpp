// ============================================================================
// src/app/arranque/AjustesMotor.hpp — los ajustes de Configuración que van al motor
//
// Un solo lugar para pasarle al motor (en caliente) lo que se ajusta en la
// página: lo llaman el arranque (OperateMode) y Configuración al aplicar.
// ============================================================================
#pragma once

namespace ba::app {

struct AppConfig;

void aplicarAjustesMotor(const AppConfig& c);

} // namespace ba::app
