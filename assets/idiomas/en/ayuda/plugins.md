**Never answer “it can't be done” because of an unusual device or a request the tools don't cover: write a plugin.** A plugin is a Python program that runs in its own process (if it crashes, the broadcast keeps going and it restarts on its own) and adds whatever the system needs. You write it; the operator doesn't program.

**Where they live**: each plugin is a folder in `Datos/Plugins/<id>/` (`plugin_listar` gives the path in `carpeta`): `plugin.json` (manifest), `plugin.py` (the code), `_libs/` (its pip libraries), `datos/` (whatever it saves) and its log in `Logs/plugins/<id>.log`. They live in the data, not in the program: they survive updates. The examples ship with the program (`plugin_listar` → `ejemplos`); in the interface, the **Plugins** page (rail) → “New from an example”. Don't write files by hand in that folder: use `plugin_escribir` (it validates the id and the paths).

Typical cases:
- A USB camera with a PTZ that "only works with the vendor's app" (Logitech Rally, Jabra, Huddly, OBSBOT…): it shows up as a webcam (UVC) and the PTZ goes through standard UVC controls, through its own **UVC extension unit** (XU) or through HID.
- 360 / fisheye cameras (Kandao, Insta360, Ricoh): one or two 180° images; they have to be split, dewarped or stitched, and sometimes they carry data (the direction someone is speaking from).
- Several devices working together (three room cameras that detect who is speaking): one plugin per device publishes events and another "director" plugin gathers them and drives the studio.
- Any device with its own API (HTTP, WebSocket, UDP, serial): control surfaces, tally, counters, sensors.
- Processing video or audio in Python (numpy, OpenCV, **GStreamer**): a card in the graph. An effect, a filter or a video analysis that isn't in `video_modulos`: see “Video” below.
- Automating (“when X happens, do Y”) what the triggers (`alert_types`) don't cover: listen to studio events and call tools.
- An on-demand animated graphic the repertoire doesn't have (with logic, outside data or drawn frame by frame): `@plugin.grafico` (`help {"topic": "graficos"}`, example `ejemplo-grafico`).
- An IDENTITY DOCUMENT that isn't the Argentine DNI (a driver's license, another country's ID, a badge with a QR code): `@plugin.documento` (below, example `documento-aamva`). The kiosk offers it in its `documento` fields. An ID card with an MRZ zone on the back (ICAO TD1) is already read by the built-in `mrz-td1`; a plugin also receives the lines from a back side (format “MRZ”, three lines separated by line breaks, already validated) if its `formatos` include it or are empty.

