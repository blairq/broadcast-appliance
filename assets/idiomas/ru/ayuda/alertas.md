## Зачем
Вместо того чтобы время от времени опрашивать (mixer_list, remotos_fila_ver...), попросите сервер следить: он проверяет каждые 250 мс и, когда что-то происходит, **сообщает в чат** (вкладка «Ассистент») и/или на экран, а также может **действовать сам** (действия = инструменты MCP).

## Создать
`alert_create {"type": ..., "params": {...}, "message": "...", "notify": ["llm","ui"], "actions": [{"tool": ..., "arguments": {...}}], "once": false, "cooldownSec": N}`. Типы и параметры: `alert_types`.

| Тип | Следит за |
|---|---|
| `audio_peak` | канал (или все, `source: "*"`) превышает порог (`thresholdDb`, -1) |
| `audio_silence` | тишина в канале дольше `forSec` (только в эфире/при записи) |
| `media_end` | заканчивается видео/трек (`beforeSec`, чтобы предупредить заранее) |
| `speaker_time` | у выступающего осталось `remainingSec` (0 = время вышло) |
| `lobby` / `hand` / `guest_drop` | кто-то ждёт в зале ожидания / поднимает руку / отключается |
| `chat` | приходит сообщение (платформа, `contains` или `regex`) |
| `guest_message` | гость пишет в чат конференции (`privateOnly`) |
| `stream_health` | трансляция теряет кадры или обрывается |
| `disk` | при записи заканчивается место на диске |
| `timer` | через `in` секунд, в `at` "HH:MM" или каждые `every` секунд |

## Примеры
- Не перегружать: `{"type":"audio_peak","params":{"source":"*","thresholdDb":-1}}` и, получив уведомление, убавить на 3 дБ через `audio_set`.
- Музыка заканчивается, и возвращаемся на камеру: `{"type":"media_end","params":{"source":"Musica"},"actions":[{"tool":"scene_switch","arguments":{"scene":"Principal"}}]}`.
- Предупредить выступающего: `{"type":"speaker_time","params":{"remainingSec":30}}` и `chat_send` выступающему.
- Напоминание: `{"type":"timer","name":"Cierre","params":{"at":"21:55"}}`.

## Управление
`alert_list`, `alert_update {"id","enabled":false}`, `alert_delete`, `alert_events` (история), `alert_wait` (для клиентов, которые не получают уведомлений в чат: возвращается, как только появляется одно). Триггеры сохраняются (Config/alertas.json).
