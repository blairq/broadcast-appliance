#!/usr/bin/env python3
"""El corpus de tc-corpus.py, listo para Unsloth Studio (~/.unsloth/studio).

QUE ARMA, en --salida:
    LEEME.md                    como cargarlo en el Studio, paso a paso
    huella.json                 de que corpus salio y con que sumas
    datos/entrenar.parquet      la columna «messages» (formato chatml del Studio)
    datos/validar.parquet       idem: va en «eval dataset»
    config/qwen3.5-4b-lora.yaml los hiperparametros de entrenar-rapido.py
    evaluacion/                 prueba y charla-prueba tal cual: son para
                                tc-evaluar.py, NO para entrenar

POR QUE PARQUET. El prompt de cada ejemplo repite las fichas de todas las
herramientas (~12 mil tokens): el jsonl pesa ~590 MB, mas que el tope de subida
del Studio (500 MB). En parquet comprimido baja a pocos MB.

POR QUE «messages» Y NO EL TEXTO CRUDO. El Studio le aplica al ejemplo la
plantilla de chat del modelo. Con Qwen3.5 la plantilla rearma, caracter por
caracter, el mismo texto con el que entrena tc-bucle (system con las fichas,
user, y el assistant con <think> vacio y la llamada): --verificar lo comprueba.

    .venv-entrenar/bin/python tools/entrenar/exportar-unsloth-studio.py \\
        --corpus ~/entrenamientos/tc-v6b/vuelta-1/corpus --salida ~/entrenamientos/unsloth-studio/v6b
"""
import argparse
import hashlib
import json
import math
import re
import shutil
from pathlib import Path

import pyarrow as pa
import pyarrow.parquet as pq

TURNO = re.compile(r"<\|im_start\|>(\w+)\n(.*?)<\|im_end\|>\n", re.S)
CIERRE = "<|im_start|>assistant\n<think>\n\n</think>\n\n"
ESQUEMA = pa.schema([("messages", pa.list_(pa.struct([("role", pa.string()), ("content", pa.string())])))])


def mensajes(x):
    p = x["prompt"]
    if not p.endswith(CIERRE):
        raise ValueError(f"prompt sin el cierre esperado: {p[-80:]!r}")
    ms = [{"role": r, "content": c} for r, c in TURNO.findall(p)]
    ms.append({"role": "assistant", "content": x["completion"].removesuffix("<|im_end|>")})
    return ms


def exportar(origen, destino, lote=500):
    n, filas = 0, []
    with pq.ParquetWriter(destino, ESQUEMA, compression="zstd") as w:
        for linea in open(origen):
            filas.append(mensajes(json.loads(linea)))
            if len(filas) == lote:
                w.write_table(pa.table({"messages": filas}, schema=ESQUEMA))
                n, filas = n + len(filas), []
        if filas:
            w.write_table(pa.table({"messages": filas}, schema=ESQUEMA))
            n += len(filas)
    return n


def verificar(origen, base, cada):
    """La plantilla del modelo sobre «messages» tiene que dar el texto de tc-bucle."""
    from transformers import AutoTokenizer
    t = AutoTokenizer.from_pretrained(base)
    n, mal, largos = 0, 0, []
    for i, linea in enumerate(open(origen)):
        if i % cada:
            continue
        x = json.loads(linea)
        texto = t.apply_chat_template(mensajes(x), tokenize=False)
        n += 1
        mal += texto.rstrip("\n") != (x["prompt"] + x["completion"]).rstrip("\n")
        largos.append(len(t(texto)["input_ids"]))
    return n, mal, max(largos)


