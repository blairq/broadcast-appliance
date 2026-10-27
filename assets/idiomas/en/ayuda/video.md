A single graph for all the studio's video, like a GStreamer pipeline. Each input (camera, network, virtual) is a sub-graph: `<entrada>.entrada` -> cards -> `<entrada>.escena` (what the scenes see). Image cards run in each input's GStreamer chain; the ones the engine doesn't run yet (output, take, compositor) say so in their status. All inputs march to the studio's video clock (`status`: canvas and fps).

## How to work (always in this order)
1. **Look**: `video_estado` (all of them, with their diagnosis) and `video_estado {"entrada": ...}` before touching anything: size, fps, format, color (BT.601/709, range), whether it arrives interlaced, clock, network buffer. `video_grafo` shows the cards.
2. **Build** with `video_pipeline` (ONE call per input; it validates everything and changes nothing if something fails). The individual tools (`video_tarjeta_agregar`, `_ajustar`, `_quitar`, `video_cable`) are for touch-ups.
3. **Verify**: `video_estado {"entrada": ...}` again (the `diagnostico` must end up with no errors) and `foto_fuente {"name": ...}` to see the image.

## Recipes
**The same color correction on several cameras** (change it once and it changes on all of them), in ONE call:
`video_pipeline {"entradas": ["Sony", "Pelco", "Panasonic"], "tarjetas": [{"tipo": "gst", "elemento": "videobalance", "propiedades": {"contrast": 1.1, "saturation": 0.9}, "compartida": "Camera color"}], "local_por_entrada": {"Pelco": {"propiedades": {"hue": 0.04}}}}`. The controller becomes `estudio.camera-color`. `modo: "agregar"` if the input already has cards that must be kept (by default it replaces them). Afterwards, adjust the shared one: `video_tarjeta_ajustar` on any of the managed cards (it goes to the controller) or on the controller. `video_controlador` to make one from a card that already exists.

**Correcting a camera** (ALWAYS GStreamer first): a `gst` card with `videobalance` (brightness, contrast, saturation, hue) and `gamma`; a color cast (green, magenta): a small `hue` (±0.02 to ±0.08). Only if GStreamer isn't enough: the built-in `color` card.

**Interlaced / wrongly declared** (`video_estado` says so): `{"tipo": "normalizar", "params": {"desentrelazar": "yadif"}}` (deinterlaces at the source, where the fields are); shifted colors in HD declared as 601: `{"matriz_declarada": "601", "matriz_real": "709"}`. Frame rates different from the canvas are converted by the input on its own (it repeats or drops frames): no card needed.

**Mosaic / picture in picture / side by side**: `video_mosaico {"escena", "entradas": [...], "disposicion": "mosaico|pip|lado", "nombre"}` in ONE call (it takes each input with its cards and hides the loose ones in that scene: the mosaic still receives them). To touch up: `video_tarjeta_ajustar` on the compositor with `capas.in_N` {x, y, ancho, alto 0..1, opacidad, orden}.

**Joining cameras: panorama or 360** (all on the graphics card): `video_panoramica {"camaras": [...], "preset": "fila"|"360", "campo", "escena", "nombre"}` in ONE call. It joins up to 8 cameras (one stream each; the first is lens 1) side by side on its canvas and corrects each one's distortion and how they fit together. `preset: "fila"`: regular cameras of `campo` degrees spread out with a 10% overlap; the output is the **strip** (cylindrical panorama) at the size of the sum of the resolutions (4 at 1080p = 7680x1080; maximum 16384 wide). `preset: "360"`: two 190° fisheyes back to back; the output is the **view**, a regular camera that is a virtual PTZ (`yaw`, `pitch`, `fov` = pan, tilt, zoom). A 360 camera that sends everything in ONE stream (equirectangular or the two circles together): a `vista360` card in its chain (`modo`).
Calibrate by hand with `video_tarjeta_ajustar` on the card (`estudio.panoramica`), watching with `foto_fuente`:
1. `ver: "entrada"`: the raw image with the fisheye circles and each camera's limits; move `lenteN_x`, `lenteN_y`, `lenteN_radio` until the circles fit.
2. `ver: "tira"` (or `"panorama"`) with `guia: "esfera"` (parallels and meridians every 15°): how each one fits (`lenteN_yaw`, `_pitch`, `_roll`) and its `_campo`, until the horizon is straight and the seams don't show (`mezcla`: the width of the blend, in degrees).
3. A `rectangulo` or `circulo` guide (`guia_x`, `guia_y`, `guia_ancho`, `guia_alto` in fractions of the output) over something that is one (a door, a clock): the distortion (`lenteN_k1`, `_k2`; barrel < 0 < pincushion) until it matches. `lenteN_tipo`: `ojo` (fisheye) or `normal`.
The parameters, one by one: `video_tarjeta_ayuda {"tipo": "panoramica"}`. In the interface: Video page, the card, with shortcuts for each step.

