## Básico
- `scene_list`: cenas e suas fontes (de baixo para cima). `live` marca a que está no ar.
- `scene_switch {"name"}`: no ar (com a transição de `transition_set`: cut, fade, swipe, slide, fade_to_color).
- `scene_create` / `scene_rename` / `scene_delete`.

## Definir uma cena de uma vez: `scene_define`
```json
{"name": "Entrevista", "replace": true, "switch": false, "sources": [
  {"type": "color_source_v3", "name": "Fondo", "settings": {"color": 4280295456, "width": 1920, "height": 1080}, "layout": "full"},
  {"type": "ba_guest_video", "name": "Invitado 1", "settings": {"slot": 1}, "layout": "left"},
  {"type": "ba_guest_video", "name": "Invitado 2", "settings": {"slot": 2}, "layout": "right"},
  {"type": "text_ft2_source_v2", "name": "Zocalo", "settings": {"text": "Entrevista en vivo"}, "layout": "lower-third"}
]}
```
- A ordem é de **baixo para cima** (a última cobre as anteriores).
- Um `name` que já existe **reutiliza** essa fonte (a mesma câmera em várias cenas).
- `scene_export` devolve este mesmo formato: exporte, modifique e defina de novo.

## Boas práticas
- Monte numa cena que NÃO esteja no ar e revise-a com `foto_fuente {"name": "<escena>"}` antes do `scene_switch`.
- `foto_todas_escenas` para ver todas de uma vez.