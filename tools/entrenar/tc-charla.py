#!/usr/bin/env python3
"""Charla general para el corpus de tool calling: lo que evita que el modelo olvide lo genérico.

Con solo comandos el modelo se especializa y pierde capacidad general (Qwen3-4B
conservó el 61 % ajustado a un catálogo, arXiv 2605.17774); mezclando texto
general se mitiga (Alopex, 2411.05209). Ver docs/modelos/entrenamiento-avanzado.md.

Las RESPUESTAS las escribe el modelo BASE (Qwen3.5 sin ajustar, sin thinking):
lo que se le enseña a conservar es su propio comportamiento, no el estilo de
otro. Los PEDIDOS salen de:
  - databricks-dolly-15k multilingüe, la parte en castellano (CC-BY-SA 3.0;
    se usan solo las instrucciones, para entrenar, no se redistribuyen);
  - pedidos del oficio: explicaciones sobre frases del operador y fragmentos de
    docs/ (los mismos de entrenar-mtp.py).
Se guardan solo las respuestas TERMINADAS (no cortadas por el largo).

    tools/entrenar/tc-charla.py --salida ~/entrenamientos/tc/charla.jsonl --cuantas 4000 [--parte 0/2]
"""
import argparse
import json
import random
import subprocess
import sys
import time
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
HF = RAIZ / ".venv-entrenar/bin/hf"
# como va a contestar en uso: rioplatense, corto y dicho en voz alta (pruebas de la v2: salia neutro o
# peninsular, «mantén», «tienes»). Y nunca afirma una accion (la v2 dijo «le escribo a Juan»).
# sin esto Qwen se extiende y 2 de cada 3 respuestas no terminan en 220 tokens
BREVE = ("Sos el asistente de un estudio de TV en Buenos Aires y lo que contestás se dice en voz alta por los "
         "auriculares del operador. Respondé en castellano rioplatense, con voseo (tenés, mirá, fijate), en una o dos "
         "oraciones, sin listas ni markdown. Nunca digas que hiciste algo: solo contestás.")
DOLLY = "argilla/databricks-dolly-15k-curated-multilingual"


def pedidos_dolly():
    r = subprocess.run([str(HF), "download", DOLLY, "--repo-type", "dataset", "--include", "data/es-*"],
                       capture_output=True, text=True, check=True)
    ruta = Path(r.stdout.strip().splitlines()[-1].split("=", 1)[-1].strip())
    import pandas as pd
    df = pd.concat(pd.read_parquet(p) for p in sorted(ruta.glob("data/es-*.parquet")))
    out = []
    for _, f in df.iterrows():
        ins = str(f.get("instruction") or "").strip()
        ctx = str(f.get("context") or "").strip()
        if not ins or len(ins) > 400:
            continue
        out.append(f"{ins}\n\n{ctx}" if ctx and len(ctx) < 900 else ins)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", default="Qwen/Qwen3.5-4B")
    ap.add_argument("--salida", required=True)
    ap.add_argument("--cuantas", type=int, default=4000)
    ap.add_argument("--oficio", type=float, default=0.3, help="proporción de pedidos del oficio")
    ap.add_argument("--largo", type=int, default=220)
    ap.add_argument("--parte", default="0/1", help="i/n: generar solo esa parte (una por placa)")
    ap.add_argument("--semilla", type=int, default=11)
    args = ap.parse_args()

    import importlib.util
    spec = importlib.util.spec_from_file_location("mtp", Path(__file__).parent / "entrenar-mtp.py")
    mtp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mtp)
    rnd = random.Random(args.semilla)
    dolly = pedidos_dolly()
    rnd.shuffle(dolly)
    n_of = int(args.cuantas * args.oficio)
    pedidos = [("general", p) for p in dolly[:args.cuantas - n_of]] + \
              [("oficio", p) for p in mtp.prompts_texto(n_of, args.semilla)]
    rnd.shuffle(pedidos)
    i, n = map(int, args.parte.split("/"))
    pedidos = pedidos[i::n]

    import torch
    from transformers import AutoModelForCausalLM, AutoTokenizer
    tok = AutoTokenizer.from_pretrained(args.base)
    tok.padding_side = "left"
    m = AutoModelForCausalLM.from_pretrained(args.base, dtype=torch.bfloat16, device_map={"": "cuda:0"}).eval()
    fin = tok.convert_tokens_to_ids("<|im_end|>")
    sal = Path(args.salida).expanduser()
    sal.parent.mkdir(parents=True, exist_ok=True)
    hechos = set()
    if sal.exists():
        hechos = {json.loads(l)["pedido"] for l in sal.read_text().splitlines() if l.strip()}
    pendientes = [p for p in pedidos if p[1] not in hechos]
    t0, n_ok = time.time(), 0
    with open(sal, "a", encoding="utf-8") as fo:
        for j in range(0, len(pendientes), 32):
            tanda = pendientes[j:j + 32]
            ps = [f"<|im_start|>system\n{BREVE}<|im_end|>\n<|im_start|>user\n{p}<|im_end|>\n"
                  f"<|im_start|>assistant\n<think>\n\n</think>\n\n" for _, p in tanda]
            enc = tok(ps, return_tensors="pt", padding=True, add_special_tokens=False).to("cuda:0")
            with torch.no_grad():
                gen = m.generate(**enc, max_new_tokens=args.largo, do_sample=False, pad_token_id=tok.pad_token_id or fin)
            for (clase, p), g in zip(tanda, gen[:, enc.input_ids.shape[1]:].tolist()):
                if fin not in g:
                    continue  # cortada por el largo: no
                texto = tok.decode(g[:g.index(fin)]).strip()
                if len(texto) < 20:
                    continue
                fo.write(json.dumps({"clase": clase, "pedido": p, "respuesta": texto}, ensure_ascii=False) + "\n")
                n_ok += 1
            fo.flush()
            print(f"charla: {n_ok} respuestas · {j + len(tanda)}/{len(pendientes)} · {time.time() - t0:.0f} s", flush=True)


if __name__ == "__main__":
    main()
