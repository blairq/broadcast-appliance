#!/usr/bin/env bash
# La curva de cobertura contra precision, barriendo la escala del vector.
#
# PARA QUE SIRVE. La escala es UN escalar y se aplica al servir, asi que el
# punto de operacion se elige despues de entrenar: cuanto mas alta, mas deriva
# al asistente grande. Hay programas donde conviene que el chico arriesgue y
# programas donde conviene que derive a la primera duda. Esto dice donde esta
# cada punto de esa curva, con numeros y no con opinion.
#
# COMO SE LEE. Precision es lo que se ve al aire: una herramienta equivocada en
# vivo la ve todo el mundo. Cobertura es velocidad: lo que deriva de mas lo
# resuelve el grande igual, pero tarda. Se elige la escala mas baja que deje la
# precision donde la quieras, no la que maximice el acierto total —el acierto
# total premia derivar de mas, que es justo lo que no queremos—.
#
#   tools/entrenar/barrer-escala.sh <modelo> <vector.gguf> [escalas...]
set -euo pipefail
MODELO="${1:?falta el modelo}"
VECTOR="${2:?falta el vector (tools/entrenar/vector-control.py)}"
shift 2
ESCALAS=("$@")
[ ${#ESCALAS[@]} -eq 0 ] && ESCALAS=(0 0.25 0.5 1 1.5 2 3)
AQUI="$(cd "$(dirname "$0")" && pwd)"
PY="${PYTHON:-python3}"

echo "modelo: $MODELO"
echo "vector: $VECTOR"
echo
for e in "${ESCALAS[@]}"; do
    echo "=== escala $e ==="
    "$PY" "$AQUI/evaluar.py" --modelo "$MODELO" --vector "$VECTOR" --escala "$e" 2>&1 \
        | grep -E "^(cobertura|precision|derivado|acierto)" || echo "  (sin salida)"
    echo
done
echo "Elegi la escala mas baja que te deje la precision donde la queres."