## The path
1. **What's there**: `dispositivos_listar` (cameras and microphones with their `gst` ready for a pipeline, and USB/HID/serial with vid:pid). If you don't know how to talk to the device: look up the protocol (WebSearch if you have it) and/or write an exploration plugin (below).
2. **Start from a template**: `plugin_listar` (the `ejemplos` with their `categoria`) → `plugin_ver {"id": "<ejemplo>"}` and, if it resembles what needs doing, `plugin_desde_ejemplo {"ejemplo", "id"}` and change what's needed. The templates: `ejemplo-video-gst` (a video effect, filter or analysis with GStreamer), `ejemplo-automatizacion` (listen to the studio and act), `ejemplo-equipo-red` (a device with a text protocol over TCP, with a simulator), `ejemplo-basico` (everything a plugin can do: card, audio filter, input, panel, control). For devices: `ejemplo-ptz-uvc`, `ejemplo-360`, `ejemplo-director`, and the ready-to-use ones (VISCA, TSL, Stream Deck, MIDI, OSC, Bosch DCN).
3. **Write it**: `plugin_escribir {"id", "manifiesto": {"nombre", "descripcion", "dependencias": [...], "config": {...}}, "archivos": {"plugin.py": "..."}}`.
4. **Test it without touching the air**: `plugin_probar {"id"}` → report (what it declared, cards with ms per frame and what they changed, inputs with frames per second, controls, events, log with the Python errors). If `ok` is false or something doesn't add up, fix it and test again. `controles: [{"nombre", "args"}]` calls controls; `ptz: true` moves the device for an instant.
5. **Activate it**: `plugin_activar {"id"}`. Its inputs appear on their own (the manifest's scene or “Plugins”), its PTZ becomes that input's PTZ (`ptz_*` and the control surface work the same), its cards are placed with `plugin_tarjeta` (or in `video_pipeline` with `{"tipo": "plugin", "plugin", "tarjeta", "parametros"}`) and tuned with `video_tarjeta_ajustar {"id": "<card id>", "params": {"parametros": {"fuerza": 0.5}}}` (or the loose parameters: `{"fuerza": 0.5}`). An input that isn't on air is only drawn when someone is looking at it: to verify, capture twice (the first may be an old frame).
6. **Verify live**: `video_estado {"entrada"}`, `imagen_preguntar`, `plugin_registro`, `plugin_control {"id"}` (info about its media), `plugin_eventos`.
7. **Tell the operator** what it does and how to use it. If you change the code later: `plugin_escribir` + `plugin_reiniciar`.

## The SDK (`import ba_plugin as ba`)
```python
import ba_plugin as ba
plugin = ba.Plugin()                     # plugin.config (dict from plugin.json), plugin.datos (its own folder)

# INPUTS (video and/or audio). a) a GStreamer pipeline run by the app (the cheapest, no copies):
plugin.entrada("Rally", gst="avfvideosrc device-index=1", escena="Room")          # +gst_audio="osxaudiosrc ..."
# b) frames made in Python (numpy RGBA (height, width, 4) uint8; audio float32 (n, channels)):
e = plugin.entrada("Ball", video=True, audio=True)
e.enviar_video(arr, fps=30); e.enviar_audio(muestras, tasa=48000, canales=2)    # in a thread from @plugin.al_iniciar

# CARDS: the frame is changed IN PLACE (shared memory); fast (vectorized numpy, no per-pixel loops)
@plugin.tarjeta("negativo", descripcion="...", parametros={"fuerza": 1.0})
def negativo(cuadro, params):            # cuadro.array() -> (height, width, 4) RGBA at canvas size
    a = cuadro.array(); a[..., :3] = 255 - a[..., :3]
@plugin.tarjeta("ganancia", medio="audio", parametros={"db": -6})
def ganancia(bloque, params): bloque.array()[:] *= 10 ** (params["db"] / 20)   # (samples, channels) float32

# PTZ for an input (from this plugin or one that already exists): speeds -1..1
@plugin.ptz("Rally")
class Ptz:
    capacidades = {"panTilt": True, "zoom": True, "home": True, "presets": 8}
    def mover(self, pan, tilt, zoom): ...
    def parar(self): ...
    def home(self): ...
    def ir_a_preset(self, n): ...; def guardar_preset(self, n): ...   # also enfocar(v), auto_foco(b)

@plugin.control("modo", descripcion="...", parametros={"modo": "auto|manual"})   # plugin_control
def modo(args): return {"ok": True}

# ANIMATED GRAPHICS (help {"topic": "graficos"}): they appear in Graphics and Lower thirds as plugin-<id>-<nombre>
@plugin.grafico("marcador", ancho=1000, alto=420, parametros=[{"clave": "puntos", "tipo": "datos", "porDefecto": "Yes: 3"}])
def marcador(valores): return {"declarativo": {...}}          # or {"qml": "..."}; or def f(lienzo, ms, valores) with numpy

# PANEL: the operator's interface (drawn by the app; plugin_panel drives it the same way). Values are saved automatically.
plugin.panel([
    ba.seccion("Tracking"),
    ba.interruptor("seguir", "Follow the speaker", defecto=True),
    ba.deslizador("suavidad", "Smoothness", 0, 1, defecto=0.5),
    ba.opcion("modo", "Mode", ["auto", "manual"]),
    ba.boton("calibrar", "Calibrate", al_tocar=calibrar),        # also texto(), numero()
    ba.indicador("caras", "Faces seen"), ba.medidor("confianza", "Confidence", 0, 1),
])
plugin.valores["suavidad"]                                      # what the operator set
@plugin.al_cambiar("modo")
def al_modo(valor): ...
plugin.mostrar("caras", 3)                                      # live indicators (can be called on every frame)

plugin.evento("hablando", {"angulo": 37, "persona": 2})        # data channel (timestamped)
@plugin.escuchar("*/hablando")                                  # "kandao-1/hablando", "*/x", "id/*" (its own events arrive too)
def al_hablar(datos, de): ...

plugin.llamar("scene_switch", {"name": "Exterior"})             # ANY system tool (yours)

# IDENTITY DOCUMENTS (docs/funciones/DOCUMENTOS.md): the app reads the barcode from the camera (zxing) and,
# if it isn't a built-in type (dni-ar), passes you {formato, texto, bytes}. You return the normalized data or None
# (not yours). It becomes the type «plugin:<id>/licencia» (documento_tipos; chosen in the kiosk field).
@plugin.documento("licencia", titulo="Driver's license (US)", formatos=["PDF417"], pais="US",
                  como_se_dice="your license", ejemplos=[{"formato": "PDF417", "texto": "@\n...ANSI 636000..."}])
def licencia(codigo):                    # codigo["texto"] (str), codigo["bytes"] (bytes), codigo["formato"]
    if "ANSI " not in codigo["texto"]: return None
    return {"numero": "...", "apellido": "...", "nombres": "...", "nacimiento": "1990-03-15",   # ISO dates
            "vencimiento": "...", "sexo": "F", "extra": {"estado": "CA"}}   # no `numero` = it didn't read it
# plugin_probar runs the `ejemplos` (SYNTHETIC: never a real person's data) and shows what it returned.
# The data is sensitive: don't send it to any outside service or print it whole in the log.
plugin.aviso("text", "warning"); plugin.estado("connected to the Rally"); plugin.secreto("clave")  # encrypted keys
@plugin.al_iniciar / @plugin.al_terminar / @plugin.cada(segundos) / @plugin.al_cambiar_config
plugin.correr()                                                 # at the end
```
Devices (`from ba_plugin import dispositivos as d`; dependencies `pyusb`, `libusb-package`, `hidapi`, `pyserial`):
- `d.usb_listar()` → `[{"vid": "046d", "pid": "0881", "fabricante", "producto", "serie", "bus", "direccion", "uvc": true}]` (vid/pid in hex, as text); `d.hid_listar()` → `[{vid, pid, producto, fabricante, uso, ruta}]`; `d.serie_listar()` → `[{puerto, descripcion, vid, pid, serie}]`.
- `uvc = d.UVC.abrir("046d", "0881")`: `uvc.info()` (camera terminal, extension units with their GUID, ranges), `uvc.mover(pan, tilt, zoom)` / `uvc.parar()` (standard UVC), `uvc.ir_a(pan=, tilt=, zoom=)`, and the vendor-specific part: `uvc.unidades()`, `uvc.xu_explorar(unidad)` (which selectors answer), `uvc.xu_leer(unidad, selector)`, `uvc.xu_escribir(unidad, selector, bytes)`. It doesn't take the image away from the system: it moves while being watched.
- `h = d.hid_abrir(vid, pid)`: `h.write(bytes)`, `h.read(64, 100)`.

## Video: an effect, a filter or an analysis the graph doesn't have
First `video_modulos {"buscar": ...}`: if GStreamer already has it, it's a `gst` card and you're done. If not (a custom effect, an unusual correction, counting people, detecting motion, reading a scoreboard), **program it yourself as a plugin card**: it's the “plugin” card of the video graph (the `baplugin` module), it runs in the input's chain like any other, and the code runs in the plugin's process (if it hangs, the image still passes through and the broadcast keeps going).

Inside the card you can use numpy and **all of GStreamer from Python** (`from ba_plugin import gst as bagst`; the program ships the bindings): chains of modules and **custom GStreamer elements written in Python**. Minimal:
```python
import ba_plugin as ba
from ba_plugin import gst as bagst
plugin = ba.Plugin()

@bagst.elemento("baumbral", propiedades={"nivel": 128})     # a real GstBase.BaseTransform
def umbral(a, props):                                          # a: (height, width, 4) RGBA, no copy
    gris = a[..., :3].mean(axis=2) > props["nivel"]
    a[..., :3] = (gris * 255).astype("uint8")[..., None]

@plugin.tarjeta("umbral", parametros={"nivel": 128})
def tarjeta(cuadro, params):
    c = bagst.cadena("baumbral name=u ! videobalance contrast=1.2")   # one per placed card; built once
    c.procesar(cuadro)                                                 # the result stays in the frame
    c.poner("u", nivel=int(params["nivel"]))                           # change live without rebuilding

plugin.correr()
```
Without GStreamer works too: `@plugin.tarjeta` and numpy on `cuadro.array()`. An analysis does NOT change the image: it measures on `cuadro.array()[::8, ::8]` every few frames, `plugin.mostrar` in its panel and `plugin.evento("movimiento", {...})` (with hysteresis; never one event per frame).

The full path: `plugin_desde_ejemplo {"ejemplo": "ejemplo-video-gst", "id": "mi-efecto"}` (or `plugin_escribir`) → `plugin_probar {"id"}` (ms per frame for each card, at 320x180: at 1080p it's ~30 times more; it has to stay well below one frame) → `plugin_activar` → `plugin_tarjeta {"id", "tarjeta", "entrada"}` (it stays in the graph as “Plugin · <card>”; on the Video page it's shown, moved and tuned like the others) → `video_tarjeta_ajustar {"id": "<card id>", "params": {"nivel": 90}}` → `foto_fuente` to see it. Costs at 1080p: going through a chain ~1 ms, `videobalance` ~8, `gaussianblur` ~35 (too much: downscale or choose another), numpy in uint16 and contiguous 1-3 ms (in float or with `a[..., :3]` over the whole frame, 20).

## Recipes
- **Unusual PTZ**: a `gst` input with the camera (`dispositivos_listar` gives the `avfvideosrc device-index=N`) + `@plugin.ptz` that speaks UVC/XU/HID. First try standard UVC (`uvc.mover`); if it doesn't move, explore the XUs (an "explore" control that returns `uvc.info()` and `xu_explorar`) and look up that model's protocol.
- **Splitting one image into several inputs** (two fisheyes in one frame, a recorder's 2x2): a single input and in its graph `recorte` + `salida` cards (each output is a named source). Don't open the same device twice.
- **Dewarping a fisheye / stitching 360**: a plugin card with OpenCV (`opencv-python-headless`): compute the maps ONCE (`cv2.fisheye.initUndistortRectifyMap` or `cv2.remap` with your own maps) and apply `cv2.remap` per frame. If it gets heavy, have the input come in smaller.
- **Several identical devices**: one plugin per device (copy with `plugin_desde_ejemplo` or `plugin_escribir`) with its `config` (serial number, IP) and its `escena`; or one that declares N inputs.
- **Who's speaking / director**: each device plugin publishes `plugin.evento("hablando", {...})` (careful: if there's a director running that listens to `*/hablando`, it receives it too; in tests use another channel or its `config.de`); a director plugin `@plugin.escuchar("*/hablando")` decides (minimum time on each shot, don't cut mid-sentence) and uses `plugin.llamar(...)`: `scene_switch`, the compositor with `video_tarjeta_ajustar` (layers), `ptz_*` to aim another camera. Ask the operator whether they want it automatic or with confirmation (`operator_ask`).
- **Audio from a device**: an input with `gst_audio` or `enviar_audio`; audio cards on any source with `plugin_tarjeta {"fuente"}`.
- **Automation** (template `ejemplo-automatizacion`): `@plugin.escuchar("aire/escena")` (the topics: `eventos_temas`) and `plugin.llamar(...)`; put `"herramientas": [...]` in plugin.json with the only things it may call. A “simulate” control that walks through the cases lets you test it with `plugin_probar {"controles": [...]}` (the system calls show up in the report without being executed).
- **A device over the network** (template `ejemplo-equipo-red`): connection in a thread from `al_iniciar` with a timeout and reconnection with growing back-off, commands with replies, whatever it sends on its own as `plugin.evento`, and a simulator of the device inside (`modo: simulador`) to test it without the hardware.

## Rules
- Nothing blocks forever: everything with a timeout; loops in threads from `al_iniciar` and checking `plugin.andando`.
- Cards have to be fast (a frame lasts 1000/fps ms of the canvas: 33 ms at 30 fps, 16 ms at 60; and it's in series with the video): vectorized numpy; if it takes too long, the previous frame goes out. If the plugin only has to MEASURE (color, brightness, who's there), have it measure every few frames on a downscaled version (`a[::8, ::8]`) and apply what it computes with a gst card (`plugin.llamar("video_tarjeta_ajustar", ...)` on a `videobalance`): processing the whole frame in Python on every camera loads the machine and freezes the interface.
- `print` goes to the log (`plugin_registro`); use `plugin.estado(...)` for what the operator needs to see.
- **Every plugin the operator has to handle gets a `plugin.panel`** (what's adjusted: sliders, switches, options; what's done: buttons; what's happening: indicators and meters). Humans use the system the same way you do: no "ask me in the chat". `@plugin.control` is for what the assistant requests with arguments (and it also appears as a button).
- Libraries go in `dependencias` (pip, downloaded into the plugin's folder: portable, they survive updates). **numpy already ships with the program** (declaring it costs nothing); the first time something big is downloaded (opencv) it can take a couple of minutes.
- Device credentials (user, password, the vendor's API token): never through the chat or in `config`. Declare them in plugin.json (`"secretos": [{"clave": "clave", "etiqueta": "Camera password"}]`), request them with `plugin_credenciales {"id"}` (secure form in the bar) and in the plugin use `plugin.secreto("clave")` (None if they aren't there yet: have it wait and say so with `plugin.estado`).
