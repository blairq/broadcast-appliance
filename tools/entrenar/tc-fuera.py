#!/usr/bin/env python3
"""Las frases que caen en el grupo de OTRA herramienta: quedan, se mueven, se reescriben o salen.

POR QUE. La geometria (tc-geometria.py, con el modelo base) ubica cada frase en el
centro de nube mas cercano. El 17/10/2026, 363 de ~2.500 (mas del 10 %) caian en
el de otra herramienta: o pedian lo de la otra (mal asignadas), o no decian lo
que separa a la suya, o eran ambiguas. Es una excepcion pedida sobre las
congeladas (datos/tc-congeladas.json): se tocan SOLO estas frases.

QUE HACE:
  1. repetidas: dos frases iguales (misma plantilla normalizada y lo mismo
     pedido) quedan en UNA: sale la otra, nunca las dos.
  2. un agente (consignas/tc-fuera.md) ve cada frase con las dos fichas y las
     frases de las dos herramientas, y decide: queda, mover (a la otra),
     reescribir (en la suya, diciendo lo que la separa) o sacar.
  3. lo que cambia pasa por el validador (esquema, tipos, STE-ES: tc-anotar) y
     por la regla de copia (sin_copias); si no pasa, la frase QUEDA como estaba
     (salvo una mover/reescribir que resulta copia: ahi sale, porque la otra ya esta).
     Ninguna herramienta baja de --minimo llamadas por sacar: ahi la frase queda.
  4. --aplicar escribe datos/tc-anotaciones.json y el informe datos/tc-fuera-cambios.json.

Despues, medir otra vez (tc-geometria.py --modelo Qwen/Qwen3.5-4B --json ...).

    BA_PERFIL=vivo6 tools/entrenar/tc-fuera.py --geometria geo.json [--aplicar]
"""
import argparse
import collections
import importlib.util
import json
import re
import sys
import tempfile
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
E = Path(__file__).resolve().parent
sys.path.insert(0, str(E))
import agente  # noqa: E402
import lib_tc as tc  # noqa: E402

ANOT = RAIZ / "datos" / "tc-anotaciones.json"
INFORME = RAIZ / "datos" / "tc-fuera-cambios.json"


def _modulo(nombre, archivo):
    spec = importlib.util.spec_from_file_location(nombre, E / archivo)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def pedido(a):
    return json.dumps(a.get("args") if "falta" not in a else {"falta": a["falta"]}, sort_keys=True, ensure_ascii=False)


def llamadas(lista):
    return sum(1 for a in lista if a.get("plantilla") and "falta" not in a)


