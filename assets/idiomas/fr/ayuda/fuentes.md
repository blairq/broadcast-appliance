## Types
`source_types` liste ceux de cette installation. Les plus utilisés :
| type | settings typiques |
|---|---|
| `image_source` | `file` (chemin) |
| `ffmpeg_source` | `local_file`, `looping`, `restart_on_activate` |
| `color_source_v3` | `color` (entier ABGR : 0xAABBGGRR, par ex. rouge opaque = 4278190335), `width`, `height` |
| `text_ft2_source_v2` | `text`, `font` {face, size}, `color1`, `color2`, `outline`, `drop_shadow` |
| `v4l2_input` (Linux) | caméra : `device_id`, résolution (voir source_properties) |
| `macos-avcapture` (macOS) | caméra : `device` (voir source_properties) |
| `coreaudio_output_capture` (macOS) | audio du système : `device_id` |
| micros (tous les systèmes) | entrent par GStreamer : `audio_input_add` (pas `crear_fuente`) ; `ba_gst_fuente` avec `pipeline_audio` et `microfono`. Filtres : `entrada_filtros_audio` ; table ronde : `automixer` |
| `screen_capture` | un écran : `monitor` (son nom : les options figurent dans source_properties ; vide = le principal), `show_cursor` |
| `ba_qr` | un code QR dans la scène : `qr` = \"aparato:publico\" (la page du public, avec sa clé), \"aparato:sala\", \"aparato:prompter\", \"aparato:rotulos\" ou l'id d'un QR enregistré (qr_listar) ; vide = `texto`. `fondo` (marge blanche), `tamano`. Se tient à jour tout seul |
| `window_capture` | la fenêtre d'une application, même cachée : `ventana` = "aplicacion\ntitulo" (celles qui sont ouvertes figurent dans source_properties), `show_cursor`. L'application est mémorisée : si on la ferme et qu'on la rouvre, la capture continue toute seule. Sur Mac, demande l'autorisation d'enregistrement de l'écran |
| `dshow_input` (Windows) | caméra |
| `ba_guest_video` | invité : `slot` (1..N), `channel` guest (caméra) ou share (écran partagé) |
| `ba_grilla` | grille de personnes : une mosaïque qui grandit toute seule avec les personnes admises dans la Conférence et les membres autorisés de l'Assemblée. `origen` (sala/asamblea/ambas), `orden` (llegada/habla/prioridad), `tope` (avec le « +N »), `grupo` (plusieurs grilles se répartissent les personnes sans répétition ; `reparto` en_orden/parejo). Une personne déjà visible dans une autre grille à l'antenne n'est pas répétée. Voir grilla_personas_estado et grilla_personas_ajustar ; à la main : grilla_personas_fijar / _sacar / _mover / _devolver |
| `slideshow` | `files`, `slide_time` |
| `ba_chat_overlay` | chat en direct (YouTube, Twitch, Facebook et/ou conférence) par-dessus la vidéo : `show_youtube`, `show_twitch`, `show_facebook`, `show_room` (ou `channel` all/youtube/twitch/facebook/room), `max_messages`, `font_size`, `width`, `height`, `bg_opacity`, `show_names`, `fade_sec` |

Pour TOUTES les options d'une source : `source_properties {"name"}` ; pour en changer une : `source_set_property`. `source_update {"settings": {...}}` fusionne les clés (n'efface pas les autres).

## Position (`layout`)
Fractions du canevas : `{"x":0.1,"y":0.1,"w":0.8,"h":0.8}` ou presets :
`full`, `left`, `right`, `top`, `bottom`, `center`, `top-left`, `top-right`, `bottom-left`, `bottom-right`, `pip-tl`, `pip-tr`, `pip-bl`, `pip-br` (petite fenêtre dans ce coin), `lower-third` (synthé), `third-1..3`, `grid-<n>-<i>` (case i d'une grille pour n : `grid-4-1` .. `grid-4-4`).
La source s'ajuste dans le rectangle en conservant ses proportions.

## Ordre et visibilité
`source_update {"name": ..., "order": "top"|"bottom"|"up"|"down"|n}`, `{"visible": false}` (masque sans supprimer : idéal pour préparer et montrer au bon moment).

## Invités
`guest_source_add {"slot": 1, "kind": "camera"|"screen", "layout": "left"}` place dans la scène l'invité de la place 1 (quel qu'il soit).
