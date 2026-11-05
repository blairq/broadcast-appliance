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
# Uso: tools/ci/compilar-en-docker.sh [linux|win|todo]     (por defecto: todo)
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
#   - símbolos de Windows: solo si cambió el bundle.
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

# DE A UNA CORRIDA POR MÁQUINA. Todas usan el mismo volumen (ba-trabajo: un solo clon en
# /trabajo/ba, con `git checkout -f` del commit de cada una) y la misma colima: dos a la vez
# (dos worktrees, o Linux y Windows por separado) se pisan el árbol en medio de la
# compilación —sale un paquete mezclado de dos commits— y la que termina primero apaga
# colima debajo de la otra. El candado es una carpeta con el pid de la corrida; la de un
# proceso que ya no existe se saca sola. BA_DOCKER_CANDADO cambia dónde vive.
CANDADO="${BA_DOCKER_CANDADO:-$HOME/.cache/broadcast-appliance/docker.lock}"
mkdir -p "$(dirname "$CANDADO")"
avisado=0
while ! mkdir "$CANDADO" 2>/dev/null; do
    otro="$(cat "$CANDADO/pid" 2>/dev/null || true)"
    if [ -n "$otro" ] && ! kill -0 "$otro" 2>/dev/null; then
        log "el candado de Docker era de una corrida que ya no está (pid $otro): lo saco"
        rm -rf "$CANDADO"
        continue
    fi
    if [ "$avisado" = 0 ]; then
        log "otra corrida usa Docker (pid ${otro:-?}, $(cat "$CANDADO/quien" 2>/dev/null || echo '?')): espero a que termine"
        avisado=1
    fi
    sleep 5
done
echo $$ >"$CANDADO/pid"
echo "$ROOT $QUE" >"$CANDADO/quien"
APAGAR_COLIMA=0
al_salir() {
    if [ "$APAGAR_COLIMA" = 1 ]; then log "apagando colima"; colima stop >/dev/null 2>&1 || true; fi
    [ "$(cat "$CANDADO/pid" 2>/dev/null)" = "$$" ] && rm -rf "$CANDADO"
}
trap al_salir EXIT

# Colima: si está instalada y apagada, se prende para compilar y se APAGA al terminar
# (bien o mal). La VM reserva mucha memoria: dejarla prendida tras un armado ayudó a
# agotar la memoria de la Mac en una prueba de carga (kernel panic del 2026-11-01).
# Si ya estaba prendida, se la deja como estaba. COLIMA_DEJAR=1 no la apaga.
if command -v colima >/dev/null 2>&1 && ! colima status >/dev/null 2>&1; then
    log "prendiendo colima para compilar (se apaga al terminar)"
    colima start >/dev/null 2>&1 || morir "no se pudo prender colima"
    [ "${COLIMA_DEJAR:-0}" = 1 ] || APAGAR_COLIMA=1
fi

cd "$ROOT"
[ -z "$(git status --porcelain --untracked-files=no)" ] || morir "hay cambios sin commitear: se compila un commit, no el arbol de trabajo"
SHA="$(git rev-parse HEAD)"
# De dónde se trae el commit: el .git común (en un worktree, $ROOT/.git es un archivo que
# apunta a una ruta de la Mac que el contenedor no ve).
GITDIR="$(cd "$(git rev-parse --git-common-dir)" && pwd)"
mkdir -p "$ROOT/dist"

# Un registro por corrida en dist/logs/ (ignorado por git): todo lo que sale por la
# terminal, para ver después qué falló y dónde se fue el tiempo. Los registros de los
# tests que fallaron quedan al lado, en dist/logs/<corrida>/.
CORRIDA="$(date +%Y%m%d-%H%M%S)-$QUE-$(git rev-parse --short HEAD)"
mkdir -p "$ROOT/dist/logs"
exec > >(tee -a "$ROOT/dist/logs/$CORRIDA.log") 2>&1
log "registro de esta corrida: dist/logs/$CORRIDA.log"

log "imagen ba-compilar (Ubuntu 24.04 x86_64)"
d build -q --platform linux/amd64 -t ba-compilar -f "$ROOT/tools/ci/compilar.Dockerfile" "$ROOT/tools/ci" >/dev/null

log "compilando $QUE en el commit $(git rev-parse --short HEAD) (la primera vez tarda horas)"
d run --rm --platform linux/amd64 \
    -v ba-trabajo:/trabajo -v "$GITDIR:/repo.git:ro" -v "$ROOT/dist:/salida" \
    -v ba-ccache:/ccache -e CCACHE_DIR=/ccache -e CCACHE_MAXSIZE=30G \
    -e SHA="$SHA" -e QUE="$QUE" -e CORRIDA="$CORRIDA" -e PERFILAR="${PERFILAR:-0}" -e DIAG_ACOPLES="${DIAG_ACOPLES:-1}" \
    -e SIN_CACHE="${SIN_CACHE:-0}" -e TESTS_SIN_CACHE="${TESTS_SIN_CACHE:-0}" \
    -e TESTS_SIN_LENTOS="${TESTS_SIN_LENTOS:-0}" ${TESTS_WIN_JOBS:+-e TESTS_WIN_JOBS="$TESTS_WIN_JOBS"} ${TESTS_JOBS:+-e TESTS_JOBS="$TESTS_JOBS"} \
    ${PYTEST_ARGS:+-e PYTEST_ARGS="$PYTEST_ARGS"} \
    ba-compilar bash -c '
set -euo pipefail
export T0=$(date +%s)
ccache -z >/dev/null 2>&1 || true
# La caché de ccache, en su propio volumen (ba-ccache): en /root del contenedor
# (--rm) se perdía en cada corrida y ccache no aceleraba nada.
git config --global --add safe.directory "*"
[ -d /trabajo/ba/.git ] || git clone -q /repo.git /trabajo/ba
cd /trabajo/ba
git fetch -q /repo.git "$SHA"
git checkout -q -f "$SHA"
# El resto, del mismo commit (tools/ci/receta-docker.sh).
exec bash tools/ci/receta-docker.sh
'
log "listo: $(ls "$ROOT"/dist/*-linux-x64.tar.gz "$ROOT"/dist/*-windows-x64.zip 2>/dev/null | xargs -n1 basename | tr '\n' ' ')"
