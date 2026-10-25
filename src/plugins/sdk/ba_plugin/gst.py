"""GStreamer adentro de un plugin: cadenas de módulos y elementos propios escritos en Python.

El programa trae GStreamer entero con sus bindings para este Python (PyGObject y
gst-python, en los tres sistemas): un plugin puede usar CUALQUIER módulo de
GStreamer y escribir los suyos. Lo típico es hacerlo adentro de una tarjeta
(``@plugin.tarjeta``), que es la tarjeta `baplugin` del grafo de video::

    import ba_plugin as ba
    from ba_plugin import gst as bagst
    plugin = ba.Plugin()

    # Un ELEMENTO de GStreamer escrito en Python (un GstBase.BaseTransform de verdad,
    # registrado en el proceso del plugin): recibe (alto, ancho, 4) RGBA y lo cambia en el lugar.
    @bagst.elemento("bainvertir", propiedades={"fuerza": 1.0})
    def invertir(a, props):
        if props["fuerza"] > 0.5:
            a[..., :3] = 255 - a[..., :3]

    # La tarjeta pasa cada cuadro por una cadena de módulos (los de GStreamer y los propios).
    @plugin.tarjeta("efecto", parametros={"cadena": "bainvertir ! videobalance saturation=0"})
    def efecto(cuadro, params):
        bagst.cadena(params["cadena"]).procesar(cuadro)

    plugin.correr()

Por qué así y no un elemento cargado adentro de la app: el plugin corre en SU
proceso (si se cuelga o se cae, el video sigue) y la tarjeta `baplugin` le
pasa los cuadros por memoria compartida con un tope de espera. Lo que corre
acá adentro (la cadena y los elementos propios) no puede trabar la emisión.

Costos (Mac M1, 1920x1080 RGBA): pasar el cuadro por una cadena vacía ~1 ms;
``videobalance`` ~8 ms; ``gaussianblur`` ~35 ms (demasiado para 30 cuadros por
segundo: achicá antes o elegí otro). Un elemento propio con numpy vectorizado,
1 a 3 ms (nada de bucles por píxel). ``plugin_probar`` mide cada tarjeta.
"""
from __future__ import annotations

import threading
from typing import Any, Callable

_Gst = None
_lock = threading.Lock()
_registrados: dict[str, type] = {}


def iniciar():
    """Carga GStreamer (una vez) y devuelve el módulo ``Gst``. Error claro si el programa no lo trae."""
    global _Gst
    with _lock:
        if _Gst is None:
            try:
                import gi
                gi.require_version("Gst", "1.0")
                gi.require_version("GstBase", "1.0")
                gi.require_version("GstVideo", "1.0")
                from gi.repository import Gst
            except (ImportError, ValueError) as e:
                raise RuntimeError(f"este programa no trae GStreamer para Python ({e})") from e
            Gst.init(None)
            _Gst = Gst
    return _Gst


def disponible() -> bool:
    """Si se puede usar GStreamer desde el plugin."""
    try:
        iniciar()
        return True
    except RuntimeError:
        return False


def existe(modulo: str) -> bool:
    """Si hay un módulo de GStreamer con ese nombre (``"videobalance"``, ``"bavineta"``…)."""
    return iniciar().ElementFactory.find(modulo) is not None


# --- Elementos propios ---------------------------------------------------------------------

def _gprop(nombre: str, defecto: Any):
    from gi.repository import GObject
    rw = GObject.ParamFlags.READWRITE
    if isinstance(defecto, bool):
        return (bool, nombre, nombre, bool(defecto), rw)
    if isinstance(defecto, int):
        return (int, nombre, nombre, -2**31, 2**31 - 1, int(defecto), rw)
    if isinstance(defecto, float):
        return (float, nombre, nombre, -1e12, 1e12, float(defecto), rw)
    return (str, nombre, nombre, str(defecto), rw)


