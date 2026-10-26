// src/core/hw/Antibucle.cpp — ver Antibucle.hpp

#include "hw/Antibucle.hpp"

#include <algorithm>

namespace ba::hw {

void Antibucle::programar(std::int64_t ahoraMs) {
    std::int64_t cuando = ahoraMs + t_.esperaMs;
    if (hubo_) cuando = std::max(cuando, inicio_ + t_.minimoEntreMs);
    // Si ya había una programada, se queda con la más tardía (los avisos seguidos la corren
    // hasta que se calman, sin pasar el tope).
    programada_ = pendiente_ ? std::max(programada_, cuando) : cuando;
    pendiente_ = true;
}

bool Antibucle::aviso(std::int64_t ahoraMs, const std::string& firma) {
    if (enumerando_ || (hubo_ && ahoraMs < fin_ + t_.graciaMs)) {
        ++ignorados_; // el eco de la enumeración propia
        return pendiente_;
    }
    if (hubo_ && firma == firmaVista_ && !pendiente_) {
        ++ignorados_; // lo mismo que ya se vio
        return false;
    }
    programar(ahoraMs);
    return true;
}

bool Antibucle::vence(std::int64_t ahoraMs, const std::string& firma) {
    if (!pendiente_ || enumerando_ || ahoraMs < programada_) return false;
    pendiente_ = false;
    if (hubo_ && firma == firmaVista_) {
        ++ignorados_; // fue y volvió mientras se esperaba
        return false;
    }
    return true;
}

void Antibucle::empieza(std::int64_t ahoraMs, const std::string& firma) {
    enumerando_ = true;
    pendiente_ = false;
    hubo_ = true;
    inicio_ = ahoraMs;
    firmaInicio_ = firma;
}

void Antibucle::termina(std::int64_t ahoraMs, const std::string& firma) {
    enumerando_ = false;
    fin_ = ahoraMs;
    // Lo que vio la enumeración es lo que había al empezar.
    firmaVista_ = firmaInicio_;
    if (firma != firmaInicio_) programar(ahoraMs);
}

std::int64_t Antibucle::falta(std::int64_t ahoraMs) const {
    if (!pendiente_) return -1;
    return std::max<std::int64_t>(0, programada_ - ahoraMs);
}

} // namespace ba::hw
