#!/usr/bin/env python3
"""Escribir un banco INTERMEDIO del ciclo (tc-ciclo.py): pedidos de operador, nuevos.

POR QUE. El banco de siempre (datos/orientacion.jsonl) es el banco FINAL: no se
toca nunca, contra el se comparan todas las versiones. Los intermedios se usan
dos veces: primero MIDEN un modelo que nunca los vio, y despues lo que ese modelo
erro entra al corpus (tc-aprendidas.py). Por eso cada uno se escribe de cero.

COMO.
  1. Un agente por lote, sin ver el corpus ni los bancos (agente.py), escribe
     pedidos para cada herramienta del perfil y pedidos de derivar, con una voz
     distinta en cada banco (--numero): asi los tres no son la misma muestra.
  2. Un revisor CIEGO (no sabe la herramienta que se quiso pedir) elige a cual va:
     queda solo lo que coincide. Lo ambiguo no mide nada.
  3. Fuera lo que se parece demasiado (palabras compartidas, como tc-corpus) al
     banco final, al examen, al corpus o a los otros intermedios.

  BA_PERFIL=vivo6 tools/entrenar/tc-banco-escribir.py --numero 1
  -> datos/bancos-ciclo/banco-1.jsonl (+ banco-1.resumen.json)
"""
import argparse
import collections
import json
import os
import random
import re
import sys
import tempfile
import unicodedata
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
E = Path(__file__).resolve().parent
sys.path.insert(0, str(E))
import agente  # noqa: E402
import lib_tc as tc  # noqa: E402

SALIDA = RAIZ / "datos" / "bancos-ciclo"
ESTILOS = {
    1: "un operador de cabina con años de oficio: hablás corto y directo, con jerga técnica y apuro.",
    2: "el productor o director del evento: no tocás los equipos, pedís por el resultado («que se vea...», "
       "«que no se escuche...»), de forma indirecta y a veces explicando el motivo.",
    3: "alguien nuevo en la cabina, que viene de una radio o un canal chico: hablás coloquial, preguntás "
       "mucho, describís las cosas en vez de nombrarlas y a veces usás mal una palabra técnica.",
}
VACIAS = set("el la los las lo le les de del a al en con por para que y o u un una unos unas es esta este esto eso "
             "esa ese se su sus mi me te tu ya ahi aca hay como cual cuales dale eh che ojo rapido porfa bueno ahora "
             "toque".split())


def norm(s):
    s = unicodedata.normalize("NFKD", s.lower()).encode("ascii", "ignore").decode()
    return re.sub(r"\W+", " ", s).strip()


def toks(s):
    t = set(norm(s).split())
    return (t - VACIAS) or t


