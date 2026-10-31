## Основы
- `scene_list`: сцены и их источники (снизу вверх). `live` отмечает ту, что в эфире.
- `scene_switch {"name"}`: в эфир (с переходом из `transition_set`: cut, fade, swipe, slide, fade_to_color).
- `scene_create` / `scene_rename` / `scene_delete`.

## Определить сцену за один раз: `scene_define`
```json
{"name": "Entrevista", "replace": true, "switch": false, "sources": [
  {"type": "color_source_v3", "name": "Fondo", "settings": {"color": 4280295456, "width": 1920, "height": 1080}, "layout": "full"},
  {"type": "ba_guest_video", "name": "Invitado 1", "settings": {"slot": 1}, "layout": "left"},
  {"type": "ba_guest_video", "name": "Invitado 2", "settings": {"slot": 2}, "layout": "right"},
  {"type": "text_ft2_source_v2", "name": "Zocalo", "settings": {"text": "Entrevista en vivo"}, "layout": "lower-third"}
]}
```
- Порядок — **снизу вверх** (последний перекрывает предыдущие).
- `name`, который уже существует, **переиспользует** этот источник (одна и та же камера в нескольких сценах).
- `scene_export` возвращает этот же формат: экспортируйте, измените и определите заново.

## Хорошие практики
- Собирайте в сцене, которая НЕ в эфире, и проверяйте её через `foto_fuente {"name": "<escena>"}` перед `scene_switch`.
- `foto_todas_escenas` — чтобы увидеть все сразу.
