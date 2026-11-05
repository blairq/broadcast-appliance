// ============================================================================
// src/gateway/rtc/ColasCompartidas.hpp — cuántos hilos para las colas compartidas de libwebrtc
//
// La topología de la máquina y el reparto de las colas en sus dos grupos (pesadas y
// livianas), sin libwebrtc: lo usan las capas (rtc/CapasHilos) y la fábrica de colas en sí,
// que está en el backend (webrtc/ColasRtc.hpp: qué son y por qué).
// ============================================================================
#pragma once

#include <string_view>

namespace ba::gateway::colas {

struct Opciones {
    int hilosPesadas = 0;  ///< 0 = automático (nucleosDeUnBloque)
    int hilosLivianas = 0; ///< 0 = automático (2; 1 si pesadas es 1)
};

/// Los núcleos físicos que comparten el último nivel de caché con el núcleo 0 (un CCD en
/// Ryzen, un grupo de rendimiento en Apple M), sin limitar. 0 si el sistema no lo dice.
int nucleosDeUnBloque();

/// Los hilos de cada grupo que usaría `o` en esta máquina (después de lo automático).
Opciones resolver(const Opciones& o);

/// Si la cola `nombre` (el que le pone libwebrtc) va al grupo de las pesadas.
bool esPesada(std::string_view nombre);

} // namespace ba::gateway::colas
