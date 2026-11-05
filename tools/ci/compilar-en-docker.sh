#!/usr/bin/env bash
# ============================================================================
# tools/ci/compilar-en-docker.sh — Linux y Windows armados en ESTA maquina,
# igual que el CI, dentro de un Ubuntu 24.04 x86_64 en Docker
#
# Para no depender del CI de GitHub (corre a mano y gasta cupo) ni de tener un
# Linux a mano. En una Mac con Apple Silicon, el contenedor x86_64 corre con
# Rosetta (Colima/Lima con vz + rosetta): mucho mas rapido que emular con qemu.
#
# Compila el commit ACTUAL (tiene que estar commiteado) en un volumen de Docker
# que persiste (ba-trabajo): extern/ y los builds se reusan entre corridas, asi
# que solo la primera compila GStreamer, FFmpeg y compania. Corre los mismos make
# que el CI y deja en dist/ de aca el .tar.gz y/o el .zip, listos para
# `tools/publicar.sh`.
#
# BASE Y CAPAS (tools/ci/capa-docker.sh): en el volumen está master ya compilado (la base,
# de solo lectura: `make docker-base` la rehace después de integrar) y cada worktree compila
# en su CAPA encima (overlayfs: copia solo lo que se sobreescribe). Una rama compila solo lo
# que difiere de master, la segunda vez solo lo nuevo, y antes cada corrida hacía `git
# checkout -f` de su commit sobre el MISMO clon: alternar ramas distintas recompilaba casi
# todo, ida y vuelta. Las capas de distintos worktrees corren a la vez.
#   make docker-capas            qué capas hay, de qué base y cuánto ocupan
#   make docker-capas-limpiar    tira las capas sin usar hace DIAS (7) días (DIAS=0: todas)
#
# Uso: tools/ci/compilar-en-docker.sh [linux|win|todo]     (por defecto: todo)
#      BA_DOCKER_BASE=1 tools/ci/compilar-en-docker.sh todo   rehace la base (make docker-base)
#      DOCKER="limactl shell colima sudo docker" ...        si no hay `docker`
#
# Si no cambió nada, casi nada se rehace, y cada paso dice qué salteó y por qué
# (líneas «[atajo]», «[instalar]», «[tests-cache]»). Todo por CONTENIDO, nunca
# por fechas:
#   - deps-win/deps-linux: no se verifican si no cambiaron Makefile,
#     versions.cmake, tools/deps/, el toolchain ni lo que hay en extern/;
#   - pip install: solo si cambió requirements-dev.lock;
#   - compilar: una vez (ctest y dist-linux no lo repiten);
#   - tests (ctest en Linux, Wine en Windows): los que ya pasaron con las mismas
#     entradas no se corren (tools/ci/tests-con-cache.py);
#   - pytest: por archivo, sin lo que ya corrió ctest, y con caché por lo que lee
#     cada archivo (tools/ci/pytest-con-cache.py);
#   - release-win/release: instalación incremental (tools/ci/instalar-incremental.sh);
#     strip de Linux, chequeo de portabilidad y manifest solo si cambió el bundle;
#   - .zip/.tar.gz y su copia a dist/ de acá: solo si cambió el bundle;
#   - símbolos (los .pdb de Windows, los .debug de Linux): solo con SIMBOLOS=1 (lo pone
#     `make publicar`) y solo si cambió el bundle, al almacén ÚNICO de la máquina
#     (BA_SIMBOLOS: dist/simbolos del árbol principal, también desde un worktree).
# Lo de adentro del contenedor está en tools/ci/receta-docker.sh. Cada corrida deja su
# registro en dist/logs/ (y los de lo que falló, en dist/logs/<corrida>/).
# SIN_CACHE=1 lo hace todo de cero (`make publicar` lo pone solo).
# TESTS_SIN_CACHE=1, TESTS_SIN_LENTOS=1, TESTS_JOBS, TESTS_WIN_JOBS y PYTEST_ARGS también llegan adentro.
# ============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
QUE="${1:-todo}"
log() { echo "[compilar-en-docker] $*" >&2; }
morir() { echo "[compilar-en-docker] ERROR: $*" >&2; exit 1; }
case "$QUE" in linux|win|todo) ;; *) morir "uso: $0 [linux|win|todo]" ;; esac

if [ -z "${DOCKER:-}" ]; then
    if command -v docker >/dev/null 2>&1; then DOCKER=docker
    elif command -v limactl >/dev/null 2>&1; then
        export LIMA_HOME="${LIMA_HOME:-$HOME/.colima/_lima}"
        DOCKER="limactl shell colima sudo docker"
    else morir "no hay docker (ni limactl para usar el de Colima)"; fi
fi
d() { $DOCKER "$@"; }

