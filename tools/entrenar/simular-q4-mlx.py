#!/usr/bin/env python3
"""Los pesos de un modelo, redondeados EXACTAMENTE como los deja MLX en q4.

Para medir en la GPU, con evaluar.py y el banco entero, si cuantizar a 4 bits
le cambia la eleccion al modelo. En la CPU del servidor MLX corre en un solo
nucleo y el banco tardaria horas; la latencia se mide en la Mac, pero el
efecto de la cuantizacion no depende de donde corre.

Se usa la propia funcion de MLX (mx.quantize + mx.dequantize, afin, grupos
de 64) sobre lo mismo que cuantiza mlx_lm.convert en un modelo denso: los
pesos de las capas lineales y el embedding (2D, ultima dimension multiplo del
grupo). Normas, convolucion, A_log y dt_bias quedan como estan.

    .venv-mlx/bin/python tools/entrenar/simular-q4-mlx.py --hf ~/entrenamientos/mlx/v15q4-hf \\
        --salida ~/entrenamientos/mlx/v15q4-sim-q4
"""
import argparse
import shutil
from pathlib import Path

import mlx.core as mx


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hf", required=True)
    ap.add_argument("--salida", required=True)
    ap.add_argument("--bits", type=int, default=4)
    ap.add_argument("--grupo", type=int, default=64)
    args = ap.parse_args()
    src, dst = Path(args.hf), Path(args.salida)
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst, ignore=shutil.ignore_patterns("*.safetensors"))
    n_q = n_t = 0
    for f in sorted(src.glob("*.safetensors")):
        pesos = mx.load(str(f))
        out = {}
        for k, w in pesos.items():
            n_t += 1
            if w.ndim == 2 and w.shape[-1] % args.grupo == 0 and "norm" not in k:
                wq, s, b = mx.quantize(w.astype(mx.float32), group_size=args.grupo, bits=args.bits)
                w = mx.dequantize(wq, s, b, group_size=args.grupo, bits=args.bits).astype(w.dtype)
                n_q += 1
            out[k] = w
        mx.save_safetensors(str(dst / f.name), out, metadata={"format": "pt"})
    # el config vuelve a «qwen3_5_text»: exportar-mlx.py lo cambia a «qwen3_5»
    # para mlx-lm, pero transformers lo leeria como el modelo multimodal entero
    import json
    cfg = json.loads((dst / "config.json").read_text())
    if cfg.get("model_type") == "qwen3_5":
        cfg["model_type"] = "qwen3_5_text"
        (dst / "config.json").write_text(json.dumps(cfg, indent=2))
    print(f"cuantizados {n_q} de {n_t} tensores a {args.bits} bits (grupo {args.grupo}) -> {dst}")


if __name__ == "__main__":
    main()
