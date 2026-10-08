"""Dibuja el recinto de la demo parlamentaria (assets/demo): un hemiciclo con sus bancas
numeradas, la presidencia y un cartel. Es la «cámara» de la demo: el PTZ digital encuadra
cada banca. Todo inventado (sin marcas ni nombres reales).

    python3 tools/demo/recinto.py <salida.png> <bancas.json>
"""
import json
import math
import sys

from PIL import Image, ImageDraw, ImageFont

W, H = 3840, 2160


def fuente(t):
    for n in ("/System/Library/Fonts/Supplemental/Arial Bold.ttf", "DejaVuSans-Bold.ttf", "arialbd.ttf"):
        try:
            return ImageFont.truetype(n, t)
        except OSError:
            continue
    return ImageFont.load_default()


def main(salida, archivo_bancas):
    bancas = json.load(open(archivo_bancas, encoding="utf-8"))
    im = Image.new("RGB", (W, H), (32, 36, 44))
    d = ImageDraw.Draw(im)
    # Piso y fondo.
    d.rectangle((0, int(H * 0.55), W, H), fill=(58, 46, 40))
    d.rectangle((0, 0, W, int(H * 0.14)), fill=(24, 27, 34))
    d.text((W // 2, int(H * 0.07)), "CONSEJO DE LA CIUDAD DE PUERTO EJEMPLO", font=fuente(96), fill=(230, 200, 120), anchor="mm")
    # Presidencia, abajo al centro.
    px, py = W // 2, int(H * 0.86)
    d.rounded_rectangle((px - 420, py - 120, px + 420, py + 120), 30, fill=(110, 30, 40), outline=(230, 200, 120), width=8)
    d.text((px, py - 30), "PRESIDENCIA", font=fuente(70), fill=(255, 255, 255), anchor="mm")
    d.text((px, py + 50), "Pres. " + bancas[0]["presidencia"], font=fuente(44), fill=(235, 225, 210), anchor="mm")
    # El hemiciclo: dos arcos alrededor de la presidencia.
    filas = [bancas[1:7], bancas[7:]]
    centros = []
    for k, fila in enumerate(filas):
        radio = 1050 + k * 520
        n = len(fila)
        for i, b in enumerate(fila):
            ang = math.radians(200 + (140 * (i + 0.5) / n))
            x = px + radio * math.cos(ang)
            y = py + 80 + radio * 0.62 * math.sin(ang)
            bw, bh = 300, 190
            color = {"Frente Ciudadano": (40, 90, 160), "Unión Vecinal": (40, 130, 80),
                     "Alianza del Puerto": (150, 100, 30), "Independientes": (100, 80, 140)}.get(b["bloque"], (90, 90, 90))
            d.rounded_rectangle((x - bw / 2, y - bh / 2, x + bw / 2, y + bh / 2), 24, fill=color, outline=(240, 240, 240), width=5)
            d.text((x, y - 45), str(b["banca"]), font=fuente(84), fill=(255, 255, 255), anchor="mm")
            d.text((x, y + 40), b["nombre"].split()[0] + " " + b["nombre"].split()[-1], font=fuente(30), fill=(255, 255, 255), anchor="mm")
            d.text((x, y + 75), b["bloque"], font=fuente(22), fill=(225, 225, 225), anchor="mm")
            centros.append({"banca": b["banca"], "x": round(x / W, 4), "y": round(y / H, 4)})
    im.save(salida)
    print(json.dumps(centros))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