# CAPAS (tools/ci/capa-docker.sh). Cada worktree y plataforma compila en SU capa, encima de
# la base de master (overlayfs): no pisa a nadie, así que dos corridas de distintos
# worktrees (o Linux y Windows del mismo) andan a la vez. Lo que sí se comparte:
#   - la capa misma: de a una corrida por capa (candado capa-<capa>);
#   - la base: `make docker-base` la rehace en su lugar, así que espera a que no corra
#     ninguna capa, y las capas esperan mientras se rehace (candado base);
#   - colima: la prende la primera corrida y la apaga la última (si la prendió alguna).
# Los candados son carpetas con el pid de su dueño; la de un proceso que ya no existe se saca
# sola. BA_DOCKER_CANDADOS cambia dónde viven. CPU: la reparte el árbitro (turno compilar).
MODO=capa
[ "${BA_DOCKER_BASE:-0}" = 1 ] && MODO=base
CANDADOS="${BA_DOCKER_CANDADOS:-$HOME/.cache/broadcast-appliance/docker}"
mkdir -p "$CANDADOS/corridas"
# El nombre de la capa: el worktree (su carpeta) y la plataforma. BA_DOCKER_CAPA lo fija.
CAPA="${BA_DOCKER_CAPA:-$(basename "$ROOT")-$QUE}"
CAPA="$(printf '%s' "$CAPA" | tr -c 'A-Za-z0-9._-' '_')"