def juzgar(casos, anot, fichas, tmp, reusar):
    """El juez a ciegas: cada frase (rendida, sin el estado) con sus dos opciones
    mezcladas. Para las de derivar la opcion es la otra o NINGUNA (no es de este
    modelo). Devuelve (los que no coinciden, con «juez»; {id: veredicto} de los
    confirmados)."""
    import random
    geo = _modulo("geo", "tc-geometria.py")
    rnd = random.Random(17)
    lotes, por_n = [], {}
    for i in range(0, len(casos), 15):
        frases, hs = [], set()
        for c in casos[i:i + 15]:
            h, j = c["id"].rsplit("#", 1)
            u, _ = geo.usuario_de(h, anot[h][int(j)])
            frase = (u or c["plantilla"]).split("Pedido: ", 1)[-1]
            ops = [c["otra"]["real"]] + ([] if h == tc.DERIVAR else [h])
            rnd.shuffle(ops)
            n = len(por_n)
            por_n[n] = c
            frases.append({"id": n, "frase": frase, "opciones": ops})
            hs |= set(ops)
        lotes.append({"herramientas": {o: fichas[o]["description"] for o in sorted(hs)}, "frases": frases})
    trabajos = []
    for k, lote in enumerate(lotes):
        f = tmp / f"juez-{k}.json"
        if not reusar:
            f.write_text(json.dumps(lote, ensure_ascii=False, indent=1))
        trabajos.append(agente.Trabajo("juez.md", f, tmp / f"juez-{k}.jsonl", ["id", "herramienta"]))
    if not reusar:
        agente.correr_lotes(trabajos, paralelo=4)
    ver = {}
    for t in trabajos:
        if Path(t.salida).exists():
            for l in Path(t.salida).read_text().splitlines():
                try:
                    d = json.loads(l)
                    ver[int(d["id"])] = d["herramienta"]
                except (json.JSONDecodeError, KeyError, ValueError):
                    continue
    disputados, confirmadas = [], {}
    for n, c in por_n.items():
        h = c["suya"]["real"]
        v = ver.get(n)
        if v is not None and (v == h or (h == tc.DERIVAR and v == "NINGUNA")):
            confirmadas[c["id"]] = v
        else:
            disputados.append({**c, "juez": v or "NINGUNA"})
    return disputados, confirmadas


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--geometria", required=True, help="diagnostico de tc-geometria.py --json (fuera_de_su_grupo)")
    ap.add_argument("--minimo", type=int, default=18, help="llamadas por herramienta por debajo de las que no se saca")
    ap.add_argument("--grupo", type=int, default=10, help="casos por agente")
    ap.add_argument("--aplicar", action="store_true")
    ap.add_argument("--reusar", help="carpeta de una corrida anterior (/tmp/tc-fuera-...): sus decisiones, sin agentes")
    ap.add_argument("--juez", action="store_true",
                    help="antes, un juez A CIEGAS (consignas/juez.md) elige entre las dos sin saber cual es la asignada:"
                         " si coincide, la frase queda confirmada; si no, va al agente con el veredicto")
    args = ap.parse_args()
    tca = _modulo("tca", "tc-anotar.py")
    fichas, esquemas = tc.fichas(), tc.esquemas()
    anot = json.loads(ANOT.read_text())
    diag = json.loads(Path(args.geometria).expanduser().read_text())
    informe = []
    sale = collections.defaultdict(set)  # herramienta -> indices que salen

    # 1. REPETIDAS: misma plantilla normalizada y lo mismo pedido -> queda la primera
    vistas = {}
    for h in sorted(anot):
        for j, a in enumerate(anot[h]):
            if not a.get("plantilla"):
                continue
            k = (tc.normal(a["plantilla"]), pedido(a))
            if k in vistas:
                sale[h].add(j)
                informe.append({"id": f"{h}#{j}", "plantilla": a["plantilla"], "decision": "repetida",
                                "final": "sale", "queda": vistas[k]})
            else:
                vistas[k] = f"{h}#{j}"
    print(f"repetidas: salen {sum(map(len, sale.values()))} (queda una de cada)")

    # 2. LOS CASOS: cada frase que cae en otra, con su anotacion
    usados = collections.defaultdict(set)
    casos = []
    for x in diag["fuera_de_su_grupo"]:
        h, b = x["herramienta"], x["cae_en"]
        if h not in anot or b not in fichas or h not in fichas:
            continue
        j = next((j for j, a in enumerate(anot[h]) if a.get("plantilla") == x["plantilla"]
                  and j not in usados[h] and j not in sale[h]), None)
        if j is None:
            continue
        usados[h].add(j)
        a = anot[h][j]

        def ficha(o):
            return {"real": o, "descripcion": fichas[o]["description"], "esquema": esquemas.get(o, {}),
                    "frases": [y["plantilla"] for k, y in enumerate(anot.get(o, []))
                               if y.get("plantilla") and not (o == h and k == j)][:30]}
        casos.append({"id": f"{h}#{j}", "plantilla": a["plantilla"],
                      **({"falta": a["falta"]} if "falta" in a else {"args": a.get("args", {})}),
                      "suya": ficha(h), "otra": ficha(b)})
    casos.sort(key=lambda c: (c["suya"]["real"], c["otra"]["real"]))
    tmp = Path(args.reusar) if args.reusar else Path(tempfile.mkdtemp(prefix="tc-fuera-"))
    confirmadas = {}
    if args.juez:
        casos, confirmadas = juzgar(casos, anot, fichas, tmp, args.reusar)
        print(f"juez a ciegas: {len(confirmadas)} confirmadas (coincide con la asignada), {len(casos)} van al agente")
    print(f"casos para el agente: {len(casos)}")

    trabajos = []
    for i in range(0, len(casos), args.grupo):
        f = tmp / f"lote-{i}.json"
        if not args.reusar:
            f.write_text(json.dumps({"casos": casos[i:i + args.grupo]}, ensure_ascii=False, indent=1))
        trabajos.append(agente.Trabajo("tc-fuera.md", f, tmp / f"salida-{i}.jsonl", ["id", "decision"]))
    if not args.reusar:
        agente.correr_lotes(trabajos, paralelo=4)
    dec = {}
    for t in trabajos:
        if Path(t.salida).exists():
            for l in Path(t.salida).read_text().splitlines():
                try:
                    d = json.loads(l)
                except json.JSONDecodeError:
                    continue
                dec[d.get("id")] = d

    # 3. APLICAR, con el validador, la regla de copia y el minimo
    nuevo = {h: list(v) for h, v in anot.items()}
    agregar = collections.defaultdict(list)
    cuenta = collections.Counter()
    sacar_pend = []
    por_id = {c["id"]: c for c in casos}
    for cid, juez in confirmadas.items():
        h, j = cid.rsplit("#", 1)
        cuenta["confirmada por el juez: queda"] += 1
        informe.append({"id": cid, "herramienta": h, "plantilla": anot[h][int(j)]["plantilla"],
                        "decision": "juez", "juez": juez, "final": "queda (confirmada por el juez)"})
    for cid, c in por_id.items():
        h, j = cid.rsplit("#", 1)
        j = int(j)
        b = c["otra"]["real"]
        a = anot[h][j]
        d = dec.get(cid)
        fila = {"id": cid, "herramienta": h, "cae_en": b, "plantilla": a["plantilla"]}
        if d is None:
            cuenta["sin decision: queda"] += 1
            informe.append({**fila, "decision": None, "final": "queda"})
            continue
        q = d.get("decision")
        fila["decision"] = q
        if q == "queda":
            cuenta["queda"] += 1
            informe.append({**fila, "final": "queda"})
            continue
        if q == "sacar":
            sacar_pend.append((h, j, fila, d.get("motivo")))
            continue
        if q not in ("mover", "reescribir"):
            cuenta["decision rara: queda"] += 1
            informe.append({**fila, "final": "queda", "rara": q})
            continue
        dest = b if q == "mover" else h
        m = {k: v for k, v in a.items() if k not in ("args", "falta", "plantilla")}
        m["plantilla"] = d.get("plantilla") or a["plantilla"]
        m.update({"falta": d["falta"]} if "falta" in d else {"args": d.get("args", {})})
        m["fuera"] = "2026-10-17" + (f" desde {h}" if q == "mover" else " reescrita")
        err = tca.validar(m, esquemas[dest], dest) if dest in esquemas else []
        if err:
            cuenta[f"{q}, no valida: queda"] += 1
            informe.append({**fila, "final": "queda", "nueva": m["plantilla"], "error": err[:2]})
            continue
        otras = [y for k, y in enumerate(nuevo[dest]) if not (dest == h and k == j)] + agregar[dest]
        _, copias = tc.sin_copias(otras + [m])
        if any(cp is m for cp in copias):
            # la nueva ya esta en el destino: sale la original y no se agrega (queda UNA)
            sacar_pend.append((h, j, {**fila, "nueva": m["plantilla"]}, "copia de una que ya esta"))
            continue
        if q == "reescribir":
            nuevo[h][j] = m
            cuenta["reescrita"] += 1
            informe.append({**fila, "final": "reescrita", "nueva": m["plantilla"]})
        else:
            agregar[b].append(m)
            sale[h].add(j)
            cuenta["movida"] += 1
            informe.append({**fila, "final": f"movida a {b}", "nueva": m["plantilla"]})
    for h, ms in agregar.items():
        nuevo.setdefault(h, []).extend(ms)
    # DE DOS PARECIDAS SALE UNA: si la frase mas parecida a esta (en cualquier
    # herramienta) ya salio, esta queda. El agente marcaba las dos de un par, cada
    # una «casi igual» a la otra (source_update: «Traé/Mandá {fuente} al frente»)
    def parecido(x, y):
        c, d = tc.contenido(x), tc.contenido(y)
        return len(c & d) / max(1, len(c | d))
    salidas = set()
    for h, j, fila, motivo in sorted(sacar_pend, key=lambda x: (x[0], x[1])):
        # la gemela: la que el agente nombra en el motivo («casi igual a «...»»), o una
        # que comparte casi todas las palabras
        citadas = [tc.normal(x) for x in re.findall(r"[«'\"]([^»'\"]{6,})[»'\"]", motivo or "")]
        ya = [(o, k) for (o, k) in salidas | {(o, k) for o, ks in sale.items() for k in ks}
              if parecido(anot[h][j]["plantilla"], anot[o][k]["plantilla"]) >= 0.45
              or any(c and (tc.normal(anot[o][k]["plantilla"]).startswith(c[:40]) or c.startswith(
                  tc.normal(anot[o][k]["plantilla"])[:40])) for c in citadas)]
        if ya:
            cuenta["sacar, pero su gemela ya sale: queda"] += 1
            informe.append({**fila, "final": "queda (sale su gemela)", "motivo": motivo,
                            "gemela": f"{ya[0][0]}#{ya[0][1]}"})
            continue
        quedan = llamadas([y for k, y in enumerate(nuevo[h]) if k not in sale[h]])
        if quedan - (0 if "falta" in anot[h][j] else 1) < args.minimo and h != tc.DERIVAR:
            cuenta["sacar, pero bajaria del minimo: queda"] += 1
            informe.append({**fila, "final": "queda (minimo)", "motivo": motivo})
            continue
        sale[h].add(j)
        salidas.add((h, j))
        cuenta["sale"] += 1
        informe.append({**fila, "final": "sale", "motivo": motivo})
    for h, js in sale.items():
        nuevo[h] = [y for k, y in enumerate(nuevo[h]) if k not in js]
    print(dict(cuenta))
    (tmp / "informe.json").write_text(json.dumps(informe, ensure_ascii=False, indent=1))
    antes, despues = sum(map(llamadas, anot.values())), sum(map(llamadas, nuevo.values()))
    print(f"llamadas: {antes} -> {despues} · informe: {tmp / 'informe.json'}")
    if args.aplicar:
        ANOT.write_text(json.dumps(dict(sorted(nuevo.items())), ensure_ascii=False, indent=1) + "\n")
        INFORME.write_text(json.dumps(informe, ensure_ascii=False, indent=1) + "\n")
        print(f"aplicado -> {ANOT.relative_to(RAIZ)} · {INFORME.relative_to(RAIZ)}")
    else:
        (tmp / "anotaciones.json").write_text(json.dumps(dict(sorted(nuevo.items())), ensure_ascii=False, indent=1))
        print(f"sin aplicar: {tmp / 'anotaciones.json'}")


if __name__ == "__main__":
    main()
