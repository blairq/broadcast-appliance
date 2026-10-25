"""Ejemplo: tarjetas de video con GStreamer escritas en Python.

Cómo se usa (el asistente lo hace solo; a mano, en la página Plugins):
  1. Activarlo.
  2. «Usar» → poner la tarjeta en una entrada (o `plugin_tarjeta {"id": "ejemplo-video-gst",
     "tarjeta": "efecto", "entrada": "Cámara 1"}`). Queda en el grafo de video como una tarjeta
     «Plugin · efecto» (el módulo `baplugin`), en la cadena de esa entrada.
  3. Graduarla: los parámetros de la tarjeta (página Video, o `video_tarjeta_ajustar`).

Las tres tarjetas muestran los tres casos típicos:
  - «vineta»: un ELEMENTO de GStreamer escrito acá (`bavineta`, un GstBase.BaseTransform de
    verdad) que oscurece los bordes. Es la forma de programar un efecto propio.
  - «efecto»: el cuadro pasa por CUALQUIER cadena de módulos de GStreamer, los que trae el
    programa (`video_modulos` los lista: videobalance, gamma, coloreffects, edgetv…) y los
    propios. El parámetro `cadena` es como en gst-launch.
  - «medir»: un ANÁLISIS. No cambia la imagen: mide brillo y movimiento sobre una versión chica
    cada pocos cuadros, lo muestra en el panel y avisa con el evento «movimiento» (otro plugin,
    un disparador o el asistente lo pueden escuchar).

Todo corre en el proceso del plugin: si algo se cuelga, la tarjeta deja pasar la imagen y la
emisión sigue. Cada tarjeta tiene que tardar menos que un cuadro (33 ms a 30 fps): numpy
vectorizado, nada de bucles por píxel. `plugin_probar` mide cuánto tarda cada una.
"""
import threading
import time

import numpy as np

import ba_plugin as ba
from ba_plugin import gst as bagst

plugin = ba.Plugin()


# --- 1) Un elemento de GStreamer escrito en Python ------------------------------------------
# `a` es el cuadro (alto, ancho, 4) RGBA SIN copiar; se cambia en el lugar. `props` son sus
# propiedades (se ponen en la cadena: «bavineta fuerza=0.8 radio=0.9»).
_mascaras: dict = {}


def _mascara(alto: int, ancho: int, fuerza: float, radio: float) -> np.ndarray:
    """La máscara de la viñeta (x256, en uint16), calculada una vez por tamaño y ajuste."""
    clave = (alto, ancho, round(fuerza, 3), round(radio, 3))
    m = _mascaras.get(clave)
    if m is None:
        yy, xx = np.ogrid[-1:1:alto * 1j, -1:1:ancho * 1j]
        r = np.sqrt(xx * xx + yy * yy) / max(radio, 0.05)
        luz = np.clip(1.0 - fuerza * np.clip(r - 0.5, 0, None) ** 2 * 2.0, 0.0, 1.0)
        m = np.repeat((luz * 256).astype(np.uint16)[..., None], 4, axis=2)
        m[..., 3] = 256  # el alfa no se toca
        if len(_mascaras) > 8:
            _mascaras.clear()
        _mascaras[clave] = m
    return m


@bagst.elemento("bavineta", propiedades={"fuerza": 0.6, "radio": 1.0},
                descripcion="Oscurece los bordes del cuadro (viñeta)")
def vineta(a, props):
    alto, ancho = a.shape[:2]
    m = _mascara(alto, ancho, float(props["fuerza"]), float(props["radio"]))
    # Todo en uint16 y contiguo: ~2 ms en 1080p (con a[..., :3] y floats eran 20).
    tmp = np.multiply(a, m, dtype=np.uint16)
    np.right_shift(tmp, 8, out=tmp)
    np.copyto(a, tmp, casting="unsafe")


# --- 2) Tarjetas del grafo -------------------------------------------------------------------

@plugin.tarjeta("vineta", descripcion="Oscurece los bordes (un elemento de GStreamer hecho en Python)",
                parametros={"fuerza": 0.6, "radio": 1.0})
def tarjeta_vineta(cuadro, params):
    c = bagst.cadena("bavineta name=v")
    c.procesar(cuadro)  # la primera vez arma el pipeline; después solo pasa cuadros
    # Los parámetros se cambian en vivo, sin rearmar la cadena (vale desde el cuadro que viene).
    c.poner("v", fuerza=float(params.get("fuerza", 0.6)), radio=float(params.get("radio", 1.0)))


@plugin.tarjeta("efecto", descripcion="Pasa el cuadro por una cadena de módulos de GStreamer (`cadena`, como en gst-launch)",
                parametros={"cadena": "bavineta fuerza=0.5 ! videobalance saturation=0.6 contrast=1.1"})
def tarjeta_efecto(cuadro, params):
    bagst.cadena(str(params.get("cadena") or "identity")).procesar(cuadro)


# --- 3) Un análisis: medir sin tocar la imagen ------------------------------------------------
# Lo que recuerda entre cuadros, por hilo: cada tarjeta puesta (una por entrada) tiene el suyo.
_medicion = threading.local()


@plugin.tarjeta("medir", descripcion="Mide brillo y movimiento (no cambia la imagen) y avisa con el evento «movimiento»",
                parametros={"cada": 3})
def tarjeta_medir(cuadro, params):
    m = _medicion
    if not hasattr(m, "n"):
        m.n, m.gris, m.moviendo, m.aviso = 0, None, False, 0.0
    m.n += 1
    if m.n % max(1, int(params.get("cada", 3))):
        return
    # Una versión chica (1 de cada 8 píxeles por lado) alcanza para medir y cuesta casi nada.
    a = cuadro.array()[::8, ::8, :3]
    gris = a.mean(axis=2, dtype=np.float32)
    brillo = float(gris.mean()) / 255
    prev = m.gris
    mov = float(np.abs(gris - prev).mean()) / 255 if prev is not None and prev.shape == gris.shape else 0.0
    m.gris = gris
    plugin.mostrar("brillo", round(brillo, 3))
    plugin.mostrar("movimiento", round(mov, 4))
    umbral = float(plugin.valores.get("umbral", 0.02))
    ahora = time.monotonic()
    # Con histéresis y como mucho un aviso por segundo: un evento por cuadro inundaría el bus.
    if not m.moviendo and mov > umbral and ahora - m.aviso > 1.0:
        m.moviendo, m.aviso = True, ahora
        plugin.evento("movimiento", {"hay": True, "cuanto": round(mov, 4), "brillo": round(brillo, 3)})
    elif m.moviendo and mov < umbral / 2:
        m.moviendo = False
        plugin.evento("movimiento", {"hay": False, "cuanto": round(mov, 4), "brillo": round(brillo, 3)})


# --- Panel del operador ---------------------------------------------------------------------
plugin.panel([
    ba.seccion("Análisis (tarjeta «medir»)"),
    ba.medidor("brillo", "Brillo", 0, 1),
    ba.indicador("movimiento", "Movimiento"),
    ba.deslizador("umbral", "Avisar movimiento desde", 0.002, 0.1, defecto=0.02,
                  ayuda="Cuánto tiene que cambiar la imagen para el evento «movimiento»"),
])

plugin.estado("listo: poné «vineta», «efecto» o «medir» en una entrada")
plugin.correr()
