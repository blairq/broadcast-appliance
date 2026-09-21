#!/usr/bin/env python3
"""Genera el ícono del programa en los formatos que pide cada sistema.

El dibujo es el mismo que `assets/icons/broadcast-appliance.svg`, hecho a mano
acá con Pillow porque en esta máquina no hay rasterizador de SVG y no queremos
sumar una dependencia del sistema para algo que se corre una vez cada tanto.

    tools/icono.py          # reescribe los PNG y el .ico en assets/icons/

Lo generado se versiona: ni el build ni CI necesitan Pillow.
"""

from __future__ import annotations

import pathlib
import sys

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("Falta Pillow: pip install Pillow (solo para regenerar el ícono).")

SALIDA = pathlib.Path(__file__).resolve().parent.parent / "assets" / "icons"
BASE = 1024  # se dibuja grande y se achica: los bordes salen suaves

CHASIS = (21, 25, 28)
BORDE = (44, 52, 56)
PANTALLA = (233, 237, 238)
MARCO = (57, 67, 74)
TALLY = (224, 72, 58)
BARRA = (61, 71, 77)
BARRA_VIVA = (79, 191, 174)


def esc(v: float) -> int:
    """De las coordenadas del SVG (256) a las del lienzo grande."""
    return round(v * BASE / 256)


def dibujar() -> Image.Image:
    img = Image.new("RGBA", (BASE, BASE), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    d.rounded_rectangle([esc(8), esc(8), esc(248), esc(248)], radius=esc(52),
                        fill=CHASIS, outline=BORDE, width=esc(1))
    d.rounded_rectangle([esc(48), esc(68), esc(208), esc(162)], radius=esc(10),
                        fill=PANTALLA, outline=MARCO, width=esc(2))

    # El tally se recorta contra el chasis para que se despegue de la pantalla.
    d.ellipse([esc(196 - 17), esc(80 - 17), esc(196 + 17), esc(80 + 17)], fill=CHASIS)
    d.ellipse([esc(196 - 11), esc(80 - 11), esc(196 + 11), esc(80 + 11)], fill=TALLY)

    for x, y, alto, color in ((80, 186, 30, BARRA), (119, 176, 40, BARRA_VIVA), (158, 192, 24, BARRA)):
        d.rounded_rectangle([esc(x), esc(y), esc(x + 18), esc(y + alto)], radius=esc(4), fill=color)
    return img


def main() -> int:
    SALIDA.mkdir(parents=True, exist_ok=True)
    grande = dibujar()

    for lado in (16, 32, 48, 64, 128, 256, 512):
        chico = grande.resize((lado, lado), Image.LANCZOS)
        chico.save(SALIDA / f"broadcast-appliance-{lado}.png")
    grande.resize((256, 256), Image.LANCZOS).save(SALIDA / "broadcast-appliance.png")

    # El .ico lleva varios tamaños adentro: Windows elige el que necesita
    # (16 en la barra de tareas, 256 en la vista de iconos grandes).
    grande.resize((256, 256), Image.LANCZOS).save(
        SALIDA / "broadcast-appliance.ico",
        sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)],
    )
    print(f"ícono escrito en {SALIDA}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
