#!/usr/bin/env python3
"""El modelo entrenado, a MLX cuantizado: lo que corre en la Apple M1 Pro.

DOS PASOS, en dos entornos:
  1. (este, .venv-entrenar) el LoRA se FUSIONA en la base, en bf16 y solo la
     parte de texto. Se guarda como modelo de Hugging Face, con el config que
     espera mlx-lm: model_type «qwen3_5» (transformers lo guarda como texto
     suelto, «qwen3_5_text», que mlx-lm no reconoce).
  2. (.venv-mlx) mlx_lm.convert lo cuantiza: 4 bits, grupos de 64 por defecto.
     mlx-lm descarta la parte de vision; para usar la vision se exporta aparte
     con mlx-vlm.

POR QUE FUSIONADO Y NO LORA APARTE: es lo mas simple de servir y medir. Para
usar la MISMA base con y sin el adaptador (elegir herramienta / mirar una
imagen) habra que exportar el LoRA por separado; eso viene despues.

    tools/entrenar/exportar-mlx.py --modelo ~/entrenamientos/reactivo-v15q4 \\
        --salida ~/entrenamientos/mlx/v15q4 [--bits 4] [--grupo 64]
Deja <salida>-hf (fusionado bf16) y <salida>-q<bits> (MLX).
"""
import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", required=True, help="carpeta del adaptador entrenado")
    ap.add_argument("--salida", required=True)
    ap.add_argument("--bits", type=int, default=4)
    ap.add_argument("--grupo", type=int, default=64)
    args = ap.parse_args()

    import torch
    from peft import PeftModel
    from transformers import AutoModelForCausalLM, AutoTokenizer

    cfg = json.loads((Path(args.modelo) / "adapter_config.json").read_text())
    base = cfg["base_model_name_or_path"]
    hf = Path(args.salida + "-hf")
    mlx = Path(args.salida + f"-q{args.bits}")
    print(f"base {base} · adaptador {args.modelo}")
    m = AutoModelForCausalLM.from_pretrained(base, dtype=torch.bfloat16)
    m = PeftModel.from_pretrained(m, args.modelo).merge_and_unload()
    if hf.exists():
        shutil.rmtree(hf)
    m.save_pretrained(hf)
    AutoTokenizer.from_pretrained(args.modelo).save_pretrained(hf)
    c = json.loads((hf / "config.json").read_text())
    if c.get("model_type") == "qwen3_5_text":
        c["model_type"] = "qwen3_5"
        (hf / "config.json").write_text(json.dumps(c, indent=2))
    # la firma del prompt viaja con el modelo: el evaluador la compara
    if (Path(args.modelo) / "huella-prompt.json").exists():
        shutil.copy(Path(args.modelo) / "huella-prompt.json", hf)
    del m
    print(f"fusionado: {hf}")

    conv = RAIZ / ".venv-mlx/bin/mlx_lm.convert"
    if not conv.exists():
        sys.exit("falta .venv-mlx: correr tools/entrenar/venv-mlx.sh")
    if mlx.exists():
        shutil.rmtree(mlx)
    subprocess.run([str(conv), "--hf-path", str(hf), "--mlx-path", str(mlx), "-q",
                    "--q-bits", str(args.bits), "--q-group-size", str(args.grupo)], check=True)
    if (hf / "huella-prompt.json").exists():
        shutil.copy(hf / "huella-prompt.json", mlx)
    tam = sum(f.stat().st_size for f in mlx.rglob("*") if f.is_file()) / 2**30
    print(f"MLX q{args.bits}: {mlx} · {tam:.2f} GiB")


if __name__ == "__main__":
    main()
