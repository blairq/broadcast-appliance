#!/usr/bin/env bash
# La curva de cobertura contra precision, sesgando el logit de `derivar`.
#
# POR QUE ESTE Y NO EL VECTOR DE CONTROL. Medido: el vector separa bien los dos
# grupos (0,53 de separacion relativa) y aun asi empujarlo NUNCA produjo una
# derivacion, en ninguna escala, mientras rompia la precision. La direccion
# codifica de que habla el pedido, no la decision de derivarlo.
#
# Elegir un nombre de una lista cerrada se decide en el logit de un token. Ahi
# hay que empujar: es monotono, no toca nada mas y no necesita entrenamiento.
#
#   tools/entrenar/barrer-sesgo.sh <modelo> [sesgos...]
set -euo pipefail
MODELO="${1:?falta el modelo}"
shift || true
SESGOS=("$@")
[ ${#SESGOS[@]} -eq 0 ] && SESGOS=(0 2 4 6 8 10 14 20)
AQUI="$(cd "$(dirname "$0")" && pwd)"
PY="${PYTHON:-python3}"
echo "modelo: $MODELO"
for s in "${SESGOS[@]}"; do
    echo "=== sesgo +$s ==="
    "$PY" "$AQUI/evaluar.py" --modelo "$MODELO" --sesgo "$s" 2>&1 \
        | grep -E "^(cobertura|precision|derivado|acierto)" || echo "  (sin salida)"
done
echo "Elegi el sesgo mas bajo que te deje la precision donde la queres."
