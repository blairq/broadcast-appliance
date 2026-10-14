#!/usr/bin/env python3
"""El corpus PAREJO: cada herramienta del perfil con --min a --max frases, todas distintas.

POR QUE. Despues de reasignar (tc-reasignar.py) algunas herramientas quedaron con 2
o 6 frases (speakers_status, bancas_listar: casi todo lo que tenian valia tambien
para la vecina) y otras siguen con 40 (source_update). Una nube de 2 puntos no se
puede medir ni aprender; una de 40 con la mitad de variantes pesa el doble sin
enseñar mas.

QUE HACE, en este orden:
  1. limpiar   saca del archivo lo que de todos modos no entra al corpus: las que no
               validan (esquema y STE-ES) y las copias (lib_tc.sin_copias).
  2. completar las que tienen menos de --min: un agente (consignas/tc-completar.md)
               escribe frases de CONTRASTE con sus vecinas (los grupos de
               tc-reasignar.grupos con la geometria) y de variedad; pasan el
               validador, la regla de copia y la novedad del vector
               (tc-geometria.py --filtrar). Hasta --vueltas veces.
  3. podar     las que tienen mas de --max: salen las que menos suman segun el
               modelo (tc-geometria.py --podar), primero las del agente.

El instrumento por defecto es el modelo BASE: no vio ninguna de estas frases (el
entrenado las tiene apretadas por memoria). Como el base no distingue los pares
que se pisan, lo que cae en el grupo de la vecina no se descarta (--permitir-otra):
de eso se encarga el contraste de la consigna.

    BA_PERFIL=vivo6 tools/entrenar/tc-parejo.py --geometria geo.json --aplicar
"""
import argparse
import collections
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
E = Path(__file__).resolve().parent
PY = RAIZ / ".venv-entrenar/bin/python"
sys.path.insert(0, str(E))
import lib_tc as tc  # noqa: E402

ANOT = RAIZ / "datos" / "tc-anotaciones.json"


