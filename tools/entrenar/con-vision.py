#!/usr/bin/env python3
"""El checkpoint COMPLETO (texto ajustado + vision) a partir del fusionado de texto.

Entrenamos con AutoModelForCausalLM, que carga solo la parte de texto de
Qwen3.5: el fusionado (exportar-mlx.py) sale sin la torre de vision. Pero el
LoRA no toca la vision, asi que el modelo completo es la BASE con las capas que
el LoRA cambio reemplazadas por las fusionadas:

  - del fusionado, SOLO los modulos que el adaptador entreno (target_modules):
    lo demas sale de la base tal cual. Asi tambien se conserva el tipo original
    de cada tensor: A_log (el decaimiento de la atencion lineal) queda en fp32,
    que el fusionado habia redondeado a bf16 al guardar.
  - la vision (model.visual.*) y sus configs (preprocessor, video), de la base.
  - sin mtp.*: la cabeza va aparte (entrenar-mtp.py / mtp-mlx.py).

Sale con el config de la base (Qwen3_5ForConditionalGeneration): lo leen
mlx_vlm.convert (MLX con vision) y convert_hf_to_gguf.py --mmproj (llama.cpp).

    tools/entrenar/con-vision.py --fusionado export/modelo-hf --base <snapshot> \\
        --adaptador adaptador --salida export/modelo-vl-hf
"""
import argparse
import json
import shutil
from pathlib import Path

from safetensors import safe_open
from safetensors.torch import save_file

TAM_ARCHIVO = 4 * 2**30


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fusionado", required=True, help="el fusionado de texto (carpeta HF)")
    ap.add_argument("--base", required=True, help="snapshot de la base completa")
    ap.add_argument("--adaptador", required=True, help="el adaptador: de ahi los modulos entrenados")
    ap.add_argument("--salida", required=True)
    args = ap.parse_args()
    fus, base, sal = Path(args.fusionado), Path(args.base), Path(args.salida)
    objetivos = json.loads((Path(args.adaptador) / "adapter_config.json").read_text())["target_modules"]
    objetivos = set(objetivos if isinstance(objetivos, list) else [objetivos])

    # el fusionado nombra la parte de texto como la base (model.language_model.*)
    # o, con transformers viejos, model.*: se lleva todo al nombre de la base
    nuestros = {}
    for f in sorted(fus.glob("*.safetensors")):
        with safe_open(str(f), "pt") as st:
            for k in st.keys():
                nk = k
                if k.startswith("model.") and not k.startswith("model.language_model."):
                    nk = "model.language_model." + k[len("model."):]
                nuestros[nk] = (f, k)
    mapa = json.loads((base / "model.safetensors.index.json").read_text())["weight_map"]
    if sal.exists():
        shutil.rmtree(sal)
    sal.mkdir(parents=True)
    tanda, tam, n, indice = {}, 0, 0, {}
    cambiados = de_base = 0

    def volcar():
        nonlocal tanda, tam, n
        if tanda:
            n += 1
            nombre = f"model-{n:05d}.safetensors"
            save_file(tanda, str(sal / nombre), metadata={"format": "pt"})
            indice.update({k: nombre for k in tanda})
            tanda, tam = {}, 0

    for k in sorted(mapa):
        if k.startswith("mtp."):
            continue
        partes = k.split(".")
        entrenado = k.startswith("model.language_model.") and len(partes) > 2 and partes[-2] in objetivos
        if entrenado:
            if k not in nuestros:
                raise SystemExit(f"el fusionado no tiene {k}")
            f, kk = nuestros[k]
            with safe_open(str(f), "pt") as st:
                t = st.get_tensor(kk)
            cambiados += 1
        else:
            with safe_open(str(base / mapa[k]), "pt") as st:
                t = st.get_tensor(k)
            de_base += 1
        tanda[k] = t.contiguous()
        tam += t.numel() * t.element_size()
        if tam >= TAM_ARCHIVO:
            volcar()
    volcar()
    total = sum((sal / f).stat().st_size for f in set(indice.values()))
    (sal / "model.safetensors.index.json").write_text(json.dumps(
        {"metadata": {"total_size": total}, "weight_map": indice}, indent=2))
    for f in base.iterdir():
        if f.suffix in (".json", ".jinja", ".txt") and f.name != "model.safetensors.index.json":
            shutil.copy(f, sal / f.name)
    if (fus / "huella-prompt.json").exists():
        shutil.copy(fus / "huella-prompt.json", sal)
    print(f"modelo completo: {cambiados} tensores del ajuste ({', '.join(sorted(objetivos))}), {de_base} de la base"
          f" (vision incluida) · {total / 2**30:.1f} GiB -> {sal}")


if __name__ == "__main__":
    main()
