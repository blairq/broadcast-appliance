#!/usr/bin/env python3
"""Genera el logo y los íconos del programa en todos los formatos que se usan.

El logo es una **B** (de Broadcast) con el punto rojo del tally —la luz de «al
aire» de una cámara— en su panza de arriba, y dos ondas de señal que salen de
él: un aparato que transmite. Los colores son los de la interfaz (Theme.qml):
el naranja del acento, el rojo de «en vivo», el ámbar y el casi negro de fondo.

Hay dos dibujos de la misma geometría:
  - el COMPLETO, con las ondas, de 48 px para arriba;
  - el CHICO, sin ondas y con la B centrada, para 16-32 px (favicon, barra de
    tareas): a ese tamaño las ondas son ruido y la B se achica al pedo.

Todo sale de acá, de UNA sola geometría: los PNG, el .ico de Windows, el .icns
de macOS, y los SVG (que se escriben con los mismos números, no se dibujan
aparte). Lo generado se versiona: ni el build ni CI necesitan Pillow.

    .venv/bin/pip install Pillow     # una vez, en el venv del proyecto
    .venv/bin/python tools/generar/icono.py  # reescribe assets/icons y assets/web/comun

El logo con el nombre (logo.png, para el README) usa Carlito Bold, de las
tipografías de texto del bundle (make deps-fuentes-texto); si no está, se saltea.
"""

from __future__ import annotations

import math
import pathlib
import re
import sys
import urllib.parse

try:
    from PIL import Image, ImageDraw, ImageFilter, ImageFont
except ImportError:
    sys.exit("Falta Pillow: .venv/bin/pip install Pillow (solo para regenerar el logo).")

RAIZ = pathlib.Path(__file__).resolve().parents[2]
ICONOS = RAIZ / "assets" / "icons"
WEB = RAIZ / "assets" / "web"
N = 1024  # se dibuja grande y se achica: los bordes salen suaves

# Los de src/app/qml/Theme.qml.
FONDO_ARRIBA = (32, 32, 37)
FONDO_ABAJO = (10, 10, 12)
ACENTO = (255, 106, 43)       # accent
ACENTO_CLARO = (255, 164, 94)  # entre accentSoft y ambar: arriba del degradé
AMBAR = (245, 184, 61)         # amber
EN_VIVO = (255, 59, 48)        # live

# --- Geometría (sobre un lienzo de 1024) ---------------------------------------
MARGEN = 32          # del cuadrado redondeado al borde
RADIO_FONDO = 220
TRAZO = 88           # grosor de la B
ARRIBA, ABAJO, MEDIO = 214, 810, 526
LARGO_PANZA_1, LARGO_PANZA_2 = 150, 185  # recta de cada panza antes de la curva
ONDAS = ((300, 1.0), (378, 0.59))        # radio y opacidad de cada onda
ONDA_DESDE, ONDA_HASTA, ONDA_GROSOR = -60, -20, 36


def geometria(chico: bool) -> dict:
    """Todos los números del dibujo, para Pillow y para el SVG."""
    asta = 324 if not chico else 340  # la B va un poco a la izquierda para dejar lugar a las ondas
    y0 = ARRIBA + TRAZO / 2
    y2 = ABAJO - TRAZO / 2
    panzas = []
    for (ya, yb), largo in (((y0, MEDIO), LARGO_PANZA_1), ((MEDIO, y2), LARGO_PANZA_2)):
        panzas.append({"ya": ya, "yb": yb, "cx": asta + largo, "cy": (ya + yb) / 2, "rc": (yb - ya) / 2})
    p = panzas[0]
    hueco = p["rc"] - TRAZO / 2
    izq, der = asta + TRAZO / 2, p["cx"] + hueco
    tally = {"x": (izq + der) / 2, "y": p["cy"], "r": min(hueco, (der - izq) / 2) - 30}
    return {"asta": asta, "panzas": panzas, "tally": tally, "ondas": not chico}


