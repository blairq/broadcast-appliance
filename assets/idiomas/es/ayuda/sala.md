## Personas
`room_status` lista a todos: `state` waiting (lobby) o admitted, `slot` (lugar en el programa, 0 = ninguno), `lobbySlot`, `speaker` (modo oradores), mic/cam/screen/hand, conexion, info y contacto.

## Lobby
- Privado: quien espera solo te ve a vos (lo que elijas con `operator_feed`) y nunca a los demas.
- `participant_admit`, `participant_admit_all`, `participant_reject`.
- `participant_talk {"on": true}`: conversacion privada (solo esa persona te escucha; vos la escuchas por monitoreo).

## Adentro
- Su camara es la fuente `Invitado N` (N = slot). `guest_source_add` la pone en una escena.
- `participant_request {"what": "mute-mic"|"mute-cam"|"unmute-mic"|"unmute-cam"|"stop-screen"|"lower-hand"}` (los unmute vuelven a prender lo que se apago), `participant_kick`.
- `chat_send` (a todos o a una persona), `invitados_chat_leer`.
- Archivos que comparten: `share_list`, `compartida_revisar` (ver la imagen), `share_take` (a la escena), `share_delete`.

## Ingreso
- `room_link {"target": "local"|"remote", "withPin"}` da el enlace y su QR (imagen). `room_pin` cambia el PIN. `room_set {"lobby", "locked"}`.
- `network_status`: HTTPS, tunel WireGuard, enlaces.
- Que el asistente **atienda solo** a los que llegan (por chat o **hablando**: lo que dicen pasa a texto y se les contesta con voz), el alta automática en Personas y la voz del atendedor: `atencion_configurar` / `atencion_estado` (también la sesión remota de la asamblea: con `asambleaHablando`, al que espera se le habla con `chat_send {"to": <id del integrante>}` y se lo oye con `chat_wait {"participant": <id>}`; de a uno, termina al habilitarlo). Quién entra solo si se lo reconoce es el padrón del evento (`padron_ver {"solos": true}`).
- Para que entren **desde internet** (pinggy, Cloudflare, túnel propio): `help {"topic": "acceso_remoto"}`.