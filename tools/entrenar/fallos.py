#!/usr/bin/env python3
"""Clasifica los fallos del examen y dice QUE FRASES FALTA ESCRIBIR.

POR QUE EXISTE. Contar aciertos dice si el modelo mejoro; no dice que hacer
para que mejore. Y los fallos no son todos iguales: medido en este proyecto,
los del modelo sin entrenar eran todos entre herramientas VECINAS, y los que
quedaron despues de entrenar son otra cosa —lee el sintoma como una orden—.
Cada tipo se arregla distinto, y con mas volumen no se arregla ninguno.

LOS TIPOS, y por que se pueden separar sin adivinar:

  PELIGROSO   eligio una herramienta DESTRUCTIVA cuando todo lo aceptable era
              de SOLO LECTURA. O sea: habia que MIRAR y fue a TOCAR. Sale de
              las marcas que ya tiene docs/mcp.json, no de una heuristica.
              Ejemplo real: «se esta cortando la transmision» -> stream_stop.
              El operador reporta un problema y el aparato lo ejecuta. En una
              bateria cuenta como un error mas; en vivo no.

  VECINA      elegida y esperada son de la MISMA categoria. Se confundio entre
              parientes por una palabra de superficie: «corta a la camara 2»
              -> transition_set, por «corta» -> corte.

  LEJOS       otra categoria. O la frase es ambigua de verdad, o falta
              vocabulario de esa herramienta.

QUE HACER CON CADA UNO, que es lo que el informe imprime:
  · PELIGROSO -> pares REPORTE contra ORDEN («se esta cortando la transmision»
    contra «corta la transmision»). Y en el aparato: que una destructiva
    elegida por el modelo chico pida confirmacion.
  · VECINA -> pares que las DISTINGAN, en las semillas de las dos.
  · LEJOS -> frases nuevas para la esperada, con las palabras de la frase.

  tools/entrenar/fallos.py docs/bateria-local.json
"""
import argparse
import json
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools"))
import lib_prompt as lp  # noqa: E402

SEMILLAS = RAIZ / "datos/semillas-reactivas.json"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("archivo", help="el json que deja bateria-local.py o evaluar.py")
    args = ap.parse_args()

    d = json.loads(Path(args.archivo).read_text(encoding="utf-8"))
    inv = lp.inventario()
    porn = {h["nombre"]: h for h in inv["herramientas"]}
    sem = json.loads(SEMILLAS.read_text(encoding="utf-8"))["herramientas"]

    grupos = {"PELIGROSO": [], "VECINA": [], "LEJOS": []}
    for f in d.get("fallos", []):
        el, ac = f["elegida"], f["aceptables"]
        he = porn.get(el)
        if he and he.get("destructiva") and all(porn.get(a, {}).get("soloLectura") for a in ac if a in porn):
            grupos["PELIGROSO"].append(f)
        elif he and any(porn.get(a, {}).get("categoria") == he["categoria"] for a in ac if a in porn):
            grupos["VECINA"].append(f)
        else:
            grupos["LEJOS"].append(f)

    total = d.get("casos", 0)
    print(f"\n{d.get('modelo','?')}: {d.get('aciertos',0)}/{total} "
          f"({100*d.get('aciertos',0)//max(1,total)}%) · {len(d.get('fallos',[]))} fallos\n")

    consejos = {
        "PELIGROSO": ("Habia que MIRAR y fue a TOCAR. Escribi pares REPORTE contra ORDEN en las\n"
                      "     semillas de las dos herramientas. Y en el aparato: que una destructiva\n"
                      "     elegida por el modelo chico pida confirmacion antes de correr."),
        "VECINA": ("Se confundio entre parientes. Escribi frases que las DISTINGAN, una en cada\n"
                   "     semilla, nombrando lo que las separa."),
        "LEJOS": ("O la frase es ambigua, o a la esperada le falta vocabulario. Agregale frases\n"
                  "     con las palabras que uso el operador."),
    }
    for tipo in ("PELIGROSO", "VECINA", "LEJOS"):
        fs = grupos[tipo]
        if not fs:
            continue
        print(f"--- {tipo} ({len(fs)}) ---")
        for f in fs:
            print(f"  {f['frase'][:44]:46s} -> {f['elegida']:22s} (esperaba {', '.join(f['aceptables'][:3])})")
        print(f"  QUE HACER: {consejos[tipo]}\n")

    # Lo accionable, junto: a que semillas hay que meterles mano.
    tocar = {}
    for tipo, fs in grupos.items():
        for f in fs:
            for h in [f["elegida"]] + f["aceptables"]:
                if h in porn:
                    tocar.setdefault(h, set()).add(tipo)
    if tocar:
        print("SEMILLAS A TOCAR (datos/semillas-reactivas.json):")
        for h, tipos in sorted(tocar.items()):
            cuantas = len(sem.get(h, {}).get("frases", []))
            print(f"  {h:24s} {cuantas:2d} frases hoy   · por {', '.join(sorted(tipos))}")
        print("\nDespues: make corpus-entrenamiento  y volver a entrenar.")


if __name__ == "__main__":
    main()
