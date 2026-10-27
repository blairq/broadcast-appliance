## Basics
- `scene_list`: scenes and their sources (bottom to top). `live` marks the one on air.
- `scene_switch {"name"}`: on air (with the transition from `transition_set`: cut, fade, swipe, slide, fade_to_color).
- `scene_create` / `scene_rename` / `scene_delete`.

## Define a scene in one go: `scene_define`
```json
{"name": "Entrevista", "replace": true, "switch": false, "sources": [
  {"type": "color_source_v3", "name": "Fondo", "settings": {"color": 4280295456, "width": 1920, "height": 1080}, "layout": "full"},
  {"type": "ba_guest_video", "name": "Invitado 1", "settings": {"slot": 1}, "layout": "left"},
  {"type": "ba_guest_video", "name": "Invitado 2", "settings": {"slot": 2}, "layout": "right"},
  {"type": "text_ft2_source_v2", "name": "Zocalo", "settings": {"text": "Entrevista en vivo"}, "layout": "lower-third"}
]}
```
- The order is **bottom to top** (the last one covers the previous ones).
- A `name` that already exists **reuses** that source (same camera in several scenes).
- `scene_export` returns this same format: export, modify, and define it again.

## Good practices
- Build in a scene that is NOT on air and check it with `foto_fuente {"name": "<escena>"}` before `scene_switch`.
- `foto_todas_escenas` to see all of them at a glance.