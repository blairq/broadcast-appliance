#!/usr/bin/env python3
"""Que cambio de un modelo al siguiente, frase por frase.

PARA QUE. El acierto global de dos modelos puede ser igual y esconder que el
nuevo arreglo cuarenta casos y rompio otros cuarenta. Para saber como seguir
con el corpus hace falta ver QUE se movio:

  · arreglados y rotos: lo que el cambio de corpus consiguio y lo que costo;
  · por herramienta: cuales mejoraron, cuales empeoraron, cuales no se movieron;
  · derivar por categoria: que areas administrativas se siguen escapando hacia
    una herramienta reactiva;
  · los errores que PERSISTEN en los dos modelos: son los que el corpus no esta
    pudiendo ensenar. Si una confusion sobrevive a un reentrenamiento con frases
    nuevas para esas herramientas, mas frases del mismo tipo no la van a
    arreglar; hace falta otra cosa (pares contrastivos, sacar semillas
    culpables, o una decision de producto).

Entrada: los archivos de `evaluar.py --salida-casos` de cada modelo, sobre el
MISMO conjunto de casos y con el MISMO evaluador.

  tools/entrenar/comparar-modelos.py casos/v7-banco.jsonl casos/v8-banco.jsonl
"""
import argparse
import collections
import json
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools"))


def cargar(p):
    return {r["frase"]: r for r in (json.loads(l) for l in Path(p).read_text(encoding="utf-8").splitlines() if l.strip())}


def resumen(cs, derivar):
    suyos = [c for c in cs if derivar not in c["aceptables"]]
    ajenos = [c for c in cs if derivar in c["aceptables"]]
    at = [c for c in suyos if c["elegida"] != derivar]
    bien = sum(1 for c in at if c["elegida"] in c["aceptables"])
    der = sum(1 for c in ajenos if c["elegida"] == derivar)
    ok = sum(1 for c in cs if c["elegida"] in c["aceptables"])
    return {"cobertura": len(at) / max(1, len(suyos)), "precision": bien / max(1, len(at)),
            "derivado": der / max(1, len(ajenos)), "acierto": ok / max(1, len(cs)),
            "mal_al_aire": len(at) - bien + sum(1 for c in ajenos if c["elegida"] != derivar)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--ejemplos", type=int, default=6)
    ap.add_argument("--json")
    ap.add_argument("--excluir", default="",
                    help="herramientas a dejar afuera, separadas por coma. Cuando una herramienta"
                         " sale del repertorio entre un modelo y otro, sus frases cambian de"
                         " respuesta correcta; excluirlas aisla el efecto del corpus.")
    args = ap.parse_args()
    import lib_prompt as lp
    D = lp.DERIVAR
    cat = {h["nombre"]: h.get("categoria") or "otras" for h in lp.inventario()["herramientas"]}
    A, B = cargar(args.a), cargar(args.b)
    fuera = {x.strip() for x in args.excluir.split(",") if x.strip()}
    comunes = [f for f in A if f in B
               and (A[f].get("real") or A[f]["aceptables"][0]) not in fuera]
    ca, cb = [A[f] for f in comunes], [B[f] for f in comunes]
    na, nb = Path(args.a).stem, Path(args.b).stem
    ra, rb = resumen(ca, D), resumen(cb, D)

    print(f"{len(comunes)} casos en comun\n")
    print(f"  {'':22s} {na:>12s} {nb:>12s}")
    for k in ("cobertura", "precision", "derivado", "acierto"):
        print(f"  {k:22s} {100*ra[k]:11.1f}% {100*rb[k]:11.1f}%   {100*(rb[k]-ra[k]):+.1f}")
    print(f"  {'errores que se ven':22s} {ra['mal_al_aire']:12d} {rb['mal_al_aire']:12d}"
          "   (herramienta equivocada ejecutada)")

    ok = lambda c: c["elegida"] in c["aceptables"]
    arreglados = [f for f in comunes if not ok(A[f]) and ok(B[f])]
    rotos = [f for f in comunes if ok(A[f]) and not ok(B[f])]
    persisten = [f for f in comunes if not ok(A[f]) and not ok(B[f])]
    print(f"\n  arreglados {len(arreglados)} · rotos {len(rotos)} · errores que persisten {len(persisten)}")
    print(f"\n  ROTOS (bien en {na}, mal en {nb}):")
    for f in rotos[:args.ejemplos]:
        print(f"    «{f[:48]}»  {A[f]['elegida']} -> {B[f]['elegida']}  (esperaba {B[f]['aceptables'][0]})")

    # --- por herramienta, solo lo del repertorio ------------------------------
    por = collections.defaultdict(lambda: [0, 0, 0])      # casos, bien a, bien b
    for f in comunes:
        esp = A[f]["aceptables"][0]
        if esp == D:
            continue
        por[esp][0] += 1
        por[esp][1] += ok(A[f])
        por[esp][2] += ok(B[f])
    cambios = sorted(por.items(), key=lambda kv: (kv[1][2] - kv[1][1]))
    print(f"\n  POR HERRAMIENTA (banco, lo que es suyo) — las que mas cambiaron:")
    for h, (n, a_, b_) in cambios[:6] + [("...", (0, 0, 0))] + cambios[-6:]:
        if h == "...":
            print("    ...")
            continue
        print(f"    {h:22s} {a_}/{n} -> {b_}/{n}   {b_-a_:+d}")
    siguen_mal = sorted((h for h, (n, a_, b_) in por.items() if n >= 3 and b_ / n < 0.6),
                        key=lambda h: por[h][2] / por[h][0])
    if siguen_mal:
        print(f"\n  SIGUEN MAL (menos de 60% en {nb}, con 3 o mas casos):")
        for h in siguen_mal:
            n, a_, b_ = por[h]
            print(f"    {h:22s} {b_}/{n}")

    # --- derivar por categoria --------------------------------------------------
    dc = collections.defaultdict(lambda: [0, 0, 0])
    for f in comunes:
        if A[f]["aceptables"][0] != D:
            continue
        c = cat.get(A[f].get("real") or "", "fuera del aparato")
        dc[c][0] += 1
        dc[c][1] += A[f]["elegida"] == D
        dc[c][2] += B[f]["elegida"] == D
    print(f"\n  DERIVAR POR CATEGORIA (que fraccion de lo ajeno pasa al grande):")
    for c, (n, a_, b_) in sorted(dc.items(), key=lambda kv: kv[1][2] / kv[1][0]):
        print(f"    {c:20s} {100*a_/n:5.0f}% -> {100*b_/n:5.0f}%   ({n} frases)")

    # --- confusiones que persisten ---------------------------------------------
    conf = collections.Counter((B[f]["aceptables"][0], B[f]["elegida"]) for f in persisten)
    print(f"\n  CONFUSIONES QUE PERSISTEN EN LOS DOS MODELOS (esperada -> elegida):")
    for (e, g), n in conf.most_common(12):
        print(f"    {e:22s} -> {g:22s} {n:3d}")
    if args.json:
        Path(args.json).write_text(json.dumps({
            "a": ra, "b": rb, "arreglados": arreglados, "rotos": rotos, "persisten": persisten,
            "por_herramienta": {h: v for h, v in por.items()},
            "derivar_por_categoria": {c: v for c, v in dc.items()},
            "confusiones_persistentes": [[e, g, n] for (e, g), n in conf.most_common()]},
            ensure_ascii=False, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()
