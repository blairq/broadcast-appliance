// ============================================================================
// src/core/platform/Ritmo.cpp — ver Ritmo.hpp (lo común; dormirHasta va por sistema)
// ============================================================================
#include "platform/Ritmo.hpp"

namespace ba::platform {

Ritmo::Ritmo(Reloj::duration periodo, Reloj::duration atrasoMax)
    : periodo_(periodo), atrasoMax_(atrasoMax > periodo ? atrasoMax : periodo), proximo_(Reloj::now()) {}

void Ritmo::reiniciar() { proximo_ = Reloj::now(); }

void Ritmo::esperar() {
    proximo_ += periodo_;
    dormirHasta(proximo_);
    const auto ahora = Reloj::now();
    if (ahora - proximo_ > atrasoMax_) proximo_ = ahora; // se atrasó de más: no corre para alcanzarlo
}

} // namespace ba::platform
