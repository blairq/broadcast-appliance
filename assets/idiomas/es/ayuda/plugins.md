**Nunca contestes «no se puede» por un equipo raro o un pedido que las herramientas no cubren: escribí un plugin.** Un plugin es un programa en Python que corre en su propio proceso (si se cae, la emisión sigue y se reinicia solo) y le suma al sistema lo que haga falta. Lo escribís vos; el operador no programa.

**Dónde están**: cada plugin es una carpeta en `Datos/Plugins/<id>/` (`plugin_listar` da la ruta en `carpeta`): `plugin.json` (manifiesto), `plugin.py` (el código), `_libs/` (sus bibliotecas de pip), `datos/` (lo que guarde) y su registro en `Logs/plugins/<id>.log`. Viven en los datos, no en el programa: sobreviven a las actualizaciones. Los ejemplos vienen con el programa (`plugin_listar` → `ejemplos`); en la interfaz, página **Plugins** (riel) → «Nuevo desde un ejemplo». No escribas archivos a mano en esa carpeta: usá `plugin_escribir` (valida el id y las rutas).

Casos típicos:
- Una cámara USB con un PTZ que "solo anda con la app de la marca" (Logitech Rally, Jabra, Huddly, OBSBOT…): se ve como una webcam (UVC) y el PTZ va por controles UVC estándar, por una **unidad de extensión UVC** (XU) propia o por HID.
- Cámaras 360 / ojo de pez (Kandao, Insta360, Ricoh): una o dos imágenes de 180°; hay que partirlas, desdoblarlas o coserlas, y a veces traen datos (hacia dónde habla alguien).
- Varios equipos que trabajan juntos (tres cámaras de sala que detectan quién habla): un plugin por equipo publica eventos y otro plugin "director" los junta y maneja el estudio.
- Cualquier equipo con una API propia (HTTP, WebSocket, UDP, serie): botoneras, tally, contadores, sensores.
- Procesar video o audio en Python (numpy, OpenCV, **GStreamer**): una tarjeta del grafo. Un efecto, un filtro o un análisis de video que no está en `video_modulos`: ver «Video» abajo.
- Automatizar («cuando pasa X, hacé Y») lo que los disparadores (`alert_types`) no cubren: escuchar eventos del estudio y llamar herramientas.
- Un gráfico animado a pedido que el repertorio no tiene (con lógica, datos de afuera o dibujado cuadro a cuadro): `@plugin.grafico` (`help {"topic": "graficos"}`, ejemplo `ejemplo-grafico`).
- Un DOCUMENTO DE IDENTIDAD que no es el DNI argentino (una licencia de conducir, el documento de otro país, una credencial con QR): `@plugin.documento` (abajo, ejemplo `documento-aamva`). El tótem lo ofrece en sus campos `documento`. Una cédula con zona MRZ en el dorso (OACI TD1) ya la lee `mrz-td1` de fábrica; a un plugin le llegan también los renglones de un dorso (formato «MRZ», tres renglones separados por salto de línea, ya validados) si sus `formatos` lo incluyen o están vacíos.

