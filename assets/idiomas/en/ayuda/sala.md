## People
`room_status` lists everyone: `state` waiting (lobby) or admitted, `slot` (place in the program, 0 = none), `lobbySlot`, `speaker` (speakers mode), mic/cam/screen/hand, connection, info and contact.

## Lobby
- Private: whoever is waiting only sees you (what you choose with `operator_feed`) and never the others.
- `participant_admit`, `participant_admit_all`, `participant_reject`.
- `participant_talk {"on": true}`: private conversation (only that person hears you; you hear them through monitoring).

## Inside
- Their camera is the source `Invitado N` (N = slot). `guest_source_add` puts it in a scene.
- `participant_request {"what": "mute-mic"|"mute-cam"|"unmute-mic"|"unmute-cam"|"stop-screen"|"lower-hand"}` (the unmutes turn back on what was turned off), `participant_kick`.
- `chat_send` (to everyone or to one person), `invitados_chat_leer`.
- Files they share: `share_list`, `compartida_revisar` (see the image), `share_take` (to the scene), `share_delete`.

## Joining
- `room_link {"target": "local"|"remote", "withPin"}` gives the link and its QR code (image). `room_pin` changes the PIN. `room_set {"lobby", "locked"}`.
- `network_status`: HTTPS, WireGuard tunnel, links.
- Having the assistant **attend on its own** to those who arrive (by chat or **by speaking**: what they say is turned into text and they are answered by voice), automatic sign-up in People and the attendant's voice: `atencion_configurar` / `atencion_estado` (also the assembly's remote session: with `asambleaHablando`, the person waiting is spoken to with `chat_send {"to": <id del integrante>}` and heard with `chat_wait {"participant": <id>}`; one at a time, it ends when they are let in). Who gets in on their own if recognized is the event roll (`padron_ver {"solos": true}`).
- For them to join **from the internet** (pinggy, Cloudflare, own tunnel): `help {"topic": "acceso_remoto"}`.