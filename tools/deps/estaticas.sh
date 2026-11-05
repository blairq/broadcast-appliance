#!/usr/bin/env bash
# ============================================================================
# tools/deps/estaticas.sh — mbedtls, libsamplerate, argon2, libdeflate y zxing-cpp,
# compiladas UNA vez por máquina y plataforma (cmake/estaticas)
#
# Quedan en extern/estaticas/<plataforma>-<huella>/ (la huella: las fuentes fijadas, la
# configuración de mbedtls y la receta; cmake/estaticas/huella.cmake). Si ya está esa
# carpeta, no hace nada. Las fuentes son las de extern/ (make deps-src).
#
# Uso: tools/deps/estaticas.sh <macos|linux|win|win-mt> [jobs]
# Dos a la vez (dos worktrees) no se pisan: cada uno compila en su carpeta y la que llega
# segunda descarta la suya (el resultado es el mismo).
# ============================================================================
set -euo pipefail
PLAT="${1:?uso: $0 <macos|linux|win|win-mt> [jobs]}"
JOBS="${2:-4}"
RAIZ="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$RAIZ"
log() { echo "[estaticas] $PLAT: $*"; }

DEST="$(cmake -DPLATAFORMA="$PLAT" -DRAIZ="$RAIZ" -P cmake/estaticas/huella.cmake)"
if [ -f "$DEST/ba-estaticas.cmake" ]; then
    log "al día ($DEST)"
    exit 0
fi

args=(-DBA_RAIZ="$RAIZ" -DBA_PLATAFORMA="$PLAT" -DCMAKE_BUILD_TYPE=Release)
command -v ninja >/dev/null 2>&1 && args+=(-G Ninja)
case "$PLAT" in
    macos)
        # Lo mismo que BA_CMAKE_PLATAFORMA del Makefile; ar reproducible.
        args+=(-DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.3)
        export ZERO_AR_DATE=1 ;;
    linux) ;;
    win)    args+=(-DCMAKE_TOOLCHAIN_FILE="$RAIZ/cmake/toolchains/windows-clang-cl.cmake"
                   -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL) ;;
    win-mt) args+=(-DCMAKE_TOOLCHAIN_FILE="$RAIZ/cmake/toolchains/windows-clang-cl.cmake"
                   -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded) ;;
    *) echo "[estaticas] plataforma desconocida: $PLAT" >&2; exit 2 ;;
esac

OBRA="extern/estaticas/.obra-$PLAT-$$"
PARCIAL="$DEST.parcial-$$"
limpiar() { rm -rf "$OBRA" "$PARCIAL"; }
trap limpiar EXIT
log "compilando a $DEST"
cmake -S cmake/estaticas -B "$OBRA" "${args[@]}" >/dev/null
cmake --build "$OBRA" --target ba_estaticas -j"$JOBS"
cmake --install "$OBRA" --component ba --prefix "$PARCIAL" >/dev/null
# (mv sobre una carpeta que ya existe metería esta ADENTRO: primero se mira.)
if [ -e "$DEST" ]; then
    log "otro las terminó antes: quedan esas"
else
    mv "$PARCIAL" "$DEST"
    log "listas ($(ls "$DEST/lib" | wc -l | tr -d ' ') bibliotecas)"
fi
