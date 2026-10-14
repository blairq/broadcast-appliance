#!/usr/bin/env python3
"""Cada anotacion a la herramienta que de verdad hace lo que pide.

POR QUE. Las pasadas sin entrenar (Qwen base y v4 sobre las 2.083 frases) y la
geometria (tc-geometria.py con el base) marcaron los mismos pares: chat_history /
chat_recientes, bancas_listar / speakers_status, rostro_ver / ptz_rostros, las
capture_*... En esos pares hay frases que piden lo de la OTRA herramienta, y frases
que valen para las dos sin decir cual. Entrenar con eso no enseña nada: aprieta las
dos nubes una contra la otra.

QUE HACE. Junta las herramientas en GRUPOS que se pisan (pares con CDNV alta en el
diagnostico, o con anotaciones que caen en la otra), y le pasa a un agente
(consignas/tc-reasignar.md) las fichas de cada grupo —con las etiquetas y las
descripciones del perfil, las que separan— y todas sus anotaciones. Por cada una:
queda, se mueve a otra del grupo (con los argumentos de esa) o sale por ambigua.
Lo movido pasa por el validador (esquema y STE-ES) de su destino; si no valida,
sale. Sin --aplicar solo informa.

    BA_PERFIL=vivo6 tools/entrenar/tc-reasignar.py --geometria geo.json [--aplicar]
"""
import argparse
import collections
import importlib.util
import json
import sys
import tempfile
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
E = Path(__file__).resolve().parent
sys.path.insert(0, str(E))
import lib_tc as tc  # noqa: E402

ANOT = RAIZ / "datos" / "tc-anotaciones.json"


