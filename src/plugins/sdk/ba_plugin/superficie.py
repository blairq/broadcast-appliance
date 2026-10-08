"""Superficies de hardware: un Stream Deck, un X-keys, un controlador MIDI… como una BOTONERA más.

El aparato se registra y el operador le asigna un perfil y una página en la app (página Botonera, o
``botonera_dispositivo_asignar``). La app manda lo que tiene que mostrar cada celda (texto, colores,
tally, valor de un fader) y el plugin avisa qué tecla apretaron. Qué hace cada tecla lo decide la app
con el perfil del aparato: el plugin nunca llama herramientas por su cuenta desde acá.

    import ba_plugin as ba
    plugin = ba.Plugin()
    sup = plugin.superficie

    sup.registrar("deck1", "Stream Deck XL", modelo="streamdeck-xl", columnas=8, filas=4,
                  teclas_lcd=True, tamano_px=96)

    @sup.al_estado("deck1")
    def dibujar(id, estado):
        for celda in estado["celdas"]:
            imagen = sup.render_tecla(celda, 96)     # PIL.Image lista para la tecla
            ...

    # cuando el aparato avisa:
    sup.presionar("deck1", x, y)
    sup.soltar("deck1", x, y)

El estado (``{"type": "estado", "pagina": {"columnas", "filas", …}, "paginas": [...], "celdas": [...],
"confirmar": "x,y"?}``) trae por celda: ``x, y, ancho, alto, tipo, texto, icono, color, fondo,
titilar, activo, valor, min, max, nivel``, y ``imagen`` (data: URL chica) o ``cara`` (``{id, cuadros, fps}``:
una foto o un video corto propio, en cuadros de 288 px en la carpeta de datos; ``render_tecla`` la lee
sola, en el cuadro de ese momento, así que para animarla alcanza con redibujar cada 1/fps s las celdas
que tengan ``cara_animada(celda)``). Si una celda cambia el aire estando al aire, la app pide
confirmación: ``estado["confirmar"]`` dice cuál, y apretarla de nuevo (dentro de 5 s) confirma
(``presionar`` lo hace solo).
"""

from __future__ import annotations

import threading
from typing import Any, Callable


