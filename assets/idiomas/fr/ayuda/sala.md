## Personnes
`room_status` liste tout le monde : `state` waiting (salle d'attente) ou admitted, `slot` (place dans le programme, 0 = aucune), `lobbySlot`, `speaker` (mode intervenants), mic/cam/screen/hand, connexion, infos et contact.

## Salle d'attente
- Privée : la personne qui attend ne voit que vous (ce que vous choisissez avec `operator_feed`) et jamais les autres.
- `participant_admit`, `participant_admit_all`, `participant_reject`.
- `participant_talk {"on": true}` : conversation privée (seule cette personne vous entend ; vous l'entendez dans le retour).

## À l'intérieur
- Sa caméra est la source `Invitado N` (N = slot). `guest_source_add` la place dans une scène.
- `participant_request {"what": "mute-mic"|"mute-cam"|"unmute-mic"|"unmute-cam"|"stop-screen"|"lower-hand"}` (les unmute rallument ce qui avait été coupé), `participant_kick`.
- `chat_send` (à tous ou à une personne), `invitados_chat_leer`.
- Fichiers qu'ils partagent : `share_list`, `compartida_revisar` (voir l'image), `share_take` (vers la scène), `share_delete`.

## Entrée
- `room_link {"target": "local"|"remote", "withPin"}` donne le lien et son QR (image). `room_pin` change le PIN. `room_set {"lobby", "locked"}`.
- `network_status` : HTTPS, tunnel WireGuard, liens.
- Pour que l'assistant **accueille tout seul** les personnes qui arrivent (par chat ou **à la voix** : ce qu'elles disent est transcrit et on leur répond avec une voix), l'inscription automatique dans Personnes et la voix de l'accueil : `atencion_configurar` / `atencion_estado` (aussi la séance à distance de l'assemblée : avec `asambleaHablando`, on parle à la personne qui attend avec `chat_send {"to": <id del integrante>}` et on l'entend avec `chat_wait {"participant": <id>}` ; une à la fois, cela se termine quand on l'autorise). Qui entre tout seul s'il est reconnu, c'est la liste des membres de l'événement (`padron_ver {"solos": true}`).
- Pour qu'ils entrent **depuis internet** (pinggy, Cloudflare, tunnel propre) : `help {"topic": "acceso_remoto"}`.
