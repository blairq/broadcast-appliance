"""ba_plugin — el SDK de los plugins de Broadcast Appliance (docs/funciones/PLUGINS.md).

Un plugin es un programa en Python que corre en SU PROPIO PROCESO (si se cuelga
o se cae, la emisión sigue y la app lo vuelve a arrancar) y que le suma al
sistema cualquiera de estas cosas:

- ENTRADAS de video y/o audio: un equipo raro, una cámara que hay que partir en
  dos, algo que se genera en Python. Aparecen como entradas del grafo de video
  (con su colchón y el reloj del estudio) y su audio entra a la mezcla.
  Dos maneras:
    * ``plugin.entrada("Nombre", gst="avfvideosrc device-index=1")``: la app
      corre ese pipeline de GStreamer (sin copias: lo más barato), o
    * ``e = plugin.entrada("Nombre", video=True)`` y ``e.enviar_video(arr)``:
      los cuadros los hace el plugin.
- TARJETAS del grafo de video (y filtros de audio): una función que recibe cada
  cuadro (o bloque de audio) y lo cambia. ``@plugin.tarjeta("nombre")``.
- PTZ de una cámara que se maneja de una forma rara (USB propio, HID, serie,
  HTTP de la marca): ``@plugin.ptz("Entrada")`` sobre una clase con mover/parar…
  y la cámara queda manejable como cualquier otra (botonera, posiciones,
  asistente).
- CONTROLES: acciones con nombre que el asistente o la interfaz pueden pedir
  (``@plugin.control("modo")``).
- EVENTOS (canales de datos): ``plugin.evento("hablando", {...})`` publica;
  ``@plugin.escuchar("otro-plugin/hablando")`` recibe. Con eso un plugin
  "director" junta lo de varios equipos. Los eventos DE LA APP (bus de
  eventos) se escuchan por tema: ``"aire/tally"``, ``"salida/#"``,
  ``"audio/nivel/+"``, ``"alarma/#"``, ``"oradores/al_aire"`` (``de`` es
  ``"app"``); la lista completa la da la herramienta ``eventos_temas``.
- PANEL: su propia interfaz para el operador, DESCRIPTA (la dibuja la app con
  sus controles: si el plugin se cae, la app sigue). ``plugin.panel([...])``
  con ``ba.deslizador``, ``ba.interruptor``, ``ba.opcion``, ``ba.texto``,
  ``ba.numero``, ``ba.boton``, ``ba.indicador``, ``ba.medidor`` y
  ``ba.seccion``. Lo que mueve el operador llega a ``plugin.valores`` (y a
  ``@plugin.al_cambiar("clave")``); ``plugin.mostrar(clave, valor)`` actualiza
  un indicador (o mueve un control) en vivo. Se guarda solo entre reinicios.
- Y puede ACTUAR SOBRE TODO EL SISTEMA: ``plugin.llamar("scene_switch",
  {"name": "Exterior"})`` llama cualquier herramienta del servidor MCP de la app
  (escenas, audio, PTZ, grafo de video, avisos al operador…).

Ejemplo mínimo::

    import ba_plugin as ba
    plugin = ba.Plugin()

    @plugin.tarjeta("negativo", descripcion="Invierte los colores")
    def negativo(cuadro, params):
        a = cuadro.array()          # (alto, ancho, 4) uint8, RGBA, en memoria compartida
        a[..., :3] = 255 - a[..., :3]

    plugin.correr()

Reglas: nunca bloquear para siempre (todo con timeout), nada de print() de
datos masivos (va al registro), y las bibliotecas se declaran en plugin.json
(``"dependencias": ["numpy"]``): la app las baja a la carpeta del plugin.
"""

from __future__ import annotations

import json
import os
import queue
import socket
import sys
import threading
import time
import traceback
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Any, Callable

from . import medios as _medios
from . import superficie as _superficie
from .medios import Cuadro, Bloque  # noqa: F401  (para los que lo importan de acá)

__all__ = ["Plugin", "Cuadro", "Bloque", "Entrada", "necesita", "SDK_VERSION"]

SDK_VERSION = 1


