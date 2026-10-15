#!/usr/bin/env python3
"""Pares de preferencia (dpo) y respuestas marcadas (kto) con los errores del PROPIO modelo.

PARA QUE. Despues del SFT quedan errores puntuales: llamar con un dato inventado
cuando habia que preguntarlo, la herramienta vecina, un argumento mal copiado.
entrenar-rapido.py --objetivo dpo|kto los empuja en contra (preferencias.py),
pero necesita respuestas malas que el modelo DE VERDAD da: las de este script.

COMO. Igual que el autoexamen de tc-bucle, con mas casos:
  1. rinde las plantillas ENTRENADAS (no las de prueba, que solo miden) con
     estados nuevos, y una parte con un nombre que no esta en el estado (lo
     correcto es preguntar: falta_dato). Las mismas anotaciones y los mismos
     filtros que tc-corpus.py: solo las que validan, sin copias, y fuera lo que se
     parece al banco o al examen. Nunca se usa el banco ni el examen.
  2. el modelo contesta cada una voraz y --muestras veces con temperatura (en
     lote, sobre el mismo cache: cuesta casi lo mismo que una). Una placa por
     proceso; con --placas 0,1 la mitad de los casos en cada una.
  3. cada respuesta se juzga como en tc-evaluar (herramienta, argumentos exactos,
     falta_dato). La buena es la del corpus; cada mala distinta es un par. Fuera
     las frases de dos oraciones («¿sigue grabando? Pará»): son dos pedidos y el
     modelo contesta una llamada; ni se miden ni se entrenan asi.

QUE DEJA, en --salida:
    dpo/{entrenar,validar}.jsonl   {prompt, chosen, rejected} + de donde salio
    kto/{entrenar,validar}.jsonl   {prompt, completion, buena}: las malas, la
                                   buena de esas mismas frases, y buenas de las
                                   que salieron bien hasta parejar
    resumen.json                   cuantos fallos de cada clase y por herramienta
validar son las plantillas apartadas (--validar, por plantilla: ninguna frase de
validar comparte plantilla con entrenar).

    .venv-entrenar/bin/python tools/entrenar/tc-preferencias.py \\
        --modelo ~/entrenamientos/tc-v6b/mejor --salida ~/entrenamientos/tc-v6b-pref/pares
"""
import argparse
import collections
import hashlib
import importlib.util
import json
import os
import random
import re
import subprocess
import sys
import time
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
E = Path(__file__).resolve().parent
sys.path.insert(0, str(E))
import lib_tc as tc  # noqa: E402


def modulo(nombre, archivo):
    spec = importlib.util.spec_from_file_location(nombre, E / archivo)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def rendir_casos(args, rnd):
    """Las filas a contestar, como las rinde tc-corpus (mismas anotaciones y filtros)."""
    tcc = modulo("tcc", "tc-corpus.py")
    anot, _ = tcc.anotaciones_entrenables(args.anotaciones, args.copia)
    parecida = tcc.filtro_parecidas(args.parecido)
    filas = []
    for h, lista in sorted(anot.items()):
        for a in lista:
            if not a.get("plantilla") or (tcc.es_prueba(a["frase"], args.prueba) and not a.get("nueva")):
                continue
            filas += tcc.rendir(h, a, rnd, args.por_plantilla, variar=True)
            if rnd.random() < args.ausentes:
                f = tcc.ausente(h, a, rnd)
                if f:
                    filas.append(f)
    n0 = len(filas)
    filas = [f for f in filas if not parecida(f)]
    vistas, out = set(), []
    for f in filas:
        clave = (f["frase"], f["herramienta"], json.dumps(f["args"], sort_keys=True))
        if clave not in vistas:
            vistas.add(clave)
            out.append(f)
    print(f"casos: {len(out)} (fuera {n0 - len(filas)} parecidas al banco o al examen,"
          f" {len(filas) - len(out)} repetidas)", flush=True)
    return out


def compuesta(frase):
    """Dos oraciones («¿sigue grabando? Pará»): dos pedidos en uno. El modelo
    contesta UNA llamada, asi que no se mide ni se entrena con eso."""
    return bool(re.search(r"[?!.]\s+\S", frase))