**Network streams** (SRT, RTMP, RTSP): `video_entrada_red {"nombre", "uri", "escena", "latencia_ms"}` (never `network_input_add` for this: that one isn't synchronized). Each frame goes out at its capture time + latency (its own buffer). More latency = more tolerance to the network and more delay.

**Synchronizing inputs**: `video_sincronia {"a": ..., "b": ..., "aplicar": true}` measures how far one lags behind the other by comparing what's seen (it needs something in common that changes: the same image, movement, a clap) and corrects it; measuring again should give ~0. By hand: `video_desfase {"entrada", "ms"}`.
- RTSP carries the sender's capture time (RTCP/NTP): with the same clock it aligns by itself.
- SRT and RTMP don't carry it: measure with `video_sincronia`.
- Local cameras can only be delayed; to align with a network one, delay the local one or advance the network one (its offset accepts negative values).

**Clocks**: `video_reloj` with no arguments reports the studio clock and how it stands on each network input. `{"tipo": "ntp", "servidor": ...}` (all against the same NTP), `{"tipo": "ptp", "dominio": 0}` (IEEE 1588), `{"tipo": "red", "servidor": ip, "puerto": 8554}` (ANOTHER device's clock), `{"publicar_puerto": 8554}` (publish this one's clock so others can use it), `{"latencia_ms": ...}` (the buffer of all the network inputs).

**GStreamer mix** (any multi-input module: `compositor`, `glvideomixer`, `d3d12compositor`, `glmosaic`, `smpte`, `shapewipe`, `glstereomix`; `video_modulos {"soloMezclas": true}` lists the ones on this system): `video_tarjeta_agregar {"tipo": "mezcla", "elemento": "smpte", "params": {"propiedades": {"type": "bar-wipe-lr"}, "nombre": "Wipe"}}`. Its ports are the module's (`sink_0`, `sink_1`… on request, always one free; `sink1`/`sink2` in smpte; `video_sink`/`mask_sink` in shapewipe): `video_cable` from each input to its port. Each input's properties (xpos, ypos, width, height, alpha, zorder…): `video_filtros {"id", "puerto"}` lists them and `video_tarjeta_ajustar {"id", "params": {"pads": {"sink_1": {"xpos": 960}}}}` changes them. It comes out as a source with `nombre` (`video_salida_a_escena`). It runs on the graphics card if the module can (D3D12 on Windows, GL on Mac/Linux).

**GStreamer modules**: `video_modulos` (a documented catalog; `buscar`, `categoria`, `elemento` with its properties). Only the `tarjeta: true` ones go on an input; the `mezcla: true` ones, as a GStreamer mix. A module in 4:2:0 or with latency is flagged by the diagnosis. Properties are passed with their GStreamer name (enums by their nick).

**"Is everything OK?" (check everything)**: `video_estado` with no arguments (all inputs with their diagnosis) -> apply the fixes with severity `aviso` or `error` (each one comes with the exact call; the `info` ones are just to report) -> if offsets or clocks were touched, `video_sincronia` without applying between the ones that must match (it should give ~0) -> `status` (fps, dropped frames, CPU) -> `mixer_list` (what audio they carry: inputs hidden in a mosaic keep sounding in that scene if they carry audio). A “arrived late” with “just connected” is the startup burst: don't raise the buffer, look again in 10 s.

**Seeing the image**: `imagen_preguntar` on a scene or source, with a concrete question (“is anything stretched or black?”, “what time do the clocks in each box show?”). It's good for framing, black frames, stretching and reading text; NOT for comparing fine brightness or color between boxes: that's what the numbers are for (`video_grafo`, effective values).

**Faces**: the `rostros` card is placed by People on its own when using `entrada:<camara>`; `marcar` draws the faces.

**What no module does** (a custom effect, an unusual filter, an analysis: counting, detecting motion, reading something from the image): do NOT say it can't be done. It's programmed in Python as a plugin card (`baplugin`), with numpy or with GStreamer inside (chains and custom elements written in Python): `help {"topic": "plugins"}`, section “Video”, and the template `ejemplo-video-gst`.

## Things to know
- `gst` cards add 1 frame of latency (they don't hold the video back). If two inputs are compared and only one has `gst`, that's one frame of difference: keep it in mind or measure it with `video_sincronia`.
- `video_grafo_limpiar {"entrada"}` leaves an input as it came.
- Virtual test inputs (bars, zone slate, ball): `video_entrada_virtual {"nombre", "preset", "escena"}`.