def degrade(c1, c2) -> Image.Image:
    g = Image.new("RGBA", (N, N))
    d = ImageDraw.Draw(g)
    for y in range(N):
        t = y / N
        d.line([(0, y), (N, y)], fill=tuple(round(c1[i] * (1 - t) + c2[i] * t) for i in range(3)) + (255,))
    return g


def dibujar(chico: bool = False, con_fondo: bool = True) -> Image.Image:
    g = geometria(chico)
    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    if con_fondo:
        mascara = Image.new("L", (N, N), 0)
        caja = [MARGEN, MARGEN, N - MARGEN, N - MARGEN]
        ImageDraw.Draw(mascara).rounded_rectangle(caja, radius=RADIO_FONDO, fill=255)
        img.paste(degrade(FONDO_ARRIBA, FONDO_ABAJO), (0, 0), mascara)
        ImageDraw.Draw(img).rounded_rectangle(caja, radius=RADIO_FONDO, outline=(255, 255, 255, 30), width=6)

    # La B: el asta (con las puntas redondas) y las dos panzas.
    b = Image.new("L", (N, N), 0)
    d = ImageDraw.Draw(b)
    x = g["asta"]
    d.rounded_rectangle([x - TRAZO // 2, ARRIBA, x + TRAZO // 2, ABAJO], radius=TRAZO // 2, fill=255)
    for p in g["panzas"]:
        # La recta se mete unos px en la curva: si terminan justo en el mismo
        # punto, el suavizado de las dos deja una muesca en la union.
        d.rectangle([x, p["ya"] - TRAZO / 2, p["cx"] + 6, p["ya"] + TRAZO / 2], fill=255)
        d.rectangle([x, p["yb"] - TRAZO / 2, p["cx"] + 6, p["yb"] + TRAZO / 2], fill=255)
        r = p["rc"] + TRAZO / 2
        d.arc([p["cx"] - r, p["cy"] - r, p["cx"] + r, p["cy"] + r], -90, 90, fill=255, width=TRAZO)
    img.paste(degrade(ACENTO_CLARO, ACENTO), (0, 0), b)

    # El tally, con su brillo.
    t = g["tally"]
    brillo = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    rb = t["r"] * 1.25
    ImageDraw.Draw(brillo).ellipse([t["x"] - rb, t["y"] - rb, t["x"] + rb, t["y"] + rb], fill=EN_VIVO + (120,))
    img.alpha_composite(brillo.filter(ImageFilter.GaussianBlur(t["r"] * 0.35)))
    d = ImageDraw.Draw(img)
    d.ellipse([t["x"] - t["r"], t["y"] - t["r"], t["x"] + t["r"], t["y"] + t["r"]], fill=EN_VIVO)

    if g["ondas"]:
        for radio, opacidad in ONDAS:
            caja = [t["x"] - radio, t["y"] - radio, t["x"] + radio, t["y"] + radio]
            d.arc(caja, ONDA_DESDE, ONDA_HASTA, fill=AMBAR + (round(255 * opacidad),), width=ONDA_GROSOR)
    return img


# --- SVG: los mismos números ---------------------------------------------------------

def _hex(c) -> str:
    return "#%02x%02x%02x" % c


def svg(chico: bool = False, con_fondo: bool = True) -> str:
    g = geometria(chico)
    x = g["asta"]
    partes = [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {N} {N}" width="{N}" height="{N}">',
        "<defs>",
        f'<linearGradient id="f" x1="0" y1="0" x2="0" y2="{N}" gradientUnits="userSpaceOnUse">'
        f'<stop offset="0" stop-color="{_hex(FONDO_ARRIBA)}"/><stop offset="1" stop-color="{_hex(FONDO_ABAJO)}"/>'
        "</linearGradient>",
        f'<linearGradient id="b" x1="0" y1="0" x2="0" y2="{N}" gradientUnits="userSpaceOnUse">'
        f'<stop offset="0" stop-color="{_hex(ACENTO_CLARO)}"/><stop offset="1" stop-color="{_hex(ACENTO)}"/>'
        "</linearGradient>",
        '<filter id="g" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="16"/></filter>',
        "</defs>",
    ]
    if con_fondo:
        lado = N - 2 * MARGEN
        partes.append(
            f'<rect x="{MARGEN}" y="{MARGEN}" width="{lado}" height="{lado}" rx="{RADIO_FONDO}" fill="url(#f)"'
            ' stroke="#ffffff" stroke-opacity="0.12" stroke-width="6"/>'
        )
    # Asta con puntas redondas y panzas con puntas rectas (quedan tapadas por el asta).
    trazo = f'fill="none" stroke="url(#b)" stroke-width="{TRAZO}"'
    partes.append(
        f'<line x1="{x}" y1="{ARRIBA + TRAZO / 2:g}" x2="{x}" y2="{ABAJO - TRAZO / 2:g}" {trazo} stroke-linecap="round"/>'
    )
    for p in g["panzas"]:
        partes.append(
            f'<path d="M{x} {p["ya"]:g}H{p["cx"]:g}A{p["rc"]:g} {p["rc"]:g} 0 0 1 {p["cx"]:g} {p["yb"]:g}H{x}" {trazo}/>'
        )
    t = g["tally"]
    partes.append(
        f'<circle cx="{t["x"]:g}" cy="{t["y"]:g}" r="{t["r"] * 1.25:g}" fill="{_hex(EN_VIVO)}" opacity="0.47" filter="url(#g)"/>'
    )
    partes.append(f'<circle cx="{t["x"]:g}" cy="{t["y"]:g}" r="{t["r"]:g}" fill="{_hex(EN_VIVO)}"/>')
    if g["ondas"]:
        for radio, opacidad in ONDAS:
            a0, a1 = math.radians(ONDA_DESDE), math.radians(ONDA_HASTA)
            x0, y0 = t["x"] + radio * math.cos(a0), t["y"] + radio * math.sin(a0)
            x1, y1 = t["x"] + radio * math.cos(a1), t["y"] + radio * math.sin(a1)
            partes.append(
                f'<path d="M{x0:.1f} {y0:.1f}A{radio} {radio} 0 0 1 {x1:.1f} {y1:.1f}" fill="none"'
                f' stroke="{_hex(AMBAR)}" stroke-opacity="{opacidad:g}" stroke-width="{ONDA_GROSOR}"/>'
            )
    partes.append("</svg>")
    return "\n".join(partes) + "\n"


def svg_con_nombre() -> str:
    """El logo con el nombre al lado (README, documentación)."""
    marca = svg(chico=False, con_fondo=True).split("\n", 1)[1].rsplit("</svg>", 1)[0]
    return (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 3400 1024" width="1700" height="512">\n'
        f"{marca}"
        '<text x="1130" y="470" font-family="Inter, \'Segoe UI\', Helvetica, Arial, sans-serif" font-size="300"'
        ' font-weight="700" fill="#f4f4f6">Broadcast</text>\n'
        '<text x="1134" y="800" font-family="Inter, \'Segoe UI\', Helvetica, Arial, sans-serif" font-size="300"'
        f' font-weight="700" fill="{_hex(ACENTO)}">Appliance</text>\n'
        "</svg>\n"
    )


def logo_con_nombre(completo: Image.Image, texto=(244, 244, 246)) -> Image.Image | None:
    fuente = RAIZ / "extern" / "fuentes-texto" / "Carlito-Bold.ttf"
    if not fuente.exists():
        return None
    ancho, alto = 1700, 512
    img = Image.new("RGBA", (ancho, alto), (0, 0, 0, 0))
    img.alpha_composite(completo.resize((alto, alto), Image.LANCZOS), (0, 0))
    f = ImageFont.truetype(str(fuente), 170)
    d = ImageDraw.Draw(img)
    d.text((560, 80), "Broadcast", font=f, fill=texto + (255,))
    d.text((562, 260), "Appliance", font=f, fill=ACENTO + (255,))
    return img


def favicon_embebido(chico_svg: str) -> str:
    """El <link> del favicon con el SVG adentro: anda en cualquier servidor
    (la consola del operador y la sala se sirven desde lugares distintos)."""
    # Comillas simples adentro (las dobles cierran el href) y el # codificado
    # (en una URL data: empieza un fragmento y corta el SVG en url(#b)).
    cuerpo = " ".join(chico_svg.split()).replace('"', "'")
    return '<link rel="icon" type="image/svg+xml" href="data:image/svg+xml,' + urllib.parse.quote(
        cuerpo, safe=" =:/'") + '">'



def main() -> int:
    ICONOS.mkdir(parents=True, exist_ok=True)
    completo = dibujar(chico=False)
    chico = dibujar(chico=True)

    def tam(lado: int) -> Image.Image:
        return (chico if lado <= 32 else completo).resize((lado, lado), Image.LANCZOS)

    for lado in (16, 24, 32, 48, 64, 128, 256, 512, 1024):
        tam(lado).save(ICONOS / f"broadcast-appliance-{lado}.png")
    tam(256).save(ICONOS / "broadcast-appliance.png")

    # .ico: varios tamaños adentro (16 en la barra de tareas, 256 en iconos
    # grandes), cada uno con su dibujo.
    lados_ico = (16, 24, 32, 48, 64, 128, 256)
    tam(256).save(ICONOS / "broadcast-appliance.ico", sizes=[(l, l) for l in lados_ico],
                  append_images=[tam(l) for l in lados_ico])
    # .icns (macOS): lo escribe Pillow, no hace falta iconutil ni una Mac.
    completo.save(ICONOS / "broadcast-appliance.icns", append_images=[tam(l) for l in (16, 32, 64, 128, 256, 512)])

    (ICONOS / "broadcast-appliance.svg").write_text(svg(chico=False))
    (ICONOS / "broadcast-appliance-chico.svg").write_text(svg(chico=True))
    (ICONOS / "logo-marca.svg").write_text(svg(chico=False, con_fondo=False))
    dibujar(chico=False, con_fondo=False).resize((512, 512), Image.LANCZOS).save(ICONOS / "logo-marca.png")
    (ICONOS / "logo.svg").write_text(svg_con_nombre())
    con_nombre = logo_con_nombre(completo)
    if con_nombre is not None:
        # logo.png para fondos oscuros; logo-claro.png (texto oscuro) para los
        # claros, como el tema claro de GitHub.
        con_nombre.save(ICONOS / "logo.png")
        logo_con_nombre(completo, texto=(27, 27, 31)).save(ICONOS / "logo-claro.png")
    else:
        print("aviso: sin extern/fuentes-texto/Carlito-Bold.ttf no se genera logo.png")

    # Web: el ícono de «agregar a la pantalla de inicio» y el favicon.
    comun = WEB / "comun"
    comun.mkdir(parents=True, exist_ok=True)
    tam(180).save(comun / "icono-180.png")
    tam(512).save(comun / "icono-512.png")
    (comun / "icono.svg").write_text(svg(chico=True))
    # Y el favicon, embebido en cada pagina: se reemplaza el que haya.
    link = favicon_embebido(svg(chico=True))
    for pagina in sorted(WEB.glob("*/index.html")):
        html = pagina.read_text()
        nuevo = re.sub(r'<link rel="icon" type="image/svg\+xml" href="data:[^"]*">', lambda _m: link, html)
        if nuevo != html:
            pagina.write_text(nuevo)
    print(f"logo e íconos escritos en {ICONOS} y {comun}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
