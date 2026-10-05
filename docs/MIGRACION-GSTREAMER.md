# Migración a GStreamer

**Dirección (2026-10-05):** todo pasa a GStreamer, de a poco, y en algún momento se saca libobs.
Regla para lo nuevo: el procesamiento va en elementos de GStreamer (`src/gst-plugins/`) en la capa de entrada
(`ba_gst_fuente`), nunca como fuente o filtro de OBS. Cuando se toca algo que vive en OBS, se migra si es
razonable.

## Ya está en GStreamer

| Qué | Dónde |
|---|---|
| Entradas de red (SRT, RTSP, RTMP, HTTP, UDP), pipelines y plugins | `ba_gst_fuente` (`src/obs-module/ba_gst_fuente.cpp`), sincronía estándar con reloj y latencia compartidos |
| Filtros de imagen del grafo (color, croma, recorte, legalizador) | plugin `bafiltros` |
| Superresolución (VideoToolbox, CoreML, ONNX, FSR, GLSL) | plugin `basuperres` |
| Filtros de audio de cada entrada (plugins de Python, audiodynamic…) | `filtros_audio` de `ba_gst_fuente` |
| **Micrófonos y líneas del equipo** (2026-10-05) | `ba_gst_fuente` + `pipeline_audio` (`audio/EntradasAudio.*`); los viejos se migran solos |
| **Automixer de mesa redonda** (2026-10-05) | `baautomixer`, plugin `baaudio` |
| **Racks de audio (LSP/CLAP) de las entradas de GStreamer** (2026-10-05) | `barack`, registrado por la app (`audio/GstRack.*`): el mismo `AudioRack` corre en la cadena de audio de `ba_gst_fuente` (ajuste `rack`). Los racks guardados se montan solos donde corresponda |

## Todavía en libobs (en orden propuesto)

1. **Racks de las fuentes que siguen en libobs** (invitados, medios, voces, NDI): todavía como filtro de OBS
   `ba_audio_rack`. Pasan solos a `barack` el día que su fuente pase a GStreamer (RackManager elige por el tipo
   de la fuente). La escucha del rack (la salida a los auriculares) sigue saliendo por el monitoreo de libobs
   hasta que se mude la mezcla (punto 5).
2. **Cámaras locales.** `macos-avcapture` / `dshow_input` / `v4l2_input` → `avfvideosrc` / `mfvideosrc` /
   `v4l2src` en `ba_gst_fuente` (mismo esquema que los micrófonos: dispositivo + migración al cargar).
   Así cámara y micrófono de la sala comparten reloj y latencia.
3. **Medios locales y playlist.** `ffmpeg_source` → `uridecodebin3` (la playlist ya maneja in/out y auto-next
   por la API de medios; hay que darle a `ba_gst_fuente` play/pausa/seek/estado).
4. **Audio del sistema** (`*_output_capture`), invitados (`ba_guest_*`), NDI (`ba_ndi_input`): entradas de
   GStreamer (appsrc desde la memoria compartida, `ndisrc`).
5. **La mezcla de audio** (mezclador de OBS, pistas, monitoreo) → `audiomixer` con buses, el limitador del
   máster (T12) y la matriz.
6. **La composición de video** (escenas, transiciones, DSK, gráficos, multiview) → `compositor`/`glvideomixer`
   con la GPU. El grafo de video (`ba_video_grafo`) ya está pensado como nodos.
7. **Salidas** (grabación, transmisión, SRT, NDI, cámara virtual) y codificadores → `encodebin2`, `srtsink`,
   `splitmuxsink`, `rtmp2sink`.

Cuando estén 5–7, libobs queda sin uso y se saca del bundle.

## Trampas conocidas

- Una entrada de GStreamer en modo estándar espera la **latencia del estudio** (la mayor de todas las
  cadenas): un micrófono sale atrasado lo mismo que las cámaras de red, para que todo quede en sincronía.
- Los micrófonos de Windows: el id de libobs (`{0.0.1.00000000}.{guid}`) no es el de `wasapi2src`;
  `audio::idDesdeLibobs` lo convierte. **Sin probar todavía en Windows.**
- Linux: el monitor de dispositivos de PulseAudio también lista «Monitor of …» (lo que suena): se filtra
  por `device.class = monitor`.
