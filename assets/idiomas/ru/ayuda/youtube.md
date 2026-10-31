## Настроить (один раз)
1. `youtube_status`: говорит, не хватает ли учётных данных или аккаунта.
2. Учётные данные: человек создаёт OAuth-клиент типа **Приложение для ПК** в Google Cloud (на вкладке YouTube в приложении есть шаги со ссылками: проект, включить YouTube Data API v3, экран согласия, тестовый пользователь, клиент) и сохраняет его через `youtube_setup {"fromDownloads": true}` (импортирует скачанный JSON) или через защищённую форму `account_credentials_request {"platform": "youtube"}` (см. `help {"topic": "cuentas"}`).
3. `youtube_login`: открывает браузер на этой машине; ЧЕЛОВЕК выбирает аккаунт и соглашается. `youtube_status` подтверждает канал.

## Прямая трансляция
- Всё в одном: `youtube_live_create {"title": "...", "privacy": "private", "start": true}` создаёт событие, берёт сервер и ключ (только в памяти, никогда на диск) и начинает трансляцию. С автозапуском событие переходит В ЭФИР, когда YouTube получает сигнал (~10-30 с).
- В существующее событие: `youtube_live_list` -> `youtube_live_start {"id"}`.
- Проверить: `status` (streaming, fps, потерянные кадры) и `youtube_live_health {"id"}` (active / good).
- Завершить: `youtube_live_end {"id"}` (закрывает событие и останавливает приложение). Изменить/сменить статус/удалить: `youtube_live_update`.
- Новые каналы: в первый раз YouTube включает прямые трансляции до 24 ч (ошибка "liveStreamingNotEnabled").

## Видео
`youtube_videos`, `youtube_video_update` (название, описание, доступ, теги, миниатюра), `youtube_video_delete`, `youtube_video_upload` (без `path` — последняя запись приложения).

## Чат
- `youtube_chat {"broadcast": id}` выбирает событие; `{"send": "texto"}` пишет; возвращает последние сообщения.
- На экране: `chat_overlay_add {"channel": "all"|"youtube"|"twitch"|"facebook"|"room"}` добавляет в сцену источник **Chat en vivo** (тип `ba_chat_overlay`); с `all` смешивает все подключённые платформы (каждое сообщение со своей отметкой). Настраивается через `source_update` (settings: show_youtube, show_twitch, show_facebook, show_room, max_messages, font_size, width, height, bg_opacity, show_names, fade_sec). Проверить через `foto_fuente`.

## Квота
10 000 единиц в день на проект: список и редактирование стоят мало; загрузка видео ~1600; чтение чата ~5 за чтение (приложение читает раз в 6 с или реже, только при выбранном событии).