def juzgar(f, texto, igual):
    """(bien, motivo) como tc-evaluar: falta pide falta_dato; si no, herramienta y argumentos exactos."""
    h, a = tc.leer(texto)
    if f["tipo"] == "falta":
        if h == tc.FALTA:
            return True, "bien"
        return False, "sin_formato" if h is None else ("falta_deriva" if h == tc.DERIVAR else "falta_inventa")
    esperada = f["herramienta"]
    if h is None:
        return False, "sin_formato"
    if h != esperada:
        if h == tc.FALTA:
            return False, "pregunta_de_mas"
        if h == tc.DERIVAR:
            return False, "deriva_de_mas"
        return False, "no_deriva" if esperada == tc.DERIVAR else "vecina"
    if isinstance(a, dict) and set(a) == set(f["args"]) and all(igual(f["args"][k], a[k]) for k in a):
        return True, "bien"
    return False, "argumentos"


def trabajador(args):
    """Contesta las filas que le tocan: voraz y --muestras con temperatura."""
    import torch
    from transformers import AutoModelForCausalLM, AutoTokenizer
    import cache_hibrido as ch
    torch.manual_seed(args.semilla + args.parte)
    m_dir = Path(args.modelo).expanduser()
    if (m_dir / "adapter_config.json").exists():
        from peft import PeftModel
        cfg = json.loads((m_dir / "adapter_config.json").read_text())
        modelo = AutoModelForCausalLM.from_pretrained(cfg["base_model_name_or_path"], dtype=torch.bfloat16,
                                                      device_map={"": "cuda:0"})
        modelo = PeftModel.from_pretrained(modelo, str(m_dir)).merge_and_unload()
        tok = AutoTokenizer.from_pretrained(str(m_dir))
    else:
        modelo = AutoModelForCausalLM.from_pretrained(args.modelo, dtype=torch.bfloat16, device_map={"": "cuda:0"})
        tok = AutoTokenizer.from_pretrained(args.modelo)
    modelo.eval()
    dev = modelo.device
    if ch.es_hibrido(modelo):
        ch.parchear()
    cab = tc.cabeza()
    ids_cab = tok(cab, add_special_tokens=False, return_tensors="pt").input_ids.to(dev)
    with torch.no_grad():
        # sin los logits de los ~12 mil tokens del prefijo (12k x 248k: 6 GB que nadie usa)
        kv = modelo(input_ids=ids_cab, use_cache=True, logits_to_keep=1).past_key_values
    Lp = ids_cab.shape[1]
    foto = ch.foto(kv)
    fin = tok.convert_tokens_to_ids("<|im_end|>")

    def adelante(ids, cache):
        with torch.no_grad():
            return modelo(input_ids=ids, past_key_values=cache, use_cache=True, logits_to_keep=1).logits[:, -1]

    def contestar(usuario):
        """[voraz, muestra 1..k]. Las k muestras van en un lote sobre una copia del cache."""
        try:
            lg = adelante(torch.tensor([tok(usuario + tc.COLA, add_special_tokens=False).input_ids], device=dev), kv)
            kv_k = ch.desde_tensores(modelo.config, ch.tensores(kv)) if args.muestras else None
            if kv_k is not None:
                ch.repetir(kv_k, args.muestras)
            out = [generar(lg, kv, voraz=True)[0]]
        finally:
            ch.volver(kv, foto, Lp)
        if kv_k is not None:
            out += generar(lg.expand(args.muestras, -1), kv_k, voraz=False)
        return out

    def generar(lg, cache, voraz):
        n = lg.shape[0]
        toks, vivos = [[] for _ in range(n)], [True] * n
        for _ in range(args.max_nuevos):
            if voraz:
                t = lg.argmax(-1, keepdim=True)
            else:
                t = torch.multinomial(torch.softmax(lg.float() / args.temperatura, -1), 1)
            for j, x in enumerate(t[:, 0].tolist()):
                if not vivos[j]:
                    continue
                if x == fin:
                    vivos[j] = False
                    continue
                toks[j].append(x)
                if tok.decode(toks[j][-6:]).endswith("</tool_call>"):
                    vivos[j] = False
            if not any(vivos):
                break
            lg = adelante(t, cache)
        return [tok.decode(x) for x in toks]

    filas = [json.loads(l) for l in Path(args.trabajador).read_text().splitlines() if l.strip()]
    t0 = time.time()
    with open(args.resultado, "w", encoding="utf-8") as sal:
        for k, (i, f) in enumerate(filas):
            usuario = f["prompt"][len(cab):-len(tc.COLA)]
            sal.write(json.dumps({"i": i, "respuestas": contestar(usuario)}, ensure_ascii=False) + "\n")
            sal.flush()
            if (k + 1) % 100 == 0:
                dt = time.time() - t0
                print(f"[placa {args.parte}] {k + 1}/{len(filas)} · {dt / (k + 1):.1f} s/caso · "
                      f"faltan {dt / (k + 1) * (len(filas) - k - 1) / 60:.0f} min", flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", required=True, help="el adaptador SFT del que salen las malas (y la referencia)")
    ap.add_argument("--salida", required=True)
    ap.add_argument("--anotaciones", default=str(RAIZ / "datos/tc-anotaciones.json"))
    ap.add_argument("--por-plantilla", type=int, default=1, help="estados nuevos por plantilla entrenada")
    ap.add_argument("--ausentes", type=float, default=0.5,
                    help="porcion de plantillas que suman su version con un nombre que no esta (falta_dato)")
    ap.add_argument("--muestras", type=int, default=4, help="respuestas con temperatura por caso, ademas de la voraz")
    ap.add_argument("--temperatura", type=float, default=0.8)
    ap.add_argument("--max-nuevos", type=int, default=120)
    ap.add_argument("--max-malas", type=int, default=2, help="malas distintas por caso, como mucho")
    ap.add_argument("--con-compuestas", action="store_true", help="no sacar las frases de dos oraciones")
    ap.add_argument("--validar", type=float, default=0.1, help="porcion de plantillas apartadas para validar")
    ap.add_argument("--placas", default="0,1")
    ap.add_argument("--limite", type=int, default=0, help="solo N casos (prueba)")
    ap.add_argument("--semilla", type=int, default=21)
    # los de tc-corpus, para rendir igual
    ap.add_argument("--prueba", type=float, default=0.15)
    ap.add_argument("--copia", type=float, default=0.7)
    ap.add_argument("--parecido", type=float, default=0.6)
    # internos: un proceso por placa
    ap.add_argument("--trabajador", help=argparse.SUPPRESS)
    ap.add_argument("--resultado", help=argparse.SUPPRESS)
    ap.add_argument("--parte", type=int, default=0, help=argparse.SUPPRESS)
    args = ap.parse_args()
    if args.trabajador:
        return trabajador(args)

    sal = Path(args.salida).expanduser()
    sal.mkdir(parents=True, exist_ok=True)
    rnd = random.Random(args.semilla)
    casos_arch = sal / "casos.jsonl"
    if casos_arch.exists():  # retomar: los mismos casos
        filas = [json.loads(l) for l in casos_arch.read_text().splitlines() if l.strip()]
        print(f"casos: {len(filas)} (de {casos_arch})")
    else:
        filas = rendir_casos(args, rnd)
        rnd.shuffle(filas)
        if args.limite:
            filas = filas[:args.limite]
        casos_arch.write_text("".join(json.dumps(f, ensure_ascii=False) + "\n" for f in filas))

    # UN PROCESO POR PLACA, cada uno con su parte. Lo ya contestado no se repite
    # (se puede cortar y volver a correr con la misma --salida)
    placas = [p for p in args.placas.split(",") if p != ""]
    hechos = {}
    for r in sal.glob("respuestas-*.jsonl"):
        for l in r.read_text().splitlines():
            try:
                x = json.loads(l)
            except json.JSONDecodeError:  # la ultima linea de un corte
                continue
            hechos[x["i"]] = x["respuestas"]
    faltan = [(i, f) for i, f in enumerate(filas) if i not in hechos]
    if faltan:
        print(f"a contestar: {len(faltan)} casos en {len(placas)} placas ({len(hechos)} ya hechos)", flush=True)
        procs = []
        sello = int(time.time())
        for k, p in enumerate(placas):
            parte = faltan[k::len(placas)]
            arch = sal / f"parte-{k}.jsonl"
            arch.write_text("".join(json.dumps([i, f], ensure_ascii=False) + "\n" for i, f in parte))
            cmd = [sys.executable, __file__, "--modelo", args.modelo, "--salida", str(sal), "--trabajador", str(arch),
                   "--resultado", str(sal / f"respuestas-{sello}-{k}.jsonl"), "--parte", str(k),
                   "--muestras", str(args.muestras), "--temperatura", str(args.temperatura),
                   "--max-nuevos", str(args.max_nuevos), "--semilla", str(args.semilla)]
            procs.append(subprocess.Popen(cmd, cwd=RAIZ, env=dict(os.environ, CUDA_VISIBLE_DEVICES=p)))
        if any(pr.wait() for pr in procs):
            sys.exit("fallo un trabajador: volver a correr con la misma --salida sigue donde quedo")
        for r in sal.glob(f"respuestas-{sello}-*.jsonl"):
            for l in r.read_text().splitlines():
                x = json.loads(l)
                hechos[x["i"]] = x["respuestas"]
        for k in range(len(placas)):
            (sal / f"parte-{k}.jsonl").unlink(missing_ok=True)

    # JUZGAR Y ARMAR
    igual = modulo("tce", "tc-evaluar.py").igual
    validar = {}

    def en_validar(f):
        p = f.get("plantilla") or f["frase"]
        if p not in validar:
            validar[p] = int(hashlib.sha256((p + "#pref").encode()).hexdigest()[:8], 16) / 0xFFFFFFFF < args.validar
        return validar[p]

    dpo = {"entrenar": [], "validar": []}
    kto = {"entrenar": [], "validar": []}
    buenas_sueltas = {"entrenar": [], "validar": []}
    motivos, por_herr, voraz_mal = collections.Counter(), collections.Counter(), collections.Counter()
    vecinas = collections.Counter()
    n_comp = 0
    for i, f in enumerate(filas):
        if not args.con_compuestas and compuesta(f["frase"]):
            n_comp += 1
            continue
        resp = hechos[i]
        parte = "validar" if en_validar(f) else "entrenar"
        base = {"prompt": f["prompt"], "herramienta": f["herramienta"], "tipo": f["tipo"],
                "plantilla": f.get("plantilla"), "frase": f["frase"]}
        malas, vistas = [], set()
        for j, texto in enumerate(resp):
            bien, motivo = juzgar(f, texto, igual)
            if j == 0 and not bien:
                voraz_mal[f["tipo"]] += 1
            if bien or texto.strip() in vistas:
                continue
            vistas.add(texto.strip())
            malas.append((texto, motivo, j == 0))
            motivos[motivo] += 1
            por_herr[f["herramienta"]] += 1
            if motivo == "vecina":
                vecinas[f"{f['herramienta']} -> {tc.leer(texto)[0]}"] += 1
        if not malas:
            buenas_sueltas[parte].append({**base, "completion": f["completion"], "buena": True})
            continue
        malas.sort(key=lambda x: not x[2])  # la voraz primero: es la que se ve al aire
        for texto, motivo, es_voraz in malas[:args.max_malas]:
            rej = texto + tc.FIN
            dpo[parte].append({**base, "chosen": f["completion"], "rejected": rej, "motivo": motivo,
                               "voraz": es_voraz})
            kto[parte].append({**base, "completion": rej, "buena": False, "motivo": motivo, "voraz": es_voraz})
        kto[parte].append({**base, "completion": f["completion"], "buena": True})
    # KTO PAREJO: tantas buenas como malas (las de las frases que fallaron y, para
    # completar, de las que salieron bien)
    for parte in kto:
        n_malas = sum(not x["buena"] for x in kto[parte])
        n_buenas = sum(x["buena"] for x in kto[parte])
        rnd.shuffle(buenas_sueltas[parte])
        kto[parte] += buenas_sueltas[parte][:max(0, n_malas - n_buenas)]
    for nombre, d in (("dpo", dpo), ("kto", kto)):
        (sal / nombre).mkdir(exist_ok=True)
        for parte, lista in d.items():
            rnd.shuffle(lista)
            (sal / nombre / f"{parte}.jsonl").write_text("".join(json.dumps(x, ensure_ascii=False) + "\n"
                                                                for x in lista))
    tipos = collections.Counter(f["tipo"] for f in filas if args.con_compuestas or not compuesta(f["frase"]))
    resumen = {
        "modelo": str(Path(args.modelo).expanduser().resolve()), "casos": len(filas) - n_comp,
        "compuestas_fuera": n_comp, "por_tipo": dict(tipos),
        "muestras": args.muestras, "temperatura": args.temperatura,
        "voraz_mal": {t: f"{voraz_mal[t]}/{tipos[t]}" for t in tipos},
        "malas_distintas": dict(motivos.most_common()), "malas_por_herramienta": dict(por_herr.most_common(25)),
        "vecinas": dict(vecinas.most_common(25)),
        "dpo": {p: len(v) for p, v in dpo.items()},
        "kto": {p: {"buenas": sum(x["buena"] for x in v), "malas": sum(not x["buena"] for x in v)}
                for p, v in kto.items()},
        "anotaciones": hashlib.sha256(Path(args.anotaciones).read_bytes()).hexdigest()[:16],
        "prompt": tc.huella(), "semilla": args.semilla,
    }
    (sal / "resumen.json").write_text(json.dumps(resumen, ensure_ascii=False, indent=1) + "\n")
    print(json.dumps({k: resumen[k] for k in ("casos", "compuestas_fuera", "voraz_mal", "malas_distintas", "dpo", "kto")},
                     ensure_ascii=False))
    print(f"listo: {sal}")


if __name__ == "__main__":
    main()
