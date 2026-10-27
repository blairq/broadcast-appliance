## Basico
- `scene_list`: escenas y sus fuentes (de abajo hacia arriba). `live` marca la del aire.
- `scene_switch {"name"}`: al aire (con la transicion de `transition_set`: cut, fade, swipe, slide, fade_to_color).
- `scene_create` / `scene_rename` / `scene_delete`.

## Definir una escena de una vez: `scene_define`
```json
{"name": "Entrevista", "replace": true, "switch": false, "sources": [
  {"type": "color_source_v3", "name": "Fondo", "settings": {"color": 4280295456, "width": 1920, "height": 1080}, "layout": "full"},
  {"type": "ba_guest_video", "name": "Invitado 1", "settings": {"slot": 1}, "layout": "left"},
  {"type": "ba_guest_video", "name": "Invitado 2", "settings": {"slot": 2}, "layout": "right"},
  {"type": "text_ft2_source_v2", "name": "Zocalo", "settings": {"text": "Entrevista en vivo"}, "layout": "lower-third"}
]}
```
- El orden es de **abajo hacia arriba** (el ultimo tapa a los anteriores).
- Un `name` que ya existe **reusa** esa fuente (misma camara en varias escenas).
- `scene_export` devuelve este mismo formato: exporta, modifica, y volve a definir.

## Buenas practicas
- Arma en una escena que NO este al aire y revisala con `foto_fuente {"name": "<escena>"}` antes de `scene_switch`.
- `foto_todas_escenas` para ver todas de un vistazo.