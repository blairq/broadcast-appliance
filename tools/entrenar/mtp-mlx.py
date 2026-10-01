#!/usr/bin/env python3
"""La cabeza MTP para el modelo MLX, como la carga ba-modelo (src/modelo-mac).

ba-modelo (mlx-swift-lm) arma una carpeta de «drafter» con el config.json del
modelo (model_type qwen3_5) y un enlace a uno de estos archivos:

  mtp.safetensors     la cabeza en fp16, sin cuantizar   (--mtp fp16)
  mtp-q8.safetensors  las lineales en 8 bits afin, grupos de 64; el resto en
                      fp16                                (--mtp q8 / auto)

Todo en FP16: el aparato es una M1, sin bf16 nativo.

LAS NORMAS VAN EN LA CONVENCION DE HUGGING FACE (centradas en cero, SIN el +1).
Con model_type qwen3_5, mlx-swift-lm (Qwen35TextMTPRegistration, commit
ac6f565) carga la cabeza con preconvertedNorms=false y le suma 1 a las normas
el mismo (qwenMTPSanitizeWeights). Si el archivo ya trajera el +1, quedaria
sumado dos veces y la cabeza propondria basura. (MTPLX, en cambio, espera el +1
aplicado: este formato es el de NUESTRO motor, no el de MTPLX.)

Fuera de model*.safetensors a proposito: mlx-lm (0.31.3, qwen3_5.sanitize)
toma cualquier peso mtp.* ahi como un checkpoint sin convertir y le suma 1 a
todas las normas del modelo.

    .venv-mlx/bin/python tools/entrenar/mtp-mlx.py --entrada export/mtp.safetensors --salida export/modelo-vl-q4
"""
import argparse
import json
import shutil
from pathlib import Path

import mlx.core as mx

GRUPO = 64


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--entrada", required=True, help="safetensors con los mtp.* (bf16, convencion de HF)")
    ap.add_argument("--salida", required=True, help="carpeta del modelo MLX")
    ap.add_argument("--base", help="(no se usa: compatibilidad)")
    args = ap.parse_args()
    w = {k: v for k, v in mx.load(args.entrada).items() if k.startswith("mtp.")}
    if not w:
        raise SystemExit(f"{args.entrada} no tiene pesos mtp.*")
    maximo = max(float(mx.max(mx.abs(v.astype(mx.float32)))) for v in w.values())
    if maximo > 6e4:
        raise SystemExit(f"un peso de la cabeza ({maximo}) no entra en fp16")
    f16 = {k: v.astype(mx.float16) for k, v in w.items()}
    q8, n8 = {}, 0
    for k, v in f16.items():
        if v.ndim == 2 and k.endswith(".weight") and v.shape[-1] % GRUPO == 0:
            wq, s, b = mx.quantize(v, group_size=GRUPO, bits=8)
            base = k[:-len(".weight")]
            q8[k], q8[base + ".scales"], q8[base + ".biases"] = wq, s, b
            n8 += 1
        else:
            q8[k] = v
    sal = Path(args.salida)
    meta = {"format": "mlx", "normas": "convencion de Hugging Face (sin +1)"}
    mx.save_safetensors(str(sal / "mtp.safetensors"), f16, metadata=dict(meta, dtype="float16"))
    mx.save_safetensors(str(sal / "mtp-q8.safetensors"), q8,
                        metadata=dict(meta, dtype="float16", quantization=json.dumps({"bits": 8, "group_size": GRUPO})))
    # restos del formato de MTPLX (version anterior de este script): fuera
    for viejo in ("mtp-fp16.safetensors", "mtplx_runtime.json"):
        (sal / viejo).unlink(missing_ok=True)
    shutil.rmtree(sal / "mtp", ignore_errors=True)
    cfg_f = sal / "config.json"
    if cfg_f.exists():
        cfg = json.loads(cfg_f.read_text())
        if any(cfg.pop(k, None) is not None for k in ("mlx_lm_extra_tensors", "mtplx_mtp_contract",
                                                      "mtplx_mtp_quantization")):
            cfg_f.write_text(json.dumps(cfg, indent=2))
    tam = {n: round((sal / n).stat().st_size / 2**20) for n in ("mtp.safetensors", "mtp-q8.safetensors")}
    print(f"cabeza MTP (fp16, normas de HF, como la carga ba-modelo): {len(w)} tensores, {n8} lineales a 8 bits · MB {tam}")


if __name__ == "__main__":
    main()
