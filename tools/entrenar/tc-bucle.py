#!/usr/bin/env python3
"""El bucle del formato de tool calling: corpus -> entrenar -> medir -> arreglar -> otra vez.

Como el bucle del corpus de nombres (bucle-corpus.md), con la metodologia que
funciono (docs/modelos/entrenamiento-avanzado.md), para el formato nuevo:

  1. corpus     tc-corpus.py con las anotaciones de turno.
  2. entrenar   entrenar-rapido.py (prefijo compartido: las herramientas del
                system; recalculo por capa: el prefijo son ~8.000 tokens).
  3. medir      tc-evaluar.py en las dos placas a la vez: prueba + charla en una,
                banco (o una muestra) + examen en la otra. Lo que MIDE nunca lo
                ve el agente: ni prueba, ni banco, ni examen.
  4. buscar     tc-evaluar.py sobre las plantillas ENTRENADAS rendidas con
                estados que nunca vio («autoexamen»): si ahi falla, no aprendio a
                copiar del estado o el patron.
  5. geometria  tc-geometria.py con el adaptador de la vuelta: la CDNV entre
                las nubes de cada herramienta (bucle-corpus.md), a la bitacora.
  6. arreglar   un agente (consignas/tc-arreglar.md) corrige la anotacion si
                estaba mal o escribe anotaciones nuevas que enseñen lo que fallo,
                sabiendo con quien se pisa cada herramienta y que plantillas caen
                en otra. Lo nuevo pasa por el validador (tc-anotar.validar) y por
                los tres filtros de la geometria (cae en su grupo o corrige un
                error visto, suma variedad, no acerca vecinas) antes de sumarse.
  7. otra vuelta, hasta --vueltas o hasta que dos vueltas seguidas no mejoren.

Cada vuelta queda en datos/bitacora-tc.jsonl (versionada, como la del corpus) y
el mejor adaptador en <trabajo>/mejor.

    tools/entrenar/tc-bucle.py --trabajo ~/entrenamientos/tc --vueltas 3
"""
import argparse
import collections
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
sys.path.insert(0, str(RAIZ / "tools" / "entrenar"))
sys.path.insert(0, str(E))
import lib_tc as tc  # noqa: E402

BITACORA = RAIZ / "datos" / "bitacora-tc.jsonl"
ANOT = RAIZ / "datos" / "tc-anotaciones.json"


def log(*a):
    print("\033[35m[tc-bucle]\033[0m", *a, flush=True)


PARADO = 3
PARAR = None  # <trabajo>/PARAR: tools/entrenar/tc-parar.sh lo crea


def parar_si_toca(donde):
    """Entre etapas: con PARAR, se sale (codigo 3). Se retoma con --retomar."""
    if PARAR is not None and PARAR.exists():
        log(f"PARADO a pedido ({donde}). Para seguir: tc-bucle.py --trabajo ... --retomar")
        sys.exit(PARADO)


def correr(cmd, env=None, salida=None):
    cmd = [str(c) for c in cmd]
    parar_si_toca(Path(cmd[1]).name if len(cmd) > 1 else cmd[0])
    log("$", " ".join(cmd)[:200])
    with open(salida, "w") if salida else open(os.devnull, "w") as f:
        r = subprocess.run(cmd, cwd=RAIZ, env=dict(os.environ, **(env or {}), BA_PARAR=str(PARAR or "")),
                           stdout=f, stderr=subprocess.STDOUT)
    if r.returncode:
        parar_si_toca(f"durante {Path(cmd[1]).name if len(cmd) > 1 else cmd[0]}")
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
    mias = set(tc.esquemas())
    filas = []
    for h, lista in sorted(anot.items()):
        if h not in mias:
            continue  # las que salieron del perfil: ahi derivar es lo correcto
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


