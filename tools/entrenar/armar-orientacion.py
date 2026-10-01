#!/usr/bin/env python3
"""Arma el banco de orientacion a partir de lo que escribieron los autores.

QUE ES EL BANCO. Frases escritas por alguien que NO leyo el corpus de
entrenamiento, con la herramienta que les corresponde. El bucle de corpus lo usa
para saber donde se pierde el modelo (ver docs/bucle-corpus.md). Nunca se
entrena, y la guarda del bucle impide copiarlo en una semilla.

QUE HACE ESTE SCRIPT, y por que cada paso:

  1. Valida los nombres: cada frase tiene que apuntar a una herramienta que
     exista en el MCP (o a `derivar`). Un nombre mal escrito es una frase que no
     mide nada.
  2. SACA LO QUE SE PARECE AL EXAMEN. Si el banco tiene frases casi iguales a
     preguntas del examen, orientar con el banco es orientar con el examen, y el
     examen deja de ser el juez. Es la misma guarda que usa el generador de
     corpus.
  3. Saca duplicados exactos.
  4. Avisa que herramientas quedaron con pocas frases.

  tools/entrenar/armar-orientacion.py lote-1.jsonl lote-2.jsonl ... \\
      --salida datos/orientacion.jsonl
"""
import argparse
import collections
import importlib.util
import json
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools"))
CORPUS_EXAMEN = RAIZ / "src/app/tests/datos/preguntas.json"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("lotes", nargs="+")
    ap.add_argument("--salida", default=str(RAIZ / "datos/orientacion.jsonl"))
    ap.add_argument("--autor", default="", help="quien lo escribio, para el registro")
    args = ap.parse_args()

    import lib_prompt as lp
    spec = importlib.util.spec_from_file_location("ce", RAIZ / "tools/corpus-entrenamiento.py")
    ce = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ce)

    validas = {h["nombre"] for h in lp.inventario()["herramientas"]} | {lp.DERIVAR}
    examen = json.loads(CORPUS_EXAMEN.read_text(encoding="utf-8"))["casos"]
    prohibidas = [set(ce.sin_tildes(c["pregunta"].lower()).split()) for c in examen]

    filas, malas, examen_n, dup = [], collections.Counter(), 0, 0
    vistas = set()
    for lote in args.lotes:
        for l in Path(lote).read_text(encoding="utf-8").splitlines():
            if not l.strip():
                continue
            try:
                r = json.loads(l)
            except json.JSONDecodeError:
                malas["json roto"] += 1
                continue
            fr, h = r.get("frase", "").strip(), r.get("herramienta", "").strip()
            if not fr or h not in validas:
                malas[f"herramienta inexistente: {h}"] += 1
                continue
            if ce.parecido(fr, prohibidas):
                examen_n += 1
                continue
            k = ce.sin_tildes(fr.lower())
            if k in vistas:
                dup += 1
                continue
            vistas.add(k)
            filas.append({"frase": fr, "herramienta": h})

    por = collections.Counter(f["herramienta"] for f in filas)
    faltan = sorted(validas - set(por) - {lp.DERIVAR})
    flacas = sorted(h for h, n in por.items() if n < 3)
    Path(args.salida).write_text(
        "".join(json.dumps(f, ensure_ascii=False) + "\n" for f in filas), encoding="utf-8")
    rep = set(lp.herramientas_reactivas())
    print(f"{len(filas)} frases para {len(por)} herramientas -> {args.salida}")
    print(f"  del repertorio reactivo: {sum(n for h, n in por.items() if h in rep)} frases"
          f" · del resto del MCP: {sum(n for h, n in por.items() if h not in rep and h != lp.DERIVAR)}"
          f" · derivar: {por[lp.DERIVAR]}")
    print(f"  descartadas: {examen_n} por parecerse al examen · {dup} duplicadas"
          + (" · " + ", ".join(f"{m} ({n})" for m, n in malas.items()) if malas else ""))
    if faltan:
        print(f"  SIN NINGUNA FRASE ({len(faltan)}): {', '.join(faltan[:20])}"
              + (" ..." if len(faltan) > 20 else ""))
    if flacas:
        print(f"  con menos de 3 frases: {', '.join(flacas)}")


if __name__ == "__main__":
    main()
