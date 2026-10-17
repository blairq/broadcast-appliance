#!/bin/bash
# DPO sobre un adaptador SFT, de punta a punta (experimental: ver
# docs/modelos/bucle-tool-calling.md, «Preferencias»):
#
#   1. entrenar-rapido --objetivo dpo con los pares de <trabajo>/pares/dpo
#      (los arma tools/entrenar/tc-preferencias.py, con anclas), DPO+NLL;
#      un checkpoint cada EVAL pasos, todos guardados;
#   2. elegir el checkpoint con <trabajo>/pares/seleccion (los casos de las
#      plantillas de validar, bien y mal): ni la prueba, ni el banco, ni el examen;
#   3. medir ese como tc-bucle (prueba + charla en la placa 0, banco + examen en la 1).
#
#   BA_PERFIL=vivo6 tools/entrenar/tc-dpo.sh <trabajo> <adaptador-sft> <corpus>
#
# <corpus> es el de la vuelta del SFT (para la prueba y la charla). Deja en
# <trabajo>: dpo/ (checkpoints), dpo-sel/ (la seleccion de cada uno), dpo.log y
# dpo-medidas.{a,b}.json. Hiperparametros por variable: LR (1e-5), EPOCAS (1),
# BETA (0,1), NLL (1,0: alfa x NLL por token de la buena), EVAL (5), MICRO (1).
set -u
[ -n "${BA_PERFIL:-}" ] || { echo "falta BA_PERFIL (el perfil con el que se entreno el SFT)"; exit 1; }
[ $# -eq 3 ] || { sed -n '2,20p' "$0"; exit 1; }
P=$(realpath "$1"); SFT=$(realpath "$2"); C=$(realpath "$3")
RAIZ=$(cd "$(dirname "$0")/../.." && pwd)
cd "$RAIZ"
export PYTORCH_CUDA_ALLOC_CONF=expandable_segments:True
PY=.venv-entrenar/bin/python
E=tools/entrenar/tc-evaluar.py
[ -d "$P/pares/dpo" ] || { echo "no hay $P/pares/dpo: correr antes tc-preferencias.py --salida $P/pares"; exit 1; }

$PY tools/entrenar/entrenar-rapido.py --fsdp --recalcular --base Qwen/Qwen3.5-4B \
    --datos "$P/pares/dpo" --salida "$P/dpo" --adaptador-inicial "$SFT" --objetivo dpo \
    --nll-buena "${NLL:-1.0}" --beta "${BETA:-0.1}" --lr "${LR:-1e-5}" --epocas "${EPOCAS:-1}" \
    --lote 16 --micro "${MICRO:-1}" --eval-cada "${EVAL:-5}" --conservar-checkpoints \
    > "$P/dpo.log" 2>&1 || { echo "fallo el entrenamiento (ver $P/dpo.log)"; exit 1; }

# la seleccion: el SFT y cada checkpoint, de a dos (una placa cada uno)
mkdir -p "$P/dpo-sel"
cands=("$SFT" $(ls -d "$P"/dpo/checkpoint-* | sort -t- -k2 -n))
i=0
for c in "${cands[@]}"; do
    n=$(basename "$c"); [ "$c" = "$SFT" ] && n=sft
    CUDA_VISIBLE_DEVICES=$((i % 2)) $PY $E --modelo "$c" --corpus "$P/pares/seleccion" --conjuntos prueba \
        --json "$P/dpo-sel/$n.json" > "$P/dpo-sel/$n.log" 2>&1 &
    i=$((i + 1))
    if (( i % 2 == 0 )); then wait; fi
done
wait
mejor=$($PY - "$P" <<'PYEOF'
import json, sys, glob, os
P = sys.argv[1]
fil = []
for f in sorted(glob.glob(f"{P}/dpo-sel/*.json")):
    m = json.load(open(f))["medidas"]["prueba"]["conteos"]
    bien = m["args_exactos"][0] + m["falta_dato"][0]
    casos = m["args_exactos"][1] + m["falta_dato"][1]
    n = os.path.basename(f)[:-5]
    fil.append((bien / casos, n))
    print(f"  {n:16s} {bien}/{casos} = {bien / casos:.1%}  (args {m['args_exactos']} falta {m['falta_dato']})",
          file=sys.stderr)
print(max(f for f in fil if f[1] != "sft")[1])
PYEOF
)
echo "elegido: $mejor"
M="$P/dpo/$mejor"
CUDA_VISIBLE_DEVICES=0 $PY $E --modelo "$M" --corpus "$C" --conjuntos prueba,charla \
    --json "$P/dpo-medidas.a.json" > "$P/dpo-medidas.a.log" 2>&1 & A=$!
CUDA_VISIBLE_DEVICES=1 $PY $E --modelo "$M" --corpus "$C" --conjuntos banco,examen --limite-banco 500 \
    --json "$P/dpo-medidas.b.json" > "$P/dpo-medidas.b.log" 2>&1 || { echo "fallo midiendo b"; exit 1; }
wait $A || { echo "fallo midiendo a"; exit 1; }
echo "listo: $P/dpo-medidas.{a,b}.json"
