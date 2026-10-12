// ============================================================================
// src/core/ipc/CanalesEscena.hpp — los canales de escena hacia las pantallas
//
// Una pantalla de la sala puede mostrar una escena que no es el programa
// (docs/funciones/PANTALLAS.md). Cada escena que se está mostrando viaja de A
// a B por su propio canal de video, BA.Escena.<k>; dos pantallas con la misma
// escena comparten canal. Cuántos hay es un tope fijo para los dos procesos:
// cada canal ocupado es una composición más en el motor y una codificación
// por pantalla en el gateway.
// ============================================================================
#pragma once

#include <string>

namespace ba::ipc {

constexpr int kCanalesEscena = 4;

inline std::wstring nombreCanalEscena(int k) { return L"BA.Escena." + std::to_wstring(k); }

} // namespace ba::ipc
