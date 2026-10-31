## Pessoas
`room_status` lista todos: `state` waiting (sala de espera) ou admitted, `slot` (lugar no programa, 0 = nenhum), `lobbySlot`, `speaker` (modo oradores), mic/cam/screen/hand, conexão, info e contato.

## Sala de espera
- Privada: quem espera só vê você (o que você escolher com `operator_feed`) e nunca os demais.
- `participant_admit`, `participant_admit_all`, `participant_reject`.
- `participant_talk {"on": true}`: conversa privada (só essa pessoa ouve você; você a ouve pelo retorno).

## Dentro
- A câmera dela é a fonte `Invitado N` (N = slot). `guest_source_add` a coloca numa cena.
- `participant_request {"what": "mute-mic"|"mute-cam"|"unmute-mic"|"unmute-cam"|"stop-screen"|"lower-hand"}` (os unmute voltam a ligar o que foi desligado), `participant_kick`.
- `chat_send` (para todos ou para uma pessoa), `invitados_chat_leer`.
- Arquivos que compartilham: `share_list`, `compartida_revisar` (ver a imagem), `share_take` (para a cena), `share_delete`.

## Entrada
- `room_link {"target": "local"|"remote", "withPin"}` dá o link e o QR dele (imagem). `room_pin` muda o PIN. `room_set {"lobby", "locked"}`.
- `network_status`: HTTPS, túnel WireGuard, links.
- Que o assistente **atenda sozinho** quem chega (por chat ou **falando**: o que dizem vira texto e eles recebem resposta por voz), o cadastro automático em Pessoas e a voz do atendente: `atencion_configurar` / `atencion_estado` (também a sessão remota da assembleia: com `asambleaHablando`, fala-se com quem espera via `chat_send {"to": <id del integrante>}` e ouve-se com `chat_wait {"participant": <id>}`; um de cada vez, termina ao habilitá-lo). Quem entra sozinho se for reconhecido é a lista de membros do evento (`padron_ver {"solos": true}`).
- Para que entrem **pela internet** (pinggy, Cloudflare, túnel próprio): `help {"topic": "acceso_remoto"}`.