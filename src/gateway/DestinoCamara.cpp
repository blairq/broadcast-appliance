// ============================================================================
// src/gateway/DestinoCamara.cpp — ver DestinoCamara.hpp
// ============================================================================
#include "DestinoCamara.hpp"

#include "MediaPump.hpp"
#include "ajustes/Ajustes.hpp"

namespace ba::gateway {

bool DestinoCamara::quiereEntero() const { return pump_ != nullptr && pump_->llegoCamara(sesion_); }

void DestinoCamara::entero(const std::uint8_t* bgra, int w, int h) {
    if (pump_ == nullptr || bgra == nullptr || w <= 0 || h <= 0) return;
    pump_->forwardVideo(sesion_, bgra, static_cast<std::size_t>(w) * h * 4u, w, h);
}

Grillas::PedidoCelda DestinoCamara::pedirCelda(int w, int h) {
    if (grillas_ == nullptr) return {};
    return grillas_->pedirCelda(persona_, w, h, Grillas::reloj());
}

void DestinoCamara::guardarCelda(Grillas::PedidoCelda&& p) {
    if (grillas_ != nullptr) grillas_->guardarCelda(std::move(p));
}

void DestinoCamara::soltarCelda(Grillas::PedidoCelda&& p) {
    if (grillas_ != nullptr) grillas_->soltarCelda(std::move(p));
}

bool DestinoCamara::celdaEnPlaca() { return ba::ajustes::siNo("gateway.celdaEnPlaca", true); }

} // namespace ba::gateway
