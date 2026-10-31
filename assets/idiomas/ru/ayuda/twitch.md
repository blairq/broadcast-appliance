## Настроить (один раз)
1. `twitch_status`: говорит, не хватает ли приложения или аккаунта.
2. Приложение: человек включает двухэтапную проверку в Twitch и регистрирует приложение на https://dev.twitch.tv/console/apps/create (перенаправление `http://localhost`, категория **Broadcaster Suite**, тип клиента **Public**). Сохраняется через защищённую форму `account_credentials_request {"platform": "twitch"}` (или `twitch_setup {"clientId": "..."}`: ID публичного клиента не секретен).
3. `twitch_login`: возвращает **код** и открывает twitch.tv/activate; ЧЕЛОВЕК входит под аккаунтом канала и подтверждает (покажите ему код). `twitch_status` подтверждает аккаунт.

## Транслировать
- `twitch_live_start {"title": "...", "game": "Just Chatting"}`: применяет название/категорию, берёт ключ канала (только в памяти) и транслирует из приложения на `rtmp://live.twitch.tv/app`.
- Проверить: `status` (streaming, fps, потерянные кадры) и `twitch_stream_status` (live, зрители) через ~15 с.
- Остановить: `twitch_live_stop`. В Twitch нет «событий»: канал в эфире, пока получает сигнал.

## Канал и VOD
- `twitch_channel` читает; `twitch_channel {"title", "game", "tags", "language"}` меняет; `{"search": "..."}` ищет категории.
- `twitch_videos`, `twitch_video_delete {"ids": [...]}`. Эфиры сохраняются как VOD, только если на канале включено «Сохранять прошедшие трансляции».

## Чат
- `twitch_chat {"on": true}` подключается к чату канала (защищённый IRC); `{"send": "texto"}` пишет от имени канала; возвращает последние сообщения с цветом и значками.
- На экране: `chat_overlay_add {"channel": "twitch"}` (или `all`, чтобы смешать платформы).
