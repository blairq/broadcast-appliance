// ============================================================================
// src/app/ui/EjecutorLinea.cpp — ver EjecutorLinea.hpp
// ============================================================================
#include "ui/EjecutorLinea.hpp"

#include <algorithm>
#include <limits>

namespace ba::app {

EjecutorLinea::EjecutorLinea(evento::linea::Corrida& corrida, std::string salida, Hacer hacer, std::function<int64_t()> reloj,
                             QObject* parent)
    : QObject(parent), corrida_(corrida), salida_(std::move(salida)), hacer_(std::move(hacer)), reloj_(std::move(reloj)) {
    // Un disparo a la hora justa. Si al disparar todavía no toca (el reloj de pared
    // se movió), el tick no hace nada y se vuelve a programar.
    timer_.setSingleShot(true);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &EjecutorLinea::toca);
}

void EjecutorLinea::programar() {
    const int64_t ms = corrida_.proximoMs(reloj_(), salida_);
    if (ms < 0) {
        timer_.stop();
        return;
    }
    timer_.start(static_cast<int>(std::min<int64_t>(ms, std::numeric_limits<int>::max())));
}

} // namespace ba::app
