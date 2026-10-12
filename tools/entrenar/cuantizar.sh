#!/usr/bin/env bash
# ============================================================================
# tools/entrenar/cuantizar.sh — cuantizar CALIBRANDO CON NUESTRO PROPIO TEXTO
#
# Se entrena sin cuantizar (bf16) y se cuantiza al final: eso ya lo hace
# entrenar.py. Lo que falta es COMO se cuantiza.
#
# Cuantizar a 4 bits no reparte el error parejo: hay pesos que importan mucho
# mas que otros, y cuales son depende DEL TEXTO que el modelo vaya a ver. La
# matriz de importancia (imatrix) se calcula pasandole un corpus representativo
# y despues guia al cuantizador para gastar precision donde duele.
#
# MEDIDO EN ESTE PROYECTO, sin entrenar: Q8 62% contra Q4 58%. Cuatro puntos
# que se pagan por cuantizar, y la documentacion de llama.cpp dice que el
# imatrix recupera buena parte —y que por debajo de Q5_K_M es directamente
# recomendable—. Nuestro caso es mejor que el promedio: el corpus de
# calibracion no es texto generico de internet, es EXACTAMENTE lo que el modelo
# va a leer en cabina.
#
# El examen NO entra en la calibracion. No cambiaria la etiqueta de nada, pero
# es la unica medicion honesta que hay y no se toca ni de costado.
#
# Uso: tools/entrenar/cuantizar.sh <modelo-f16.gguf> [q4_k_m]
# ============================================================================
set -euo pipefail

RAIZ="$(cd "$(dirname "$0")/../.." && pwd)"
LLAMA="$RAIZ/extern/llamacpp"
FUENTE="${1:?falta el .gguf en f16/bf16}"
TIPO="${2:-q4_k_m}"
TRABAJO="$(dirname "$FUENTE")"
CALIB="$TRABAJO/calibracion.txt"
IMATRIX="$TRABAJO/imatrix.dat"
SALIDA="${FUENTE%.*}-${TIPO}-imatrix.gguf"

log() { printf '\033[36m[cuantizar]\033[0m %s\n' "$*"; }
export LD_LIBRARY_PATH="$LLAMA"

[ -x "$LLAMA/llama-imatrix" ] || { echo "falta llama.cpp: make deps-llamacpp" >&2; exit 1; }

# El corpus de calibracion: los prompts de ENTRENAMIENTO, enteros. Enteros
# porque lo que se quiere calibrar es el modelo leyendo las fichas, que es lo
# que hace en cada pedido de verdad.
log "armando el corpus de calibracion (solo el split de entrenamiento)"
python3 - "$CALIB" "$RAIZ" <<'PY'
import json, sys
from pathlib import Path
# La raiz llega por argumento: dependia del directorio desde donde se llamara
# al script, que es una forma tonta de romperse cuando lo corras desde otro lado.
ent = Path(sys.argv[2]) / "datos/entrenamiento/entrenar.jsonl"
filas = [json.loads(l) for l in ent.read_text(encoding="utf-8").splitlines() if l.strip()]
# Una muestra: con el prompt entero repetido mil veces se calibra sobre las
# fichas y nada mas. Se toma un subconjunto variado de frases.
vistas, textos = set(), []
for f in filas:
    if f["herramienta"] in vistas and len(textos) > 150:
        continue
    vistas.add(f["herramienta"])
    textos.append(f["prompt"] + f["completion"])
Path(sys.argv[1]).write_text("\n\n".join(textos), encoding="utf-8")
print(f"  {len(textos)} textos, {sum(len(t) for t in textos)//1024} KB")
PY

log "calculando la matriz de importancia (tarda unos minutos en CPU)"
"$LLAMA/llama-imatrix" -m "$FUENTE" -f "$CALIB" -o "$IMATRIX" --chunks 64 -t 8 2>&1 | tail -3

log "cuantizando a $TIPO con la matriz"
"$LLAMA/llama-quantize" --imatrix "$IMATRIX" "$FUENTE" "$SALIDA" "$TIPO" 2>&1 | tail -3

log "listo: $SALIDA ($(du -h "$SALIDA" | cut -f1))"
echo
echo "Para comparar contra la misma cuantizacion SIN calibrar:"
echo "  tools/entrenar/bateria-local.py --modelo $SALIDA --solo-nombre"
