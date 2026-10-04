"""Ejemplo: una cámara USB (UVC) con PTZ.

La imagen la toma la app con GStreamer (la entrada `gst`: sin copias, como cualquier
cámara). El PTZ va por USB con los controles UVC: los ESTÁNDAR (pan/tilt/zoom de la
Terminal de Cámara) o, si la marca los esconde, los de sus UNIDADES DE EXTENSIÓN (XU),
que se descubren con el control «explorar».
"""
import threading

import ba_plugin as ba
from ba_plugin import dispositivos as disp

plugin = ba.Plugin()
cfg = plugin.config
nombre = cfg.get("entrada", "Cámara USB")

# La imagen: el pipeline que corre la app (dispositivos_listar da el device-index).
plugin.entrada(nombre, gst=cfg.get("video", "avfvideosrc device-index=0"))

uvc = None
lock = threading.Lock()


def camara():
    """Abre la cámara por USB la primera vez (y si se desenchufó, la vuelve a buscar)."""
    global uvc
    with lock:
        if uvc is None:
            uvc = disp.UVC.abrir(cfg.get("vid", "046d"), cfg.get("pid"))
            plugin.estado(f"PTZ por USB: {uvc.info().get('rangos') or 'sin rangos'}")
        return uvc


@plugin.ptz(nombre)
class PtzUvc:
    capacidades = {"panTilt": True, "zoom": True, "home": True, "presets": 0}

    def mover(self, pan, tilt, zoom):
        v = float(cfg.get("velocidad", 0.6))
        camara().mover(pan * v, tilt * v, zoom * v)

    def parar(self):
        camara().parar()

    def home(self):
        camara().ir_a(pan=0, tilt=0)


@plugin.control("explorar", descripcion="Lo que expone la cámara por USB: terminal de cámara, rangos y "
                                        "unidades de extensión con los selectores que contestan")
def explorar(args):
    c = camara()
    info = c.info()
    info["extensiones_detalle"] = {str(u["unidad"]): c.xu_explorar(u["unidad"], int(args.get("hasta", 24)))
                                   for u in c.unidades()}
    return info


@plugin.control("usb", descripcion="Los equipos USB conectados (vid, pid, marca, si son UVC)")
def usb(args):
    return disp.usb_listar()


@plugin.al_iniciar
def arrancar():
    try:
        camara()
    except Exception as e:  # sin la cámara igual corre: la imagen la maneja la app
        plugin.estado(f"sin PTZ por USB: {e}", ok=False)


@plugin.al_terminar
def soltar():
    if uvc is not None:
        try:
            uvc.parar()
        except Exception:
            pass


plugin.correr()
