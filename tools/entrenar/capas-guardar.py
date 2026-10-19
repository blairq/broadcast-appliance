#!/usr/bin/env python3
"""Guardar un analisis de capas (tc-capas.py) en el sistema de analisis, versionado.

Cada analisis queda con TODO lo que hace falta para leerlo despues: el corpus con
el que se entreno el modelo, las anotaciones con las que se armaron las nubes, el
banco contra el que se comparo, los parametros de entrenamiento y las medidas del
modelo. El corpus y el banco se guardan UNA vez por version (por su huella): diez
analisis del mismo corpus apuntan a la misma copia.

  ~/entrenamientos/analisis/           (o BA_ANALISIS)
    corpus/<huella>/   resumen.json, prefijos.json, colas.json, entrenar.jsonl.gz, ..., tc-anotaciones.json
    banco/<huella>/    resumen.json, orientacion.jsonl
    analisis/<0001>/   meta.json, vista.json, curvas.json

El corpus de una vuelta pesa ~700 MB porque cada fila repite el prompt de sistema
entero: se guarda el prompt comun una vez (prefijos.json) y cada fila con su turno
de usuario y su respuesta, comprimido. Sin perdida: prompt = prefijo + usuario + cola.

  # mide (si no estaba medido) y guarda
  BA_PERFIL=vivo6 tools/entrenar/capas-guardar.py --modelo ~/entrenamientos/tc-v8/vuelta-1/adaptador \\
      --grupo v8 --nombre "v8 vuelta 1"

  # guardar un analisis ya hecho
  BA_PERFIL=vivo6 tools/entrenar/capas-guardar.py --modelo ~/entrenamientos/tc-v8/vuelta-0/adaptador \\
      --capas ~/entrenamientos/capas/v8v0 --grupo v8 --nombre "v8 vuelta 0"

Despues: tools/entrenar/capas-web.py y abrir http://127.0.0.1:8770
"""
import argparse
import collections
import datetime
import gzip
import hashlib
import json
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
E = Path(__file__).resolve().parent
BASE = Path(os.environ.get("BA_ANALISIS", "~/entrenamientos/analisis")).expanduser()
MARCA = "<|im_start|>user\n"
FIN = "<|im_end|>"
ARCHIVOS_CORPUS = ["entrenar.jsonl", "validar.jsonl", "prueba.jsonl", "charla-prueba.jsonl"]