def elemento(nombre: str, propiedades: dict | None = None, descripcion: str = "",
             formatos: tuple[str, ...] = ("RGBA",)):
    """Decorador: ``def f(a, props)`` se vuelve un elemento de GStreamer llamado `nombre`.

    Es un ``GstBase.BaseTransform`` en el lugar: `a` es la vista numpy SIN copiar del cuadro
    ((alto, ancho, 4) uint8; el orden de los canales es el del primer formato de `formatos`,
    RGBA de fábrica) y `props` las propiedades de ahora (las de `propiedades` con su valor
    de fábrica; el tipo sale de ese valor: float, int, bool o texto). Se usa por su nombre en
    cualquier cadena del plugin (``bagst.cadena("mielemento fuerza=0.3")``) o en
    ``Gst.parse_launch``. Vale solo adentro del proceso del plugin.

    Si hace falta más (cambiar el tamaño, varios pads, audio): escribí la clase de GObject a mano
    con ``GstBase.BaseTransform`` como base, igual que esta, y registrala con
    ``Gst.Element.register(None, nombre, Gst.Rank.NONE, Clase)``.
    """
    Gst = iniciar()
    from gi.repository import GObject, GstBase, GstVideo
    import numpy as np

    props_defecto = dict(propiedades or {})
    caps = Gst.Caps.from_string("video/x-raw,format=(string){" + ",".join(formatos) + "}")

    def deco(fn: Callable):
        if nombre in _registrados:
            return fn

        def init(self):
            GstBase.BaseTransform.__init__(self)
            self._props = dict(props_defecto)
            self._info = None
            self.set_in_place(True)

        def do_get_property(self, p):
            return self._props[p.name.replace("-", "_")]

        def do_set_property(self, p, v):
            self._props[p.name.replace("-", "_")] = v

        def do_set_caps(self, entra, _sale):
            self._info = GstVideo.VideoInfo.new_from_caps(entra)
            return self._info is not None

        def do_transform_ip(self, buf):
            info = self._info
            if info is None:
                return Gst.FlowReturn.NOT_NEGOTIATED
            ok, m = buf.map(Gst.MapFlags.READ | Gst.MapFlags.WRITE)
            if not ok:
                return Gst.FlowReturn.ERROR
            try:
                a = np.ndarray((info.height, info.width, 4), np.uint8, buffer=m.data,
                               strides=(info.stride[0], 4, 1))
                fn(a, self._props)
            finally:
                buf.unmap(m)
            return Gst.FlowReturn.OK

        clase = type("BaPy_" + nombre.replace("-", "_"), (GstBase.BaseTransform,), {
            "__gstmetadata__": (nombre, "Filter/Effect/Video", descripcion or (fn.__doc__ or nombre).strip(),
                                "plugin de Broadcast Appliance"),
            "__gsttemplates__": (Gst.PadTemplate.new("src", Gst.PadDirection.SRC, Gst.PadPresence.ALWAYS, caps),
                                 Gst.PadTemplate.new("sink", Gst.PadDirection.SINK, Gst.PadPresence.ALWAYS, caps)),
            "__gproperties__": {k.replace("_", "-"): _gprop(k.replace("_", "-"), v) for k, v in props_defecto.items()},
            "__init__": init,
            "do_get_property": do_get_property,
            "do_set_property": do_set_property,
            "do_set_caps": do_set_caps,
            "do_transform_ip": do_transform_ip,
        })
        GObject.type_register(clase)
        if not Gst.Element.register(None, nombre, Gst.Rank.NONE, clase):
            raise RuntimeError(f"no se pudo registrar el elemento «{nombre}»")
        _registrados[nombre] = clase
        return fn
    return deco


# --- Cadenas: pasar un cuadro por módulos de GStreamer --------------------------------------

