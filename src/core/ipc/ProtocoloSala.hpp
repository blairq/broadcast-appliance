// ============================================================================
// src/core/ipc/ProtocoloSala.hpp — la versión del contrato entre la app (A) y la sala (B)
//
// Lo que A y B se dicen: la API de control (Runtime/gateway-admin.json y /api/v1/*), el
// flujo de eventos (SSE /api/v1/events) y lo que la sala le manda a las páginas por /ws.
// B la publica en gateway-admin.json y en el primer `room` de cada conexión de eventos;
// A la compara con la suya y, si no coinciden, lo dice en el registro (un paquete mezclado
// de dos versiones, no un error de la sala).
//
// Sube SOLO cuando un cambio deja de ser aditivo (una clave que cambia de sentido, una
// ruta que se va). Agregar claves o eventos no la cambia: los dos lados ignoran lo que no
// conocen.
//
//   1  lo de siempre (sin versión)
//   2  `epoch` y `reanudado` en el primer `room`, ids «<epoch>:<n>» en los eventos y
//      Last-Event-ID; el tally viaja entero por /ws
// ============================================================================
#pragma once

namespace ba::ipc {

inline constexpr int kProtocoloSala = 2;

} // namespace ba::ipc