## El camino
1. **Qué hay**: `dispositivos_listar` (cámaras y micrófonos con su `gst` listo para un pipeline, y USB/HID/serie con vid:pid). Si no sabés cómo se le habla al equipo: buscá el protocolo (WebSearch si lo tenés) y/o escribí un plugin de exploración (abajo).
2. **Arrancá de un molde**: `plugin_listar` (los `ejemplos` con su `categoria`) → `plugin_ver {"id": "<ejemplo>"}` y, si se parece a lo que hay que hacer, `plugin_desde_ejemplo {"ejemplo", "id"}` y cambiá lo que haga falta. Los moldes: `ejemplo-video-gst` (efecto, filtro o análisis de video con GStreamer), `ejemplo-automatizacion` (escuchar el estudio y actuar), `ejemplo-equipo-red` (un aparato con protocolo de texto por TCP, con simulador), `ejemplo-basico` (todo lo que puede un plugin: tarjeta, filtro de audio, entrada, panel, control). Para equipos: `ejemplo-ptz-uvc`, `ejemplo-360`, `ejemplo-director`, y los listos para usar (VISCA, TSL, Stream Deck, MIDI, OSC, Bosch DCN).
3. **Escribilo**: `plugin_escribir {"id", "manifiesto": {"nombre", "descripcion", "dependencias": [...], "config": {...}}, "archivos": {"plugin.py": "..."}}`.
4. **Probalo sin tocar el aire**: `plugin_probar {"id"}` → informe (lo declarado, tarjetas con ms por cuadro y qué cambiaron, entradas con cuadros por segundo, controles, eventos, registro con los errores de Python). Si `ok` es false o algo no cierra, arreglá y volvé a probar. `controles: [{"nombre", "args"}]` llama controles; `ptz: true` mueve el equipo un instante.
5. **Activalo**: `plugin_activar {"id"}`. Sus entradas aparecen solas (escena del manifiesto o «Plugins»), su PTZ queda como el de esa entrada (`ptz_*` y la botonera andan igual), sus tarjetas se ponen con `plugin_tarjeta` (o en `video_pipeline` con `{"tipo": "plugin", "plugin", "tarjeta", "parametros"}`) y se gradúan con `video_tarjeta_ajustar {"id": "<id de la tarjeta>", "params": {"parametros": {"fuerza": 0.5}}}` (o los parámetros sueltos: `{"fuerza": 0.5}`). Una entrada que no está al aire solo se dibuja cuando alguien la mira: para verificar, capturá dos veces (la primera puede ser un cuadro viejo).
6. **Verificá en vivo**: `video_estado {"entrada"}`, `imagen_preguntar`, `plugin_registro`, `plugin_control {"id"}` (info de sus medios), `plugin_eventos`.
7. **Contale al operador** qué hace y cómo se usa. Si después cambiás el código: `plugin_escribir` + `plugin_reiniciar`.