def geometria(adaptador, salida, v, filtrar=None, evidencia=None):
    """tc-geometria.py en un proceso aparte (la placa 0 queda libre para entrenar).
    Sin `filtrar`: el diagnostico de la vuelta (a la bitacora). Con `filtrar`: los
    tres filtros sobre las anotaciones nuevas; devuelve las aprobadas."""
    cmd = [PY, E / "tc-geometria.py", "--modelo", adaptador]
    if filtrar is None:
        correr(cmd + ["--json", salida], env={"CUDA_VISIBLE_DEVICES": "0"}, salida=salida.with_suffix(".log"))
        d = json.loads(salida.read_text())
        res = {"cdnv_mediana": round(d["cdnv_mediana"], 4), "cdnv_peor": round(d["cdnv_peor"], 4),
               "pares_sobre_objetivo": d["pares_sobre_objetivo"], "pares_medidos": d["pares_medidos"],
               "dim_mediana": round(d["dim_mediana"], 2), "fuera_de_su_grupo": len(d["fuera_de_su_grupo"])}
        log(f"geometria: {json.dumps(res)}")
        with open(BITACORA, "a", encoding="utf-8") as b:
            b.write(json.dumps({"fecha": datetime.datetime.now().isoformat(timespec="seconds"), "vuelta": v,
                                "geometria": res}, ensure_ascii=False) + "\n")
        return d
    ev = salida.with_suffix(".evidencia.json")
    ev.write_text(json.dumps([[h, e, n] for (h, e), n in evidencia.items()], ensure_ascii=False))
    correr(cmd + ["--filtrar", filtrar, "--evidencia", ev, "--aprobadas", salida],
           env={"CUDA_VISIBLE_DEVICES": "0"}, salida=salida.with_suffix(".log"))
    log(Path(salida.with_suffix(".log")).read_text().strip().splitlines()[-1])
    return [json.loads(l) for l in salida.read_text().splitlines() if l.strip()]


