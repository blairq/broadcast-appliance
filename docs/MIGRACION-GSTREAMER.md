# Migración a GStreamer

**Dirección (2026-10-05):** todo pasa a GStreamer, de a poco, y en algún momento se saca libobs.
Regla para lo nuevo: el procesamiento va en elementos de GStreamer (`src/gst-plugins/`) en la capa de entrada
(`ba_gst_fuente`), nunca como fuente o filtro de OBS. Cuando se toca algo que vive en OBS, se migra si es
razonable.

Cómo cablear los pipelines (reglas, el motor de destino sin libobs, subtítulos y el plan por etapas):
[obs-gstreamer-migration.md](obs-gstreamer-migration.md).

## Ya está en GStreamer

| Qué | Dónde |
|---|---|
| Entradas de red (SRT, RTSP, RTMP, HTTP, UDP), pipelines y plugins | `ba_gst_fuente` (`src/obs-module/ba_gst_fuente.cpp`), sincronía estándar con reloj y latencia compartidos |
| Filtros de imagen del grafo (color, croma, recorte, legalizador) | plugin `bafiltros` |
| Superresolución (VideoToolbox, CoreML, ONNX, FSR, GLSL) | plugin `basuperres` |
| Filtros de audio de cada entrada (plugins de Python, audiodynamic…) | `filtros_audio` de `ba_gst_fuente` |
| **Micrófonos y líneas del equipo** (2026-10-05) | `ba_gst_fuente` + `pipeline_audio` (`audio/EntradasAudio.*`); los viejos se migran solos |
| **Automixer de mesa redonda** (2026-10-05) | `baautomixer`, plugin `baaudio` |
| **Audio de los invitados y del remoto con la palabra** (2026-10-05) | `ba_gst_fuente` + `baanillo canal=BA.Audio.Guest.<n>` (plugin `baipc`): lee el anillo que escribe el gateway. Para las plantillas y la interfaz siguen siendo «ba_guest_audio» (`inputs::tipoLogico`) |
| **Cámaras y capturadoras del equipo** (2026-10-05) | `ba_gst_fuente` con `camara` = id estable (avfvideosrc / mfvideosrc / v4l2src, resuelto al armar), `camara_modo` opcional (sin modo: el que mejor va con el lienzo). Lista y modos del monitor de dispositivos (`video/Camaras.*`); las guardadas se migran solas. Sus tarjetas del grafo corren en su cadena |
| **Video de los invitados, sus pantallas y el remoto con la palabra** (2026-10-05) | `ba_gst_fuente` + `bavideoanillo canal=BA.Guest.<n>` (plugin `baipc`): lee el canal que escribe el gateway, BGRA sin normalizar. Audio y video del invitado van por la hora que les pone el gateway (`qpc` pasado al reloj del pipeline), fuera de la compensación del estudio. Aparecen en el grafo de video (grupo Invitados); para las plantillas siguen siendo «ba_guest_video» |
| **Medios locales y playlist** (2026-10-05) | `ba_gst_medio` (mismo código que `ba_gst_fuente`): un archivo con los ajustes de ffmpeg_source y los controles de medios de libobs (play/pausa, reiniciar, parar, ir a, tiempo, duración, estado). Mira qué tiene el archivo (GstDiscoverer) y arma solo esas ramas; `looping` sin cortes con saltos por segmentos. Cualquier ffmpeg_source de un archivo local se crea como `ba_gst_medio` (motor y carga de la configuración); sus racks corren en barack |
| **Racks de audio (LSP/CLAP) de las entradas de GStreamer** (2026-10-05) | `barack`, registrado por la app (`audio/GstRack.*`): el mismo `AudioRack` corre en la cadena de audio de `ba_gst_fuente` (ajuste `rack`). Los racks guardados se montan solos donde corresponda |

## Todavía en libobs (en orden propuesto)

1. **Racks de las fuentes que siguen en libobs** (voces, NDI): todavía como filtro de OBS
   `ba_audio_rack`. Pasan solos a `barack` el día que su fuente pase a GStreamer (RackManager elige por el tipo
   de la fuente). La escucha del rack (la salida a los auriculares) sigue saliendo por el monitoreo de libobs
   hasta que se mude la mezcla (punto 5).
