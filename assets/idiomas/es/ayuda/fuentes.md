## Tipos
`source_types` lista los de esta instalacion. Los mas usados:
| type | settings tipicos |
|---|---|
| `image_source` | `file` (ruta) |
| `ffmpeg_source` | `local_file`, `looping`, `restart_on_activate` |
| `color_source_v3` | `color` (entero ABGR: 0xAABBGGRR, p. ej. rojo opaco = 4278190335), `width`, `height` |
| `text_ft2_source_v2` | `text`, `font` {face, size}, `color1`, `color2`, `outline`, `drop_shadow` |
| `v4l2_input` (Linux) | camara: `device_id`, resolucion (ver source_properties) |
| `macos-avcapture` (macOS) | camara: `device` (ver source_properties) |
| `coreaudio_output_capture` (macOS) | audio del sistema: `device_id` |
| microfonos (todos los sistemas) | entran por GStreamer: `audio_input_add` (no `crear_fuente`); `ba_gst_fuente` con `pipeline_audio` y `microfono`. Filtros: `entrada_filtros_audio`; mesa redonda: `automixer` |
| `screen_capture` | una pantalla: `monitor` (su nombre: las opciones salen en source_properties; vacio = la principal), `show_cursor` |
| `ba_qr` | un codigo QR en la escena: `qr` = \"aparato:publico\" (la pagina del publico, con su clave), \"aparato:sala\", \"aparato:prompter\", \"aparato:rotulos\" o el id de uno guardado (qr_listar); vacio = `texto`. `fondo` (margen blanco), `tamano`. Se mantiene al dia solo |
| `window_capture` | la ventana de una aplicacion, aunque este tapada: `ventana` = "aplicacion\ntitulo" (las abiertas salen en source_properties), `show_cursor`. Se recuerda la aplicacion: si se cierra y se abre, sigue sola. En la Mac pide permiso de grabacion de pantalla |
| `dshow_input` (Windows) | camara |
| `ba_guest_video` | invitado: `slot` (1..N), `channel` guest (camara) o share (pantalla compartida) |
| `ba_grilla` | grilla de personas: un mosaico que crece solo con los admitidos de la Conferencia y los integrantes habilitados de la Asamblea. `origen` (sala/asamblea/ambas), `orden` (llegada/habla/prioridad), `tope` (con el «+N»), `grupo` (varias grillas se reparten la gente sin repetir; `reparto` en_orden/parejo). Quien ya se ve en otra grilla al aire no se repite. Ver grilla_personas_estado y grilla_personas_ajustar; a mano: grilla_personas_fijar / _sacar / _mover / _devolver |
| `slideshow` | `files`, `slide_time` |
| `ba_chat_overlay` | chat en vivo (YouTube, Twitch, Facebook y/o conferencia) sobre el video: `show_youtube`, `show_twitch`, `show_facebook`, `show_room` (o `channel` all/youtube/twitch/facebook/room), `max_messages`, `font_size`, `width`, `height`, `bg_opacity`, `show_names`, `fade_sec` |

Para TODAS las opciones de una fuente: `source_properties {"name"}`; para cambiar una: `source_set_property`. `source_update {"settings": {...}}` mezcla claves (no borra las demas).

## Ubicacion (`layout`)
Fracciones del lienzo: `{"x":0.1,"y":0.1,"w":0.8,"h":0.8}` o presets:
`full`, `left`, `right`, `top`, `bottom`, `center`, `top-left`, `top-right`, `bottom-left`, `bottom-right`, `pip-tl`, `pip-tr`, `pip-bl`, `pip-br` (ventanita en esa esquina), `lower-third` (zocalo), `third-1..3`, `grid-<n>-<i>` (celda i de una grilla para n: `grid-4-1` .. `grid-4-4`).
La fuente se ajusta dentro del rectangulo conservando su proporcion.

## Orden y visibilidad
`source_update {"name": ..., "order": "top"|"bottom"|"up"|"down"|n}`, `{"visible": false}` (oculta sin borrar: ideal para preparar y mostrar en el momento justo).

## Invitados
`guest_source_add {"slot": 1, "kind": "camera"|"screen", "layout": "left"}` pone al invitado del lugar 1 (quien sea que este) en la escena.