## El SDK (`import ba_plugin as ba`)
```python
import ba_plugin as ba
plugin = ba.Plugin()                     # plugin.config (dict de plugin.json), plugin.datos (carpeta propia)

# ENTRADAS (video y/o audio). a) un pipeline de GStreamer que corre la app (lo más barato, sin copias):
plugin.entrada("Rally", gst="avfvideosrc device-index=1", escena="Sala")          # +gst_audio="osxaudiosrc ..."
# b) cuadros hechos en Python (numpy RGBA (alto, ancho, 4) uint8; audio float32 (n, canales)):
e = plugin.entrada("Pelota", video=True, audio=True)
e.enviar_video(arr, fps=30); e.enviar_audio(muestras, tasa=48000, canales=2)    # en un hilo de @plugin.al_iniciar

# TARJETAS: se cambia el cuadro EN EL LUGAR (memoria compartida); rápido (numpy vectorizado, nada de bucles por píxel)
@plugin.tarjeta("negativo", descripcion="...", parametros={"fuerza": 1.0})
def negativo(cuadro, params):            # cuadro.array() -> (alto, ancho, 4) RGBA del tamaño del lienzo
    a = cuadro.array(); a[..., :3] = 255 - a[..., :3]
@plugin.tarjeta("ganancia", medio="audio", parametros={"db": -6})
def ganancia(bloque, params): bloque.array()[:] *= 10 ** (params["db"] / 20)   # (muestras, canales) float32

# PTZ de una entrada (de este plugin o una que ya exista): velocidades -1..1
@plugin.ptz("Rally")
class Ptz:
    capacidades = {"panTilt": True, "zoom": True, "home": True, "presets": 8}
    def mover(self, pan, tilt, zoom): ...
    def parar(self): ...
    def home(self): ...
    def ir_a_preset(self, n): ...; def guardar_preset(self, n): ...   # también enfocar(v), auto_foco(b)

@plugin.control("modo", descripcion="...", parametros={"modo": "auto|manual"})   # plugin_control
def modo(args): return {"ok": True}

# GRÁFICOS ANIMADOS (help {"topic": "graficos"}): aparecen en Gráficos y Rótulos como plugin-<id>-<nombre>
@plugin.grafico("marcador", ancho=1000, alto=420, parametros=[{"clave": "puntos", "tipo": "datos", "porDefecto": "Sí: 3"}])
def marcador(valores): return {"declarativo": {...}}          # o {"qml": "..."}; o def f(lienzo, ms, valores) con numpy

# PANEL: la interfaz del operador (la dibuja la app; plugin_panel la maneja igual). Los valores se guardan solos.
plugin.panel([
    ba.seccion("Seguimiento"),
    ba.interruptor("seguir", "Seguir al que habla", defecto=True),
    ba.deslizador("suavidad", "Suavidad", 0, 1, defecto=0.5),
    ba.opcion("modo", "Modo", ["auto", "manual"]),
    ba.boton("calibrar", "Calibrar", al_tocar=calibrar),        # también texto(), numero()
    ba.indicador("caras", "Caras vistas"), ba.medidor("confianza", "Confianza", 0, 1),
])
plugin.valores["suavidad"]                                      # lo que puso el operador
@plugin.al_cambiar("modo")
def al_modo(valor): ...
plugin.mostrar("caras", 3)                                      # indicadores en vivo (se puede llamar en cada cuadro)

plugin.evento("hablando", {"angulo": 37, "persona": 2})        # canal de datos (con hora)
@plugin.escuchar("*/hablando")                                  # "kandao-1/hablando", "*/x", "id/*" (los propios también llegan)
def al_hablar(datos, de): ...

plugin.llamar("scene_switch", {"name": "Exterior"})             # CUALQUIER herramienta del sistema (las tuyas)

# DOCUMENTOS DE IDENTIDAD (docs/funciones/DOCUMENTOS.md): la app lee el código de barras de la cámara (zxing) y,
# si no es un tipo de fábrica (dni-ar), te pasa {formato, texto, bytes}. Devolvés los datos normalizados o None
# (no es tuyo). Queda como el tipo «plugin:<id>/licencia» (documento_tipos; se elige en el campo del tótem).
@plugin.documento("licencia", titulo="Licencia de conducir (EE.UU.)", formatos=["PDF417"], pais="US",
                  como_se_dice="tu licencia", ejemplos=[{"formato": "PDF417", "texto": "@\n...ANSI 636000..."}])
def licencia(codigo):                    # codigo["texto"] (str), codigo["bytes"] (bytes), codigo["formato"]
    if "ANSI " not in codigo["texto"]: return None
    return {"numero": "...", "apellido": "...", "nombres": "...", "nacimiento": "1990-03-15",   # fechas en ISO
            "vencimiento": "...", "sexo": "F", "extra": {"estado": "CA"}}   # sin `numero` = no lo leyó
# plugin_probar corre los `ejemplos` (SINTÉTICOS: nunca datos de una persona real) y muestra qué devolvió.
# Los datos son sensibles: no los mandes a ningún servicio de afuera ni los imprimas enteros en el registro.
plugin.aviso("texto", "warning"); plugin.estado("conectado a la Rally"); plugin.secreto("clave")  # claves cifradas
@plugin.al_iniciar / @plugin.al_terminar / @plugin.cada(segundos) / @plugin.al_cambiar_config
plugin.correr()                                                 # al final
```
Equipos (`from ba_plugin import dispositivos as d`; dependencias `pyusb`, `libusb-package`, `hidapi`, `pyserial`):
- `d.usb_listar()` → `[{"vid": "046d", "pid": "0881", "fabricante", "producto", "serie", "bus", "direccion", "uvc": true}]` (vid/pid en hex, texto); `d.hid_listar()` → `[{vid, pid, producto, fabricante, uso, ruta}]`; `d.serie_listar()` → `[{puerto, descripcion, vid, pid, serie}]`.
- `uvc = d.UVC.abrir("046d", "0881")`: `uvc.info()` (terminal de cámara, unidades de extensión con su GUID, rangos), `uvc.mover(pan, tilt, zoom)` / `uvc.parar()` (UVC estándar), `uvc.ir_a(pan=, tilt=, zoom=)`, y lo propio de la marca: `uvc.unidades()`, `uvc.xu_explorar(unidad)` (qué selectores contestan), `uvc.xu_leer(unidad, selector)`, `uvc.xu_escribir(unidad, selector, bytes)`. No le saca la imagen al sistema: se mueve mientras se ve.
- `h = d.hid_abrir(vid, pid)`: `h.write(bytes)`, `h.read(64, 100)`.

