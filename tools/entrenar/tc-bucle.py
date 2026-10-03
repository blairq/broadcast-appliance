#!/usr/bin/env python3
"""El bucle del formato de tool calling: corpus -> entrenar -> medir -> arreglar -> otra vez.

Como el bucle del corpus de nombres (bucle-corpus.md), con la metodologia que
funciono (docs/entrenamiento-avanzado.md), para el formato nuevo:

  1. corpus     tc-corpus.py con las anotaciones de turno.
  2. entrenar   entrenar-rapido.py (prefijo compartido: las herramientas del
                system; recalculo por capa: el prefijo son ~8.000 tokens).
  3. medir      tc-evaluar.py en las dos placas a la vez: prueba + charla en una,
                banco (o una muestra) + examen en la otra. Lo que MIDE nunca lo
                ve el agente: ni prueba, ni banco, ni examen.
  4. buscar     tc-evaluar.py sobre las plantillas ENTRENADAS rendidas con
                estados que nunca vio («autoexamen»): si ahi falla, no aprendio a
                copiar del estado o el patron.
  5. arreglar   un agente (consignas/tc-arreglar.md) corrige la anotacion si
                estaba mal o escribe anotaciones nuevas que enseñen lo que fallo;
                el programa las valida (tc-anotar.validar) antes de sumarlas.
  6. otra vuelta, hasta --vueltas o hasta que dos vueltas seguidas no mejoren.

Cada vuelta queda en datos/bitacora-tc.jsonl (versionada, como la del corpus) y
el mejor adaptador en <trabajo>/mejor.

    tools/entrenar/tc-bucle.py --trabajo ~/entrenamientos/tc --vueltas 3
"""
import argparse
import datetime
import json
import os
import random
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
E = Path(__file__).resolve().parent
PY = RAIZ / ".venv-entrenar/bin/python"
sys.path.insert(0, str(RAIZ / "tools"))
sys.path.insert(0, str(E))
import lib_tc as tc  # noqa: E402

BITACORA = RAIZ / "datos" / "bitacora-tc.jsonl"
ANOT = RAIZ / "datos" / "tc-anotaciones.json"


def log(*a):
    print("\033[35m[tc-bucle]\033[0m", *a, flush=True)


def correr(cmd, env=None, salida=None):
    cmd = [str(c) for c in cmd]
    log("$", " ".join(cmd)[:200])
    with open(salida, "w") if salida else open(os.devnull, "w") as f:
        r = subprocess.run(cmd, cwd=RAIZ, env=dict(os.environ, **(env or {})), stdout=f, stderr=subprocess.STDOUT)
    if r.returncode:
        sys.exit(f"fallo: {' '.join(cmd)[:200]} (ver {salida})")


def puntaje(m):
    """Lo que manda: argumentos exactos en prueba y precision en el banco (lo que
    se ve al aire), con la cobertura y el derivado como desempate."""
    p, b, e = m.get("prueba", {}), m.get("banco", {}), m.get("examen", {})
    return (p.get("args_exactos", 0) + b.get("precision", 0)
            + 0.5 * (b.get("cobertura", 0) + b.get("derivado", 0)) + 0.5 * p.get("falta_dato", 0)
            + 0.5 * e.get("acierto", 0))  # el examen tambien manda (la vuelta 2 lo subio 6,6 puntos)


