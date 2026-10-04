#!/usr/bin/env python3
"""La base de fabricantes por MAC (OUI) para el escáner de red: datos/oui.tsv.

Sale del registro público de la IEEE, en la forma en que lo publica Wireshark
(`manuf`, se actualiza una vez por semana: no hace falta bajarlo más seguido).
Formato de salida, una línea por bloque: PREFIJO<TAB>BITS<TAB>FABRICANTE, con
el prefijo en hexadecimal sin separadores (24, 28 o 36 bits).

Uso: tools/oui-generar.py [manuf]   (sin argumento, lo baja)
"""
import sys
import urllib.request

URL = "https://www.wireshark.org/download/automated/data/manuf"


def main():
    if len(sys.argv) > 1:
        texto = open(sys.argv[1], encoding="utf-8").read()
    else:
        texto = urllib.request.urlopen(URL, timeout=120).read().decode("utf-8")
    filas = []
    for linea in texto.splitlines():
        if not linea or linea.startswith("#"):
            continue
        partes = linea.split("\t")
        if len(partes) < 2:
            continue
        bloque = partes[0].strip()
        nombre = (partes[2] if len(partes) > 2 else partes[1]).strip()
        bits = 24
        if "/" in bloque:
            bloque, b = bloque.split("/")
            bits = int(b)
        hexa = bloque.replace(":", "").replace("-", "").upper()
        hexa = hexa[: (bits + 3) // 4]
        filas.append(f"{hexa}\t{bits}\t{nombre}")
    sys.stdout.write("# Fabricantes por MAC (registro OUI de la IEEE, vía Wireshark manuf). tools/oui-generar.py\n")
    sys.stdout.write("\n".join(filas) + "\n")


if __name__ == "__main__":
    main()
