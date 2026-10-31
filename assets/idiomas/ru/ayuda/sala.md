## Люди
`room_status` перечисляет всех: `state` waiting (зал ожидания) или admitted, `slot` (место в программе, 0 = нет), `lobbySlot`, `speaker` (режим выступающих), mic/cam/screen/hand, подключение, информация и контакт.

## Зал ожидания
- Закрытый: тот, кто ждёт, видит только вас (то, что вы выберете через `operator_feed`) и никогда — других.
- `participant_admit`, `participant_admit_all`, `participant_reject`.
- `participant_talk {"on": true}`: личный разговор (вас слышит только этот человек; вы слышите его через мониторинг).

## Внутри
- Его камера — источник `Invitado N` (N = slot). `guest_source_add` ставит его в сцену.
- `participant_request {"what": "mute-mic"|"mute-cam"|"unmute-mic"|"unmute-cam"|"stop-screen"|"lower-hand"}` (unmute снова включают то, что было выключено), `participant_kick`.
- `chat_send` (всем или одному человеку), `invitados_chat_leer`.
- Файлы, которыми делятся: `share_list`, `compartida_revisar` (посмотреть изображение), `share_take` (в сцену), `share_delete`.

## Вход
- `room_link {"target": "local"|"remote", "withPin"}` даёт ссылку и её QR (изображение). `room_pin` меняет PIN. `room_set {"lobby", "locked"}`.
- `network_status`: HTTPS, туннель WireGuard, ссылки.
- Чтобы ассистент **сам встречал** приходящих (в чате или **голосом**: сказанное переводится в текст, а отвечают им голосом), автоматическое добавление в «Люди» и голос помощника: `atencion_configurar` / `atencion_estado` (также удалённое заседание собрания: с `asambleaHablando` с ожидающим говорят через `chat_send {"to": <id del integrante>}` и слушают его через `chat_wait {"participant": <id>}`; по одному, заканчивается, когда его допускают). Кто входит сам, если его узнают, — это список участников мероприятия (`padron_ver {"solos": true}`).
- Чтобы входили **из интернета** (pinggy, Cloudflare, собственный туннель): `help {"topic": "acceso_remoto"}`.
