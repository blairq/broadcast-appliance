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

cd "$ROOT"
[ -z "$(git status --porcelain --untracked-files=no)" ] || morir "hay cambios sin commitear: se compila un commit, no el arbol de trabajo"
SHA="$(git rev-parse HEAD)"
mkdir -p "$ROOT/dist"

log "imagen ba-compilar (Ubuntu 24.04 x86_64)"
d build -q --platform linux/amd64 -t ba-compilar -f "$ROOT/tools/ci/compilar.Dockerfile" "$ROOT/tools/ci" >/dev/null

log "compilando $QUE en el commit $(git rev-parse --short HEAD) (la primera vez tarda horas)"
d run --rm --platform linux/amd64 \
    -v ba-trabajo:/trabajo -v "$ROOT:/repo:ro" -v "$ROOT/dist:/salida" \
    -v ba-ccache:/ccache -e CCACHE_DIR=/ccache -e CCACHE_MAXSIZE=30G \
    -e SHA="$SHA" -e QUE="$QUE" -e PERFILAR="${PERFILAR:-0}" ba-compilar bash -c '
set -euo pipefail
# Cuánto tarda cada etapa (al final, un resumen): para saber dónde se va el tiempo.
T0=$(date +%s); TE=$T0; RESUMEN=""
etapa() { local t=$(date +%s); RESUMEN="$RESUMEN$(printf "  %-28s %4d s" "$1" $((t - TE)))\n"; TE=$t; }
resumen() {
    printf "\n[tiempos] total %d s\n%b" $(( $(date +%s) - T0 )) "$RESUMEN"
    ccache -s 2>/dev/null | grep -iE "hits|misses|cache size" | sed "s/^/  ccache: /" || true
}
trap resumen EXIT
ccache -z >/dev/null 2>&1 || true
# La caché de ccache, en su propio volumen (ba-ccache): en /root del contenedor
# (--rm) se perdía en cada corrida y ccache no aceleraba nada.
git config --global --add safe.directory "*"
[ -d /trabajo/ba/.git ] || git clone -q /repo /trabajo/ba
cd /trabajo/ba
git fetch -q /repo "$SHA"
git checkout -q -f "$SHA"
[ -x .venv/bin/python ] || python3 -m venv .venv
etapa "checkout"
J=$(nproc)
SUF=""; [ "$PERFILAR" = 1 ] && SUF="-perfil"   # PERFILAR=1: un build que mide (diag/Perfil)
if [ "$QUE" != win ]; then
    make deps-linux JOBS=$J
    make build JOBS=$J PERFILAR=$PERFILAR
    make test
    .venv/bin/pip install -q -r requirements-dev.lock
    .venv/bin/pytest -q
    make dist-linux JOBS=$J PERFILAR=$PERFILAR
    cp dist/*-linux-x64$SUF.tar.gz dist/*-linux-x64$SUF.tar.gz.sha256 dist/*-linux-x64$SUF.tar.gz.commit /salida/
    mkdir -p /salida/simbolos && cp -rn dist/simbolos/linux /salida/simbolos/ 2>/dev/null || true
fi
if [ "$QUE" != linux ]; then
    make deps-win JOBS=$J BA_ACCEPT_MSVC_LICENSE=1
    etapa "deps-win"
    make win JOBS=$J BA_ACCEPT_MSVC_LICENSE=1 PERFILAR=$PERFILAR
    etapa "compilar (win)"
    # Los tests de Windows corren bajo Wine. En una Mac, Wine va encima de
    # Rosetta y se cae de vez en cuando (rc 133) en tests que en el runner del
    # CI pasan: cada corrida, uno distinto. Por eso un fallo aca NO frena el
    # .zip, pero se informa y la corrida termina con error: no es verde.
    tests_win=0
    make test-win BA_ACCEPT_MSVC_LICENSE=1 PERFILAR=$PERFILAR || tests_win=$?
    etapa "tests bajo Wine"
    make release-win JOBS=$J BA_ACCEPT_MSVC_LICENSE=1 PERFILAR=$PERFILAR
    etapa "release-win (install+chequeo+manifest)"
    make -o release-win dist-win JOBS=$J BA_ACCEPT_MSVC_LICENSE=1 PERFILAR=$PERFILAR
    etapa "zip (7z)"
    cp dist/*-windows-x64$SUF.zip dist/*-windows-x64$SUF.zip.sha256 dist/*-windows-x64$SUF.zip.commit /salida/
    # Los binarios y los .pdb de este build, al almacén de dist/simbolos (en la
    # Mac): para leer después las pilas de sus volcados (tools/caidas/analizar.py).
    python3 tools/caidas/simbolos.py guardar --release release-win --buscar build/win extern/gstreamer-win-pdb \
        --destino /salida/simbolos --commit "$SHA" || echo "ATENCION: no se guardaron los símbolos" >&2
    if [ "$tests_win" != 0 ]; then
        echo "ATENCION: los tests de Windows (bajo Wine) fallaron: ver arriba. El .zip se armo igual." >&2
        exit 3
    fi
fi
'
log "listo: $(ls "$ROOT"/dist/*-linux-x64.tar.gz "$ROOT"/dist/*-windows-x64.zip 2>/dev/null | xargs -n1 basename | tr '\n' ' ')"