def huella_archivo(f):
    """La de tc-corpus (huella.json, «anotaciones»): sha256 del archivo, 16 letras."""
    h = hashlib.sha256()
    with open(f, "rb") as x:
        for b in iter(lambda: x.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()[:16]


def huella_corpus(d):
    h = hashlib.sha256()
    for n in ARCHIVOS_CORPUS:
        if (d / n).exists():
            h.update(n.encode())
            h.update(huella_archivo(d / n).encode())
    return h.hexdigest()[:16]


def siguiente_numero(carpeta):
    nums = [json.loads((x / "resumen.json").read_text()).get("numero", 0)
            for x in carpeta.glob("*") if (x / "resumen.json").exists()]
    return max(nums, default=0) + 1


def guardar_corpus(d, anotaciones):
    """La version del corpus (por su huella). Devuelve su resumen."""
    v = huella_corpus(d)
    dst = BASE / "corpus" / v
    if (dst / "resumen.json").exists():
        return json.loads((dst / "resumen.json").read_text())
    tmp = BASE / "corpus" / f".{v}.tmp"
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir(parents=True)
    prefijos, colas = {}, {}
    filas, por_h, por_tipo = {}, collections.Counter(), collections.Counter()
    for n in ARCHIVOS_CORPUS:
        if not (d / n).exists():
            continue
        k = 0
        with open(d / n, encoding="utf-8") as fi, gzip.open(tmp / f"{n}.gz", "wt", encoding="utf-8") as fo:
            for linea in fi:
                if not linea.strip():
                    continue
                x = json.loads(linea)
                p = x.pop("prompt", None)
                if p is not None:
                    i = p.rindex(MARCA) + len(MARCA)
                    j = p.index(FIN, i)
                    x["pre"] = prefijos.setdefault(p[:i], len(prefijos))
                    x["usuario"] = p[i:j]
                    x["cola"] = colas.setdefault(p[j:], len(colas))
                fo.write(json.dumps(x, ensure_ascii=False) + "\n")
                k += 1
                if n == "entrenar.jsonl":
                    por_tipo[x.get("tipo", "?")] += 1
                    if x.get("tipo") == "herramienta":
                        por_h[x.get("herramienta")] += 1
        filas[n] = k
    # el prompt de cada fila: prefijos[pre] + usuario + colas[cola]
    (tmp / "prefijos.json").write_text(json.dumps(list(prefijos), ensure_ascii=False))
    (tmp / "colas.json").write_text(json.dumps(list(colas), ensure_ascii=False))
    huella = json.loads((d / "huella.json").read_text()) if (d / "huella.json").exists() else {}
    if (d / "huella.json").exists():
        shutil.copy2(d / "huella.json", tmp / "huella.json")
    # las anotaciones con las que se genero (y con las que tc-capas arma las nubes)
    anot_ok = anotaciones.exists() and huella.get("anotaciones") == huella_archivo(anotaciones)
    if anot_ok:
        shutil.copy2(anotaciones, tmp / "tc-anotaciones.json")
    res = {"version": v, "numero": siguiente_numero(BASE / "corpus"), "fecha": ahora(), "origen": str(d),
           "filas": filas, "por_tipo": dict(por_tipo), "por_herramienta": dict(por_h.most_common()),
           "prefijos": len(prefijos), "huella": huella,
           "anotaciones": huella.get("anotaciones") if anot_ok else None}
    (tmp / "resumen.json").write_text(json.dumps(res, ensure_ascii=False, indent=1))
    tmp.rename(dst)
    if not anot_ok:
        print(f"OJO: datos/tc-anotaciones.json ya no es la de este corpus ({huella.get('anotaciones')}):"
              " la version del corpus queda sin anotaciones", file=sys.stderr)
    return res


def guardar_banco(f):
    v = huella_archivo(f)
    dst = BASE / "banco" / v
    if (dst / "resumen.json").exists():
        return json.loads((dst / "resumen.json").read_text())
    dst.mkdir(parents=True)
    shutil.copy2(f, dst / f.name)
    casos = [json.loads(l) for l in f.read_text().splitlines() if l.strip()]
    por_h = collections.Counter(x.get("herramienta") for x in casos)
    res = {"version": v, "numero": siguiente_numero(BASE / "banco"), "fecha": ahora(), "origen": str(f),
           "archivo": f.name, "casos": len(casos), "por_herramienta": dict(por_h.most_common())}
    (dst / "resumen.json").write_text(json.dumps(res, ensure_ascii=False, indent=1))
    return res


def parametros(ad):
    """Los de entrenar-rapido (parametros.json, desde el 17/10/2026) o, si el
    adaptador es anterior, la linea de comando que dejo el bucle en su bitacora."""
    out = {}
    if (ad / "parametros.json").exists():
        out = json.loads((ad / "parametros.json").read_text())
        out["origen"] = "parametros.json"
    else:
        for log in [ad.parent.parent / "bucle.log", ad.parent / "bucle.log"]:
            if not log.exists():
                continue
            for linea in log.read_text(errors="replace").splitlines():
                if "entrenar-rapido.py" in linea and f"--salida {ad}" in linea:
                    t = shlex.split(linea[linea.index("entrenar-rapido.py") + len("entrenar-rapido.py"):])
                    a, k = {}, 0
                    while k < len(t):
                        if t[k].startswith("--"):
                            if k + 1 < len(t) and not t[k + 1].startswith("--"):
                                a[t[k][2:].replace("-", "_")] = t[k + 1]
                                k += 2
                                continue
                            a[t[k][2:].replace("-", "_")] = True
                        k += 1
                    out = {"args": a, "origen": f"{log.name} (reconstruido; lo no escrito tiene el valor por defecto)"}
                    break
            if out:
                break
            # el bucle corta la linea en la bitacora: si el adaptador es de una vuelta de
            # tc-bucle, la llamada es la de su plantilla (tc-bucle.py, «2. entrenar»)
            if ad.name == "adaptador" and ad.parent.name.startswith("vuelta-") and "entrenar-rapido.py" in log.read_text(errors="replace"):
                out = {"args": {"fsdp": True, "recalcular": True, "base": "Qwen/Qwen3.5-4B", "datos": str(ad.parent / "corpus"),
                                "salida": str(ad), "epocas": 2.0, "eval_cada": 200, "lote": 16, "micro": 2,
                                "objetivo": "sft", "lr": 2e-4, "rango": 32, "semilla": 7},
                       "origen": "plantilla de tc-bucle.py (la bitacora corta la linea; --epocas 2 salvo que el bucle"
                                 " se lanzara con otro valor)"}
                break
    if (ad / "adapter_config.json").exists():
        c = json.loads((ad / "adapter_config.json").read_text())
        out["lora"] = {k: c.get(k) for k in ("r", "lora_alpha", "lora_dropout", "target_modules", "base_model_name_or_path")}
    return out


def ahora():
    return datetime.datetime.now().isoformat(timespec="seconds")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--modelo", required=True, help="el adaptador analizado")
    ap.add_argument("--capas", help="la salida de tc-capas.py (sin esto: ~/entrenamientos/capas/<nombre>, y se mide si falta)")
    ap.add_argument("--corpus", help="el corpus con el que se entreno (por defecto <adaptador>/../corpus)")
    ap.add_argument("--medidas", help="medidas.json del modelo (por defecto <adaptador>/../medidas.json)")
    ap.add_argument("--grupo", default="", help="para juntar analisis en el navegador (p. ej. v8)")
    ap.add_argument("--nombre", default="")
    ap.add_argument("--nota", default="")
    ap.add_argument("--limite-banco", type=int, default=500)
    args = ap.parse_args()
    ad = Path(args.modelo).expanduser().resolve()
    vuelta = ad.parent
    slug = f"{vuelta.parent.name}-{vuelta.name}" if ad.name == "adaptador" else ad.name
    capas = Path(args.capas).expanduser() if args.capas else Path.home() / "entrenamientos" / "capas" / slug
    medidas = Path(args.medidas).expanduser() if args.medidas else vuelta / "medidas.json"
    corpus = Path(args.corpus).expanduser() if args.corpus else vuelta / "corpus"
    if not (capas / "curvas.json").exists():
        if not os.environ.get("BA_PERFIL"):
            sys.exit("falta BA_PERFIL para medir (p. ej. BA_PERFIL=vivo6)")
        cmd = [sys.executable, str(E / "tc-capas.py"), "--modelo", str(ad), "--salida", str(capas),
               "--limite-banco", str(args.limite_banco)] + (["--medidas", str(medidas)] if medidas.exists() else [])
        print("$", " ".join(cmd), flush=True)
        subprocess.run(cmd, check=True, env={"CUDA_VISIBLE_DEVICES": "0", **os.environ})
    cur = json.loads((capas / "curvas.json").read_text())
    if Path(cur["modelo"]).expanduser().resolve() != ad:
        sys.exit(f"{capas} es el analisis de {cur['modelo']}, no de {ad}")
    if not (corpus / "entrenar.jsonl").exists():
        sys.exit(f"no esta el corpus de entrenamiento en {corpus} (--corpus)")
    anot = RAIZ / "datos" / "tc-anotaciones.json"
    banco_f = RAIZ / "datos" / "orientacion.jsonl"
    # las nubes salen de las anotaciones y el banco de orientacion.jsonl DEL MOMENTO del analisis:
    # si cambiaron despues, lo que se guardaria no es lo que se midio
    for clave, f in (("anotaciones", anot), ("banco", banco_f)):
        if cur.get(clave) and cur[clave] != huella_archivo(f):
            sys.exit(f"{f.name} cambio desde el analisis ({cur[clave]} -> {huella_archivo(f)}): "
                     "volver a medir (borrar la carpeta de --capas) o guardarlo desde un worktree de ese momento")
    BASE.mkdir(parents=True, exist_ok=True)
    print("corpus…", flush=True)
    vc = guardar_corpus(corpus, anot)
    vb = guardar_banco(banco_f)
    hechos = sorted(int(x.name) for x in (BASE / "analisis").glob("[0-9]*") if x.name.isdigit())
    num = (hechos[-1] + 1) if hechos else 1
    dst = BASE / "analisis" / f"{num:04d}"
    dst.mkdir(parents=True)
    for n in ("vista.json", "curvas.json"):
        shutil.copy2(capas / n, dst / n)
    med = json.loads(medidas.read_text())["medidas"] if medidas.exists() else {}
    try:
        commit = subprocess.run(["git", "-C", str(RAIZ), "rev-parse", "--short", "HEAD"],
                                capture_output=True, text=True).stdout.strip()
    except OSError:
        commit = ""
    erradas = cur.get("erradas", {})
    meta = {"id": f"{num:04d}", "fecha": ahora(), "grupo": args.grupo, "nombre": args.nombre or slug, "nota": args.nota,
            "modelo": str(ad), "capas": str(capas), "estados": str(capas / "estados.pt"),
            "corpus": {"version": vc["version"], "numero": vc["numero"]},
            "banco": {"version": vb["version"], "numero": vb["numero"], "limite": cur.get("limite_banco", args.limite_banco)},
            "anotaciones": cur.get("anotaciones") or vc.get("anotaciones"),
            "perfil": os.environ.get("BA_PERFIL"), "commit": commit,
            "parametros": parametros(ad), "medidas": med, "erradas": erradas}
    (dst / "meta.json").write_text(json.dumps(meta, ensure_ascii=False, indent=1))
    print(f"analisis {meta['id']} · {meta['nombre']} · corpus c{vc['numero']} ({vc['version']}) · "
          f"banco b{vb['numero']} ({vb['version']}) -> {dst}")


if __name__ == "__main__":
    main()
