#!/usr/bin/env bash
# ============================================================================
# tools/entrenar/traer-y-medir.sh — el modelo entrenado, medido DONDE VA A VIVIR
#
# El entrenamiento corre en el servidor con las 3090 y la evaluacion de ahi
# sirve para iterar, pero el numero que decide es OTRO: el aparato corre un
# GGUF cuantizado sobre la CPU de una notebook. Cuantizar cambia el acierto
# —medido sin entrenar: Q4 58%, Q8 62%— y la latencia es de esa maquina, no de
# una 3090.
#
# Asi que esto trae el .gguf y corre el MISMO examen de 48 casos que dio la
# linea de base, para que los numeros se comparen contra algo.
#
# Uso: tools/entrenar/traer-y-medir.sh <servidor> <ruta-remota-del-gguf>
# ============================================================================
set -euo pipefail

SERVIDOR="${1:?falta el servidor (usuario@host)}"
REMOTO="${2:?falta la ruta del .gguf en el servidor}"
RAIZ="$(cd "$(dirname "$0")/../.." && pwd)"
DESTINO="$RAIZ/extern/modelos"

log() { printf '\033[36m[medir]\033[0m %s\n' "$*"; }

mkdir -p "$DESTINO"
nombre="$(basename "$REMOTO")"
log "trayendo $nombre"
scp -q "$SERVIDOR:$REMOTO" "$DESTINO/$nombre"
log "$(du -h "$DESTINO/$nombre" | cut -f1)"

# Si el prompt cambio desde que se entreno, el banco lo avisa solo (compara la
# firma guardada con el conjunto). No se silencia: es justo lo que hay que ver.
log "examen, 48 casos, el mismo de la linea de base"
"$RAIZ/tools/bateria-local.py" --modelo "$DESTINO/$nombre" --solo-nombre --hilos 8

cat <<'FIN'

Para comparar (Qwen3-1.7B sin entrenar, misma maquina, mismo examen):
    Q4_K_M  58%  ·  0,79 s        Q8_0  62%  ·  0,92 s
    Q4 + 73 ejemplos en contexto  64%  ·  1,62 s
FIN
