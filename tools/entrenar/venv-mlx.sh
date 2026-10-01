#!/usr/bin/env bash
# El entorno de MLX, armado por script y no a mano.
#
# PARA QUE: el modelo chico corre en una Apple M1 Pro con MLX (GPU por Metal y
# memoria unificada). Este entorno convierte y cuantiza (mlx_lm.convert) y
# evalua. En Linux MLX corre en CPU: sirve para convertir y para una
# verificacion chica; la latencia y el banco completo se miden en la Mac.
#
# Separado de .venv-entrenar: mlx-lm trae sus propias versiones de
# transformers y no conviene que pisen las del entrenamiento.
#
#   tools/entrenar/venv-mlx.sh
set -euo pipefail
RAIZ="$(cd "$(dirname "$0")/../.." && pwd)"
VENV="$RAIZ/.venv-mlx"
# Versiones fijas: mlx-lm 0.31.3 es la primera que trajimos con Qwen3.5
# (models/qwen3_5.py, con la atencion lineal).
MLX_VER="0.32.3"
MLX_LM_VER="0.31.3"
# mlx-vlm: el modelo CON vision (la torre de Qwen3.5) y su borrador MTP. 0.7.4
# es la que trae models/qwen3_5 y lee la cabeza con el contrato de MTPLX.
MLX_VLM_VER="0.7.4"
[ -d "$VENV" ] || python3 -m venv "$VENV"
"$VENV/bin/pip" install -q --upgrade pip
if [ "$(uname -s)" = Darwin ]; then
    PAQ_MLX="mlx==$MLX_VER"            # Metal
else
    PAQ_MLX="mlx[cpu]==$MLX_VER"       # Linux: CPU, para convertir y verificar
fi
"$VENV/bin/python" -c "import mlx_lm, sys; sys.exit(mlx_lm.__version__ != '$MLX_LM_VER')" 2>/dev/null \
    || "$VENV/bin/pip" install "$PAQ_MLX" "mlx-lm==$MLX_LM_VER"
"$VENV/bin/python" -c "import mlx_vlm, sys; sys.exit(mlx_vlm.__version__ != '$MLX_VLM_VER')" 2>/dev/null \
    || "$VENV/bin/pip" install "mlx-vlm==$MLX_VLM_VER" "$PAQ_MLX" "mlx-lm==$MLX_LM_VER"
echo "listo: $VENV"
"$VENV/bin/python" -c "import mlx.core as mx, mlx_lm; print('mlx', mx.__version__, '· mlx-lm', mlx_lm.__version__, '·', mx.default_device())"