## Video: un efecto, un filtro o un análisis que el grafo no trae
Primero `video_modulos {"buscar": ...}`: si GStreamer ya lo tiene, es una tarjeta `gst` y listo. Si no (un efecto propio, una corrección rara, contar personas, detectar movimiento, leer un tablero), **programalo vos como una tarjeta de plugin**: es la tarjeta «plugin» del grafo de video (el módulo `baplugin`), corre en la cadena de la entrada como cualquier otra, y el código corre en el proceso del plugin (si se cuelga, la imagen pasa igual y la emisión sigue).

Adentro de la tarjeta podés usar numpy y **GStreamer entero desde Python** (`from ba_plugin import gst as bagst`; el programa trae los bindings): cadenas de módulos y **elementos de GStreamer propios escritos en Python**. Mínimo:
```python
import ba_plugin as ba
from ba_plugin import gst as bagst
plugin = ba.Plugin()

@bagst.elemento("baumbral", propiedades={"nivel": 128})     # un GstBase.BaseTransform de verdad
def umbral(a, props):                                          # a: (alto, ancho, 4) RGBA, sin copiar
    gris = a[..., :3].mean(axis=2) > props["nivel"]
    a[..., :3] = (gris * 255).astype("uint8")[..., None]

@plugin.tarjeta("umbral", parametros={"nivel": 128})
def tarjeta(cuadro, params):
    c = bagst.cadena("baumbral name=u ! videobalance contrast=1.2")   # una por tarjeta puesta; se arma una vez
    c.procesar(cuadro)                                                 # el resultado queda en el cuadro
    c.poner("u", nivel=int(params["nivel"]))                           # cambiar en vivo sin rearmar

plugin.correr()
```
Sin GStreamer también vale: `@plugin.tarjeta` y numpy sobre `cuadro.array()`. Un análisis NO cambia la imagen: mide sobre `cuadro.array()[::8, ::8]` cada pocos cuadros, `plugin.mostrar` en su panel y `plugin.evento("movimiento", {...})` (con histéresis; nunca un evento por cuadro).

El camino completo: `plugin_desde_ejemplo {"ejemplo": "ejemplo-video-gst", "id": "mi-efecto"}` (o `plugin_escribir`) → `plugin_probar {"id"}` (ms por cuadro de cada tarjeta, a 320x180: en 1080p son ~30 veces más; tiene que quedar bien por debajo de un cuadro) → `plugin_activar` → `plugin_tarjeta {"id", "tarjeta", "entrada"}` (queda en el grafo como «Plugin · <tarjeta>»; en la página Video se ve, se mueve y se gradúa como las demás) → `video_tarjeta_ajustar {"id": "<id de la tarjeta>", "params": {"nivel": 90}}` → `foto_fuente` para ver. Costos en 1080p: pasar por una cadena ~1 ms, `videobalance` ~8, `gaussianblur` ~35 (demasiado: achicá o elegí otro), numpy en uint16 y contiguo 1-3 ms (en float o con `a[..., :3]` sobre el cuadro entero, 20).