def grupos(diag, mias, cdnv_min, fuera_min, tope):
    """Union de los pares que se pisan; un grupo no pasa de `tope` herramientas
    (se agregan los pares de mas CDNV primero)."""
    pares = [(p["cdnv"], p["a"], p["b"]) for p in diag.get("peores_pares", [])]
    pares += [(c, *k.split("|")) for k, c in diag.get("todos_los_pares", {}).items()]
    pares += [(v["peor"]["cdnv"], h, v["peor"]["con"]) for h, v in diag.get("por_herramienta", {}).items()
              if v.get("peor", {}).get("con")]
    cuenta = collections.Counter((x["destino"], x["cae_en"]) for x in diag.get("fuera_de_su_grupo", []))
    pares += [(cdnv_min, a, b) for (a, b), n in cuenta.items() if n >= fuera_min]
    padre = {}

    def raiz(x):
        while padre.get(x, x) != x:
            x = padre[x]
        return x
    tam = collections.Counter()
    for c, a, b in sorted(set(pares), reverse=True):
        if c < cdnv_min or a not in mias or b not in mias or tc.DERIVAR in (a, b) or tc.FALTA in (a, b):
            continue
        ra, rb = raiz(a), raiz(b)
        if ra == rb or max(tam[ra], 1) + max(tam[rb], 1) > tope:
            continue
        padre[rb] = ra
        tam[ra] = max(tam[ra], 1) + max(tam[rb], 1)
    out = collections.defaultdict(set)
    for x in list(padre) + list(padre.values()):
        out[raiz(x)].add(x)
    return sorted((sorted(g) for g in out.values()), key=len, reverse=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--geometria", required=True, help="diagnostico de tc-geometria.py (--json)")
    ap.add_argument("--cdnv-min", type=float, default=0.15)
    ap.add_argument("--fuera-min", type=int, default=3, help="anotaciones de a que caen en b para juntarlas")
    ap.add_argument("--tope", type=int, default=7, help="herramientas por grupo")
    ap.add_argument("--aplicar", action="store_true")
    args = ap.parse_args()

    import agente
    spec = importlib.util.spec_from_file_location("tca", E / "tc-anotar.py")
    tca = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tca)
    diag = json.loads(Path(args.geometria).expanduser().read_text())
    fichas = tc.fichas()
    esquemas = tc.esquemas()
    anot = json.loads(ANOT.read_text())
    gs = grupos(diag, set(fichas), args.cdnv_min, args.fuera_min, args.tope)
    print(f"{len(gs)} grupos:")
    for g in gs:
        print("  ", " · ".join(f"{h}" + (f" ({tc.a_modelo(h)})" if tc.a_modelo(h) != h else "") for h in g))

    tmp = Path(tempfile.mkdtemp(prefix="tc-reasignar-"))
    trabajos = []
    for i, g in enumerate(gs):
        lote = {"herramientas": [{"real": h, "nombre": tc.a_modelo(h), "descripcion": fichas[h]["description"],
                                  "esquema": esquemas[h]} for h in g],
                "anotaciones": [{"id": f"{h}#{j}", "real": h, "plantilla": a["plantilla"],
                                 **({"falta": a["falta"]} if "falta" in a else {"args": a.get("args", {})})}
                                for h in g for j, a in enumerate(anot.get(h, [])) if a.get("plantilla")]}
        f = tmp / f"grupo-{i}.json"
        f.write_text(json.dumps(lote, ensure_ascii=False, indent=1))
        trabajos.append(agente.Trabajo("tc-reasignar.md", f, tmp / f"salida-{i}.jsonl", ["id", "decision"]))
    agente.correr_lotes(trabajos, paralelo=4)

    decisiones = {}
    for t in trabajos:
        if Path(t.salida).exists():
            for l in Path(t.salida).read_text().splitlines():
                try:
                    d = json.loads(l)
                except json.JSONDecodeError:
                    continue
                decisiones[d.get("id")] = d
    cuenta = collections.Counter()
    nuevo = {h: list(v) for h, v in anot.items()}
    sacar = collections.defaultdict(set)
    informe = []
    for g in gs:
        for h in g:
            for j, a in enumerate(anot.get(h, [])):
                if not a.get("plantilla"):
                    continue
                d = decisiones.get(f"{h}#{j}")
                if d is None:
                    cuenta["sin decision"] += 1
                    continue
                dec = d.get("decision")
                if dec == "queda":
                    cuenta["queda"] += 1
                    continue
                if dec == "mover" and d.get("a") in g and d["a"] != h:
                    m = {k: v for k, v in a.items() if k not in ("args", "falta")}
                    m["plantilla"] = d.get("plantilla") or a["plantilla"]
                    m.update({"falta": d["falta"]} if "falta" in d else {"args": d.get("args", {})})
                    m["movida_de"] = h
                    err = tca.validar(m, esquemas[d["a"]], d["a"])
                    sacar[h].add(j)
                    if err:
                        cuenta["mover, no valida: sale"] += 1
                        informe.append({"id": f"{h}#{j}", "plantilla": a["plantilla"], "a": d["a"], "sale": err[:2]})
                        continue
                    nuevo.setdefault(d["a"], []).append(m)
                    cuenta["movida"] += 1
                    informe.append({"id": f"{h}#{j}", "plantilla": a["plantilla"], "a": d["a"]})
                elif dec == "sacar":
                    sacar[h].add(j)
                    cuenta["sale (ambigua)"] += 1
                    informe.append({"id": f"{h}#{j}", "plantilla": a["plantilla"], "sale": d.get("motivo")})
                else:
                    cuenta["decision rara"] += 1
    for h, js in sacar.items():
        nuevo[h] = [a for j, a in enumerate(nuevo[h]) if j not in js or "movida_de" in a]
    print(dict(cuenta))
    (tmp / "informe.json").write_text(json.dumps(informe, ensure_ascii=False, indent=1))
    print(f"informe: {tmp / 'informe.json'}")
    for x in informe[:25]:
        print("  ", x)
    if args.aplicar:
        ANOT.write_text(json.dumps(dict(sorted(nuevo.items())), ensure_ascii=False, indent=1) + "\n")
        print(f"aplicado -> {ANOT}")


if __name__ == "__main__":
    main()
