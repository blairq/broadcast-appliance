.. ============================================================================
   ba-gateway — Proceso B (Gateway WebRTC + conversación + túnel)
   ============================================================================

..
   Página del componente gateway (src/gateway). Es un SKELETON: solo el
   andamiaje de build. Documenta su rol previsto (spec §8).

ba-gateway (Proceso B)
======================

`ba-gateway` es el **Proceso B**: gateway **WebRTC** + motor de conversación +
túnel (spec §8). Es **headless**: ícono en bandeja, sin Qt (spec §3.3). En
Windows enlaza `libwebrtc` estáticamente; en Linux se configura como esqueleto
para que el árbol compile. Usa `/MD` (CRT dinámica) por consistencia con
libwebrtc.

Estado actual: **SKELETON** — solo el andandiaje de build. La lógica (libwebrtc,
motor de conversación, señalización, supervisión de `cloudflared`) la aporta el
agente del Proceso B. Enlaza `ba_core`.

Rol previsto (spec §8)
----------------------

- **Señalización segura** (spec 8.3): emisión/verificación de tokens firmados
  con HMAC a partir de "sala + PIN" con rate-limit/lockout. La primitiva ya
  existe en el núcleo: `ba::security::TokenService` (ver :doc:`modules/core`).
- **Túnel** (spec 8.8): supervisión de `cloudflared` para exponer el gateway
  a la red.
- **Motor de conversación**: N invitados, retorno N−1, AEC afinado,
  estadísticas y app del invitado pulida (Fase 7).
- **Flujos IPC**: consume los rings de audio (`ba::ipc::SharedAudioRing`), el
  transporte Spout (`ba::ipc::SpoutTransport`) y el canal de control
  (`ba::ipc::ControlProtocol`) para el retorno de invitados al Proceso A.

Instalación
-----------

Proceso B va a `bin/64bit/` (spec §5.1).