## Recetas
- **PTZ raro**: entrada `gst` con la cámara (`dispositivos_listar` da el `avfvideosrc device-index=N`) + `@plugin.ptz` que habla UVC/XU/HID. Primero probá UVC estándar (`uvc.mover`); si no se mueve, explorá las XU (un control "explorar" que devuelva `uvc.info()` y `xu_explorar`) y buscá el protocolo de ese modelo.
- **Partir una imagen en varias entradas** (dos ojos de pez en un cuadro, 2x2 de un grabador): una sola entrada y en su grafo tarjetas `recorte` + `salida` (cada salida es una fuente con nombre). No abras el mismo dispositivo dos veces.
- **Desdoblar un ojo de pez / coser 360**: tarjeta de plugin con OpenCV (`opencv-python-headless`): calculá los mapas UNA vez (`cv2.fisheye.initUndistortRectifyMap` o `cv2.remap` con mapas propios) y aplicá `cv2.remap` por cuadro. Si se hace pesado, que la entrada venga más chica.
- **Varios equipos iguales**: un plugin por equipo (copiá con `plugin_desde_ejemplo` o `plugin_escribir`) con su `config` (número de serie, IP) y su `escena`; o uno que declare N entradas.
- **Quién habla / director**: cada plugin de equipo publica `plugin.evento("hablando", {...})` (ojo: si hay un director corriendo que escucha `*/hablando`, también lo recibe; en pruebas usá otro canal o su `config.de`); un plugin director `@plugin.escuchar("*/hablando")` decide (tiempo mínimo en cada plano, no cortar a mitad de frase) y usa `plugin.llamar(...)`: `scene_switch`, el compositor con `video_tarjeta_ajustar` (capas), `ptz_*` para apuntar otra cámara. Preguntale al operador si lo quiere automático o con confirmación (`operator_ask`).
- **Audio de un equipo**: entrada con `gst_audio` o `enviar_audio`; tarjetas de audio sobre cualquier fuente con `plugin_tarjeta {"fuente"}`.
- **Automatización** (molde `ejemplo-automatizacion`): `@plugin.escuchar("aire/escena")` (los temas: `eventos_temas`) y `plugin.llamar(...)`; poné `"herramientas": [...]` en el plugin.json con lo único que puede llamar. Un control «simular» que recorra los casos deja probarlo con `plugin_probar {"controles": [...]}` (las llamadas al sistema salen en el informe sin ejecutarse).
- **Un aparato por la red** (molde `ejemplo-equipo-red`): conexión en un hilo de `al_iniciar` con timeout y reconexión con espera creciente, órdenes con respuesta, lo que manda solo como `plugin.evento`, y un simulador del aparato adentro (`modo: simulador`) para probarlo sin el equipo.

## Reglas
- Nada bloquea para siempre: todo con timeout; los bucles en hilos de `al_iniciar` y mirando `plugin.andando`.
- Las tarjetas tienen que ser rápidas (un cuadro dura 1000/fps ms del lienzo: 33 ms a 30 fps, 16 ms a 60; y es en serie con el video): numpy vectorizado; si tarda, sale el cuadro anterior. Si el plugin solo tiene que MEDIR (color, brillo, quién está), que mida cada pocos cuadros sobre una versión achicada (`a[::8, ::8]`) y aplique lo que calcula con una tarjeta gst (`plugin.llamar("video_tarjeta_ajustar", ...)` sobre un `videobalance`): procesar el cuadro entero en Python en cada cámara carga la máquina y traba la interfaz.
- `print` va al registro (`plugin_registro`); usá `plugin.estado(...)` para lo que tiene que ver el operador.
- **Todo plugin que el operador tenga que manejar lleva `plugin.panel`** (lo que se ajusta: deslizadores, interruptores, opciones; lo que se hace: botones; lo que pasa: indicadores y medidores). Los humanos usan el sistema igual que vos: nada de "pedímelo por el chat". `@plugin.control` queda para lo que pide el asistente con argumentos (y también aparece como botón).
- Las bibliotecas van en `dependencias` (pip, se bajan a la carpeta del plugin: portable, sobreviven a las actualizaciones). **numpy ya viene con el programa** (declararlo no cuesta nada); la primera vez que se baja algo grande (opencv) puede tardar un par de minutos.
- Claves de equipos (usuario, contraseña, token de la API de la marca): nunca por el chat ni en `config`. Declaralas en plugin.json (`"secretos": [{"clave": "clave", "etiqueta": "Clave de la cámara"}]`), pedilas con `plugin_credenciales {"id"}` (formulario seguro en la barra) y en el plugin `plugin.secreto("clave")` (None si todavía no están: que espere y lo diga con `plugin.estado`).