4. **Audio del sistema** (`*_output_capture`), la escucha del lobby (`ba_guest_audio` privada, solo
   auriculares) y NDI (`ba_ndi_input`): entradas de GStreamer (`ndisrc`).
5. **La mezcla de audio** (mezclador de OBS, pistas, monitoreo) → `audiomixer` con buses, el limitador del
   máster (T12) y la matriz.
6. **La composición de video** (escenas, transiciones, DSK, gráficos, multiview) → `compositor`/`glvideomixer`
   con la GPU. El grafo de video (`ba_video_grafo`) ya está pensado como nodos.
7. **Salidas** (grabación, transmisión, SRT, NDI, cámara virtual) y codificadores → `encodebin2`, `srtsink`,
   `splitmuxsink`, `rtmp2sink`.

Cuando estén 5–7, libobs queda sin uso y se saca del bundle.

## Trampas conocidas

- `uridecodebin3` con un `ba_dec.` suelto puede enganchar el audio de un archivo solo de audio en la rama del
  video (y no termina nunca); una rama sin datos tampoco deja llegar el EOS. Por eso los medios miran antes qué
  tiene el archivo y nombran los pads (`video_0`, `audio_0`). Las entradas de red siguen como estaban.
- Eventos de GStreamer, no parches:
  - Con los sinks en `async=false` (las entradas en vivo, para que una rama sin datos no trabe el arranque)
    un salto con flush en PAUSA no vuelve a arrancar: el pipeline no recibe ASYNC_DONE y no recalcula el
    base_time. Los medios usan `async=true` (preroll: solo arman las ramas que tienen), así los saltos en
    cualquier estado, el reinicio y el bucle andan como manda GStreamer (RESET_TIME → ASYNC_DONE).
  - Un archivo no lleva latencia forzada ni la adaptativa de las entradas en vivo (mide «lo viejo» que
    llega cada cuadro, que tras un salto no tiene sentido: arrancaba ~1,5 s tarde).
  - El bus se lee entero cada cuadro (antes uno por cuadro con `gst_bus_pop_filtered`, que además tira
    los demás): LATENCY → `gst_bin_recalculate_latency`, CLOCK_LOST → pausa y play, avisos al registro.
  - La hora de lo que llega de otro proceso: la que le puso el que lo escribió (`qpc`), pasada al reloj
    del pipeline leyendo los dos relojes juntos; no la de cuando lo leímos.

- Una entrada solo de audio tiene que llevar `"pipeline": ""`: si no, `ba_gst_fuente` arma su video de
  prueba de fábrica (videotestsrc), gasta CPU y, como «recibe video», se suma a la compensación del estudio
  y el audio sale ~¾ s tarde. Los micrófonos guardados sin eso se arreglan solos al cargar.
- Todas las entradas de GStreamer se alinean con la más lenta del estudio (compensación única, T21): con
  superresolución o filtros pesados en una cámara de red, eso puede ser casi 1 s para todo. Una entrada solo
  de audio (un micrófono) también se alinea (cuenta sus bloques de audio en vez de cuadros), así queda con su
  cámara. `estudio: false` la deja con su propia demora: lo usa el audio de los invitados, porque su video
  todavía va por libobs sin esperar.
- Windows: libobs daba la cámara como «Nombre:ruta»; `video::idCamaraDesdeLibobs` deja la ruta para
  mfvideosrc. **Sin probar todavía en Windows ni Linux.**

- Una entrada de GStreamer en modo estándar espera la **latencia del estudio** (la mayor de todas las
  cadenas): un micrófono sale atrasado lo mismo que las cámaras de red, para que todo quede en sincronía.
- Los micrófonos de Windows: el id de libobs (`{0.0.1.00000000}.{guid}`) no es el de `wasapi2src`;
  `audio::idDesdeLibobs` lo convierte. **Sin probar todavía en Windows.**
- Linux: el monitor de dispositivos de PulseAudio también lista «Monitor of …» (lo que suena): se filtra
  por `device.class = monitor`.