def arreglar(fallos, dir_, adaptador, v):
    """Agente: corregir la anotacion o sumar nuevas. Devuelve cuantas se sumaron.

    Con la geometria (tc-geometria.py, la CDNV de bucle-corpus en el formato tc):
    el agente recibe con quien se pisa cada herramienta y que plantillas suyas caen
    en otra, y lo NUEVO que escribe pasa por los tres filtros antes de entrar."""
    import agente
    import importlib.util
    spec = importlib.util.spec_from_file_location("tca", E / "tc-anotar.py")
    tca = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tca)
    anot = json.loads(ANOT.read_text())
    esquemas = tc.esquemas()
    funcs = tc.fichas()
    por_h = {}
    for f in fallos:
        if f["herramienta"] not in funcs:
            continue
        por_h.setdefault(f["herramienta"], []).append(f)
    # LAS CONGELADAS NO SE REESCRIBEN (datos/tc-congeladas.json): sus preguntas ya
    # se afinaron varias veces y cada vuelta que las tocaba rompia algo (la v7).
    # Sus fallos quedan anotados para mirarlos, no van al agente.
    cong = {h: v for h, v in por_h.items() if h in tc.congeladas()}
    if cong:
        (dir_ / "fallos-congeladas.json").write_text(json.dumps(cong, ensure_ascii=False, indent=1))
        log(f"congeladas con fallos (no se tocan): {len(cong)} herramientas, {sum(map(len, cong.values()))} fallos"
            f" -> {dir_ / 'fallos-congeladas.json'}")
    por_h = {h: v for h, v in por_h.items() if h not in cong}
    if not por_h:
        return 0
    diag = geometria(adaptador, dir_ / "geometria.json", v)
    caen = collections.defaultdict(list)
    for x in diag["fuera_de_su_grupo"]:
        caen[x["herramienta"]].append({"plantilla": x["plantilla"], "cae_en": x["cae_en"]})

    def geo_de(h):
        peor = diag["por_herramienta"].get(h, {}).get("peor", {})
        return {"vecina": peor.get("con"), "cdnv": round(peor.get("cdnv", 0.0), 3), "caen_en_otra": caen[h][:6]}
    trabajos = []
    items = sorted(por_h.items())
    for i in range(0, len(items), 6):
        lote = [{"herramienta": h, "descripcion": funcs[h]["description"], "esquema": esquemas[h],
                 "anotaciones": [{k: a[k] for k in ("plantilla", "args", "falta") if k in a} for a in anot.get(h, [])],
                 "errores": [{k: e[k] for k in ("plantilla", "frase", "esperado", "obtenido")} for e in errs[:8]],
                 "geometria": geo_de(h)}
                for h, errs in items[i:i + 6]]
        f = dir_ / f"arreglar-{i}.json"
        f.write_text(json.dumps(lote, ensure_ascii=False, indent=1))
        trabajos.append(agente.Trabajo("tc-arreglar.md", f, dir_ / f"arreglos-{i}.jsonl", ["herramienta", "plantilla"]))
    agente.correr_lotes(trabajos, paralelo=4)
    sumadas = corregidas = rechazadas = 0
    candidatas = []
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
                candidatas.append({"herramienta": h, **a})
    if candidatas:
        # lo que el modelo REAL hizo mal (herramienta -> la que eligio): la evidencia
        # que deja entrar un contraste aunque caiga del lado de la vecina
        evidencia = collections.Counter(
            (f["herramienta"], f["obtenido"][0]) for f in fallos
            if isinstance(f.get("obtenido"), list) and f["obtenido"] and f["obtenido"][0] != f["herramienta"])
        cand = dir_ / "candidatas.jsonl"
        cand.write_text("".join(json.dumps(c, ensure_ascii=False) + "\n" for c in candidatas))
        for a in geometria(adaptador, dir_ / "aprobadas.jsonl", v, filtrar=cand, evidencia=evidencia):
            anot.setdefault(a.pop("herramienta"), []).append(a)
            sumadas += 1
    ANOT.write_text(json.dumps(dict(sorted(anot.items())), ensure_ascii=False, indent=1) + "\n")
    log(f"arreglos: {sumadas} nuevas (de {len(candidatas)} propuestas), {corregidas} corregidas,"
        f" {rechazadas} rechazadas por invalidas")
    return sumadas + corregidas


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trabajo", default="~/entrenamientos/tc")
    ap.add_argument("--base", default="Qwen/Qwen3.5-4B")
    ap.add_argument("--vueltas", type=int, default=3)
    ap.add_argument("--epocas", type=float, default=2.0)
    ap.add_argument("--limite-banco", type=int, default=500, help="muestra del banco por vuelta (0 = entero)")
    ap.add_argument("--desde", type=int, default=0, help="numero de la primera vuelta (para retomar)")
    ap.add_argument("--retomar", action="store_true",
                    help="seguir donde quedo (parado o caido): la vuelta la deduce de las carpetas; --vueltas"
                         " es el total contando las ya hechas")
    args = ap.parse_args()
    W = Path(args.trabajo).expanduser()
    W.mkdir(parents=True, exist_ok=True)
    # EL PERFIL A MANO: sin BA_PERFIL todo sale con el reactivo (otras herramientas,
    # otro orden) y nada falla, solo se entrena y se mide otra cosa
    if not os.environ.get("BA_PERFIL"):
        sys.exit("falta BA_PERFIL (el perfil con el que se entrena, p. ej. BA_PERFIL=vivo6)")
    log(f"perfil {os.environ['BA_PERFIL']} · prompt {tc.huella()}")
    global PARAR
    PARAR = W / "PARAR"
    if PARAR.exists():
        PARAR.unlink()  # arrancar (o retomar) es la orden contraria
    (W / "bucle.pid").write_text(str(os.getpid()))
    hasta = args.desde + args.vueltas
    if args.retomar:
        hechas = sorted(int(x.name.split("-")[1]) for x in W.glob("vuelta-*") if x.name.split("-")[1].isdigit())
        if hechas:
            ult = hechas[-1]
            # una vuelta esta cerrada cuando termino su arreglo (o era la ultima)
            args.desde = ult + 1 if (W / f"vuelta-{ult}" / "cerrada").exists() else ult
        hasta = args.vueltas
        log(f"retomo en la vuelta {args.desde} (hasta la {hasta - 1})")
    rnd = random.Random(31)
    mejor_p, sin_mejora = -1.0, 0
    if (W / "mejor" / "puntaje.json").exists() and \
            json.loads((W / "mejor" / "puntaje.json").read_text()).get("limite_banco") == args.limite_banco:
        mejor_p = json.loads((W / "mejor" / "puntaje.json").read_text())["puntaje"]  # retomar no vuelve a medir
    elif (W / "mejor" / "medidas.json").exists():
        # el mejor se re-mide con la MISMA muestra del banco que las vueltas nuevas:
        # comparar el banco entero contra una muestra de 500 corto el bucle de la v3
        # («dos vueltas sin mejorar» cuando la vuelta 3 si mejoraba)
        corpus_mejor = sorted(W.glob("vuelta-*/corpus"))[-1]
        mejor_p = puntaje(medir(W / "mejor", corpus_mejor, W / "mejor" / "medidas.json", args.limite_banco)["medidas"])
    for v in range(args.desde, hasta):
        d = W / f"vuelta-{v}"
        d.mkdir(exist_ok=True)
        log(f"=== vuelta {v} ===")
        corpus = d / "corpus"
        ad = d / "adaptador"
        # una vuelta cortada a mitad del entrenamiento sigue desde su estado: con el
        # MISMO corpus (el estado lleva su huella; regenerarlo lo invalidaria)
        a_medias = (ad / "estado" / "estado.pt").exists() and not (ad / "adapter_config.json").exists()
        if not (a_medias and (corpus / "huella.json").exists()):
            correr([PY, E / "tc-corpus.py", "--salida", corpus], salida=d / "corpus.log")
        huella = json.loads((corpus / "huella.json").read_text())
        ya_estaba = (ad / "adapter_config.json").exists()
        if not ya_estaba:
            # las dos placas como una (FSDP2; entrenar-rapido supervisa y reanuda)
            correr([PY, E / "entrenar-rapido.py", "--fsdp", "--recalcular", "--base", args.base, "--datos", corpus,
                    "--salida", ad, "--epocas", str(args.epocas), "--eval-cada", "200", "--lote", "16", "--micro", "2"]
                   + (["--reanudar"] if a_medias else []),
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
            (W / "mejor" / "puntaje.json").write_text(json.dumps({"puntaje": p, "vuelta": v,
                                                                   "limite_banco": args.limite_banco}))
            log(f"nuevo mejor: vuelta {v}")
        elif not ya_estaba:  # una vuelta retomada ya entrenada no cuenta (si no, retomar corta el bucle)
            sin_mejora += 1
            if sin_mejora >= 2:
                log("dos vueltas sin mejorar: listo")
                break
        if v == hasta - 1:
            (d / "cerrada").write_text("ultima")
            break
        # buscar que arreglar en lo ENTRENADO, con estados nuevos
        auto = autoexamen(corpus, d / "autoexamen", rnd)
        correr([PY, E / "tc-evaluar.py", "--modelo", ad, "--corpus", auto, "--conjuntos", "prueba",
                "--json", d / "autoexamen.json"], env={"CUDA_VISIBLE_DEVICES": "0"}, salida=d / "autoexamen.log")
        fal = json.loads((d / "autoexamen.json").read_text())["fallos"].get("prueba", [])
        log(f"autoexamen: {len(fal)} fallos")
        if not arreglar(fal, d, ad, v):
            log("nada que arreglar")
        (d / "cerrada").write_text("arreglada")  # retomar sigue en la vuelta siguiente
    log(f"mejor: {W / 'mejor'} · puntaje {mejor_p:.3f}")


if __name__ == "__main__":
    main()
