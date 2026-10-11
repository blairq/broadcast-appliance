// ============================================================================
// src/app/arranque/AjustesMotor.cpp — ver AjustesMotor.hpp
// ============================================================================
#include "arranque/AjustesMotor.hpp"

#include "Entrada.hpp"
#include "Salidas.hpp"
#include "arranque/AppSettings.hpp"
#include "emision/BroadcastEngine.hpp"

namespace ba::app {

void aplicarAjustesMotor(const AppConfig& c) {
    motor::ponerReposoPlaca(c.placaReposoS);
    motor::ponerAjustesEntradas(c.sinSenalMs, c.rearmarSinDatosS, c.reintentoEntradaMaxMs);
    motor::ponerReintentoTransmision(c.reintentoTransmisionMaxS);
    BroadcastEngine::ponerUmbralesDisco(c.discoApurosS, c.discoRecuperadoS);
}

} // namespace ba::app
