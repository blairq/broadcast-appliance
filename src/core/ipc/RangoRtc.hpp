// ============================================================================
// src/core/ipc/RangoRtc.hpp — el rango UDP de WebRTC por omisión
//
// Lo usan la configuración de la sala (arranque/AppSettings, gateway/Config), lo que la
// valida (gateway/PuertosRtc) y Configuración → Red de la conferencia. Aparte del tope de
// invitados (ipc/LugaresSala.hpp): nombrar el rango no arrastra el resto.
// ============================================================================
#pragma once

namespace ba::ipc {

/// El rango UDP de WebRTC por omisión (Configuración → Red de la conferencia): 1600
/// puertos. Cada sesión abre uno por red (dos en una máquina con IPv4 e IPv6, uno más
/// con el túnel de WireGuard), así que entran 533 sesiones con tres por sesión: el tope
/// de invitados más el lobby. Antes 40000-40099 (100 puertos, ~50 sesiones con dos redes).
inline constexpr int kRtcPuertoMin = 40000;
inline constexpr int kRtcPuertoMax = 41599;
/// El último puerto del rango por omisión de antes (40000-40099): un gateway.json que lo
/// tiene tal cual nunca lo eligió nadie, se lleva al rango nuevo (rangoRtcDeAntes).
inline constexpr int kRtcPuertoMaxViejo = 40099;
/// ¿Es el rango por omisión de antes? (se reemplaza por el nuevo al leer la configuración)
inline bool rangoRtcDeAntes(int min, int max) { return min == kRtcPuertoMin && max == kRtcPuertoMaxViejo; }

} // namespace ba::ipc