def autoexamen(corpus, dest, rnd, n=400):
    """Las plantillas entrenadas, con estados NUEVOS: donde buscar que arreglar."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("tcc", E / "tc-corpus.py")
    tcc = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tcc)
    anot = json.loads(ANOT.read_text())
    filas = []
    for h, lista in sorted(anot.items()):
        for a in lista:
            if not a.get("plantilla"):
                continue
            if tcc.es_prueba(a.get("frase", a["plantilla"]), 0.15) and not a.get("nueva"):
                continue  # las de prueba no: esas solo miden
            filas += tcc.rendir(h, a, rnd, 1, variar=True)
    rnd.shuffle(filas)
    dest.mkdir(parents=True, exist_ok=True)
    (dest / "prueba.jsonl").write_text("\n".join(json.dumps(f, ensure_ascii=False) for f in filas[:n]) + "\n")
    return dest


def medir(adaptador, corpus, salida, limite_banco):
    """Las dos placas a la vez."""
    a = subprocess.Popen([str(PY), E / "tc-evaluar.py", "--modelo", adaptador, "--corpus", corpus,
                          "--conjuntos", "prueba,charla", "--json", salida.with_suffix(".a.json")],
                         cwd=RAIZ, env=dict(os.environ, CUDA_VISIBLE_DEVICES="0"),
                         stdout=open(salida.with_suffix(".a.log"), "w"), stderr=subprocess.STDOUT)
    b = subprocess.Popen([str(PY), E / "tc-evaluar.py", "--modelo", adaptador, "--corpus", corpus,
                          "--conjuntos", "banco,examen", "--limite-banco", str(limite_banco),
                          "--json", salida.with_suffix(".b.json")],
                         cwd=RAIZ, env=dict(os.environ, CUDA_VISIBLE_DEVICES="1"),
                         stdout=open(salida.with_suffix(".b.log"), "w"), stderr=subprocess.STDOUT)
    if a.wait() or b.wait():
        sys.exit(f"fallo midiendo (ver {salida.with_suffix('.a.log')} / .b.log)")
    ra, rb = (json.loads(salida.with_suffix(x).read_text()) for x in (".a.json", ".b.json"))
    out = {"medidas": {**ra["medidas"], **rb["medidas"]}, "fallos": {**ra["fallos"], **rb["fallos"]}}
    salida.write_text(json.dumps(out, ensure_ascii=False, indent=1))
    return out


def arreglar(fallos, dir_):
    """Agente: corregir la anotacion o sumar nuevas. Devuelve cuantas se sumaron."""
    import agente
    import importlib.util
    spec = importlib.util.spec_from_file_location("tca", E / "tc-anotar.py")
    tca = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tca)
    anot = json.loads(ANOT.read_text())
    esquemas = tc.esquemas()
    funcs = {f["function"]["name"]: f["function"] for f in tc.funciones()}
    por_h = {}
    for f in fallos:
        por_h.setdefault(f["herramienta"], []).append(f)
    if not por_h:
        return 0
    trabajos = []
    items = sorted(por_h.items())
    for i in range(0, len(items), 6):
        lote = [{"herramienta": h, "descripcion": funcs[h]["description"], "esquema": esquemas[h],
                 "anotaciones": [{k: a[k] for k in ("plantilla", "args", "falta") if k in a} for a in anot.get(h, [])],
                 "errores": [{k: e[k] for k in ("plantilla", "frase", "esperado", "obtenido")} for e in errs[:8]]}
                for h, errs in items[i:i + 6]]
        f = dir_ / f"arreglar-{i}.json"
        f.write_text(json.dumps(lote, ensure_ascii=False, indent=1))
        trabajos.append(agente.Trabajo("tc-arreglar.md", f, dir_ / f"arreglos-{i}.jsonl", ["herramienta", "plantilla"]))
    agente.correr_lotes(trabajos, paralelo=4)
    sumadas = corregidas = rechazadas = 0
    for t in trabajos:
        if not Path(t.salida).exists():
            continue
        for l in Path(t.salida).read_text().splitlines():
            try:
                a = json.loads(l)
            except json.JSONDecodeError:
                continue
            h = a.pop("herramienta", None)
            if h not in esquemas:
                continue
            if tca.validar(a, esquemas[h], h):
                rechazadas += 1
                continue
            a.setdefault("frase", a["plantilla"])
            if a.get("corrige"):
                orig = a.pop("corrige")
                for x in anot.get(h, []):
                    if x.get("plantilla") == orig:
                        x.update({k: v for k, v in a.items() if k != "frase"})
                        x.pop("falta", None) if "args" in a else x.pop("args", None)
                        corregidas += 1
                        break
            else:
                a["nueva"] = True
                a["origen"] = "bucle"
                anot.setdefault(h, []).append(a)
                sumadas += 1
    ANOT.write_text(json.dumps(dict(sorted(anot.items())), ensure_ascii=False, indent=1) + "\n")
    log(f"arreglos: {sumadas} nuevas, {corregidas} corregidas, {rechazadas} rechazadas por invalidas")
    return sumadas + corregidas


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trabajo", default="~/entrenamientos/tc")
    ap.add_argument("--base", default="Qwen/Qwen3.5-4B")
    ap.add_argument("--vueltas", type=int, default=3)
    ap.add_argument("--epocas", type=float, default=2.0)
    ap.add_argument("--limite-banco", type=int, default=500, help="muestra del banco por vuelta (0 = entero)")
    ap.add_argument("--desde", type=int, default=0, help="numero de la primera vuelta (para retomar)")
    args = ap.parse_args()
    W = Path(args.trabajo).expanduser()
    W.mkdir(parents=True, exist_ok=True)
    rnd = random.Random(31)
    mejor_p, sin_mejora = -1.0, 0
    if (W / "mejor" / "medidas.json").exists():
        # el mejor se re-mide con la MISMA muestra del banco que las vueltas nuevas:
        # comparar el banco entero contra una muestra de 500 corto el bucle de la v3
        # («dos vueltas sin mejorar» cuando la vuelta 3 si mejoraba)
        corpus_mejor = sorted(W.glob("vuelta-*/corpus"))[-1]
        mejor_p = puntaje(medir(W / "mejor", corpus_mejor, W / "mejor" / "medidas.json", args.limite_banco)["medidas"])
    for v in range(args.desde, args.desde + args.vueltas):
        d = W / f"vuelta-{v}"
        d.mkdir(exist_ok=True)
        log(f"=== vuelta {v} ===")
        corpus = d / "corpus"
        correr([PY, E / "tc-corpus.py", "--salida", corpus], salida=d / "corpus.log")
        huella = json.loads((corpus / "huella.json").read_text())
        ad = d / "adaptador"
        if not (ad / "adapter_config.json").exists():
            correr([PY, E / "entrenar-rapido.py", "--repartir", "--recalcular", "--base", args.base, "--datos", corpus,
                    "--salida", ad, "--epocas", str(args.epocas), "--eval-cada", "200", "--lote", "16", "--micro", "4"],
                   env={"PYTORCH_CUDA_ALLOC_CONF": "expandable_segments:True"}, salida=d / "entrenar.log")
        m = medir(ad, corpus, d / "medidas.json", args.limite_banco)
        med = m["medidas"]
        p = puntaje(med)
        log(f"medidas: {json.dumps(med)} · puntaje {p:.3f} (mejor {mejor_p:.3f})")
        with open(BITACORA, "a", encoding="utf-8") as b:
            b.write(json.dumps({"fecha": datetime.datetime.now().isoformat(timespec="seconds"), "vuelta": v,
                                "corpus": huella, "medidas": med, "puntaje": round(p, 4)}, ensure_ascii=False) + "\n")
        if p > mejor_p + 0.005:
            mejor_p, sin_mejora = p, 0
            if (W / "mejor").exists():
                shutil.rmtree(W / "mejor")
            shutil.copytree(ad, W / "mejor")
            shutil.copy(d / "medidas.json", W / "mejor" / "medidas.json")
            log(f"nuevo mejor: vuelta {v}")
        else:
            sin_mejora += 1
            if sin_mejora >= 2:
                log("dos vueltas sin mejorar: listo")
                break
        if v == args.desde + args.vueltas - 1:
            break
        # buscar que arreglar en lo ENTRENADO, con estados nuevos
        auto = autoexamen(corpus, d / "autoexamen", rnd)
        correr([PY, E / "tc-evaluar.py", "--modelo", ad, "--corpus", auto, "--conjuntos", "prueba",
                "--json", d / "autoexamen.json"], env={"CUDA_VISIBLE_DEVICES": "0"}, salida=d / "autoexamen.log")
        fal = json.loads((d / "autoexamen.json").read_text())["fallos"].get("prueba", [])
        log(f"autoexamen: {len(fal)} fallos")
        if not arreglar(fal, d):
            log("nada que arreglar")
    log(f"mejor: {W / 'mejor'} · puntaje {mejor_p:.3f}")


if __name__ == "__main__":
    main()
