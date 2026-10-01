#!/usr/bin/env python3
"""La cabeza MTP para el modelo MLX, en las dos formas que sirven en una Mac.

La cabeza sale de entrenar-mtp.py en bf16, y las GPU de M1/M2 no tienen bf16
nativo: ahi el MTP en bf16 rinde mucho peor (lo avisa el PR #990 de mlx-lm).
Se escriben dos variantes, para medirlas en el aparato:

  mtp.safetensors     fp16. Sin perdida: los pesos de la cabeza estan todos
                      muy dentro del rango de fp16 (el mayor, ~2).
  mtp-q8.safetensors  las lineales cuantizadas a 8 bits como las cuantiza MLX
                      (afin, grupos de 64: <nombre>.weight/.scales/.biases);
                      las normas en fp16.

Nombres de Hugging Face (mtp.*) y normas en SU convencion (sin el +1 que
mlx-lm le suma a las del modelo al convertir): quien cargue la cabeza hace
lo mismo que hace con un checkpoint de HF.

    .venv-mlx/bin/python tools/entrenar/mtp-mlx.py --entrada export/mtp.safetensors --salida export/modelo-q4
"""
import argparse
import json
from pathlib import Path

import mlx.core as mx

GRUPO = 64


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--entrada", required=True, help="safetensors con los mtp.* (bf16)")
    ap.add_argument("--salida", required=True, help="carpeta del modelo MLX")
    args = ap.parse_args()
    w = {k: v for k, v in mx.load(args.entrada).items() if k.startswith("mtp.")}
    if not w:
        raise SystemExit(f"{args.entrada} no tiene pesos mtp.*")
    maximo = max(float(mx.max(mx.abs(v.astype(mx.float32)))) for v in w.values())
    if maximo > 6e4:
        raise SystemExit(f"un peso de la cabeza ({maximo}) no entra en fp16")
    f16 = {k: v.astype(mx.float16) for k, v in w.items()}
    q8, cuantizadas = {}, 0
    for k, v in f16.items():
        if v.ndim == 2 and k.endswith(".weight") and v.shape[-1] % GRUPO == 0:
            wq, s, b = mx.quantize(v, group_size=GRUPO, bits=8)
            base = k[:-len(".weight")]
            q8[k], q8[base + ".scales"], q8[base + ".biases"] = wq, s, b
            cuantizadas += 1
        else:
            q8[k] = v
    sal = Path(args.salida)
    meta = {"format": "mlx", "normas": "convencion de Hugging Face (sin +1)"}
    mx.save_safetensors(str(sal / "mtp.safetensors"), f16, metadata=dict(meta, dtype="float16"))
    mx.save_safetensors(str(sal / "mtp-q8.safetensors"), q8,
                        metadata=dict(meta, dtype="float16", quantization=json.dumps({"bits": 8, "group_size": GRUPO})))
    tam = {n: round((sal / n).stat().st_size / 2**20) for n in ("mtp.safetensors", "mtp-q8.safetensors")}
    print(f"cabeza MTP: {len(w)} tensores, {cuantizadas} lineales a 8 bits · MB {tam}")


if __name__ == "__main__":
    main()
