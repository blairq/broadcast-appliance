#!/usr/bin/env bash
# ============================================================================
# tools/ci/capas-docker.sh — las capas de Docker de cada worktree (tools/ci/capa-docker.sh)
#
#   tools/ci/capas-docker.sh listar           base, capas, de qué base son y cuánto ocupan
#   tools/ci/capas-docker.sh limpiar [DIAS]   tira las capas sin usar hace DIAS días (7; 0:
#                                             todas) y las que son de otra base (no sirven)
# Una capa en uso (su candado tomado por una corrida viva) no se toca. Si colima está
# apagada, la prende y la vuelve a apagar. BA_DOCKER_ARQ=arm64: las de la validación nativa
# arm64 (volumen ba-trabajo-arm64).
# ============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ACCION="${1:-listar}"
DIAS="${2:-7}"
CANDADOS="${BA_DOCKER_CANDADOS:-$HOME/.cache/broadcast-appliance/docker}"
log() { echo "[capas] $*" >&2; }
DOCKER="${DOCKER:-docker}"
ARQ="${BA_DOCKER_ARQ:-amd64}"
case "$ARQ" in
    amd64) VOLUMEN=ba-trabajo; IMAGEN=ba-compilar ;;
    arm64) VOLUMEN=ba-trabajo-arm64; IMAGEN=ba-compilar-arm64 ;;
    *) log "BA_DOCKER_ARQ: amd64 o arm64"; exit 1 ;;
esac

apagar=0
if command -v colima >/dev/null 2>&1 && ! colima status >/dev/null 2>&1; then
    log "prendiendo colima (se apaga al terminar)"
    colima start >/dev/null 2>&1 || { log "no se pudo prender colima"; exit 1; }
    apagar=1
fi
al_salir() { [ "$apagar" = 1 ] && colima stop >/dev/null 2>&1; return 0; }
trap al_salir EXIT
if ! $DOCKER volume inspect "$VOLUMEN" >/dev/null 2>&1; then
    log "$ARQ: no hay volumen $VOLUMEN (nunca se compiló en $ARQ)"
    exit 0
fi

# Las capas en uso ahora (no se tiran).
en_uso=""
for c in "$CANDADOS"/capa-*; do
    [ -d "$c" ] || continue
    p="$(cat "$c/pid" 2>/dev/null || true)"
    [ -n "$p" ] && kill -0 "$p" 2>/dev/null && en_uso="$en_uso ${c##*/capa-}"
done

$DOCKER run --rm --platform "linux/$ARQ" -v "$VOLUMEN:/trabajo" -e ACCION="$ACCION" -e DIAS="$DIAS" -e EN_USO="$en_uso" \
    "$IMAGEN" bash -c '
set -uo pipefail
base="$(cat /trabajo/base/.base-id 2>/dev/null || echo "(sin base)")"
ahora=$(date +%s)
if [ "$ACCION" = listar ]; then
    echo "[$(uname -m)] base: $base  ($(du -sh /trabajo/base 2>/dev/null | cut -f1))"
    for c in /trabajo/capas/*/; do
        [ -d "$c" ] || continue
        n="$(basename "$c")"; u=$(cat "$c/usada" 2>/dev/null || echo 0)
        printf "  %-50s %6s  usada hace %3d días  %s%s\n" "$n" "$(du -sh "$c" | cut -f1)" $(( (ahora - u) / 86400 )) \
            "$([ "$(cat "$c/base-id" 2>/dev/null)" = "$base" ] && echo "de esta base" || echo "DE OTRA BASE")" \
            "$(case " $EN_USO " in *" $n "*) echo " (en uso)";; esac)"
    done
    df -h /trabajo | tail -1 | awk "{print \"disco del volumen: \" \$3 \" usados, \" \$4 \" libres\"}"
else
    for c in /trabajo/capas/*/; do
        [ -d "$c" ] || continue
        n="$(basename "$c")"; u=$(cat "$c/usada" 2>/dev/null || echo 0)
        case " $EN_USO " in *" $n "*) echo "  $n: en uso, queda"; continue;; esac
        if [ "$(cat "$c/base-id" 2>/dev/null)" != "$base" ] || [ $(( (ahora - u) / 86400 )) -ge "$DIAS" ]; then
            echo "  $n: se tira ($(du -sh "$c" | cut -f1))"; rm -rf "$c"
        fi
    done
fi
'