class Superficies:
    def __init__(self, plugin: Any) -> None:
        self._p = plugin
        self._al_estado: dict[str, list[Callable]] = {}
        self._al_aviso: list[Callable] = []
        self._ultimo: dict[str, dict] = {}
        self._registradas: set[str] = set()
        self._lock = threading.Lock()
        plugin._al_iniciar.append(self._saludar)

    # --- Registrar ----------------------------------------------------------------

    def registrar(self, id: str, nombre: str, modelo: str = "", columnas: int = 0, filas: int = 0,
                  teclas_lcd: bool = False, tamano_px: int = 72, perillas: int = 0, faders: int = 0,
                  lcd_tira: bool = False) -> None:
        """Da de alta el aparato `id` (único en este plugin) como una botonera. Se puede llamar antes de
        ``plugin.correr()`` o en caliente (cuando se enchufa)."""
        d = {"t": "superficie_registrar", "id": id, "nombre": nombre,
             "capacidades": {"modelo": modelo, "columnas": columnas, "filas": filas, "teclas_lcd": teclas_lcd,
                             "tamano_px": tamano_px, "perillas": perillas, "faders": faders, "lcd_tira": lcd_tira}}
        with self._p._lock:
            self._p._decl[("superficie", id)] = d
            ya = self._p._declarado
        self._registradas.add(id)
        if ya:
            self._p._enviar(d)
            self.pedir_estado(id)

    def quitar(self, id: str) -> None:
        """El aparato se desenchufó."""
        with self._p._lock:
            self._p._decl.pop(("superficie", id), None)
        self._registradas.discard(id)
        self._ultimo.pop(id, None)
        self._p._enviar({"t": "superficie_quitar", "id": id})

    # --- Lo que llega de la app ----------------------------------------------------------

    def al_estado(self, id: str = "*"):
        """Decorador: ``def f(id, estado)`` cada vez que cambia lo que muestra el aparato `id` (o todos)."""
        def deco(fn):
            self._al_estado.setdefault(id, []).append(fn)
            return fn
        return deco

    def al_aviso(self, fn):
        """Decorador: ``def f(id, texto, error)`` para los avisos de la app (permiso negado, error…)."""
        self._al_aviso.append(fn)
        return fn

    def estado(self, id: str) -> dict:
        """El último estado recibido para `id` (vacío si todavía no llegó)."""
        return self._ultimo.get(id, {})

    def celda_en(self, id: str, x: int, y: int) -> dict | None:
        """La celda que ocupa (x, y) en lo que muestra ahora `id`."""
        for c in self.estado(id).get("celdas", []):
            if c["x"] <= x < c["x"] + c.get("ancho", 1) and c["y"] <= y < c["y"] + c.get("alto", 1):
                return c
        return None

    def _recibir(self, m: dict) -> None:
        id, msg = m.get("id", ""), m.get("msg") or {}
        t = msg.get("type")
        if t == "estado":
            self._ultimo[id] = msg
            for fn in self._al_estado.get(id, []) + self._al_estado.get("*", []):
                self._p._pool.submit(self._p._protegido, fn, id, msg)
        elif t == "confirmar":
            e = dict(self._ultimo.get(id, {}))
            e["confirmar"] = msg.get("celda")
            self._ultimo[id] = e
            for fn in self._al_estado.get(id, []) + self._al_estado.get("*", []):
                self._p._pool.submit(self._p._protegido, fn, id, e)
        elif t == "aviso":
            for fn in self._al_aviso:
                self._p._pool.submit(self._p._protegido, fn, id, msg.get("texto", ""), bool(msg.get("error")))

    # --- Lo que manda el aparato -------------------------------------------------------------

    def _entrada(self, id: str, msg: dict) -> None:
        self._p._enviar({"t": "superficie_entrada", "id": id, "msg": msg})

    def presionar(self, id: str, x: int, y: int) -> None:
        """Apretaron la tecla (x, y). Si esa celda espera confirmación de aire, confirma."""
        if self.estado(id).get("confirmar") == f"{x},{y}":
            self._ultimo[id] = {k: v for k, v in self._ultimo[id].items() if k != "confirmar"}
            return self._entrada(id, {"type": "confirmar", "x": x, "y": y})
        self._entrada(id, {"type": "presionar", "x": x, "y": y})

    def soltar(self, id: str, x: int, y: int) -> None:
        self._entrada(id, {"type": "soltar", "x": x, "y": y})

    def girar(self, id: str, x: int, y: int, delta: float) -> None:
        """Una perilla (encoder): `delta` en pasos (+ derecha, - izquierda)."""
        self._entrada(id, {"type": "girar", "x": x, "y": y, "delta": delta})

    def valor(self, id: str, x: int, y: int, v: float) -> None:
        """Un fader o perilla absoluta: el valor en la escala de la celda (``min``..``max``)."""
        self._entrada(id, {"type": "valor", "x": x, "y": y, "v": v})

    def pagina(self, id: str, pagina_id: str) -> None:
        self._entrada(id, {"type": "pagina", "id": pagina_id})

    def cancelar(self, id: str) -> None:
        """Descarta la confirmación de aire pendiente."""
        self._entrada(id, {"type": "cancelar"})

    def pedir_estado(self, id: str) -> None:
        self._entrada(id, {"type": "hola"})

    def _saludar(self) -> None:
        for id in list(self._registradas):
            self.pedir_estado(id)

    # --- Dibujar una tecla -----------------------------------------------------------------

    @staticmethod
    def cara_animada(celda: dict | None) -> bool:
        """¿La celda tiene de cara un video corto? (hay que redibujarla cada ``1 / celda["cara"]["fps"]`` s)."""
        return bool(celda) and isinstance(celda.get("cara"), dict) and int(celda["cara"].get("cuadros", 1)) > 1

    def render_tecla(self, celda: dict | None, tamano: int = 72, fase_titilar: bool | None = None,
                     esperando: bool = False, cuadro: int | None = None):
        """Una imagen PIL (RGB, `tamano`×`tamano`) con el fondo, la cara, el texto ajustado y el borde de
        tally o de «activo». `fase_titilar`: si es ``False`` y la celda titila, se dibuja apagada (alterná
        cada medio segundo). `esperando`: borde amarillo (confirmación de aire pendiente). `cuadro`: el de
        una cara animada (por defecto, el de este momento)."""
        from PIL import Image, ImageDraw, ImageFont  # pillow viene en el Python del programa

        img = Image.new("RGB", (tamano, tamano), "#000000")
        if not celda:
            return img
        d = ImageDraw.Draw(img)
        fondo = _color(celda.get("fondo"), (43, 47, 58))
        if celda.get("titilar") and fase_titilar is False:
            fondo = tuple(int(c * 0.35) for c in fondo)
        d.rounded_rectangle((0, 0, tamano - 1, tamano - 1), radius=max(4, tamano // 10), fill=fondo)
        cara = _cara_propia(celda.get("cara"), tamano, Image, cuadro)
        if cara is None:
            cara = _cara(celda.get("imagen"), tamano, Image)
        if cara is not None:  # la cara apagada, para que se lea el texto encima
            img = Image.blend(img, cara, 0.6)
            d = ImageDraw.Draw(img)
        if celda.get("tipo") in ("fader", "perilla", "medidor") and celda.get("max", 0) != celda.get("min", 0):
            f = (float(celda.get("valor", 0)) - celda["min"]) / (celda["max"] - celda["min"])
            f = max(0.0, min(1.0, f))
            d.rectangle((tamano * 0.82, tamano * (1 - f), tamano - 3, tamano - 3), fill=(230, 230, 230))
        borde = celda.get("borde")
        if borde and not esperando and (not celda.get("borde_titilar") or fase_titilar is not False):
            d.rounded_rectangle((2, 2, tamano - 3, tamano - 3), radius=max(4, tamano // 10),
                                outline=_color(borde, (255, 255, 255)), width=max(4, tamano // 12))
        if esperando:
            d.rounded_rectangle((1, 1, tamano - 2, tamano - 2), radius=max(4, tamano // 10), outline=(255, 204, 0), width=max(3, tamano // 16))
        elif celda.get("activo"):
            d.rounded_rectangle((1, 1, tamano - 2, tamano - 2), radius=max(4, tamano // 10), outline=(255, 255, 255), width=max(2, tamano // 24))
        texto = ((celda.get("icono") or "") + "\n" if celda.get("icono") else "") + (celda.get("texto") or "")
        if texto:
            color = _color(celda.get("color"), (255, 255, 255))
            fuente, lineas = _ajustar(texto, tamano - 8, ImageFont, d)
            alto = sum(_alto(d, l, fuente) for l in lineas) + 2 * (len(lineas) - 1)
            y = (tamano - alto) / 2
            for l in lineas:
                w = d.textlength(l, font=fuente)
                d.text(((tamano - w) / 2, y), l, font=fuente, fill=color)
                y += _alto(d, l, fuente) + 2
        ins = celda.get("insignia")
        if ins:  # turno, «P»: arriba a la derecha
            fuente, _ = _ajustar(str(ins), tamano // 3, ImageFont, d)
            w = max(tamano // 5, d.textlength(str(ins), font=fuente) + 6)
            h = _alto(d, str(ins), fuente) + 4
            d.rounded_rectangle((tamano - w - 3, 3, tamano - 3, 3 + h), radius=h // 2, fill=(255, 179, 0))
            d.text((tamano - w - 3 + (w - d.textlength(str(ins), font=fuente)) / 2, 4), str(ins), font=fuente, fill=(0, 0, 0))
        if celda.get("atenuar"):  # sin unidad de la central, o sin central
            img = Image.blend(img, Image.new("RGB", img.size, (0, 0, 0)), 0.6)
        return img


_caras: dict = {}


def _cara(url: Any, tamano: int, Image) -> Any:
    """La cara de la celda (data: URL) del tamaño de la tecla, con caché."""
    if not isinstance(url, str) or not url.startswith("data:") or "," not in url:
        return None
    clave = (hash(url), tamano)
    if clave not in _caras:
        import base64
        import io
        try:
            im = Image.open(io.BytesIO(base64.b64decode(url.split(",", 1)[1]))).convert("RGB").resize((tamano, tamano))
        except Exception:  # noqa: BLE001
            im = None
        if len(_caras) > 200:
            _caras.clear()
        _caras[clave] = im
    return _caras[clave]


def _cara_propia(cara: Any, tamano: int, Image, cuadro: int | None) -> Any:
    """La cara propia (``Media/botonera/<id>/NNNN.jpg`` en los datos, 288 px) a la medida, con caché."""
    if not isinstance(cara, dict):
        return None
    import os
    import time
    id_ = str(cara.get("id", ""))
    raiz = os.environ.get("BA_DATA_ROOT", "")
    if not id_ or not raiz or "/" in id_ or "\\" in id_ or ".." in id_:
        return None
    n = max(1, int(cara.get("cuadros", 1)))
    fps = max(1, int(cara.get("fps", 8)))
    i = (int(time.time() * fps) % n) if cuadro is None else int(cuadro) % n
    clave = (id_, i, tamano)
    if clave not in _caras:
        try:
            im = Image.open(os.path.join(raiz, "Media", "botonera", id_, f"{i:04d}.jpg")).convert("RGB")
            im = im.resize((tamano, tamano), Image.LANCZOS)
        except Exception:  # noqa: BLE001
            im = None
        if len(_caras) > 800:
            _caras.clear()
        _caras[clave] = im
    return _caras[clave]


def _color(c: Any, siNo: tuple) -> tuple:
    if isinstance(c, str) and c.startswith("#") and len(c) in (4, 7):
        h = c[1:]
        if len(h) == 3:
            h = "".join(ch * 2 for ch in h)
        try:
            return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
        except ValueError:
            return siNo
    return siNo


def _alto(d, linea: str, fuente) -> int:
    caja = d.textbbox((0, 0), linea or " ", font=fuente)
    return caja[3] - caja[1]


def _fuente(ImageFont, tam: int):
    for nombre in ("DejaVuSans-Bold.ttf", "Arial Bold.ttf", "arialbd.ttf", "Helvetica.ttc"):
        try:
            return ImageFont.truetype(nombre, tam)
        except OSError:
            continue
    try:
        return ImageFont.load_default(size=tam)
    except TypeError:
        return ImageFont.load_default()


def _ajustar(texto: str, ancho: int, ImageFont, d):
    """El tamaño más grande con el que el texto entra (cortando por palabras)."""
    for tam in (22, 18, 15, 13, 11, 9):
        f = _fuente(ImageFont, tam)
        lineas: list[str] = []
        for parrafo in texto.split("\n"):
            actual = ""
            for palabra in parrafo.split(" "):
                prueba = (actual + " " + palabra).strip()
                if d.textlength(prueba, font=f) <= ancho or not actual:
                    actual = prueba
                else:
                    lineas.append(actual)
                    actual = palabra
            lineas.append(actual)
        if all(d.textlength(l, font=f) <= ancho for l in lineas) and len(lineas) * (tam + 2) <= ancho:
            return f, lineas
    return f, lineas
