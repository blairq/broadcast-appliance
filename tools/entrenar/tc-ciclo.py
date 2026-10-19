#!/usr/bin/env python3
"""El ciclo de bancos: cada banco intermedio primero MIDE y despues ENSEÑA.

  paso 1: modelo 1 = corpus 0                      -> se mide con el banco 1 -> aprendidas 1
  paso 2: modelo 2 = corpus 0 + aprendidas 1       -> se mide con el banco 2 -> aprendidas 2
  paso 3: modelo 3 = corpus 0 + aprendidas 1..2    -> se mide con el banco 3 -> aprendidas 3
  paso 4: modelo 4 = corpus 0 + aprendidas 1..3    -> (sin banco intermedio)

Cada modelo se entrena desde la BASE con todo lo acumulado (no se continua el
anterior). Cada banco intermedio es nuevo para el modelo que mide: la medida es
honesta; despues lo que ese modelo erro entra al corpus (tc-aprendidas.py: la
frase tal cual como eje y variaciones centradas alrededor). TODOS los modelos se
miden ademas con el BANCO FINAL ENTERO (datos/orientacion.jsonl, el de siempre,
que nunca entra) y el examen: esa es la vara para compararlos entre si y con las
versiones anteriores. Y cada uno queda en el sistema de analisis (capas-guardar.py,
grupo «ciclo»).

Los bancos intermedios: tc-banco-escribir.py --numero N (datos/bancos-ciclo/).

  BA_PERFIL=vivo6 nohup tools/entrenar/tc-ciclo.py --trabajo ~/entrenamientos/tc-ciclo \\
      --modelo-1 ~/entrenamientos/tc-v8/vuelta-0/adaptador > ~/entrenamientos/tc-ciclo/ciclo.log 2>&1 &

--modelo-1: el paso 1 ya existe si hay un modelo entrenado con el corpus 0 (se ahorra
un entrenamiento). Se retoma solo: cada etapa deja su archivo y no se rehace. Para
parar: touch <trabajo>/PARAR (el entrenamiento guarda el estado y sale).
"""
import argparse
import datetime
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
E = Path(__file__).resolve().parent
PY = Path(sys.executable)
BANCOS = RAIZ / "datos" / "bancos-ciclo"
APR = RAIZ / "datos" / "tc-aprendidas.json"
W = PARAR = None


def log(*a):
    print(f"\033[36m[tc-ciclo]\033[0m {datetime.datetime.now():%H:%M}", *a, flush=True)


def correr(cmd, salida, env=None):
    if PARAR.exists():
        sys.exit(f"PARADO a pedido (antes de {Path(str(cmd[1])).name}). Para seguir: el mismo comando")
    cmd = [str(c) for c in cmd]
    log("$", " ".join(cmd)[:240])
    with open(salida, "w") as f:
        r = subprocess.run(cmd, cwd=RAIZ, env=dict(os.environ, **(env or {}), BA_PARAR=str(PARAR)),
                           stdout=f, stderr=subprocess.STDOUT)
    if r.returncode:
        if PARAR.exists():
            sys.exit(f"PARADO a pedido (durante {Path(cmd[1]).name}). Para seguir: el mismo comando")
        sys.exit(f"fallo (ver {salida})")


def medir(ad, corpus, d, banco_k):
    """Placa 0: prueba, charla, banco FINAL entero y examen. Placa 1: el banco intermedio."""
    final = d / "medidas.json"
    if not final.exists():
        a = subprocess.Popen([str(PY), E / "tc-evaluar.py", "--modelo", ad, "--corpus", corpus,
                              "--conjuntos", "prueba,charla,banco,examen", "--json", d / "medidas.final.json"],
                             cwd=RAIZ, env=dict(os.environ, CUDA_VISIBLE_DEVICES="0"),
                             stdout=open(d / "medidas.final.log", "w"), stderr=subprocess.STDOUT)
        b = None
        if banco_k:
            b = subprocess.Popen([str(PY), E / "tc-evaluar.py", "--modelo", ad, "--corpus", corpus,
                                  "--conjuntos", "banco", "--banco", banco_k, "--json", d / f"{banco_k.stem}.json"],
                                 cwd=RAIZ, env=dict(os.environ, CUDA_VISIBLE_DEVICES="1"),
                                 stdout=open(d / f"{banco_k.stem}.log", "w"), stderr=subprocess.STDOUT)
        log(f"midiendo {ad.parent.name}: banco final entero y examen" + (f" · {banco_k.name}" if banco_k else ""))
        if a.wait() or (b and b.wait()):
            sys.exit(f"fallo midiendo (ver {d}/medidas.final.log / {banco_k.stem if banco_k else ''}.log)")
        shutil.copy(d / "medidas.final.json", final)
    m = json.loads(final.read_text())["medidas"]
    res = {k: {kk: round(v * 100, 1) for kk, v in m.get(k, {}).items() if isinstance(v, float)}
           for k in ("prueba", "banco", "examen")}
    if banco_k and (d / f"{banco_k.stem}.json").exists():
        res[banco_k.stem] = {kk: round(v * 100, 1) for kk, v in
                             json.loads((d / f"{banco_k.stem}.json").read_text())["medidas"]["banco"].items()
                             if isinstance(v, float)}
    log("medidas:", json.dumps(res, ensure_ascii=False))
    return res


