// RecoveryUi.hpp
//
// Proposito:
//   INTERFAZ de la UI de recuperacion minima (Win32, spec 3.1.11): solo es
//   visible si A no esta corriendo; muestra el estado y un boton "Reiniciar
//   sistema". Esta tarea (Fase 1, parte 1) SOLO declara la interfaz; la
//   implementacion Win32 queda para una fase posterior.
//
// Modelo de threading:
//   show() se invoca desde el thread principal cuando el supervisor detecta
//   que A no esta vivo y no hay politica de reinicio aplicable.
//
// Estrategia de errores:
//   show() devuelve bool (true si mostro la UI). En esta fase es un no-op
//   documentado que loguea y devuelve false.

#pragma once

#include <string>

namespace ba::launcher {

/// Interfaz de la UI de recuperacion minima (stub en Fase 1).
class RecoveryUi {
public:
    /// Muestra la UI de recuperacion si A no esta corriendo.
    ///
    /// :param statusText: texto de estado a mostrar.
    /// :returns: true si la UI se mostro y el usuario eligio reiniciar.
    static bool show(const std::wstring& statusText);
};

} // namespace ba::launcher
