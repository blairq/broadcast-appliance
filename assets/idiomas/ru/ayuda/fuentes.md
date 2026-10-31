## Типы
`source_types` перечисляет типы этой установки. Самые используемые:
| type | типичные settings |
|---|---|
| `image_source` | `file` (путь) |
| `ffmpeg_source` | `local_file`, `looping`, `restart_on_activate` |
| `color_source_v3` | `color` (целое ABGR: 0xAABBGGRR, например непрозрачный красный = 4278190335), `width`, `height` |
| `text_ft2_source_v2` | `text`, `font` {face, size}, `color1`, `color2`, `outline`, `drop_shadow` |
| `v4l2_input` (Linux) | камера: `device_id`, разрешение (см. source_properties) |
| `macos-avcapture` (macOS) | камера: `device` (см. source_properties) |
| `coreaudio_output_capture` (macOS) | системный звук: `device_id` |
| микрофоны (все системы) | входят через GStreamer: `audio_input_add` (не `crear_fuente`); `ba_gst_fuente` с `pipeline_audio` и `microfono`. Фильтры: `entrada_filtros_audio`; круглый стол: `automixer` |
| `screen_capture` | экран: `monitor` (его имя: варианты есть в source_properties; пусто = основной), `show_cursor` |
| `ba_qr` | QR-код в сцене: `qr` = \"aparato:publico\" (страница аудитории, с её паролем), \"aparato:sala\", \"aparato:prompter\", \"aparato:rotulos\" или id сохранённого (qr_listar); пусто = `texto`. `fondo` (белое поле), `tamano`. Обновляется сам |
| `window_capture` | окно приложения, даже если оно перекрыто: `ventana` = "aplicacion\ntitulo" (открытые есть в source_properties), `show_cursor`. Приложение запоминается: если его закрыть и открыть, захват продолжится сам. На Mac требуется разрешение на запись экрана |
| `dshow_input` (Windows) | камера |
| `ba_guest_video` | гость: `slot` (1..N), `channel` guest (камера) или share (демонстрация экрана) |
| `ba_grilla` | сетка людей: мозаика, которая сама растёт с впущенными в Конференцию и допущенными участниками Собрания. `origen` (sala/asamblea/ambas), `orden` (llegada/habla/prioridad), `tope` (с «+N»), `grupo` (несколько сеток делят людей без повторов; `reparto` en_orden/parejo). Тот, кто уже виден в другой сетке в эфире, не повторяется. См. grilla_personas_estado и grilla_personas_ajustar; вручную: grilla_personas_fijar / _sacar / _mover / _devolver |
| `slideshow` | `files`, `slide_time` |
| `ba_chat_overlay` | чат в прямом эфире (YouTube, Twitch, Facebook и/или конференция) поверх видео: `show_youtube`, `show_twitch`, `show_facebook`, `show_room` (или `channel` all/youtube/twitch/facebook/room), `max_messages`, `font_size`, `width`, `height`, `bg_opacity`, `show_names`, `fade_sec` |

Для ВСЕХ параметров источника: `source_properties {"name"}`; чтобы изменить один: `source_set_property`. `source_update {"settings": {...}}` объединяет ключи (не удаляет остальные).

## Расположение (`layout`)
Доли холста: `{"x":0.1,"y":0.1,"w":0.8,"h":0.8}` или пресеты:
`full`, `left`, `right`, `top`, `bottom`, `center`, `top-left`, `top-right`, `bottom-left`, `bottom-right`, `pip-tl`, `pip-tr`, `pip-bl`, `pip-br` (окошко в этом углу), `lower-third` (титр), `third-1..3`, `grid-<n>-<i>` (ячейка i сетки на n: `grid-4-1` .. `grid-4-4`).
Источник вписывается в прямоугольник с сохранением пропорций.

## Порядок и видимость
`source_update {"name": ..., "order": "top"|"bottom"|"up"|"down"|n}`, `{"visible": false}` (скрывает, не удаляя: идеально, чтобы подготовить и показать в нужный момент).

## Гости
`guest_source_add {"slot": 1, "kind": "camera"|"screen", "layout": "left"}` ставит в сцену гостя с места 1 (кто бы там ни был).