def necesita(*paquetes: str) -> None:
    """Asegura bibliotecas de pip en la carpeta del plugin (``_libs``).

    Lo normal es declararlas en plugin.json (la app las baja antes de
    arrancar). Esto sirve para una que se necesita recién en un caso raro.
    Bloquea mientras baja (puede tardar).
    """
    import importlib
    import subprocess

    libs = Path(os.environ.get("BA_PLUGIN_DIR", ".")) / "_libs"
    faltan = []
    for p in paquetes:
        modulo = p.split("[")[0].split("=")[0].split(">")[0].split("<")[0].replace("-", "_")
        try:
            importlib.import_module(_NOMBRES_DE_MODULO.get(modulo, modulo))
        except ImportError:
            faltan.append(p)
    if not faltan:
        return
    libs.mkdir(parents=True, exist_ok=True)
    print(f"[ba_plugin] bajando {', '.join(faltan)} a {libs}", file=sys.stderr, flush=True)
    subprocess.run([sys.executable, "-I", "-B", "-m", "pip", "install", "--disable-pip-version-check", "--no-warn-script-location",
                    "--upgrade", "--target", str(libs), *faltan], check=True, stdout=sys.stderr)
    if str(libs) not in sys.path:
        sys.path.insert(0, str(libs))
    importlib.invalidate_caches()


# pip instala con un nombre y se importa con otro.
_NOMBRES_DE_MODULO = {"opencv_python": "cv2", "opencv_python_headless": "cv2", "pyusb": "usb", "pyserial": "serial",
                      "hidapi": "hid", "pillow": "PIL", "libusb_package": "libusb_package", "onvif_zeep": "onvif"}


class Entrada:
    """Una entrada de video y/o audio que aporta el plugin."""

    def __init__(self, plugin: "Plugin", nombre: str, video: bool, audio: bool, gst: str | None, gst_audio: str | None,
                 escena: str | None, latencia_ms: int | None, normalizar: bool | None):
        self.plugin = plugin
        self.nombre = nombre
        self.video = video or bool(gst)
        self.audio = audio or bool(gst_audio)
        self.gst = gst
        self.gst_audio = gst_audio
        self.escena = escena
        self.latencia_ms = latencia_ms
        self.normalizar = normalizar
        self._canal_video = _medios.CanalEntrada(nombre, "video")
        self._canal_audio = _medios.CanalEntrada(nombre, "audio")

    def declaracion(self) -> dict:
        d = {"t": "entrada", "nombre": self.nombre, "video": self.video, "audio": self.audio}
        for k in ("gst", "gst_audio", "escena", "latencia_ms", "normalizar"):
            v = getattr(self, k)
            if v is not None:
                d[k] = v
        return d

    @property
    def conectada(self) -> bool:
        """Si la app está recibiendo (hay alguien del otro lado)."""
        return self._canal_video.conectado() or self._canal_audio.conectado()

    def enviar_video(self, cuadro: Any, formato: str = "RGBA", ancho: int | None = None, alto: int | None = None,
                     fps: float = 30.0, pts_ns: int | None = None, timeout: float = 0.5) -> bool:
        """Manda un cuadro. `cuadro`: un array (alto, ancho, 4) uint8 de numpy, o bytes con `ancho` y `alto`.

        Formatos: RGBA, BGRA, RGBx, BGRx (4 bytes por píxel), I420 y NV12. Devuelve False si nadie lo
        recibió (la app todavía no conectó, o tardó más de `timeout`): se descarta, no se acumula.
        """
        return self._canal_video.enviar_video(cuadro, formato, ancho, alto, fps, pts_ns, timeout)

    def enviar_audio(self, muestras: Any, tasa: int = 48000, canales: int = 2, pts_ns: int | None = None,
                     timeout: float = 0.5) -> bool:
        """Manda audio: float32 intercalado (array (n, canales) de numpy, o bytes)."""
        return self._canal_audio.enviar_audio(muestras, tasa, canales, pts_ns, timeout)