def suma(f):
    h = hashlib.sha256()
    with open(f, "rb") as a:
        for b in iter(lambda: a.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()[:16]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", required=True, help="carpeta de tc-corpus.py (entrenar/validar/prueba.jsonl)")
    ap.add_argument("--salida", required=True)
    ap.add_argument("--base", default="Qwen/Qwen3.5-4B")
    ap.add_argument("--epocas", type=float, default=2.0)
    ap.add_argument("--lote", type=int, default=16, help="lote efectivo (= acumulacion, con lote por placa 1)")
    ap.add_argument("--verificar-cada", type=int, default=25, help="un ejemplo de cada N; 0 = no verificar")
    args = ap.parse_args()

    c, s = Path(args.corpus).expanduser(), Path(args.salida).expanduser()
    for d in ("datos", "config", "evaluacion"):
        (s / d).mkdir(parents=True, exist_ok=True)
    huella = json.loads((c / "huella.json").read_text())

    n = {k: exportar(c / f"{k}.jsonl", s / "datos" / f"{k}.parquet") for k in ("entrenar", "validar")}
    for k in ("prueba", "charla-prueba"):
        shutil.copy2(c / f"{k}.jsonl", s / "evaluacion" / f"{k}.jsonl")
    print(f"entrenar {n['entrenar']} · validar {n['validar']}")

    contexto = 16384
    if args.verificar_cada:
        vn, mal, largo = verificar(c / "entrenar.jsonl", args.base, args.verificar_cada)
        print(f"plantilla: {vn} revisados, {mal} distintos · el mas largo {largo} tokens")
        if mal:
            raise SystemExit("la plantilla del modelo no rearma el texto de tc-bucle: no usar en el Studio")
        contexto = 1 << math.ceil(math.log2(largo + 64))

    pasos = math.ceil(n["entrenar"] * args.epocas / args.lote)
    yaml = f"""# Unsloth Studio: Training > Upload (config). Mismos hiperparametros que
# tools/entrenar/entrenar-rapido.py en tc-bucle. El modelo y los datos se eligen
# en la pantalla, no van en el yaml (ver LEEME.md).
training:
  max_seq_length: {contexto}
  num_epochs: {args.epocas:g}
  learning_rate: 0.0002
  batch_size: 1
  gradient_accumulation_steps: {args.lote}
  warmup_steps: {round(pasos * 0.05)}
  max_steps: 0
  save_steps: 100
  eval_steps: {round(100 / pasos, 3)}
  weight_decay: 0.01
  random_seed: 7
  packing: false
  train_on_completions: true
  gradient_checkpointing: "unsloth"
  optim: "adamw_torch_fused"
  lr_scheduler_type: "cosine"
lora:
  lora_r: 32
  lora_alpha: 32
  lora_dropout: 0.0
  target_modules: "q_proj,k_proj,v_proj,o_proj,gate_proj,up_proj,down_proj"
  use_rslora: false
  use_loftq: false
"""
    nombre_cfg = f"{args.base.split('/')[-1].lower()}-lora.yaml"
    (s / "config" / nombre_cfg).write_text(yaml)

    (s / "huella.json").write_text(json.dumps({
        "corpus": str(c), "corpus_huella": huella, "base": args.base,
        "ejemplos": n, "pasos": pasos, "contexto": contexto,
        "sumas": {str(f.relative_to(s)): suma(f) for f in sorted(s.rglob("*")) if f.is_file() and f.name != "huella.json"},
    }, ensure_ascii=False, indent=1) + "\n")

    (s / "LEEME.md").write_text(f"""# Corpus tc para Unsloth Studio

Salido de `{c}` (anotaciones {huella.get('anotaciones')}, prompt {huella.get('prompt')}).
Lo arma `tools/entrenar/exportar-unsloth-studio.py`; no editar a mano: se regenera.

## Que hay

| carpeta | para que |
|---|---|
| `datos/entrenar.parquet` | dataset de entrenamiento: {n['entrenar']} ejemplos, columna `messages` |
| `datos/validar.parquet` | dataset de evaluacion: {n['validar']} ejemplos |
| `config/{nombre_cfg}` | hiperparametros (los de tc-bucle) |
| `evaluacion/` | prueba y charla-prueba, para `tc-evaluar.py`; no se entrena con esto |
| `huella.json` | de donde salio y las sumas de cada archivo |

Por tipo: {', '.join(f'{k} {v}' for k, v in huella.get('por_tipo', {}).items())}.

## En el Studio

1. **Model**: `{args.base}` (esta en el cache de Hugging Face). Metodo **LoRA 16-bit**,
   no QLoRA: tc-bucle entrena en bf16.
2. **Dataset** > Local: subir `datos/entrenar.parquet`. Formato **chatml** (o `auto`,
   que lo detecta por la columna `messages`). Como eval, `datos/validar.parquet`.
3. **Training** > Upload: `config/{nombre_cfg}`. Revisar que quede:
   contexto {contexto}, LoRA r 32 / alfa 32 / dropout 0, lr 2e-4 coseno,
   lote 1 x acumulacion {args.lote}, **Train on completions** prendido
   (la perdida solo sobre la respuesta, como en tc-bucle).
4. {pasos} pasos para {args.epocas:g} epocas.

## Ojo

- Cada ejemplo tiene ~12 mil tokens de prefijo (las fichas de todas las herramientas)
  que el Studio procesa entero en cada ejemplo. tc-bucle lo comparte entre los
  ejemplos del lote y usa las dos placas con FSDP; el Studio va mucho mas lento.
- No usar las placas mientras corre tc-bucle.
- Para medir el adaptador que salga: `tc-evaluar.py` con `evaluacion/`, el banco y el examen.
""")
    print(f"listo: {s}")


if __name__ == "__main__":
    main()