def _modulo(nombre, archivo):
    spec = importlib.util.spec_from_file_location(nombre, E / archivo)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def geometria(modelo, extra, log):
    cmd = [str(PY), str(E / "tc-geometria.py"), "--modelo", modelo] + [str(x) for x in extra]
    with open(log, "w") as f:
        r = subprocess.run(cmd, cwd=RAIZ, env=dict(os.environ, CUDA_VISIBLE_DEVICES="0"), stdout=f,
                           stderr=subprocess.STDOUT)
    if r.returncode:
        sys.exit(f"fallo tc-geometria (ver {log})")
    return Path(log).read_text().strip().splitlines()[-1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--geometria", required=True, help="diagnostico de tc-geometria.py (para las vecinas)")
    ap.add_argument("--modelo", default="Qwen/Qwen3.5-4B", help="el instrumento")
    ap.add_argument("--min", type=int, default=20)
    ap.add_argument("--max", type=int, default=25)
    ap.add_argument("--objetivo", type=int, default=23)
    ap.add_argument("--vueltas", type=int, default=3)
    ap.add_argument("--falta-max", type=int, default=3, help="anotaciones de falta_dato por herramienta")
    ap.add_argument("--grupo", type=int, default=4, help="herramientas por agente")
    ap.add_argument("--aplicar", action="store_true")
    args = ap.parse_args()

    import agente
    tca = _modulo("tca", "tc-anotar.py")
    rea = _modulo("rea", "tc-reasignar.py")
    fichas, esquemas = tc.fichas(), tc.esquemas()
    mias = [h for h in fichas if h not in (tc.DERIVAR, tc.FALTA)]
    anot = json.loads(ANOT.read_text())
    tmp = Path(tempfile.mkdtemp(prefix="tc-parejo-"))
    print(f"trabajo: {tmp}")

    # 1. limpiar
    inval = cop = sin_falta = 0
    for h in mias:
        buenas = [a for a in anot.get(h, []) if not tca.validar(a, esquemas[h], h)]
        inval += len(anot.get(h, [])) - len(buenas)
        anot[h], cp = tc.sin_copias(buenas)
        cop += len(cp)
        # FALTA_DATO CON TOPE POR HERRAMIENTA: rotulo_vincular tenia 25 de 25,
        # grafico_ocultar 30 de 31 («sacá el gráfico del clima» anotada como
        # «¿cuál?»); con el tope global de tc-corpus casi no quedaban LLAMADAS de
        # esas herramientas y el modelo las derivaba. Quedan las primeras (las
        # humanas antes) y el resto lo completa el agente con llamadas.
        con_falta = [a for a in anot[h] if "falta" in a]
        if len(con_falta) > args.falta_max:
            fuera = {id(a) for a in con_falta[args.falta_max:]}
            anot[h] = [a for a in anot[h] if id(a) not in fuera]
            sin_falta += len(fuera)
    n = lambda h: sum(1 for a in anot.get(h, []) if a.get("plantilla"))
    print(f"limpiar: fuera {inval} que no validan, {cop} copias y {sin_falta} de falta_dato de mas")

    # las vecinas: los grupos que se pisan, y la familia (mismo prefijo)
    diag = json.loads(Path(args.geometria).expanduser().read_text())
    vec = collections.defaultdict(set)
    for g in rea.grupos(diag, set(mias), 0.15, 3, 7):
        for h in g:
            vec[h] |= set(g) - {h}
    for h in mias:
        vec[h] |= {o for o in mias if o != h and o.split("_")[0] == h.split("_")[0]}

    # 2. completar
    for vuelta in range(args.vueltas):
        faltan = {h: args.objetivo - n(h) for h in mias if n(h) < args.min}
        if not faltan:
            break
        print(f"vuelta {vuelta}: {len(faltan)} herramientas cortas: "
              + ", ".join(f"{h} {n(h)}" for h in sorted(faltan, key=n)))
        trabajos = []
        hs = sorted(faltan)
        for i in range(0, len(hs), args.grupo):
            lote = [{"real": h, "nombre": tc.a_modelo(h), "descripcion": fichas[h]["description"],
                     "esquema": esquemas[h], "cuantas": faltan[h] + 3,
                     "anotaciones": [a["plantilla"] for a in anot.get(h, []) if a.get("plantilla")],
                     "vecinas": [{"nombre": tc.a_modelo(o), "descripcion": fichas[o]["description"]}
                                 for o in sorted(vec[h])][:8]} for h in hs[i:i + args.grupo]]
            f = tmp / f"v{vuelta}-lote-{i}.json"
            f.write_text(json.dumps(lote, ensure_ascii=False, indent=1))
            trabajos.append(agente.Trabajo("tc-completar.md", f, tmp / f"v{vuelta}-salida-{i}.jsonl",
                                           ["herramienta", "plantilla"]))
        agente.correr_lotes(trabajos, paralelo=4)
        cands, malas, copias = [], 0, 0
        for t in trabajos:
            if not Path(t.salida).exists():
                continue
            por_h = collections.defaultdict(list)
            for l in Path(t.salida).read_text().splitlines():
                try:
                    a = json.loads(l)
                except json.JSONDecodeError:
                    continue
                h = a.pop("herramienta", None)
                if h not in faltan:
                    malas += 1
                    continue
                a.setdefault("frase", a["plantilla"])
                if "falta" not in a:
                    a.setdefault("args", {})
                if tca.validar(a, esquemas[h], h):
                    malas += 1
                    continue
                a.update(nueva=True, origen="tc-completar")
                por_h[h].append(a)
            for h, nuevas in por_h.items():
                quedan, cp = tc.sin_copias(anot.get(h, []) + nuevas)
                copias += len(cp)
                cands += [{"herramienta": h, **a} for a in quedan if a.get("origen") == "tc-completar"
                          and a not in anot.get(h, [])]
        if not cands:
            print("  nada nuevo que valide")
            continue
        cf = tmp / f"v{vuelta}-candidatas.jsonl"
        cf.write_text("".join(json.dumps(c, ensure_ascii=False) + "\n" for c in cands))
        # el instrumento ve las frases con el prompt del perfil: las cuentas contra el
        # archivo de anotaciones de ESTA vuelta (las limpias), no el del disco
        ANOT_TMP = tmp / f"v{vuelta}-anot.json"
        ANOT_TMP.write_text(json.dumps(anot, ensure_ascii=False))
        os.environ["BA_TC_ANOTACIONES"] = str(ANOT_TMP)
        res = geometria(args.modelo, ["--filtrar", cf, "--permitir-otra", "--aprobadas", tmp / f"v{vuelta}-aprobadas.jsonl"],
                        tmp / f"v{vuelta}-filtro.log")
        aprob = [json.loads(l) for l in (tmp / f"v{vuelta}-aprobadas.jsonl").read_text().splitlines() if l.strip()]
        cuenta = collections.Counter()
        for a in aprob:
            h = a.pop("herramienta")
            if n(h) < args.max:
                anot.setdefault(h, []).append(a)
                cuenta[h] += 1
        print(f"  {len(cands)} candidatas ({malas} no validan, {copias} copias) · {res} · entran {sum(cuenta.values())}")

    # 3. podar
    ANOT_TMP = tmp / "poda-anot.json"
    ANOT_TMP.write_text(json.dumps(anot, ensure_ascii=False))
    os.environ["BA_TC_ANOTACIONES"] = str(ANOT_TMP)
    # el tope es de la herramienta entera: la nube no tiene las de falta, asi que su
    # tope es --max menos esas (con 8 de falta, 17 en la nube)
    topes = {h: max(1, args.max - sum(1 for a in anot.get(h, []) if "falta" in a)) for h in mias}
    (tmp / "topes.json").write_text(json.dumps(topes))
    res = geometria(args.modelo, ["--podar", args.max, "--topes", tmp / "topes.json", "--salida-poda", tmp / "poda.json"],
                    tmp / "poda.log")
    print(f"podar: {res}")
    sale = json.loads((tmp / "poda.json").read_text())
    for h, pls in sale.items():
        if h not in mias:
            continue  # derivar no se empareja: sale de otras fuentes
        fuera = set(pls)
        anot[h] = [a for a in anot[h] if a.get("plantilla") not in fuera]

    cortas = sorted((n(h), h) for h in mias if n(h) < args.min)
    largas = sorted(((n(h), h) for h in mias if n(h) > args.max), reverse=True)
    v = sorted(n(h) for h in mias)
    print(f"queda: min {v[0]} · mediana {v[len(v) // 2]} · max {v[-1]} · cortas {cortas} · largas {largas}")
    if args.aplicar:
        ANOT.write_text(json.dumps(dict(sorted(anot.items())), ensure_ascii=False, indent=1) + "\n")
        print(f"aplicado -> {ANOT}")
    else:
        (tmp / "anotaciones.json").write_text(json.dumps(dict(sorted(anot.items())), ensure_ascii=False, indent=1))
        print(f"sin aplicar: {tmp / 'anotaciones.json'}")


if __name__ == "__main__":
    main()
