#!/usr/bin/env bash
# Parar el bucle de tool calling (tc-bucle.py) sin perder lo hecho.
#
#   tools/entrenar/tc-parar.sh ~/entrenamientos/tc-v6
#
# Crea <trabajo>/PARAR. Si esta ENTRENANDO, las placas lo ven en 10 pasos (~30 s),
# guardan el estado y salen; el bucle sale detras. Si esta en otra etapa (medir,
# geometria, el agente), esa etapa se corta: al retomar se rehace (son minutos).
# Para seguir:
#
#   tools/entrenar/tc-bucle.py --trabajo ~/entrenamientos/tc-v6 --retomar --vueltas 3
set -u
W="${1:?uso: tc-parar.sh <carpeta del trabajo>}"
W="${W%/}"
# el bucle (tc-bucle.py) deja bucle.pid; el ciclo de bancos (tc-ciclo.py), ciclo.pid
PIDF="$W/bucle.pid"; [ -f "$PIDF" ] || PIDF="$W/ciclo.pid"
[ -f "$PIDF" ] || { echo "no hay $W/bucle.pid ni $W/ciclo.pid: ¿esta corriendo ahi?"; exit 1; }
PID=$(cat "$PIDF")
if ! kill -0 "$PID" 2>/dev/null; then
    echo "el proceso ($PID) ya no corre"
    exit 0
fi
touch "$W/PARAR"
# ¿esta entrenando? (un hijo del bucle es entrenar-rapido.py)
entrena() { pgrep -P "$PID" -f entrenar-rapido.py >/dev/null; }
if entrena; then
    echo "entrenando: espero a que guarde el estado y salga…"
    for _ in $(seq 1 120); do
        kill -0 "$PID" 2>/dev/null || { echo "parado (estado guardado)."; exit 0; }
        sleep 2
    done
    echo "no salio en 4 minutos: lo corto (se pierde desde el ultimo estado, a lo sumo 10 minutos)"
fi
# otra etapa (o no salio): se corta el grupo entero del bucle, por su PID
PGID=$(ps -o pgid= -p "$PID" | tr -d ' ')
if [ "$PGID" = "$PID" ]; then
    kill -TERM -- "-$PGID" 2>/dev/null
else
    kill -TERM "$PID" 2>/dev/null
fi
for _ in $(seq 1 30); do kill -0 "$PID" 2>/dev/null || break; sleep 1; done
kill -0 "$PID" 2>/dev/null && { [ "$PGID" = "$PID" ] && kill -KILL -- "-$PGID" || kill -KILL "$PID"; }
case "$PIDF" in
    *ciclo.pid) echo "parado. Para seguir: el mismo comando de tc-ciclo.py (retoma solo)" ;;
    *) echo "parado. Para seguir: tools/entrenar/tc-bucle.py --trabajo $W --retomar" ;;
esac