# --- El panel: controles que dibuja la app ---------------------------------------------
# Cada función devuelve la descripción de un control (un dict). `clave` es el
# nombre del valor (``plugin.valores[clave]``); `al_cambiar` (opcional) es la
# función ``f(valor)`` que corre cuando el operador lo mueve.

def _control(tipo: str, clave: str, etiqueta: str | None, ayuda: str, al_cambiar, **extra) -> dict:
    d = {"tipo": tipo, "clave": clave, "etiqueta": etiqueta if etiqueta is not None else clave}
    if ayuda:
        d["ayuda"] = ayuda
    d.update({k: v for k, v in extra.items() if v is not None})
    if al_cambiar is not None:
        d["_fn"] = al_cambiar
    return d


def seccion(titulo: str) -> dict:
    """Un título que separa grupos de controles."""
    return {"tipo": "seccion", "clave": "", "etiqueta": titulo}


def deslizador(clave: str, etiqueta: str | None = None, minimo: float = 0.0, maximo: float = 1.0, paso: float | None = None,
               defecto: float | None = None, unidad: str = "", ayuda: str = "", al_cambiar=None) -> dict:
    """Un valor entre `minimo` y `maximo` (con `paso` entero, el valor es entero)."""
    return _control("deslizador", clave, etiqueta, ayuda, al_cambiar, minimo=minimo, maximo=maximo, paso=paso,
                    defecto=minimo if defecto is None else defecto, unidad=unidad or None)


def interruptor(clave: str, etiqueta: str | None = None, defecto: bool = False, ayuda: str = "", al_cambiar=None) -> dict:
    """Sí / no."""
    return _control("interruptor", clave, etiqueta, ayuda, al_cambiar, defecto=bool(defecto))


def opcion(clave: str, etiqueta: str | None = None, opciones: list | tuple = (), defecto: str | None = None, ayuda: str = "",
           al_cambiar=None) -> dict:
    """Una entre varias (`opciones`: textos)."""
    ops = [str(o) for o in opciones]
    return _control("opcion", clave, etiqueta, ayuda, al_cambiar, opciones=ops, defecto=defecto if defecto is not None else (ops[0] if ops else ""))


def texto(clave: str, etiqueta: str | None = None, defecto: str = "", ayuda: str = "", al_cambiar=None) -> dict:
    """Un texto (se manda al confirmar con Enter o al salir del campo)."""
    return _control("texto", clave, etiqueta, ayuda, al_cambiar, defecto=defecto)


def numero(clave: str, etiqueta: str | None = None, defecto: float = 0, minimo: float | None = None, maximo: float | None = None,
           unidad: str = "", ayuda: str = "", al_cambiar=None) -> dict:
    """Un número escrito a mano."""
    return _control("numero", clave, etiqueta, ayuda, al_cambiar, defecto=defecto, minimo=minimo, maximo=maximo, unidad=unidad or None)


def boton(clave: str, etiqueta: str | None = None, ayuda: str = "", peligro: bool = False, al_tocar=None) -> dict:
    """Un botón: `al_tocar()` (o ``@plugin.al_cambiar(clave)``) corre cada vez que lo tocan."""
    fn = (lambda _v: al_tocar()) if al_tocar is not None else None
    return _control("boton", clave, etiqueta, ayuda, fn, peligro=peligro or None)


def indicador(clave: str, etiqueta: str | None = None, unidad: str = "", ayuda: str = "") -> dict:
    """Muestra un valor que pone el plugin (``plugin.mostrar(clave, v)``): texto o número."""
    return _control("indicador", clave, etiqueta, ayuda, None, unidad=unidad or None)


def medidor(clave: str, etiqueta: str | None = None, minimo: float = 0.0, maximo: float = 1.0, unidad: str = "",
            ayuda: str = "") -> dict:
    """Una barra que llena el plugin (``plugin.mostrar(clave, v)``): nivel, carga, confianza…"""
    return _control("medidor", clave, etiqueta, ayuda, None, minimo=minimo, maximo=maximo, unidad=unidad or None)


_ENTRADAS_PANEL = ("deslizador", "interruptor", "opcion", "texto", "numero")


