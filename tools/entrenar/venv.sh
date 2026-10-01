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
for m in unsloth:unsloth gguf:gguf numpy:numpy; do
    mod="${m%%:*}"; paq="${m##*:}"
    "$VENV/bin/python" -c "import $mod" 2>/dev/null || FALTAN+=("$paq")
done
if [ ${#FALTAN[@]} -gt 0 ]; then
    echo "instalando: ${FALTAN[*]}"
    "$PIP" install "${FALTAN[@]}"
fi
echo "listo: $VENV"
"$VENV/bin/python" -c "import torch;print('torch', torch.__version__, '· gpus', torch.cuda.device_count())"
