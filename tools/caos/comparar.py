#!/usr/bin/env python3
"""Compara dos informes del banco de caos (F4-03): ¿empeoró algo desde la corrida anterior?

Lo usa `banco.py --nocturno` al terminar: busca el informe nocturno anterior en
Logs/caos/<fecha>/caos.json y compara escenario por escenario. Es regresión:

  - «no volvió»: alguna repetición no volvió dentro del plazo (con o sin corrida anterior);
  - el RTO p95 de un escenario subió más que la tolerancia: más de `tolerancia` (25 %) Y
    más de `margen_s` (1 s) sobre el de antes (los dos: un escenario de 0,4 s que pasa a
    0,6 s es ruido, uno de 20 s que pasa a 21 s también);
  - un escenario que antes volvía y ahora no tiene RTO (no midió: algo se rompió).

Lo que ahora se saltea (no corre cloudflared en esta máquina) no cuenta. Puro: sin la app,
solo los JSON (se prueba con informes falsos en tests/test_caos_comparar.py).

    python3 -I tools/caos/comparar.py <anterior/caos.json> <actual/caos.json>

Sale con 0 si no hay regresiones y 3 si las hay (el mismo código que el banco).
"""
from __future__ import annotations

import json
import os
import sys

TOLERANCIA = 0.25
MARGEN_S = 1.0
REGRESION = 3  # el código de salida del banco cuando algo empeoró contra la corrida anterior


def leer(ruta: str) -> dict | None:
    try:
        with open(ruta, encoding="utf-8") as f:
            j = json.load(f)
        return j if isinstance(j, dict) else None
    except (OSError, ValueError):
        return None


def anterior(raiz_caos: str, actual: str | None = None) -> str | None:
    """El caos.json nocturno más reciente de `raiz_caos` (Logs/caos), sin contar la carpeta
    `actual`. Las carpetas se llaman por la fecha (AAAAMMDD-HHMMSS): ordenan solas."""
    try:
        carpetas = sorted(os.listdir(raiz_caos), reverse=True)
    except OSError:
        return None
    actual = os.path.realpath(actual) if actual else None
    for c in carpetas:
        d = os.path.join(raiz_caos, c)
        if actual and os.path.realpath(d) == actual:
            continue
        j = leer(os.path.join(d, "caos.json"))
        # Solo una corrida nocturna completa contra otra: una a pedido (dos escenarios, otro
        # plazo) o una que cortó antes no es la vara.
        if j and j.get("nocturno") and not j.get("corte"):
            return os.path.join(d, "caos.json")
    return None


def comparar(antes: dict | None, ahora: dict, tolerancia: float = TOLERANCIA, margen_s: float = MARGEN_S) -> dict:
    """{regresiones: [{escenario, que, antes, ahora}], mejoras: [...], contra: {fecha, app}}.
    `antes` puede ser None (la primera corrida): solo cuenta «no volvió»."""
    regresiones, mejoras = [], []
    previos = (antes or {}).get("escenarios", {})
    for nombre, e in (ahora.get("escenarios") or {}).items():
        if e.get("saltado"):
            continue
        veces = len([r for r in e.get("repeticiones", []) if not r.get("saltado")])
        if e.get("no_volvio"):
            regresiones.append({"escenario": nombre, "que": "no_volvio", "antes": (previos.get(nombre) or {}).get("no_volvio"),
                                "ahora": e["no_volvio"], "texto": f"no volvió {e['no_volvio']} de {veces}"})
            continue
        p = previos.get(nombre) or {}
        if p.get("saltado"):
            continue
        a, b = p.get("rto_p95_s"), e.get("rto_p95_s")
        if a is None:
            continue  # nuevo, o antes tampoco midió
        if b is None:
            regresiones.append({"escenario": nombre, "que": "sin_rto", "antes": a, "ahora": None,
                                "texto": f"antes volvía en {a:.2f} s; ahora no midió el RTO"})
        elif b > a * (1 + tolerancia) and b - a > margen_s:
            regresiones.append({"escenario": nombre, "que": "rto_p95", "antes": a, "ahora": b,
                                "texto": f"RTO p95 {a:.2f} → {b:.2f} s (+{(b / a - 1) * 100 if a > 0 else 100:.0f} %)"})
        elif b < a * (1 - tolerancia) and a - b > margen_s:
            mejoras.append({"escenario": nombre, "que": "rto_p95", "antes": a, "ahora": b,
                            "texto": f"RTO p95 {a:.2f} → {b:.2f} s"})
    contra = None
    if antes:
        contra = {"fecha": antes.get("fecha"), "app": (antes.get("app") or {}).get("version")}
    return {"regresiones": regresiones, "mejoras": mejoras, "contra": contra, "tolerancia": tolerancia, "margen_s": margen_s}


def markdown(c: dict) -> list[str]:
    """La sección del informe."""
    md = ["## Contra la corrida anterior", ""]
    if c.get("contra"):
        md.append(f"Contra la del {c['contra']['fecha']} (app {c['contra']['app']}). Regresión: RTO p95 más de "
                  f"{c['tolerancia'] * 100:.0f} % y más de {c['margen_s']:.1f} s arriba, o «no volvió».")
    else:
        md.append("No hay una corrida nocturna anterior completa: solo cuenta «no volvió».")
    md.append("")
    if not c["regresiones"]:
        md.append("**Sin regresiones.**")
    for r in c["regresiones"]:
        md.append(f"- **{r['escenario']}**: {r['texto']}")
    for m in c["mejoras"]:
        md.append(f"- mejoró {m['escenario']}: {m['texto']}")
    return md + [""]


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[0])
        print("uso: comparar.py <anterior/caos.json> <actual/caos.json>")
        return 2
    antes, ahora = leer(argv[0]), leer(argv[1])
    if ahora is None:
        print("no se pudo leer", argv[1])
        return 2
    c = comparar(antes, ahora)
    print("\n".join(markdown(c)))
    return REGRESION if c["regresiones"] else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