class Plugin:
    """El plugin. Uno por proceso: ``plugin = ba.Plugin()``."""

    _actual: "Plugin | None" = None

    def __init__(self) -> None:
        self.dir = Path(os.environ.get("BA_PLUGIN_DIR", Path.cwd()))
        self.id = os.environ.get("BA_PLUGIN_ID", self.dir.name)
        try:
            self.manifiesto = json.loads((self.dir / "plugin.json").read_text("utf-8"))
        except (OSError, ValueError):
            self.manifiesto = {}
        #: La configuración (``"config"`` de plugin.json, con lo que cambie la app encima).
        self.config: dict = dict(self.manifiesto.get("config", {}))
        #: Carpeta privada para guardar cosas (sobrevive a reinicios y actualizaciones).
        self.datos = self.dir / "datos"
        self.datos.mkdir(exist_ok=True)
        self.andando = True
        self._entradas: dict[str, Entrada] = {}
        self._tarjetas: dict[str, dict] = {}
        self._ptz: dict[str, Any] = {}
        self._controles: dict[str, dict] = {}
        self._escuchas: dict[str, list[Callable]] = {}
        self._al_iniciar: list[Callable] = []
        self._al_terminar: list[Callable] = []
        self._al_config: list[Callable] = []
        self._periodicos: list[tuple[float, Callable]] = []
        self._pendientes: dict[int, tuple[threading.Event, list]] = {}
        self._n = 0
        self._lock = threading.Lock()
        self._pool = ThreadPoolExecutor(max_workers=8, thread_name_prefix="ba-pedido")
        #: Lo que el operador puso en el panel (y lo que el plugin mostró): clave -> valor.
        self.valores: dict = {}
        self._al_valor: dict[str, list[Callable]] = {}
        self._mostrar_pend: dict = {}
        self._mostrar_hilo: threading.Thread | None = None
        self._salida = None  # el canal con la app (stdout de verdad)
        self._medios: _medios.Servidor | None = None
        self._declarado = False
        self._decl: dict[tuple, dict] = {}
        self._ultimo_estado: dict | None = None
        #: Superficies de hardware (Stream Deck, MIDI…) como botoneras: ver ``superficie.py``.
        self.superficie = _superficie.Superficies(self)
        Plugin._actual = self

    # --- Declarar -----------------------------------------------------------------

    def entrada(self, nombre: str, video: bool = False, audio: bool = False, gst: str | None = None,
                gst_audio: str | None = None, escena: str | None = None, latencia_ms: int | None = None,
                normalizar: bool | None = None) -> Entrada:
        """Declara una entrada. Con `gst` (y/o `gst_audio`) la app corre ESE pipeline de GStreamer como
        origen (p. ej. ``"avfvideosrc device-index=1 ! videocrop right=1920"``); si no, los cuadros los manda
        el plugin con ``enviar_video`` / ``enviar_audio``. `escena`: dónde ponerla la primera vez."""
        e = Entrada(self, nombre, video, audio, gst, gst_audio, escena, latencia_ms, normalizar)
        if not (e.video or e.audio):
            raise ValueError("una entrada tiene video, audio o las dos")
        self._entradas[nombre] = e
        self._redeclarar(e.declaracion())
        return e

    def tarjeta(self, nombre: str, medio: str = "video", descripcion: str = "", parametros: dict | None = None):
        """Decorador: ``@plugin.tarjeta("nombre")`` sobre ``def f(cuadro, params)``.

        Video: `cuadro` es un ``Cuadro`` RGBA del tamaño del lienzo (``cuadro.array()`` en memoria compartida:
        se cambia EN EL LUGAR, o se devuelve un array del mismo tamaño). Audio (``medio="audio"``): un ``Bloque``
        float32 (``bloque.array()`` de (muestras, canales)). `params`: lo que se ajustó en la tarjeta, sobre
        los `parametros` de fábrica. Tiene que ser rápida: si tarda más que un cuadro, sale el anterior."""
        if medio not in ("video", "audio"):
            raise ValueError("medio: video o audio")

        def deco(fn):
            self._tarjetas[nombre] = {"fn": fn, "medio": medio, "descripcion": descripcion or (fn.__doc__ or "").strip(),
                                      "parametros": parametros or {}}
            self._redeclarar({"t": "tarjeta", "nombre": nombre, "medio": medio,
                              "descripcion": self._tarjetas[nombre]["descripcion"], "parametros": parametros or {}})
            return fn
        return deco

    def ptz(self, entrada: str):
        """Decorador de CLASE: el PTZ de `entrada` (una entrada de este plugin u otra que ya exista).

        La clase implementa lo que pueda: ``mover(pan, tilt, zoom)`` (velocidades -1..1), ``parar()``,
        ``enfocar(velocidad)``, ``auto_foco(prendido)``, ``ir_a_preset(n)``, ``guardar_preset(n)``, ``home()``,
        y declara ``capacidades = {"panTilt": True, "zoom": True, "foco": False, "autoFoco": False,
        "home": True, "presets": 8}``."""
        def deco(cls):
            obj = cls() if isinstance(cls, type) else cls
            self._ptz[entrada] = obj
            caps = dict(getattr(obj, "capacidades", {}))
            for k, m in (("panTilt", "mover"), ("home", "home")):
                caps.setdefault(k, hasattr(obj, m))
            caps.setdefault("zoom", hasattr(obj, "mover"))
            caps.setdefault("foco", hasattr(obj, "enfocar"))
            caps.setdefault("autoFoco", hasattr(obj, "auto_foco"))
            caps.setdefault("presets", 0)
            self._redeclarar({"t": "ptz", "entrada": entrada, "capacidades": caps})
            return cls
        return deco

    def control(self, nombre: str, descripcion: str = "", parametros: dict | None = None):
        """Decorador: una acción con nombre (``def f(args) -> dict``) que se pide con la herramienta
        ``plugin_control`` del asistente o desde la interfaz. `parametros`: un esquema JSON simple."""
        def deco(fn):
            self._controles[nombre] = {"fn": fn}
            self._redeclarar({"t": "control", "nombre": nombre, "descripcion": descripcion or (fn.__doc__ or "").strip(),
                              "parametros": parametros or {}})
            return fn
        return deco

    def escuchar(self, canal: str):
        """Decorador: ``def f(datos, de)`` para los eventos de `canal` (``"otro-plugin/hablando"``,
        ``"*/hablando"`` o ``"otro-plugin/*"``) o un tema de la app (``"aire/tally"``,
        ``"audio/nivel/+"``, ``"alarma/#"``; ``de`` llega como ``"app"``)."""
        def deco(fn):
            self._escuchas.setdefault(canal, []).append(fn)
            self._redeclarar({"t": "escuchar", "canal": canal})
            return fn
        return deco

    def al_iniciar(self, fn):
        """Decorador: corre en su propio hilo al arrancar (p. ej. el bucle que lee el equipo)."""
        self._al_iniciar.append(fn)
        return fn

    def al_terminar(self, fn):
        """Decorador: corre al cerrar (soltar el equipo). Tiene 2 segundos."""
        self._al_terminar.append(fn)
        return fn

    def al_cambiar_config(self, fn):
        """Decorador: ``def f(config)`` cuando la app cambia la configuración."""
        self._al_config.append(fn)
        return fn

    def cada(self, segundos: float):
        """Decorador: corre la función cada tantos segundos (en su hilo)."""
        def deco(fn):
            self._periodicos.append((segundos, fn))
            return fn
        return deco

    def panel(self, controles: list, titulo: str = "") -> None:
        """Declara el panel del plugin: la lista de controles (``ba.deslizador(...)``, ``ba.boton(...)``…).

        La app lo dibuja en la página Plugins (con su estilo, también en ventana aparte) y el asistente lo
        maneja con ``plugin_panel``. Se puede volver a llamar para cambiarlo. Los valores de los controles se
        guardan solos: al reiniciar, ``plugin.valores`` vuelve con lo que había puesto el operador."""
        limpios = []
        for c in controles:
            c = dict(c)
            fn = c.pop("_fn", None)
            if fn is not None and c.get("clave"):
                self._al_valor.setdefault(c["clave"], [])
                if fn not in self._al_valor[c["clave"]]:
                    self._al_valor[c["clave"]].append(fn)
            if c.get("tipo") in _ENTRADAS_PANEL:
                self.valores.setdefault(c["clave"], c.get("defecto"))
            limpios.append(c)
        self._redeclarar({"t": "panel", "nombre": "panel", "titulo": titulo, "controles": limpios})

    def al_cambiar(self, clave: str):
        """Decorador: ``def f(valor)`` cuando el operador (o el asistente) cambia `clave` del panel o toca su botón."""
        def deco(fn):
            self._al_valor.setdefault(clave, []).append(fn)
            return fn
        return deco

    def mostrar(self, clave: str, valor: Any) -> None:
        """Pone un valor en el panel: un indicador o medidor, o mueve un control. Se puede llamar seguido
        (en cada cuadro): a la app le llega a lo sumo 10 veces por segundo, lo último."""
        self.valores[clave] = valor
        with self._lock:
            self._mostrar_pend[clave] = valor
            if self._mostrar_hilo is None:
                self._mostrar_hilo = threading.Thread(target=self._mostrar_bucle, name="ba-panel", daemon=True)
                self._mostrar_hilo.start()

    def _mostrar_bucle(self) -> None:
        while self.andando:
            time.sleep(0.1)
            with self._lock:
                pend, self._mostrar_pend = self._mostrar_pend, {}
            if pend:
                self._enviar({"t": "valores", "valores": pend})

    def _valor_de_la_app(self, clave: str, valor: Any, avisar: bool = True) -> None:
        self.valores[clave] = valor
        if avisar:
            for fn in self._al_valor.get(clave, []):
                self._pool.submit(self._protegido, fn, valor)

    # --- Hablar con el sistema ----------------------------------------------------

    def evento(self, canal: str, datos: Any = None) -> None:
        """Publica un evento (canal de datos) con la hora del momento. Lo reciben los que escuchan
        ``"<este-plugin>/<canal>"`` y queda el último en la app (``plugin_eventos``)."""
        self._enviar({"t": "evento", "canal": canal, "datos": datos, "hora_ns": time.time_ns()})

    def llamar(self, herramienta: str, args: dict | None = None, timeout: float = 30.0) -> Any:
        """Llama una herramienta del sistema (las mismas del asistente: ``help`` las lista) y devuelve su
        resultado (dict o texto). Tira ``RuntimeError`` si falla o tarda más que `timeout`."""
        with self._lock:
            self._n += 1
            n = self._n
            ev, caja = threading.Event(), []
            self._pendientes[n] = (ev, caja)
        self._enviar({"t": "llamar", "n": n, "herramienta": herramienta, "args": args or {}})
        if not ev.wait(timeout):
            self._pendientes.pop(n, None)
            raise RuntimeError(f"«{herramienta}» no contestó en {timeout} s")
        r = caja[0]
        if not r.get("ok", False):
            raise RuntimeError(r.get("error", "falló"))
        return r.get("resultado")

    def secreto(self, nombre: str, timeout: float = 5.0) -> str | None:
        """Una clave que el operador cargó en la app (cifrada; nunca en plugin.json ni en el chat).

        Se declara en plugin.json: ``"secretos": [{"clave": "clave", "etiqueta": "Clave de la cámara"}]``.
        Devuelve None si todavía no la cargaron (se cargan en la página Plugins o con ``plugin_credenciales``)."""
        with self._lock:
            self._n += 1
            n = self._n
            ev, caja = threading.Event(), []
            self._pendientes[n] = (ev, caja)
        self._enviar({"t": "secreto", "n": n, "nombre": nombre})
        if not ev.wait(timeout):
            self._pendientes.pop(n, None)
            raise RuntimeError(f"la app no contestó la clave «{nombre}»")
        r = caja[0]
        if not r.get("ok", False):
            raise RuntimeError(r.get("error", "falló"))
        return r.get("resultado")

    def aviso(self, texto: str, prioridad: str = "info") -> None:
        """Un aviso corto en la barra del operador (info, success, warning, critical)."""
        self._enviar({"t": "aviso", "texto": texto, "prioridad": prioridad})

    def estado(self, texto: str, ok: bool = True) -> None:
        """Cómo está el plugin (se ve en la lista de plugins): «conectado a la Rally», «sin el equipo»…"""
        self._ultimo_estado = {"t": "estado", "texto": texto, "ok": ok}
        self._enviar(self._ultimo_estado)

    def log(self, *a) -> None:
        print(*a, file=sys.stderr, flush=True)

    # --- Correr -------------------------------------------------------------------

    def correr(self) -> None:
        """Arranca y bloquea hasta que la app lo cierre. Va al final de plugin.py."""
        canal = os.environ.get("BA_PLUGIN_CANAL")
        if canal == "stdio" or canal is None:
            self._salida = _salida_stdio()
            entrada = sys.stdin.buffer
        else:  # pruebas: host:puerto
            h, p = canal.rsplit(":", 1)
            s = socket.create_connection((h, int(p)))
            self._salida = s.makefile("wb")
            entrada = s.makefile("rb")
        self._medios = _medios.Servidor(self)
        self._enviar({"t": "hola", "id": self.id, "sdk": SDK_VERSION, "pid": os.getpid(), "medios": self._medios.puerto,
                      "python": sys.version.split()[0]})
        with self._lock:
            pendientes = list(self._declaraciones())
            self._declarado = True
        for d in pendientes:
            self._enviar(d)
        self._enviar({"t": "listo"})
        if self._ultimo_estado:
            self._enviar(self._ultimo_estado)
        for fn in self._al_iniciar:
            threading.Thread(target=self._protegido, args=(fn,), name=f"ba-{fn.__name__}", daemon=True).start()
        for seg, fn in self._periodicos:
            threading.Thread(target=self._periodico, args=(seg, fn), daemon=True).start()
        try:
            for linea in entrada:
                if not linea.strip():
                    continue
                try:
                    m = json.loads(linea)
                except ValueError:
                    continue
                if not self._recibir(m):
                    break
        finally:
            self._terminar()

    # --- Por dentro -----------------------------------------------------------------

    def _declaraciones(self):
        return list(self._decl.values())

    def _redeclarar(self, d: dict) -> None:
        # Antes de correr() se juntan; después (algo que aparece en caliente) va directo.
        clave = (d["t"], d.get("nombre") or d.get("entrada") or d.get("canal"))
        with self._lock:
            self._decl[clave] = d
            ya = self._declarado
        if ya:
            self._enviar(d)

    def _enviar(self, m: dict) -> None:
        if self._salida is None:
            return
        datos = (json.dumps(m, ensure_ascii=False, default=str) + "\n").encode("utf-8")
        with self._lock:
            try:
                self._salida.write(datos)
                self._salida.flush()
            except (OSError, ValueError):
                self.andando = False

    def _recibir(self, m: dict) -> bool:
        t = m.get("t")
        if t == "chau":
            return False
        if t == "respuesta":
            p = self._pendientes.pop(m.get("n"), None)
            if p:
                p[1].append(m)
                p[0].set()
        elif t == "pedido":
            self._pool.submit(self._atender, m)
        elif t == "evento":
            canal, de = m.get("canal", ""), m.get("de", "")
            for patron, fns in self._escuchas.items():
                if (_coincide_tema(patron, canal) if de == "app" else _coincide(patron, f"{de}/{canal}")):
                    for fn in fns:
                        self._pool.submit(self._protegido, fn, m.get("datos"), de)
        elif t == "valor":
            self._valor_de_la_app(m.get("clave", ""), m.get("valor"))
        elif t == "valores":
            for k, v in (m.get("valores") or {}).items():
                self._valor_de_la_app(k, v)
        elif t == "superficie_estado":
            self.superficie._recibir(m)
        elif t == "config":
            self.config.update(m.get("config", {}))
            for fn in self._al_config:
                self._pool.submit(self._protegido, fn, self.config)
        return True

    def _atender(self, m: dict) -> None:
        n, accion, args = m.get("n"), m.get("accion", ""), m.get("args", {}) or {}
        try:
            r = self._ejecutar(accion, args)
            self._enviar({"t": "respuesta", "n": n, "ok": True, "resultado": r})
        except Exception as e:  # noqa: BLE001 — cualquier error del plugin vuelve como respuesta
            traceback.print_exc(file=sys.stderr)
            self._enviar({"t": "respuesta", "n": n, "ok": False, "error": f"{type(e).__name__}: {e}"})

    def _ejecutar(self, accion: str, args: dict) -> Any:
        if accion.startswith("ptz."):
            obj = self._ptz.get(args.get("entrada", ""))
            if obj is None:
                raise KeyError(f"no hay PTZ para «{args.get('entrada')}»")
            que = accion[4:]
            metodo = {"mover": "mover", "parar": "parar", "enfocar": "enfocar", "auto_foco": "auto_foco",
                      "ir_a_preset": "ir_a_preset", "guardar_preset": "guardar_preset", "home": "home"}.get(que)
            if metodo is None or not hasattr(obj, metodo):
                raise NotImplementedError(f"este PTZ no sabe «{que}»")
            argumentos = [args[k] for k in ("pan", "tilt", "zoom", "velocidad", "prendido", "numero") if k in args]
            return getattr(obj, metodo)(*argumentos)
        if accion == "control":
            c = self._controles.get(args.get("nombre", ""))
            if c is None:
                raise KeyError(f"no hay un control «{args.get('nombre')}» (hay: {', '.join(self._controles) or 'ninguno'})")
            return c["fn"](args.get("args", {}) or {})
        if accion == "info":
            return {"entradas": [e.declaracion() for e in self._entradas.values()], "tarjetas": list(self._tarjetas),
                    "ptz": list(self._ptz), "controles": list(self._controles), "escucha": list(self._escuchas), "valores": self.valores,
                    "medios": self._medios.resumen() if self._medios else {}}
        raise KeyError(f"no sé «{accion}»")

    def _protegido(self, fn, *a):
        try:
            fn(*a)
        except Exception:  # noqa: BLE001
            print(f"[ba_plugin] error en {getattr(fn, '__name__', fn)}:", file=sys.stderr)
            traceback.print_exc(file=sys.stderr)

    def _periodico(self, seg: float, fn) -> None:
        while self.andando:
            t0 = time.monotonic()
            self._protegido(fn)
            time.sleep(max(0.0, seg - (time.monotonic() - t0)))

    def _terminar(self) -> None:
        self.andando = False
        hilos = [threading.Thread(target=self._protegido, args=(fn,), daemon=True) for fn in self._al_terminar]
        for h in hilos:
            h.start()
        for h in hilos:
            h.join(2.0)
        if self._medios:
            self._medios.cerrar()
        for e in self._entradas.values():
            for c in (e._canal_video, e._canal_audio):
                _medios._soltar(c.mem)
                c.mem = None
        sys.stderr.flush()
        os._exit(0)


def _coincide(patron: str, nombre: str) -> bool:
    pp, nn = patron.split("/", 1) if "/" in patron else ("*", patron), nombre.split("/", 1)
    return all(a == "*" or a == b for a, b in zip(pp, nn))


def _coincide_tema(patron: str, tema: str) -> bool:
    """Temas de la app (bus de eventos): «+» o «*» un nivel, «#» (o «*» al final) el resto."""
    if patron in ("", "#", "*"):
        return True
    p, t = patron.split("/"), tema.split("/")
    for i, a in enumerate(p):
        if a == "#" or (a == "*" and i == len(p) - 1):
            return True
        if i >= len(t) or (a not in ("+", "*") and a != t[i]):
            return False
    return len(p) == len(t)


def _salida_stdio():
    """El stdout de verdad queda para hablar con la app; ``print`` va al registro (stderr)."""
    # El registro en UTF-8: en Windows stderr sale en la página de códigos del
    # sistema (cp1252) y un carácter que no está (chino, emojis) tiraba una excepción.
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    fd = os.dup(1)
    os.dup2(2, 1)
    sys.stdout = sys.stderr
    return os.fdopen(fd, "wb", buffering=0)
