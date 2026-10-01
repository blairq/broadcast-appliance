#!/usr/bin/env python3
"""La cabeza MTP para el modelo MLX, con el contrato de MTPLX y dos variantes para medir.

MTPLX (el motor de MTP para Apple Silicon) lee la cabeza de un archivo APARTE
que declara el config.json; asi lo arma su forja (ver el repo
Youssofal/Qwen3.5-4B-MTPLX-Optimized-Speed, revision de julio de 2026):

  mtp.safetensors       nombres mtp.* de Hugging Face; las lineales de la capa
                        (atencion y MLP) en int4 afin, grupos de 64; mtp.fc,
                        escalas y normas en FP16 (MTPLX publica bf16; aca todo
                        va en fp16: el aparato es una M1, sin bf16 nativo).
                        Copia en mtp/weights.safetensors.
  config.json           mlx_lm_extra_tensors.mtp_file, mtplx_mtp_contract y
                        mtplx_mtp_quantization.
  mtplx_runtime.json    arquitectura, contrato y profundidad (3 borradores).

LAS NORMAS VAN CON EL +1 APLICADO. Qwen3.5 guarda sus RMSNorm centradas en
cero (la cuenta es x * (1 + w)); mlx-lm, al convertir, les suma 1 a las del
modelo, y MTPLX espera lo mismo en la cabeza. Sin el +1 los borradores son
basura y el MTP hace el modelo MAS LENTO (el error que tuvo esa misma forja,
mtplx#176; nuestra version anterior las dejaba sin el +1).

Ademas, para medir en el aparato (las GPU M1/M2 no tienen bf16 nativo):
  mtp-fp16.safetensors  todo fp16 (sin perdida respecto de la entrenada)
  mtp-q8.safetensors    lineales de la capa en 8 bits; fc y normas en fp16
las dos con las normas igual que arriba (+1).

    .venv-mlx/bin/python tools/entrenar/mtp-mlx.py --entrada export/mtp.safetensors \\
        --salida export/modelo-q4 --base Qwen/Qwen3.5-4B
"""
import argparse
import json
import shutil
from pathlib import Path

import mlx.core as mx

GRUPO = 64
CONTRATO = {"base_hidden_variant": "post_norm", "concat_order": "embedding_hidden", "hidden_variant": "post_norm",
            "mtp_position_mode": "local", "mtp_quant_group_size": GRUPO, "mtp_quant_mode": "affine"}


def es_norma(k, v):
    return v.ndim == 1 and "norm" in k


def es_lineal_de_capa(k, v):
    return k.startswith("mtp.layers.") and k.endswith(".weight") and v.ndim == 2 and v.shape[-1] % GRUPO == 0


def armar(w, tipo, bits):
    """Normas +1; lineales de la capa cuantizadas a `bits` (0: sin cuantizar)."""
    out = {}
    for k, v in w.items():
        if es_norma(k, v):
            out[k] = (v.astype(mx.float32) + 1.0).astype(tipo)
        elif bits and es_lineal_de_capa(k, v):
            wq, s, b = mx.quantize(v.astype(tipo), group_size=GRUPO, bits=bits)
            base = k[:-len(".weight")]
            out[k], out[base + ".scales"], out[base + ".biases"] = wq, s, b
        else:
            out[k] = v.astype(tipo)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--entrada", required=True, help="safetensors con los mtp.* (bf16, convencion de HF)")
    ap.add_argument("--salida", required=True, help="carpeta del modelo MLX")
    ap.add_argument("--base", required=True, help="repo de la base (para mtplx_runtime.json)")
    ap.add_argument("--profundidad", type=int, default=3)
    args = ap.parse_args()
    w = {k: v for k, v in mx.load(args.entrada).items() if k.startswith("mtp.")}
    if not w:
        raise SystemExit(f"{args.entrada} no tiene pesos mtp.*")
    maximo = max(float(mx.max(mx.abs(v.astype(mx.float32)))) for v in w.values())
    if maximo > 6e4:
        raise SystemExit(f"un peso de la cabeza ({maximo}) no entra en fp16")
    sal = Path(args.salida)
    meta = {"format": "mlx"}
    mx.save_safetensors(str(sal / "mtp.safetensors"), armar(w, mx.float16, 4), metadata=meta)
    (sal / "mtp").mkdir(exist_ok=True)
    shutil.copy(sal / "mtp.safetensors", sal / "mtp" / "weights.safetensors")
    mx.save_safetensors(str(sal / "mtp-fp16.safetensors"), armar(w, mx.float16, 0), metadata=meta)
    mx.save_safetensors(str(sal / "mtp-q8.safetensors"), armar(w, mx.float16, 8), metadata=meta)

    cfg_f = sal / "config.json"
    cfg = json.loads(cfg_f.read_text())
    cfg["mlx_lm_extra_tensors"] = {"mtp_file": "mtp.safetensors"}
    cfg["mtplx_mtp_contract"] = CONTRATO
    cfg["mtplx_mtp_quantization"] = {
        "bits": 4, "group_size": GRUPO, "mode": "affine", "policy": "prequantized-int4", "prequantized": True,
        "description": "MTP layer-0 attention/MLP linears quantized to MLX affine INT4 group64; mtp.fc, scales and "
                       "norms in FP16 (final +1.0 convention). Head retrained for this fine-tuned model."}
    cfg_f.write_text(json.dumps(cfg, indent=2))
    (sal / "mtplx_runtime.json").write_text(json.dumps({
        "arch_id": "qwen3-next-mtp", "artifact_role": "forge-local", "base_trunk": args.base,
        "mtp_contract": CONTRATO, "mtp_depth_max": args.profundidad, "mtp_depth_default": args.profundidad,
        "mtp_sidecar": "int4", "mtplx_version": "2.1.0", "recommended_profile": "sustained"}, indent=2))
    tam = {f.name: round(f.stat().st_size / 2**20) for f in sorted(sal.glob("mtp*.safetensors"))}
    print(f"cabeza MTP (normas +1, contrato MTPLX): MB {tam}")


if __name__ == "__main__":
    main()
