#!/usr/bin/env bash
# El entorno de entrenamiento, armado por script y no a mano.
#
# Vive en .venv-entrenar/ dentro del repo y NO se versiona. Es idempotente:
# correrlo de nuevo no rompe nada y agrega lo que falte.
#
#   tools/entrenar/venv.sh          # crear / completar
#   tools/entrenar/venv.sh --pip X  # agregar un paquete
set -euo pipefail
RAIZ="$(cd "$(dirname "$0")/../.." && pwd)"
VENV="$RAIZ/.venv-entrenar"
[ -d "$VENV" ] || python3 -m venv "$VENV"
PIP="$VENV/bin/pip"
"$PIP" install -q --upgrade pip
# unsloth arrastra torch, transformers, trl y peft; `gguf` es para escribir el
# vector de control en el formato que lee llama.cpp.
FALTAN=()
# Qwen3.5 mezcla atencion lineal (Gated DeltaNet) con atencion completa: sin
# `fla` (flash-linear-attention, triton) transformers cae a una version en
# PyTorch puro, lenta y con mucha memoria. `causal-conv1d` no va: no hay
# binario para torch 2.12/CUDA 13 y no compila aca; la convolucion es de 4
# pasos y en PyTorch no pesa (medido: 0,81 s por paso con Qwen3.5-2B).
for m in unsloth:unsloth gguf:gguf numpy:numpy fla:flash-linear-attention; do
    mod="${m%%:*}"; paq="${m##*:}"
    "$VENV/bin/python" -c "import $mod" 2>/dev/null || FALTAN+=("$paq")
done
if [ ${#FALTAN[@]} -gt 0 ]; then
    echo "instalando: ${FALTAN[*]}"
    "$PIP" install "${FALTAN[@]}"
fi
echo "listo: $VENV"
"$VENV/bin/python" -c "import torch;print('torch', torch.__version__, '· gpus', torch.cuda.device_count())"
