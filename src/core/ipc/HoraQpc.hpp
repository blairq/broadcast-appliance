// ============================================================================
// src/core/ipc/HoraQpc.hpp — pasar una hora de otro reloj al de los sellos (QPC)
//
// Los sellos entre procesos van en el reloj de HealthBlock::nowQpc (QPC en Windows,
// steady_clock en ns en los demás). Lo que pasa en un pipeline de GStreamer tiene su
// hora en el reloj del pipeline. Para pasar de uno a otro se leen los DOS relojes en el
// mismo instante y la diferencia traduce, sin suponer que tengan la misma base. Lo usan
// el anillo de audio que escribe el motor (baanillosink) y las vistas del video
// (Vistas: la hora del contenido de cada cuadro, F2-11): las dos con la misma cuenta,
// así el audio y el video del programa quedan comparables en el gateway.
// ============================================================================
#pragma once

#include <algorithm>
#include <cstdint>

namespace ba::ipc {

/// El QPC de `hora` (en un reloj que marcaba `relojAhora` cuando QPC marcaba `qpcAhora`).
inline std::uint64_t qpcDeOtroReloj(std::uint64_t hora, std::uint64_t relojAhora, std::uint64_t qpcAhora) {
    if (hora <= relojAhora) return qpcAhora - std::min<std::uint64_t>(qpcAhora, relojAhora - hora);
    return qpcAhora + (hora - relojAhora);
}

} // namespace ba::ipc
