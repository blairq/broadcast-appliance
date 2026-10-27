## Types
`source_types` lists the ones in this installation. The most used:
| type | typical settings |
|---|---|
| `image_source` | `file` (path) |
| `ffmpeg_source` | `local_file`, `looping`, `restart_on_activate` |
| `color_source_v3` | `color` (ABGR integer: 0xAABBGGRR, e.g. opaque red = 4278190335), `width`, `height` |
| `text_ft2_source_v2` | `text`, `font` {face, size}, `color1`, `color2`, `outline`, `drop_shadow` |
| `v4l2_input` (Linux) | camera: `device_id`, resolution (see source_properties) |
| `macos-avcapture` (macOS) | camera: `device` (see source_properties) |
| `coreaudio_output_capture` (macOS) | system audio: `device_id` |
| microphones (all systems) | they come in through GStreamer: `audio_input_add` (not `crear_fuente`); `ba_gst_fuente` with `pipeline_audio` and `microfono`. Filters: `entrada_filtros_audio`; round table: `automixer` |
| `screen_capture` | a screen: `monitor` (its name: the options come from source_properties; empty = the main one), `show_cursor` |
| `ba_qr` | a QR code in the scene: `qr` = \"aparato:publico\" (the audience page, with its key), \"aparato:sala\", \"aparato:prompter\", \"aparato:rotulos\" or the id of a saved one (qr_listar); empty = `texto`. `fondo` (white margin), `tamano`. It keeps itself up to date |
| `window_capture` | an application's window, even when covered: `ventana` = "application\ntitle" (the open ones come from source_properties), `show_cursor`. The application is remembered: if it closes and opens again, it follows on its own. On the Mac it asks for screen recording permission |
| `dshow_input` (Windows) | camera |
| `ba_guest_video` | guest: `slot` (1..N), `channel` guest (camera) or share (shared screen) |
| `ba_grilla` | people grid: a mosaic that grows on its own with the admitted participants of the Conference and the enabled members of the Assembly. `origen` (sala/asamblea/ambas), `orden` (llegada/habla/prioridad), `tope` (with the “+N”), `grupo` (several grids share people without repeating; `reparto` en_orden/parejo). Someone already shown in another grid on air is not repeated. See grilla_personas_estado and grilla_personas_ajustar; by hand: grilla_personas_fijar / _sacar / _mover / _devolver |
| `slideshow` | `files`, `slide_time` |
| `ba_chat_overlay` | live chat (YouTube, Twitch, Facebook and/or conference) over the video: `show_youtube`, `show_twitch`, `show_facebook`, `show_room` (or `channel` all/youtube/twitch/facebook/room), `max_messages`, `font_size`, `width`, `height`, `bg_opacity`, `show_names`, `fade_sec` |

For ALL the options of a source: `source_properties {"name"}`; to change one: `source_set_property`. `source_update {"settings": {...}}` merges keys (it doesn't delete the others).

## Position (`layout`)
Fractions of the canvas: `{"x":0.1,"y":0.1,"w":0.8,"h":0.8}` or presets:
`full`, `left`, `right`, `top`, `bottom`, `center`, `top-left`, `top-right`, `bottom-left`, `bottom-right`, `pip-tl`, `pip-tr`, `pip-bl`, `pip-br` (small window in that corner), `lower-third` (lower third), `third-1..3`, `grid-<n>-<i>` (cell i of a grid for n: `grid-4-1` .. `grid-4-4`).
The source fits inside the rectangle keeping its aspect ratio.

## Order and visibility
`source_update {"name": ..., "order": "top"|"bottom"|"up"|"down"|n}`, `{"visible": false}` (hides without deleting: ideal for preparing and showing at the right moment).

## Guests
`guest_source_add {"slot": 1, "kind": "camera"|"screen", "layout": "left"}` puts the guest in slot 1 (whoever it is) in the scene.
