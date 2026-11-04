// ============================================================================
// src/app/arranque/AjustesMotor.cpp — ver AjustesMotor.hpp
// ============================================================================
#include "arranque/AjustesMotor.hpp"

#include "Entrada.hpp"
#include "Salidas.hpp"
#include "arranque/AppSettings.hpp"
#include "emision/ApiMotor.hpp"

namespace ba::app {

void aplicarAjustesMotor(const AppConfig& c, ApiMotor& motor) {
    motor::ponerReposoPlaca(c.placaReposoS);
    motor::ponerAjustesEntradas(c.sinSenalMs, c.rearmarSinDatosS, c.reintentoEntradaMaxMs);
    motor::ponerReintentoTransmision(c.reintentoTransmisionMaxS);
    motor.ponerUmbralesDisco(c.discoApurosS, c.discoRecuperadoS);
}

} // namespace ba::app
