#!/usr/bin/env python3
"""CONTRASTE entre las vecinas que el modelo todavia confunde, sin salir de 18-25 por herramienta.

POR QUE. Con el corpus parejo (tc-parejo.py) la v6 vuelta 0 quedo como la v5,
y la falla de las dos es la misma: elegir la VECINA (foto_invitado /
imagen_preguntar, speaker_move / participant_requeue, status / stream_stop...).
Para eso no hacen falta mas frases de cada una sino frases que digan lo que las
separa.

DE DONDE SALEN LOS PARES. De la geometria (tc-geometria.py --json) medida con
un modelo: los pares sobre el objetivo de CDNV y las anotaciones que caen en el
grupo de otra. Nunca del banco ni del examen: son para medir.

QUE HACE, por cada herramienta de esos pares:
  1. hace lugar: poda (tc-geometria.py --podar) hasta --max menos las que va a
     sumar, sacando las que menos suman (primero las del agente);
  2. un agente (consignas/tc-completar.md) escribe --nuevas frases de contraste
     con las vecinas del par al frente;
  3. pasan el validador (esquema y STE-ES), la regla de copia y los filtros de
     la geometria con el mismo modelo (--permitir-otra: la frase de contraste que
     el modelo pone en la vecina es justo la que falta).

    BA_PERFIL=vivo6 tools/entrenar/tc-contraste.py --geometria geo.json --modelo <adaptador> --aplicar
"""
import argparse
import collections
import importlib.util
import json
import os
import sys
import tempfile
from pathlib import Path

E = Path(__file__).resolve().parent
sys.path.insert(0, str(E))
import lib_tc as tc  # noqa: E402


