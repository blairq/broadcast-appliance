.. ============================================================================
   ba-gateway — Proceso B (Gateway WebRTC + conversación + túnel)
   ============================================================================

..
   Página del componente gateway (src/gateway). IMPLEMENTADO: Webrtc (sesiones
   + hub), SignalingServer (HTTP + WebSocket), Cloudflared (túnel), MediaPump
   (IPC ↔ WebRTC) y TokenAuth (sobre ba_core::security::TokenService).
   Feature-flag BA_GATEWAY_HAVE_WEBRTC. Sirve Web/guest/.

ba-gateway (Proceso B)
======================

`ba-gateway` es el **Proceso B**: gateway **WebRTC** + motor de conversación +
túnel (spec §8). Es **headless**: ícono en bandeja, sin Qt (spec §3.3). En
Windows enlaza `libwebrtc` estáticamente cuando el SDK está presente
(feature-flag `BA_GATEWAY_HAVE_WEBRTC=1`); si no, usa un **backend NULL**
(`BA_GATEWAY_HAVE_WEBRTC=0`) que completa la señalización (offer→answer) para
que el flujo sea testeable sin el SDK. Usa `/MD` (CRT dinámica) por
consistencia con libwebrtc.

Estado actual: **IMPLEMENTADO**. La lógica vive en estas piezas:

- **`Webrtc`** (`WebrtcSession` + `WebrtcHub`): una `PeerConnection` por
  invitado (B es el **answerer** del offer del invitado) y el hub que las
  agrupa (1..4 invitados, estrella con B como hub). Tracks locales de B
  (retorno: audio de presentación + video de programa) y tracks remotos del
  invitado (voz → ring `BA.Audio.Guest.<n>`; cámara → sender Spout
  `BA.Guest.<n>`). Backend real (libwebrtc: AEC3, NetEq, control de
  congestión) o NULL según el flag. Move-only.
- **`SignalingServer`**: servidor embebido (sin Qt, sin Boost) que expone la
  señalización y la app web del invitado. Escucha **solo en 127.0.0.1**
  (spec §5.6; cloudflared se conecta ahí y expone el túnel). Dos superficies:
  HTTP (estático + API: `GET /`, `/healthz`, `POST /api/join`, `GET
  /api/verify`) y WebSocket (señalización WebRTC por invitado, con el framing
  `ba_core::ipc::Frame`). Sirve `Web/guest/` (index.html, app.js, style.css).
- **`MediaPump`**: motor de medios que enruta IPC ↔ WebRTC por invitado
  (spec §8.5, §8.6). **Retorno** (A→B→invitado): consume el ring
  `BA.Audio.ContentForReturn` y el sender `BA.Program` y los inyecta en los
  tracks locales. **Uplink** (invitado→B→A): publica la voz en
  `BA.Audio.Guest.<n>` y la cámara en `BA.Guest.<n>` (un sender existe
  siempre, spec §4.2).
- **`TokenAuth`**: envoltorio thread-safe de
  `ba::security::TokenService` (spec §8.3) para el servidor: `issue(room, pin)`
  y `verify(token, room)`, con cambio de PIN en caliente (`setPin`, ChangePin
  spec §4.1) y rate-limit/lockout.
- **`Cloudflared`** (`CloudflaredSupervisor`): autostart + recovery + Job
  Object (KILL_ON_JOB_CLOSE) de `cloudflared.exe` (spec §8.8). El túnel lleva
  solo HTTP/WebSocket (la señalización), no el tráfico UDP de WebRTC.

Enlaza `ba_core`.

Rol (spec §8)
-------------

- **Señalización segura** (spec 8.3): emisión/verificación de tokens firmados
  con HMAC a partir de "sala + PIN" con rate-limit/lockout, sobre
  `ba::security::TokenService` (ver :doc:`modules/core`).
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