class Parecidas:
    """¿comparte esta porcion de palabras con alguna de las vistas? (como tc-corpus)"""
    def __init__(self, umbral):
        self.u, self.lista, self.ind = umbral, [], collections.defaultdict(list)

    def sumar(self, frase):
        t = toks(frase)
        for w in t:
            self.ind[w].append(len(self.lista))
        self.lista.append(t)

    def parecida(self, frase):
        t = toks(frase)
        return bool(t) and any(len(t & self.lista[i]) / max(len(t), len(self.lista[i])) >= self.u
                               for i in {i for w in t for i in self.ind.get(w, ())})


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--numero", type=int, required=True, choices=sorted(ESTILOS))
    ap.add_argument("--por-herramienta", type=int, default=3)
    ap.add_argument("--ajenas-por-lote", type=int, default=8)
    ap.add_argument("--herramientas-por-lote", type=int, default=6)
    ap.add_argument("--parecido", type=float, default=0.6)
    ap.add_argument("--paralelo", type=int, default=4)
    args = ap.parse_args()
    if not os.environ.get("BA_PERFIL"):  # sin el, todo sale con el perfil reactivo y nada falla
        sys.exit("falta BA_PERFIL (el perfil del modelo, p. ej. BA_PERFIL=vivo6)")
    SALIDA.mkdir(parents=True, exist_ok=True)
    rnd = random.Random(100 + args.numero)
    fichas = {h: f for h, f in tc.fichas().items() if h not in (tc.DERIVAR, tc.FALTA)}
    propias = sorted(fichas)
    mcp = json.loads((RAIZ / "docs" / "mcp.json").read_text())["herramientas"]
    ajenas = [{"nombre": x["nombre"], "descripcion": x.get("titulo") or x["descripcion"][:200]}
              for x in mcp if x["nombre"] not in fichas]
    rnd.shuffle(propias)
    rnd.shuffle(ajenas)

    # 1. escribir
    tmp = Path(tempfile.mkdtemp(prefix=f"banco-{args.numero}-"))
    trabajos, n = [], args.herramientas_por_lote
    for i in range(0, len(propias), n):
        k = i // n
        lote = {"propias": [{"nombre": h, "descripcion": fichas[h]["description"]} for h in propias[i:i + n]],
                "ajenas": ajenas[(k * 5) % len(ajenas):(k * 5) % len(ajenas) + 5]}
        (tmp / f"e{k:02d}.json").write_text(json.dumps(lote, ensure_ascii=False, indent=1))
        trabajos.append(agente.Trabajo("tc-banco.md", tmp / f"e{k:02d}.json", tmp / f"e{k:02d}.jsonl",
                                       ["frase", "herramienta"],
                                       {"ESTILO": ESTILOS[args.numero], "POR_HERRAMIENTA": args.por_herramienta,
                                        "AJENAS": args.ajenas_por_lote}))
    print(f"banco {args.numero}: {len(trabajos)} lotes a escribir ({tmp})", flush=True)
    res = agente.correr_lotes(trabajos, args.paralelo)
    filas = agente.leer([t.salida for t in trabajos])
    motivos = collections.Counter()
    vistas, escritas = set(), []
    for f in filas:
        h = f.get("herramienta")
        if h not in fichas and h != tc.DERIVAR:
            motivos["nombre que no existe"] += 1
            continue
        if not f.get("frase") or norm(f["frase"]) in vistas:
            motivos["vacia o repetida"] += 1
            continue
        vistas.add(norm(f["frase"]))
        escritas.append({"frase": f["frase"].strip(), "herramienta": h})

    # 3 (antes del revisor: no gastar en lo que igual sale). Parecidas al banco final,
    # al examen, al corpus (las plantillas sin los huecos) y a los otros intermedios
    par = Parecidas(args.parecido)
    for f in (RAIZ / "datos/orientacion.jsonl", RAIZ / "datos/entrenamiento/examen.jsonl",
              *[x for x in SALIDA.glob("banco-*.jsonl") if x.name != f"banco-{args.numero}.jsonl"]):
        if f.exists():
            for l in f.read_text().splitlines():
                if l.strip():
                    par.sumar(json.loads(l)["frase"])
    for lista in json.loads((RAIZ / "datos/tc-anotaciones.json").read_text()).values():
        for a in lista:
            if a.get("plantilla"):
                par.sumar(tc.RE_HUECO.sub(" ", a["plantilla"]))
    quedan = []
    for f in escritas:
        if par.parecida(f["frase"]):
            motivos["parecida al banco final, examen, corpus u otro intermedio"] += 1
        else:
            quedan.append(f)

    # 2. el revisor ciego
    rnd.shuffle(quedan)
    catalogo = [{"nombre": h, "descripcion": fichas[h]["description"]} for h in sorted(fichas)]
    rev, m = [], 40
    for i in range(0, len(quedan), m):
        lote = {"catalogo": catalogo, "frases": [{"id": f"{i + j}", "frase": f["frase"]} for j, f in enumerate(quedan[i:i + m])]}
        (tmp / f"r{i // m:02d}.json").write_text(json.dumps(lote, ensure_ascii=False, indent=1))
        rev.append(agente.Trabajo("tc-banco-revisar.md", tmp / f"r{i // m:02d}.json", tmp / f"r{i // m:02d}.jsonl",
                                  ["id", "herramienta"]))
    print(f"revisor ciego: {len(quedan)} frases en {len(rev)} lotes", flush=True)
    res += agente.correr_lotes(rev, args.paralelo)
    eleccion = {str(r["id"]): r["herramienta"] for r in agente.leer([t.salida for t in rev])}
    banco = []
    for i, f in enumerate(quedan):
        e = eleccion.get(str(i))
        if e is None:
            motivos["el revisor no la juzgo"] += 1
        elif e == "AMBIGUA":
            motivos["ambigua para el revisor"] += 1
        elif e != f["herramienta"]:
            motivos[f"el revisor eligio otra"] += 1
        else:
            banco.append({**f, "banco": args.numero})
    banco.sort(key=lambda x: (x["herramienta"], x["frase"]))
    out = SALIDA / f"banco-{args.numero}.jsonl"
    out.write_text("".join(json.dumps(x, ensure_ascii=False) + "\n" for x in banco))
    por = collections.Counter(x["herramienta"] for x in banco)
    resumen = {"numero": args.numero, "estilo": ESTILOS[args.numero], "casos": len(banco),
               "propias": sum(v for k, v in por.items() if k != tc.DERIVAR), "derivar": por.get(tc.DERIVAR, 0),
               "herramientas_sin_frases": sorted(set(fichas) - set(por)), "escritas": len(filas),
               "descartadas": dict(motivos), "costo_usd": round(sum(r.get("costo", 0) for r in res), 2),
               "perfil": tc.huella()}
    (SALIDA / f"banco-{args.numero}.resumen.json").write_text(json.dumps(resumen, ensure_ascii=False, indent=1))
    print(json.dumps({k: v for k, v in resumen.items() if k != "herramientas_sin_frases"}, ensure_ascii=False))
    if resumen["herramientas_sin_frases"]:
        print(f"OJO: {len(resumen['herramientas_sin_frases'])} herramientas sin frases en el banco")


if __name__ == "__main__":
    main()