def _modulo(nombre, archivo):
    spec = importlib.util.spec_from_file_location(nombre, E / archivo)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def pares(diag, mias, objetivo, fuera_min, tope):
    """[(a, b, peso)]: los de CDNV sobre el objetivo y los que se cruzan de grupo."""
    p = collections.Counter()
    todos = {k: c for k, c in diag.get("todos_los_pares", {}).items()}
    for x in diag.get("peores_pares", []):
        todos.setdefault(f"{x['a']}|{x['b']}", x["cdnv"])
    # el peor par de cada herramienta (peores_pares trae solo los primeros del total)
    for h, v in diag.get("por_herramienta", {}).items():
        if v.get("peor", {}).get("con"):
            todos.setdefault(f"{h}|{v['peor']['con']}", v["peor"]["cdnv"])
    for k, c in todos.items():
        a, b = k.split("|")
        if c > objetivo:
            p[tuple(sorted((a, b)))] += c / objetivo
    cruces = collections.Counter((x["destino"], x["cae_en"]) for x in diag.get("fuera_de_su_grupo", []))
    for (a, b), n in cruces.items():
        if n >= fuera_min:
            p[tuple(sorted((a, b)))] += n
    fuera = {tc.DERIVAR, tc.FALTA}
    out = [(a, b, w) for (a, b), w in p.most_common() if a in mias and b in mias and not {a, b} & fuera]
    return out[:tope]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--geometria", required=True, help="diagnostico de tc-geometria.py --json")
    ap.add_argument("--modelo", required=True, help="el instrumento: el mismo que midio la geometria")
    ap.add_argument("--objetivo", type=float, default=0.028)
    ap.add_argument("--fuera-min", type=int, default=2, help="anotaciones de a que caen en b para contarlo")
    ap.add_argument("--pares", type=int, default=30, help="cuantos pares, los peores primero")
    ap.add_argument("--nuevas", type=int, default=4, help="frases de contraste por herramienta")
    ap.add_argument("--max", type=int, default=25)
    ap.add_argument("--grupo", type=int, default=4, help="herramientas por agente")
    ap.add_argument("--solo", help="solo estas herramientas (reales, separadas por coma): sus vecinas siguen saliendo de los pares")
    ap.add_argument("--aplicar", action="store_true")
    args = ap.parse_args()

    import agente
    tca = _modulo("tca", "tc-anotar.py")
    par = _modulo("par", "tc-parejo.py")
    fichas, esquemas = tc.fichas(), tc.esquemas()
    mias = {h for h in fichas if h not in (tc.DERIVAR, tc.FALTA)}
    anot = json.loads(par.ANOT.read_text())
    diag = json.loads(Path(args.geometria).expanduser().read_text())
    tmp = Path(tempfile.mkdtemp(prefix="tc-contraste-"))
    print(f"trabajo: {tmp}")

    ps = pares(diag, mias, args.objetivo, args.fuera_min, args.pares)
    vec = collections.defaultdict(list)
    for a, b, w in ps:
        vec[a].append(b)
        vec[b].append(a)
    if args.solo:
        vec = {h: v for h, v in vec.items() if h in set(args.solo.split(","))}
    # las congeladas (datos/tc-congeladas.json) no reciben frases ni se podan;
    # siguen siendo la vecina contra la que se contrasta lo nuevo
    cong = tc.congeladas()
    print(f"congeladas fuera: {sorted(h for h in vec if h in cong)[:12]}{'...' if sum(h in cong for h in vec) > 12 else ''}")
    vec = {h: v for h, v in vec.items() if h not in cong}
    print(f"{len(ps)} pares, {len(vec)} herramientas:")
    for a, b, w in ps:
        print(f"   {w:5.1f}  {tc.a_modelo(a)} / {tc.a_modelo(b)}")
    n = lambda h: sum(1 for x in anot.get(h, []) if x.get("plantilla"))

    # 1. hacer lugar: la nube (sin las de falta) de cada una baja a max - nuevas - falta
    topes = {h: max(1, args.max - sum(1 for x in anot.get(h, []) if "falta" in x)) for h in mias}
    for h in vec:
        topes[h] = max(1, topes[h] - args.nuevas)
    ANOT_TMP = tmp / "anot.json"
    ANOT_TMP.write_text(json.dumps(anot, ensure_ascii=False))
    os.environ["BA_TC_ANOTACIONES"] = str(ANOT_TMP)
    (tmp / "topes.json").write_text(json.dumps(topes))
    res = par.geometria(args.modelo, ["--podar", args.max, "--topes", tmp / "topes.json",
                                      "--salida-poda", tmp / "poda.json"], tmp / "poda.log")
    sale = json.loads((tmp / "poda.json").read_text())
    podadas = 0
    for h, pls in sale.items():
        if h in vec:
            antes = len(anot[h])
            anot[h] = [x for x in anot[h] if x.get("plantilla") not in set(pls)]
            podadas += antes - len(anot[h])
    print(f"hacer lugar: {podadas} fuera ({res})")

    # 2. el contraste
    hs = sorted(vec)
    trabajos = []
    for i in range(0, len(hs), args.grupo):
        lote = [{"real": h, "nombre": tc.a_modelo(h), "descripcion": fichas[h]["description"],
                 "esquema": esquemas[h], "cuantas": args.nuevas + 2,
                 "anotaciones": [x["plantilla"] for x in anot.get(h, []) if x.get("plantilla")],
                 "vecinas": [{"nombre": tc.a_modelo(o), "descripcion": fichas[o]["description"]}
                             for o in dict.fromkeys(vec[h])][:8]} for h in hs[i:i + args.grupo]]
        f = tmp / f"lote-{i}.json"
        f.write_text(json.dumps(lote, ensure_ascii=False, indent=1))
        trabajos.append(agente.Trabajo("tc-completar.md", f, tmp / f"salida-{i}.jsonl", ["herramienta", "plantilla"]))
    agente.correr_lotes(trabajos, paralelo=4)
    cands, malas, copias = [], 0, 0
    for t in trabajos:
        if not Path(t.salida).exists():
            continue
        por_h = collections.defaultdict(list)
        for linea in Path(t.salida).read_text().splitlines():
            try:
                x = json.loads(linea)
            except json.JSONDecodeError:
                continue
            h = x.pop("herramienta", None)
            if h not in vec:
                malas += 1
                continue
            x.setdefault("frase", x["plantilla"])
            if "falta" not in x:
                x.setdefault("args", {})
            if tca.validar(x, esquemas[h], h):
                malas += 1
                continue
            x.update(nueva=True, origen="tc-contraste")
            por_h[h].append(x)
        for h, nuevas in por_h.items():
            quedan, cp = tc.sin_copias(anot.get(h, []) + nuevas)
            copias += len(cp)
            cands += [{"herramienta": h, **x} for x in quedan if x.get("origen") == "tc-contraste"
                      and x not in anot.get(h, [])]
    cf = tmp / "candidatas.jsonl"
    cf.write_text("".join(json.dumps(c, ensure_ascii=False) + "\n" for c in cands))
    ANOT_TMP.write_text(json.dumps(anot, ensure_ascii=False))
    res = par.geometria(args.modelo, ["--filtrar", cf, "--permitir-otra", "--aprobadas", tmp / "aprobadas.jsonl"],
                        tmp / "filtro.log")
    aprob = [json.loads(linea) for linea in (tmp / "aprobadas.jsonl").read_text().splitlines() if linea.strip()]
    cuenta = collections.Counter()
    for x in aprob:
        h = x.pop("herramienta")
        if n(h) < args.max and cuenta[h] < args.nuevas:
            anot.setdefault(h, []).append(x)
            cuenta[h] += 1
    print(f"contraste: {len(cands)} candidatas ({malas} no validan, {copias} copias) · {res} · "
          f"entran {sum(cuenta.values())}")
    v = sorted(n(h) for h in mias)
    print(f"queda: min {v[0]} · mediana {v[len(v) // 2]} · max {v[-1]}")
    for h in hs:
        print(f"   {tc.a_modelo(h):28} {n(h):3}  +{cuenta[h]}")
    destino = par.ANOT if args.aplicar else tmp / "anotaciones.json"
    destino.write_text(json.dumps(dict(sorted(anot.items())), ensure_ascii=False, indent=1) + "\n")
    print(("aplicado -> " if args.aplicar else "sin aplicar: ") + str(destino))


if __name__ == "__main__":
    main()
