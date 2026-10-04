"""Ejemplo básico: tarjeta de video, filtro de audio, entrada propia, panel, control y evento."""
import math
import time

import numpy as np

import ba_plugin as ba

plugin = ba.Plugin()


# 1) Una TARJETA del grafo de video: recibe cada cuadro RGBA y lo cambia en el lugar.
@plugin.tarjeta("negativo", descripcion="Invierte los colores (con `fuerza` 0..1)", parametros={"fuerza": 1.0})
def negativo(cuadro, params):
    a = cuadro.array()                      # (alto, ancho, 4) uint8, sin copiar
    f = float(params.get("fuerza", 1.0))
    rgb = a[..., :3]
    if f >= 0.999:
        np.subtract(255, rgb, out=rgb)
    else:
        rgb[:] = (rgb * (1 - f) + (255 - rgb.astype(np.float32)) * f).astype(np.uint8)


# 2) Un FILTRO DE AUDIO: bloques float32 (muestras, canales).
@plugin.tarjeta("ganancia", medio="audio", descripcion="Sube o baja el volumen (`db`)", parametros={"db": -6.0})
def ganancia(bloque, params):
    bloque.array()[:] *= 10 ** (float(params.get("db", 0.0)) / 20)


# 3) Una ENTRADA hecha en Python: video y audio.
pelota = plugin.entrada("Pelota", video=True, audio=True)


@plugin.al_iniciar
def generar_video():
    ancho, alto, fps = 640, 360, 30
    x, y, vx, vy = 100.0, 100.0, 7.0, 5.0
    rebotes = 0
    fondo = np.zeros((alto, ancho, 4), np.uint8)
    fondo[..., 3] = 255
    fondo[..., 2] = np.linspace(40, 120, ancho, dtype=np.uint8)[None, :]
    yy, xx = np.mgrid[0:alto, 0:ancho]
    siguiente = time.monotonic()
    while plugin.andando:
        if plugin.valores.get("pausa"):
            time.sleep(0.05)
            siguiente = time.monotonic()
            continue
        v = float(plugin.valores.get("velocidad", 1.0))
        x, y = x + vx * v, y + vy * v
        if not 30 < x < ancho - 30:
            vx = -vx
            rebotes += 1
            plugin.evento("rebote", {"borde": "costado", "x": round(x)})
        if not 30 < y < alto - 30:
            vy = -vy
            rebotes += 1
        plugin.mostrar("rebotes", rebotes)
        plugin.mostrar("altura", 1 - y / alto)
        r = float(plugin.valores.get("tamano", 30))
        cuadro = fondo.copy()
        cuadro[(xx - x) ** 2 + (yy - y) ** 2 < r * r, :3] = plugin.config.get("color", [255, 200, 0])
        pelota.enviar_video(cuadro, fps=fps)
        siguiente += 1 / fps
        time.sleep(max(0, siguiente - time.monotonic()))


@plugin.al_iniciar
def generar_audio():
    tasa, n, fase = 48000, 480, 0.0  # bloques de 10 ms
    siguiente = time.monotonic()
    while plugin.andando:
        t = (np.arange(n) + fase) / tasa
        tono = (0.1 * np.sin(2 * np.pi * 440 * t)).astype(np.float32)
        pelota.enviar_audio(np.stack([tono, tono], axis=1), tasa=tasa, canales=2)
        fase += n
        siguiente += n / tasa
        time.sleep(max(0, siguiente - time.monotonic()))


# 4) Un PANEL para el operador (lo dibuja la app; los valores se guardan solos).
COLORES = {"amarillo": [255, 200, 0], "rojo": [230, 40, 40], "verde": [40, 200, 90], "blanco": [255, 255, 255]}


def avisar():
    plugin.aviso("Hola desde el panel de la pelota")


plugin.panel([
    ba.seccion("Movimiento"),
    ba.interruptor("pausa", "Pausa"),
    ba.deslizador("velocidad", "Velocidad", 0.0, 3.0, defecto=1.0, unidad="x"),
    ba.deslizador("tamano", "Tamaño", 10, 80, paso=1, defecto=30, unidad="px"),
    ba.seccion("Color"),
    ba.opcion("color", "Color", list(COLORES), defecto="amarillo"),
    ba.boton("avisar", "Avisar", ayuda="Manda un aviso a la barra del operador", al_tocar=avisar),
    ba.seccion("Lo que pasa"),
    ba.indicador("rebotes", "Rebotes"),
    ba.medidor("altura", "Altura", 0, 1),
])


@plugin.al_cambiar("color")
def al_color(nombre):
    plugin.config["color"] = COLORES.get(nombre, [255, 200, 0])


# 5) Un CONTROL (lo pide el asistente con plugin_control o la interfaz) y un EVENTO.
@plugin.control("color", descripcion="Cambia el color de la pelota", parametros={"rgb": "[r, g, b] de 0 a 255"})
def cambiar_color(args):
    plugin.config["color"] = [int(v) for v in args.get("rgb", [255, 200, 0])][:3]
    plugin.evento("color", plugin.config["color"])
    return {"color": plugin.config["color"]}


plugin.estado("generando la pelota")
plugin.correr()