class Cadena:
    """Un pipeline ``appsrc ! <descripcion> ! appsink`` que procesa cuadros de a uno.

    `descripcion` es lo del medio, como en gst-launch: ``"videobalance saturation=0 ! gamma
    gamma=1.2"``. Se rodea de ``videoconvert`` (no cuesta nada si el formato ya es el mismo), así
    que sirven también los módulos que trabajan en YUV. El tamaño de salida tiene que ser el de
    entrada (nada de ``videoscale`` a otro tamaño ni ``videocrop`` sin volver a agrandar).
    Para nombrar un elemento y cambiarle algo en vivo: ``"videobalance name=bal"`` y
    ``cadena.poner("bal", saturation=0.5)``.
    """

    def __init__(self, descripcion: str):
        self.descripcion = descripcion
        self._p = None
        self._forma: tuple | None = None
        self._n = 0
        self._puesto: dict[tuple[str, str], Any] = {}

    def _armar(self, formato: str, ancho: int, alto: int) -> None:
        Gst = iniciar()
        self.cerrar()
        caps = f"video/x-raw,format={formato},width={ancho},height={alto},framerate=30/1"
        medio = self.descripcion.strip() or "identity"
        # appsink sin sincronía y con un cuadro de cola: lo que llega tarde se descarta abajo por pts.
        try:
            self._p = Gst.parse_launch(
                f"appsrc name=ba_src format=time is-live=false do-timestamp=false caps={caps} "
                f"! videoconvert ! {medio} ! videoconvert ! video/x-raw,format={formato},width={ancho},height={alto} "
                "! appsink name=ba_sink sync=false max-buffers=2 drop=false enable-last-sample=false")
        except Exception as e:  # noqa: BLE001 — GLib.Error: un módulo que no existe, una propiedad mal escrita
            self._p = None
            raise RuntimeError(f"la cadena «{self.descripcion}» no se pudo armar: {getattr(e, 'message', e)}") from None
        self._src = self._p.get_by_name("ba_src")
        self._sink = self._p.get_by_name("ba_sink")
        if self._p.set_state(Gst.State.PLAYING) == Gst.StateChangeReturn.FAILURE:
            err = self._error()
            self.cerrar()
            raise RuntimeError(f"la cadena «{self.descripcion}» no arrancó{': ' + err if err else ''}")
        self._forma = (formato, ancho, alto)
        self._puesto.clear()

    def _error(self) -> str:
        if self._p is None:
            return ""
        Gst = iniciar()
        bus = self._p.get_bus()
        m = bus.pop_filtered(Gst.MessageType.ERROR) if bus else None
        if m is None:
            return ""
        e, dbg = m.parse_error()
        return f"{e.message} ({dbg.splitlines()[0] if dbg else ''})"

    def elemento(self, nombre: str):
        """El elemento de la cadena con ese ``name=`` (None si todavía no se armó o no está)."""
        return self._p.get_by_name(nombre) if self._p is not None else None

    def poner(self, nombre: str, **propiedades) -> None:
        """Cambia propiedades de un elemento de la cadena (solo las que cambiaron: se puede llamar en cada cuadro)."""
        el = self.elemento(nombre)
        if el is None:
            return
        for k, v in propiedades.items():
            clave = (nombre, k)
            if self._puesto.get(clave) != v:
                el.set_property(k.replace("_", "-"), v)
                self._puesto[clave] = v

    def procesar(self, cuadro, espera_s: float = 0.5) -> None:
        """Pasa el cuadro (un ``Cuadro`` de una tarjeta) por la cadena y deja el resultado EN EL LUGAR.

        Si no vuelve en `espera_s`, levanta un error: la tarjeta deja pasar el cuadro como estaba."""
        Gst = iniciar()
        forma = (cuadro.formato, cuadro.ancho, cuadro.alto)
        if forma != self._forma:
            self._armar(*forma)
        datos = cuadro.datos
        self._n += 1
        buf = Gst.Buffer.new_wrapped(bytes(datos))
        buf.pts = self._n * 33_333_333
        buf.duration = 33_333_333
        if self._src.emit("push-buffer", buf) != Gst.FlowReturn.OK:
            err = self._error()
            self._forma = None  # la próxima vez se rearma
            raise RuntimeError(f"la cadena no aceptó el cuadro{': ' + err if err else ''}")
        fin_ns = int(espera_s * Gst.SECOND)
        while True:
            s = self._sink.emit("try-pull-sample", fin_ns)
            if s is None:
                err = self._error()
                if err:
                    self._forma = None
                raise RuntimeError(f"la cadena no devolvió el cuadro en {espera_s} s{': ' + err if err else ''}")
            b = s.get_buffer()
            if b.pts != Gst.CLOCK_TIME_NONE and b.pts < buf.pts:
                continue  # uno viejo que volvió tarde
            ok, m = b.map(Gst.MapFlags.READ)
            if not ok:
                raise RuntimeError("no se pudo leer el cuadro procesado")
            try:
                if m.size != len(datos):
                    raise RuntimeError(f"la cadena devolvió {m.size} bytes y el cuadro tiene {len(datos)}: "
                                       "tiene que salir del mismo tamaño y formato")
                datos[:] = m.data
            finally:
                b.unmap(m)
            return

    def cerrar(self) -> None:
        """Para el pipeline (se llama solo al cambiar de tamaño y al terminar el hilo de la tarjeta)."""
        if self._p is not None:
            try:
                self._p.set_state(iniciar().State.NULL)
            except Exception:  # noqa: BLE001
                pass
        self._p = None
        self._forma = None

    def __del__(self):
        self.cerrar()


_por_hilo = threading.local()


def cadena(descripcion: str) -> Cadena:
    """La ``Cadena`` de `descripcion` para la tarjeta que está corriendo.

    Cada tarjeta puesta en el grafo es una conexión con su propio hilo (la misma tarjeta en dos
    entradas son dos), así que se guarda una cadena por hilo y por descripción: llamarla en cada
    cuadro no arma nada nuevo. Si la descripción cambia (otro parámetro), se arma otra y la vieja
    se cierra."""
    cache: dict[str, Cadena] | None = getattr(_por_hilo, "cadenas", None)
    if cache is None:
        cache = _por_hilo.cadenas = {}
    c = cache.get(descripcion)
    if c is None:
        for vieja in cache.values():
            vieja.cerrar()
        cache.clear()
        c = cache[descripcion] = Cadena(descripcion)
    return c
