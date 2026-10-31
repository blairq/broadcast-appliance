// ============================================================================
// src/app/comun/Duracion.hpp — cuánto lleva algo, como «hh:mm:ss» (los relojes del aire)
//
// Lo usan las pastillas de la raíz (grabando, transmitiendo) y el aire de cada salida:
// un solo formato para los dos.
// ============================================================================
#pragma once

#include <QChar>
#include <QString>

#include <chrono>

namespace ba::app {

/// El tiempo desde `desde` (reloj monotónico: no se mueve si alguien cambia la hora).
inline QString hmsDesde(std::chrono::steady_clock::time_point desde) {
    const auto s = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - desde).count();
    return QString("%1:%2:%3")
        .arg(s / 3600, 2, 10, QChar('0'))
        .arg((s / 60) % 60, 2, 10, QChar('0'))
        .arg(s % 60, 2, 10, QChar('0'));
}

} // namespace ba::app