vivo() { [ -n "$1" ] && kill -0 "$1" 2>/dev/null; }
# tomar <carpeta> <qué es>: espera a que esté libre y la toma.
tomar() {
    local c="$1" que="$2" otro avisado=0
    while ! mkdir "$c" 2>/dev/null; do
        otro="$(cat "$c/pid" 2>/dev/null || true)"
        if [ -n "$otro" ] && ! vivo "$otro"; then
            log "$que: era de una corrida que ya no está (pid $otro): lo saco"
            rm -rf "$c"; continue
        fi
        if [ "$avisado" = 0 ]; then
            log "$que: lo tiene otra corrida (pid ${otro:-?}, $(cat "$c/quien" 2>/dev/null || echo '?')): espero"
            avisado=1
        fi
        sleep 5
    done
    echo $$ >"$c/pid"
    echo "$ROOT $QUE $MODO" >"$c/quien"
}
soltar() { [ "$(cat "$1/pid" 2>/dev/null)" = "$$" ] && rm -rf "$1"; return 0; }
# Las otras corridas vivas (las que anotaron su pid en corridas/).
otras_corridas() {
    local f p n=0
    for f in "$CANDADOS"/corridas/*; do
        [ -e "$f" ] || continue
        p="$(basename "$f")"
        [ "$p" = "$$" ] && continue
        if vivo "$p"; then n=$((n + 1)); else rm -f "$f"; fi
    done
    echo "$n"
}

TOMADOS=()
al_salir() {
    rm -f "$CANDADOS/corridas/$$"
    local c
    for c in "${TOMADOS[@]:-}"; do [ -n "$c" ] && soltar "$c"; done
    # Colima: la apaga la última corrida, si la prendió alguna (y no se pidió dejarla).
    tomar "$CANDADOS/colima" "colima"
    if [ -f "$CANDADOS/colima-prendida" ] && [ "$(otras_corridas)" = 0 ]; then
        log "apagando colima (la prendió una corrida y no queda ninguna)"
        colima stop >/dev/null 2>&1 || true
        rm -f "$CANDADOS/colima-prendida"
    fi
    soltar "$CANDADOS/colima"
}
trap al_salir EXIT

if [ "$MODO" = base ]; then
    tomar "$CANDADOS/base" "la base"; TOMADOS+=("$CANDADOS/base")
    echo $$ >"$CANDADOS/corridas/$$"
    avisado=0
    while [ "$(otras_corridas)" != 0 ]; do
        [ "$avisado" = 0 ] && log "la base: espero a que terminen las corridas en capas" && avisado=1
        sleep 5
    done
else
    tomar "$CANDADOS/capa-$CAPA" "la capa $CAPA"; TOMADOS+=("$CANDADOS/capa-$CAPA")
    # Anotarse y DESPUÉS mirar la base: si `make docker-base` la toma en el medio, espera
    # a que esta corrida termine (ve su pid en corridas/).
    while true; do
        echo $$ >"$CANDADOS/corridas/$$"
        otro="$(cat "$CANDADOS/base/pid" 2>/dev/null || true)"
        if [ -d "$CANDADOS/base" ] && vivo "$otro"; then
            rm -f "$CANDADOS/corridas/$$"
            log "se está rehaciendo la base (pid $otro): espero"
            sleep 10
            continue
        fi
        break
    done
fi

# Colima: si está instalada y apagada, se prende para compilar y se APAGA al terminar la
# última corrida (bien o mal). La VM reserva mucha memoria: dejarla prendida tras un armado
# ayudó a agotar la memoria de la Mac en una prueba de carga (kernel panic del 2026-11-01).
# Si ya estaba prendida, se la deja como estaba. COLIMA_DEJAR=1 no la apaga.
if command -v colima >/dev/null 2>&1; then
    tomar "$CANDADOS/colima" "colima"
    if ! colima status >/dev/null 2>&1; then
        log "prendiendo colima para compilar (se apaga al terminar la última corrida)"
        colima start >/dev/null 2>&1 || { soltar "$CANDADOS/colima"; morir "no se pudo prender colima"; }
        [ "${COLIMA_DEJAR:-0}" = 1 ] || touch "$CANDADOS/colima-prendida"
    fi
    soltar "$CANDADOS/colima"
fi
cd "$ROOT"
[ -z "$(git status --porcelain --untracked-files=no)" ] || morir "hay cambios sin commitear: se compila un commit, no el arbol de trabajo"
SHA="$(git rev-parse HEAD)"
# De dónde se trae el commit: el .git común (en un worktree, $ROOT/.git es un archivo que
# apunta a una ruta de la Mac que el contenedor no ve).
GITDIR="$(cd "$(git rev-parse --git-common-dir)" && pwd)"
mkdir -p "$ROOT/dist"
# El almacén de símbolos: uno por máquina (antes, uno por worktree: ~16 GB por corrida de
# Windows en cada uno), y solo si se pidió (SIMBOLOS=1: `make publicar`, o un paquete que va
# a la PC de pruebas). Sin pedirlo, ni se monta.
MONTAR_SIMBOLOS=""
if [ "${SIMBOLOS:-0}" = 1 ]; then
    BA_SIMBOLOS="${BA_SIMBOLOS:-$(cd "$GITDIR/.." && pwd)/dist/simbolos}"
    case "$BA_SIMBOLOS" in *" "*) morir "BA_SIMBOLOS no puede tener espacios: $BA_SIMBOLOS" ;; esac
    mkdir -p "$BA_SIMBOLOS"
    MONTAR_SIMBOLOS="-v $BA_SIMBOLOS:/simbolos"
fi

# Un registro por corrida en dist/logs/ (ignorado por git): todo lo que sale por la
# terminal, para ver después qué falló y dónde se fue el tiempo. Los registros de los
# tests que fallaron quedan al lado, en dist/logs/<corrida>/.
CORRIDA="$(date +%Y%m%d-%H%M%S)-$QUE-$(git rev-parse --short HEAD)"
mkdir -p "$ROOT/dist/logs"
exec > >(tee -a "$ROOT/dist/logs/$CORRIDA.log") 2>&1
log "registro de esta corrida: dist/logs/$CORRIDA.log"

log "imagen ba-compilar (Ubuntu 24.04 x86_64)"
d build -q --platform linux/amd64 -t ba-compilar -f "$ROOT/tools/ci/compilar.Dockerfile" "$ROOT/tools/ci" >/dev/null

if [ "$MODO" = base ]; then
    MASTER="$(git rev-parse master 2>/dev/null || true)"
    [ "$SHA" = "$MASTER" ] || [ "${FORZAR:-0}" = 1 ] || \
        morir "la base es master: este commit ($(git rev-parse --short HEAD)) no es master (FORZAR=1 para otro)"
    [ "$QUE" = todo ] || morir "la base se arma con las dos plataformas (todo)"
fi
log "compilando $QUE en el commit $(git rev-parse --short HEAD) ($([ "$MODO" = base ] && echo "la BASE" || echo "capa $CAPA"))"
# --privileged: montar la capa (overlayfs) adentro del contenedor. tools/ci/capa-docker.sh
# arma /trabajo/ba (la base o la capa), trae el commit y corre tools/ci/receta-docker.sh.
d run --rm --platform linux/amd64 --privileged \
    -v ba-trabajo:/trabajo -v "$GITDIR:/repo.git:ro" -v "$ROOT/dist:/salida" $MONTAR_SIMBOLOS \
    -v "$ROOT/tools/ci/capa-docker.sh:/capa-docker.sh:ro" \
    -v ba-ccache:/ccache -e CCACHE_DIR=/ccache -e CCACHE_MAXSIZE=30G \
    -e MODO="$MODO" -e CAPA="$CAPA" \
    -e SHA="$SHA" -e QUE="$QUE" -e CORRIDA="$CORRIDA" -e PERFILAR="${PERFILAR:-0}" -e DIAG_ACOPLES="${DIAG_ACOPLES:-1}" \
    -e SIN_CACHE="${SIN_CACHE:-0}" -e SIMBOLOS="${SIMBOLOS:-0}" -e TESTS_SIN_CACHE="${TESTS_SIN_CACHE:-0}" \
    -e TESTS_SIN_LENTOS="${TESTS_SIN_LENTOS:-0}" ${TESTS_WIN_JOBS:+-e TESTS_WIN_JOBS="$TESTS_WIN_JOBS"} ${TESTS_JOBS:+-e TESTS_JOBS="$TESTS_JOBS"} \
    ${PYTEST_ARGS:+-e PYTEST_ARGS="$PYTEST_ARGS"} \
    ba-compilar bash /capa-docker.sh
log "listo: $(ls "$ROOT"/dist/*-linux-x64.tar.gz "$ROOT"/dist/*-windows-x64.zip 2>/dev/null | xargs -n1 basename | tr '\n' ' ')"
