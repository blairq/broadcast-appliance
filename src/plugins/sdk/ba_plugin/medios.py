"""Los cuadros y el audio entre la app y el plugin.

Los datos pesados van por MEMORIA COMPARTIDA (``multiprocessing.shared_memory``:
shm_open en macOS/Linux, un mapeo con nombre en Windows); por un socket local
(127.0.0.1, un puerto por plugin) van solo avisos cortos en JSON, uno por
línea. Del lado de la app los usa el módulo de GStreamer ``baplugin`` (las
tarjetas) y ``bapluginsrc`` (las entradas), dentro de ba-sources.

Conexión de una TARJETA (la abre la app, una por tarjeta en el grafo)::

    app -> {"rol": "tarjeta", "nombre", "medio": "video", "formato": "RGBA", "ancho", "alto", "stride",
            "tam", "parametros": {...}}            (audio: "tasa", "canales", "tam")
    plugin -> {"ok": true, "shm": "/psm_..."}      (la memoria la crea el plugin)
    app -> {"c": n, "pts": ns}                     (el cuadro ya está en la memoria)
    plugin -> {"c": n}                             (ya lo cambió ahí mismo; o {"c": n, "error"})
    app -> {"parametros": {...}}                   (cambiaron los ajustes de la tarjeta)
    app -> {"caps": {...}}                         (cambió el tamaño: otra memoria, mismo ida y vuelta)

Conexión de una ENTRADA (la abre la app; el formato lo decide el plugin)::

    app -> {"rol": "entrada", "nombre", "medio": "video"|"audio"}
    plugin -> {"formato", "ancho", "alto", "stride", "fps", "tam", "shm"}   (audio: "tasa", "canales")
    plugin -> {"c": n, "pts": ns|null, "tam": bytes}    (el cuadro está en la memoria)
    app -> {"c": n}                                     (ya lo copió: la memoria está libre)
"""

from __future__ import annotations

import json
import socket
import sys
import threading
import time
import traceback
from multiprocessing import shared_memory
from typing import Any

try:  # numpy es opcional: sin él, .datos (memoryview) igual sirve
    import numpy as _np
except ImportError:  # pragma: no cover
    _np = None

BYTES_POR_PIXEL = {"RGBA": 4, "BGRA": 4, "RGBx": 4, "BGRx": 4}