def main():
    global W, PARAR
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--trabajo", default="~/entrenamientos/tc-ciclo")
    ap.add_argument("--modelo-1", help="un adaptador ya entrenado con el corpus 0 (se saltea el primer entrenamiento)")
    ap.add_argument("--pasos", type=int, default=4)
    ap.add_argument("--detener-despues", type=int, default=0,
                    help="parar al terminar este paso (medido, guardado y con sus aprendidas), para mirarlo antes de"
                         " seguir; volver a lanzar sin esto retoma en el paso siguiente")
    ap.add_argument("--base", default="Qwen/Qwen3.5-4B")
    ap.add_argument("--epocas", type=float, default=2.0)
    ap.add_argument("--peso-eje", type=int, default=3)
    ap.add_argument("--peso-variacion", type=int, default=1)
    args = ap.parse_args()
    if not os.environ.get("BA_PERFIL"):
        sys.exit("falta BA_PERFIL (p. ej. BA_PERFIL=vivo6)")
    W = Path(args.trabajo).expanduser()
    W.mkdir(parents=True, exist_ok=True)
    PARAR = W / "PARAR"
    if PARAR.exists():
        PARAR.unlink()
    (W / "ciclo.pid").write_text(str(os.getpid()))
    resumen = json.loads((W / "resumen.json").read_text()) if (W / "resumen.json").exists() else {}
    for k in range(1, args.pasos + 1):
        d = W / f"paso-{k}"
        d.mkdir(exist_ok=True)
        banco_k = BANCOS / f"banco-{k}.jsonl"
        banco_k = banco_k if k < args.pasos else None   # el ultimo paso solo se mide con el final
        if banco_k and not banco_k.exists():
            sys.exit(f"falta {banco_k}: tools/entrenar/tc-banco-escribir.py --numero {k}")
        log(f"=== paso {k} ===")
        # 1. el modelo
        if k == 1 and args.modelo_1:
            ad = Path(args.modelo_1).expanduser().resolve()
            corpus = ad.parent / "corpus"
        else:
            corpus, ad = d / "corpus", d / "adaptador"
            if not (corpus / "huella.json").exists():
                cmd = [PY, E / "tc-corpus.py", "--salida", corpus]
                if k > 1:
                    cmd += ["--aprendidas", APR, "--bancos-hasta", k - 1,
                            "--peso-eje", args.peso_eje, "--peso-variacion", args.peso_variacion]
                correr(cmd, d / "corpus.log")
            if not (ad / "adapter_config.json").exists():
                a_medias = (ad / "estado" / "estado.pt").exists()
                correr([PY, E / "entrenar-rapido.py", "--fsdp", "--recalcular", "--base", args.base, "--datos", corpus,
                        "--salida", ad, "--epocas", args.epocas, "--eval-cada", 200, "--lote", 16, "--micro", 2]
                       + (["--reanudar"] if a_medias else []), d / "entrenar.log",
                       env={"PYTORCH_CUDA_ALLOC_CONF": "expandable_segments:True"})
        # 2. medirlo: banco final entero + examen, y el banco intermedio de este paso
        res = medir(ad, corpus, d, banco_k)
        # 3. al sistema de analisis (las capas: placa 0)
        if not (d / "capas-guardado").exists():
            correr([PY, E / "capas-guardar.py", "--modelo", ad, "--medidas", d / "medidas.json", "--grupo", "ciclo",
                    "--nombre", f"ciclo paso {k}", "--nota", f"corpus 0 + aprendidas de los bancos 1..{k - 1}"
                    if k > 1 else "corpus 0", "--capas", W / "capas" / f"paso-{k}"], d / "capas.log")
            (d / "capas-guardado").touch()
        # 4. lo que erro en el banco intermedio -> aprendidas (antes de entrenar el siguiente)
        if banco_k and not (d / "aprendidas-hecho").exists():
            correr([PY, E / "tc-aprendidas.py", "--numero", k, "--medidas", d / f"{banco_k.stem}.json"],
                   d / "aprendidas.log", env={"CUDA_VISIBLE_DEVICES": "0"})
            (d / "aprendidas-hecho").touch()
        resumen[f"paso-{k}"] = {"modelo": str(ad), "medidas": res}
        (W / "resumen.json").write_text(json.dumps(resumen, ensure_ascii=False, indent=1))
        if args.detener_despues and k >= args.detener_despues:
            (d / "paso-terminado").touch()
            log(f"paso {k} terminado: paro aca (--detener-despues). Para seguir: el mismo comando sin esa opcion")
            return
    log("ciclo terminado:", json.dumps(resumen, ensure_ascii=False))


if __name__ == "__main__":
    main()
