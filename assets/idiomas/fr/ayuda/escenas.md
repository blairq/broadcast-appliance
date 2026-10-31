## Les bases
- `scene_list` : scènes et leurs sources (de bas en haut). `live` marque celle à l'antenne.
- `scene_switch {"name"}` : à l'antenne (avec la transition de `transition_set` : cut, fade, swipe, slide, fade_to_color).
- `scene_create` / `scene_rename` / `scene_delete`.

## Définir une scène d'un coup : `scene_define`
```json
{"name": "Entrevista", "replace": true, "switch": false, "sources": [
  {"type": "color_source_v3", "name": "Fondo", "settings": {"color": 4280295456, "width": 1920, "height": 1080}, "layout": "full"},
  {"type": "ba_guest_video", "name": "Invitado 1", "settings": {"slot": 1}, "layout": "left"},
  {"type": "ba_guest_video", "name": "Invitado 2", "settings": {"slot": 2}, "layout": "right"},
  {"type": "text_ft2_source_v2", "name": "Zocalo", "settings": {"text": "Entrevista en vivo"}, "layout": "lower-third"}
]}
```
- L'ordre va **de bas en haut** (la dernière couvre les précédentes).
- Un `name` qui existe déjà **réutilise** cette source (même caméra dans plusieurs scènes).
- `scene_export` renvoie ce même format : exportez, modifiez, et redéfinissez.

## Bonnes pratiques
- Montez dans une scène qui N'est PAS à l'antenne et vérifiez-la avec `foto_fuente {"name": "<escena>"}` avant `scene_switch`.
- `foto_todas_escenas` pour les voir toutes d'un coup d'œil.