def tam_video(formato: str, ancho: int, alto: int) -> tuple[int, int]:
    """(stride, bytes totales) de un cuadro."""
    if formato in BYTES_POR_PIXEL:
        stride = ancho * 4
        return stride, stride * alto
    if formato in ("I420", "NV12"):
        return ancho, ancho * alto + 2 * ((ancho + 1) // 2) * ((alto + 1) // 2)
    raise ValueError(f"formato «{formato}»: RGBA, BGRA, RGBx, BGRx, I420 o NV12")


def _nueva_memoria(tam: int) -> shared_memory.SharedMemory:
    # Queda anotada en el rastreador de Python: si el plugin se cae sin
    # limpiar, la borra él (no quedan memorias huérfanas).
    return shared_memory.SharedMemory(create=True, size=max(tam, 1))


def _nombre_shm(m: shared_memory.SharedMemory) -> str:
    n = m._name  # type: ignore[attr-defined]  (con la barra inicial en POSIX)
    return n if n.startswith("/") or sys.platform == "win32" else "/" + n


def _soltar(m: shared_memory.SharedMemory | None) -> None:
    if m is None:
        return
    try:
        m.close()
    except BufferError:
        pass  # alguien todavía tiene una vista: se libera al salir
    try:
        m.unlink()
    except (FileNotFoundError, OSError):
        pass


class Cuadro:
    """Un cuadro de video en memoria compartida."""

    def __init__(self, buf: memoryview, formato: str, ancho: int, alto: int, stride: int, pts_ns: int | None):
        self.datos = buf  #: memoryview escribible (cambiarlo cambia el cuadro)
        self.formato, self.ancho, self.alto, self.stride, self.pts_ns = formato, ancho, alto, stride, pts_ns

    def array(self):
        """La vista numpy, SIN copiar: (alto, ancho, 4) uint8 en RGBA/BGRA; plano (bytes) en I420/NV12."""
        if _np is None:
            raise RuntimeError("falta numpy: agregalo a \"dependencias\" en plugin.json")
        if self.formato in BYTES_POR_PIXEL:
            return _np.ndarray((self.alto, self.ancho, 4), dtype=_np.uint8, buffer=self.datos,
                               strides=(self.stride, 4, 1))
        return _np.ndarray((len(self.datos),), dtype=_np.uint8, buffer=self.datos)


class Bloque:
    """Un bloque de audio: float32 intercalado."""

    def __init__(self, buf: memoryview, tasa: int, canales: int, muestras: int, pts_ns: int | None):
        self.datos, self.tasa, self.canales, self.muestras, self.pts_ns = buf, tasa, canales, muestras, pts_ns

    def array(self):
        """(muestras, canales) float32, sin copiar."""
        if _np is None:
            raise RuntimeError("falta numpy: agregalo a \"dependencias\" en plugin.json")
        return _np.ndarray((self.muestras, self.canales), dtype=_np.float32, buffer=self.datos)


class _Linea:
    """Líneas JSON sobre un socket, con timeout."""

    def __init__(self, s: socket.socket):
        self.s = s
        self.buf = b""
        self.lock = threading.Lock()

    def enviar(self, m: dict) -> None:
        with self.lock:
            self.s.sendall((json.dumps(m) + "\n").encode())

    def leer(self, timeout: float | None) -> dict | None:
        """None si se cortó; {} si no llegó nada en `timeout`."""
        fin = None if timeout is None else time.monotonic() + timeout
        while b"\n" not in self.buf:
            if fin is not None:
                resta = fin - time.monotonic()
                if resta <= 0:
                    return {}
                self.s.settimeout(resta)
            else:
                self.s.settimeout(None)
            try:
                d = self.s.recv(65536)
            except socket.timeout:
                return {}
            except OSError:
                return None
            if not d:
                return None
            self.buf += d
        linea, self.buf = self.buf.split(b"\n", 1)
        try:
            return json.loads(linea)
        except ValueError:
            return {}


class CanalEntrada:
    """Lo que el plugin manda a UNA entrada (video o audio). Lo conecta la app."""

    def __init__(self, nombre: str, medio: str):
        self.nombre, self.medio = nombre, medio
        self.linea: _Linea | None = None
        self.mem: shared_memory.SharedMemory | None = None
        self.forma: tuple = ()
        self.n = 0
        self.enviados = 0
        self.descartados = 0
        self.lock = threading.Lock()

    def conectado(self) -> bool:
        return self.linea is not None

    def enganchar(self, linea: _Linea) -> None:
        with self.lock:
            self.linea = linea
            self.forma = ()  # que vuelva a mandar el formato a esta conexión

    def soltar(self, linea: _Linea) -> None:
        with self.lock:
            if self.linea is linea:
                self.linea = None

    def _mandar(self, forma: tuple, cabecera: dict, tam: int, escribir, pts_ns: int | None, extra: dict,
                timeout: float) -> bool:
        with self.lock:
            linea = self.linea
            if linea is None:
                self.descartados += 1
                return False
            try:
                if forma != self.forma or self.mem is None or self.mem.size < tam:
                    vieja = self.mem
                    self.mem = _nueva_memoria(tam)
                    linea.enviar({**cabecera, "tam": tam, "shm": _nombre_shm(self.mem)})
                    _soltar(vieja)
                    self.forma = forma
                escribir(self.mem.buf)
                self.n += 1
                linea.enviar({"c": self.n, "pts": pts_ns, "tam": tam, **extra})
                while True:
                    r = linea.leer(timeout)
                    if r is None:
                        self.linea = None
                        self.descartados += 1
                        return False
                    if not r:
                        self.descartados += 1
                        return False  # tardó: se descarta este, la memoria se reusa igual
                    if r.get("c") == self.n:
                        self.enviados += 1
                        return True
            except OSError:
                self.linea = None
                return False

    def enviar_video(self, cuadro: Any, formato: str, ancho: int | None, alto: int | None, fps: float,
                     pts_ns: int | None, timeout: float) -> bool:
        if _np is not None and isinstance(cuadro, _np.ndarray):
            if formato in BYTES_POR_PIXEL:
                if cuadro.ndim != 3 or cuadro.shape[2] != 4:
                    raise ValueError("un cuadro RGBA es un array (alto, ancho, 4)")
                alto, ancho = cuadro.shape[0], cuadro.shape[1]
            datos = _np.ascontiguousarray(cuadro, dtype=_np.uint8).reshape(-1)
        else:
            datos = memoryview(cuadro).cast("B")
        if not ancho or not alto:
            raise ValueError("faltan ancho y alto")
        stride, tam = tam_video(formato, ancho, alto)
        if len(datos) < tam:
            raise ValueError(f"el cuadro tiene {len(datos)} bytes y un {formato} de {ancho}x{alto} necesita {tam}")

        def escribir(buf):
            buf[:tam] = datos[:tam] if _np is None or not isinstance(datos, _np.ndarray) else datos[:tam].data

        return self._mandar(("v", formato, ancho, alto, round(fps * 1000)),
                            {"formato": formato, "ancho": ancho, "alto": alto, "stride": stride, "fps": fps}, tam,
                            escribir, pts_ns, {}, timeout)

    def enviar_audio(self, muestras: Any, tasa: int, canales: int, pts_ns: int | None, timeout: float) -> bool:
        if _np is not None and isinstance(muestras, _np.ndarray):
            datos = _np.ascontiguousarray(muestras, dtype=_np.float32).reshape(-1).view(_np.uint8)
        else:
            datos = memoryview(muestras).cast("B")
        tam = len(datos)
        n = tam // (4 * canales)
        cap = max(tam, tasa * canales * 4 // 2)  # medio segundo: no se rehace la memoria por cada bloque

        def escribir(buf):
            buf[:tam] = datos if _np is None or not isinstance(datos, _np.ndarray) else datos.data

        return self._mandar(("a", tasa, canales), {"tasa": tasa, "canales": canales}, cap, escribir, pts_ns,
                            {"muestras": n}, timeout)


class Servidor:
    """El socket de medios del plugin: la app se conecta una vez por tarjeta y por entrada."""

    def __init__(self, plugin) -> None:
        self.plugin = plugin
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.s.bind(("127.0.0.1", 0))
        self.s.listen(16)
        self.puerto = self.s.getsockname()[1]
        self.tarjetas: dict[str, dict] = {}  # nombre -> estadísticas
        self.abierto = True
        threading.Thread(target=self._aceptar, name="ba-medios", daemon=True).start()

    def resumen(self) -> dict:
        ent = {}
        for e in self.plugin._entradas.values():
            for c in (e._canal_video, e._canal_audio):
                if c.enviados or c.conectado():
                    ent[f"{e.nombre}/{c.medio}"] = {"conectada": c.conectado(), "enviados": c.enviados,
                                                    "descartados": c.descartados}
        return {"puerto": self.puerto, "tarjetas": self.tarjetas, "entradas": ent}

    def cerrar(self) -> None:
        self.abierto = False
        try:
            self.s.close()
        except OSError:
            pass

    def _aceptar(self) -> None:
        while self.abierto:
            try:
                c, _ = self.s.accept()
            except OSError:
                return
            c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            threading.Thread(target=self._atender, args=(c,), name="ba-medio", daemon=True).start()

    def _atender(self, c: socket.socket) -> None:
        linea = _Linea(c)
        try:
            h = linea.leer(10.0)
            if not h:
                return
            if h.get("rol") == "tarjeta":
                self._tarjeta(linea, h)
            elif h.get("rol") == "entrada":
                self._entrada(linea, h)
        except Exception:  # noqa: BLE001
            traceback.print_exc(file=sys.stderr)
        finally:
            try:
                c.close()
            except OSError:
                pass

    def _entrada(self, linea: _Linea, h: dict) -> None:
        e = self.plugin._entradas.get(h.get("nombre", ""))
        if e is None:
            linea.enviar({"error": f"este plugin no tiene la entrada «{h.get('nombre')}»"})
            return
        canal = e._canal_audio if h.get("medio") == "audio" else e._canal_video
        canal.enganchar(linea)
        # Las respuestas ({"c": n}) las lee el que manda (enviar_*): acá solo se
        # mantiene abierta la conexión mientras sea la de esta entrada.
        try:
            while self.abierto and canal.linea is linea:
                time.sleep(0.5)
        finally:
            canal.soltar(linea)

    def _tarjeta(self, linea: _Linea, h: dict) -> None:
        nombre = h.get("nombre", "")
        t = self.plugin._tarjetas.get(nombre)
        if t is None:
            linea.enviar({"ok": False, "error": f"este plugin no tiene la tarjeta «{nombre}» (tiene: "
                                                f"{', '.join(self.plugin._tarjetas) or 'ninguna'})"})
            return
        st = self.tarjetas.setdefault(nombre, {"conexiones": 0, "cuadros": 0, "errores": 0, "ms_promedio": 0.0,
                                               "ultimo_error": ""})
        st["conexiones"] += 1
        params = {**t["parametros"], **(h.get("parametros") or {})}
        mem = None

        def preparar(cab: dict):
            nonlocal mem
            _soltar(mem)
            mem = _nueva_memoria(int(cab.get("tam", 0)))
            linea.enviar({"ok": True, "shm": _nombre_shm(mem)})
            return cab

        cab = preparar(h)
        try:
            while self.abierto:
                m = linea.leer(None)
                if m is None:
                    return
                if "parametros" in m:
                    params = {**t["parametros"], **(m["parametros"] or {})}
                if "caps" in m:
                    cab = preparar(m["caps"])
                if "c" not in m:
                    continue
                t0 = time.perf_counter()
                try:
                    if t["medio"] == "audio":
                        canales = int(cab.get("canales", 2))
                        n = int(m.get("muestras", 0))
                        obj = Bloque(mem.buf[: n * canales * 4], int(cab.get("tasa", 48000)), canales, n, m.get("pts"))
                    else:
                        obj = Cuadro(mem.buf[: int(cab["tam"])], cab.get("formato", "RGBA"), int(cab["ancho"]),
                                     int(cab["alto"]), int(cab.get("stride", int(cab["ancho"]) * 4)), m.get("pts"))
                    r = t["fn"](obj, params)
                    if r is not None and r is not obj:
                        _copiar_en(obj.datos, r)
                    del obj
                    linea.enviar({"c": m["c"]})
                except Exception as e:  # noqa: BLE001 — la tarjeta deja pasar el cuadro tal cual
                    st["errores"] += 1
                    st["ultimo_error"] = f"{type(e).__name__}: {e}"
                    if st["errores"] <= 3 or st["errores"] % 300 == 0:
                        print(f"[ba_plugin] tarjeta «{nombre}» (error {st['errores']}):", file=sys.stderr)
                        traceback.print_exc(file=sys.stderr)
                    linea.enviar({"c": m["c"], "error": st["ultimo_error"]})
                ms = (time.perf_counter() - t0) * 1000
                st["cuadros"] += 1
                st["ms_promedio"] = round(st["ms_promedio"] * 0.95 + ms * 0.05 if st["cuadros"] > 1 else ms, 2)
        finally:
            _soltar(mem)


def _copiar_en(destino: memoryview, r: Any) -> None:
    if _np is not None and isinstance(r, _np.ndarray):
        datos = _np.ascontiguousarray(r).reshape(-1).view(_np.uint8)
        if datos.nbytes != len(destino):
            raise ValueError(f"devolvió {datos.nbytes} bytes y el cuadro tiene {len(destino)}: mismo tamaño y formato")
        destino[:] = datos.data
    else:
        b = memoryview(r).cast("B")
        if len(b) != len(destino):
            raise ValueError(f"devolvió {len(b)} bytes y el cuadro tiene {len(destino)}")
        destino[:] = b
