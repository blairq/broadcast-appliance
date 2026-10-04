#!/usr/bin/env bash
# ============================================================================
# tools/deps/sherpa-onnx.sh — escuchar (y hablar) aca adentro: sherpa-onnx
#
# Proposito:
#   El control por voz de la cabina (docs/AUDIO-TTS.md): push-to-talk, el
#   audio del operador al reconocedor y el texto a la barra del asistente.
#   sherpa-onnx corre los modelos de voz sobre ONNX Runtime —reconocer
#   (Parakeet, Canary...), detectar voz (Silero VAD) y, mas adelante, hablar
#   (voces de piper)— en CPU, que es lo que esta libre: la GPU la usa libobs.
#
# POR QUE COMPILADO Y NO EL BINARIO PUBLICADO. Porque va contra EL MISMO ONNX
# Runtime que ya viaja en el bundle (extern/rostros, el de los rostros): dos
# runtimes distintos en el mismo proceso es pedir un choque de simbolos.
# sherpa-onnx queda estatico (sus .a, sin dependencias nuevas en el bundle).
#
# LOS MODELOS NO VAN ACA. Se bajan a pedido desde la app (Configuracion ->
# Modelos locales), verificados, como el modelo de lenguaje. Lo unico que
# viaja en el bundle es el ONNX de rostros.
#
# Windows: `tools/deps/sherpa-onnx.sh win` lo compila DESDE Linux con clang-cl
# (cmake/toolchains/windows-clang-cl.cmake, ABI de MSVC, /MD) contra el ONNX
# Runtime de Windows (extern/onnxruntime-win) y lo deja en
# extern/sherpa-onnx-win (aparte: el volumen de compilar es el mismo).
#
# Uso: tools/deps/sherpa-onnx.sh [win]   (idempotente; necesita make deps-rostros
#                                         o, para win, deps-onnxruntime-win)
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/extern/.src/sherpa-onnx"
WIN=0
[ "${1:-}" = win ] && WIN=1
if [ "$WIN" = 1 ]; then
    OUT="$ROOT/extern/sherpa-onnx-win"
    ORT="$ROOT/extern/onnxruntime-win"
else
    OUT="$ROOT/extern/sherpa-onnx"
    ORT="$ROOT/extern/rostros/onnxruntime"
fi
VER="v1.13.8"
REV="11afbd009a7f8c08f4bcf2fc1b265d0df4670fbf"

log()  { printf '\033[36m[sherpa-onnx]\033[0m %s\n' "$*"; }
morir() { printf '\033[31m[sherpa-onnx] ERROR:\033[0m %s\n' "$*" >&2; exit 1; }

if [ -f "$OUT/.ok" ] && [ "$(cat "$OUT/.ok")" = "$REV" ]; then
    log "ya estaba ($VER)"
    exit 0
fi
[ -f "$ORT/include/onnxruntime_cxx_api.h" ] || morir "falta ONNX Runtime en $ORT: make deps-rostros (o deps-onnxruntime-win)"

if [ ! -d "$SRC/.git" ]; then
    mkdir -p "$(dirname "$SRC")"
    git clone -q https://github.com/k2-fsa/sherpa-onnx "$SRC"
fi
git -C "$SRC" fetch -q origin "$REV" 2>/dev/null || git -C "$SRC" fetch -q --tags origin
git -C "$SRC" checkout -q -f "$REV"
[ "$(git -C "$SRC" rev-parse HEAD)" = "$REV" ] || morir "no es el commit fijado ($REV)"
if [ "$WIN" = 1 ]; then
    # En Windows sin GPU, cmake/onnxruntime.cmake no define la biblioteca de
    # importación (IMPORTED_IMPLIB vacío: «set_target_properties called with
    # incorrect number of arguments»). Como la rama con GPU: la DLL y el .lib.
    sed -i 's|^        set(location_onnxruntime_lib $ENV{SHERPA_ONNXRUNTIME_LIB_DIR}/onnxruntime.lib)$|        set(location_onnxruntime_lib $ENV{SHERPA_ONNXRUNTIME_LIB_DIR}/onnxruntime.dll)\n        set(location_onnxruntime_lib2 $ENV{SHERPA_ONNXRUNTIME_LIB_DIR}/onnxruntime.lib)|' \
        "$SRC/cmake/onnxruntime.cmake"
    grep -q 'location_onnxruntime_lib2 $ENV{SHERPA_ONNXRUNTIME_LIB_DIR}/onnxruntime.lib)$' "$SRC/cmake/onnxruntime.cmake" || morir "no pude parchear cmake/onnxruntime.cmake"
fi

extra=()
if [ "$WIN" = 1 ]; then
    [ -x "$ROOT/extern/toolchain/llvm/bin/clang-cl" ] || morir "falta el toolchain de Windows: make deps-win-toolchain"
    extra=(-DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchains/windows-clang-cl.cmake")
else
case "$(uname -s)" in
    Darwin) extra=(-DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.3) ;;
    Linux) extra=(-DCMAKE_POSITION_INDEPENDENT_CODE=ON) ;;
    *) morir "plataforma sin receta todavia: $(uname -s)" ;;
esac
fi

B="$SRC/build-ba"
[ "$WIN" = 1 ] && B="$SRC/build-ba-win"
rm -rf "$B" "$OUT"
log "compilando $VER contra ONNX Runtime de $ORT"
SHERPA_ONNXRUNTIME_INCLUDE_DIR="$ORT/include" SHERPA_ONNXRUNTIME_LIB_DIR="$ORT/lib" \
cmake -S "$SRC" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release "${extra[@]}" \
    -DBUILD_SHARED_LIBS=OFF \
    -DSHERPA_ONNX_ENABLE_PORTAUDIO=OFF -DSHERPA_ONNX_ENABLE_WEBSOCKET=OFF \
    -DSHERPA_ONNX_ENABLE_PYTHON=OFF -DSHERPA_ONNX_ENABLE_TESTS=OFF \
    -DSHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION=OFF -DSHERPA_ONNX_ENABLE_TTS=ON \
    -DSHERPA_ONNX_ENABLE_BINARY=OFF -DSHERPA_ONNX_BUILD_C_API_EXAMPLES=OFF \
    -DSHERPA_ONNX_ENABLE_C_API=ON \
    -DCMAKE_INSTALL_PREFIX="$OUT" >"$B.log" 2>&1 || { tail -30 "$B.log"; morir "cmake"; }
cmake --build "$B" -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)" >>"$B.log" 2>&1 || { tail -30 "$B.log"; morir "compilacion"; }

mkdir -p "$OUT/include/sherpa-onnx/c-api" "$OUT/lib"
cp "$SRC/sherpa-onnx/c-api/c-api.h" "$SRC/sherpa-onnx/c-api/cxx-api.h" "$OUT/include/sherpa-onnx/c-api/"
if [ "$WIN" = 1 ]; then cp "$B"/lib/*.lib "$OUT/lib/"; else cp "$B"/lib/*.a "$OUT/lib/"; fi
cp "$SRC/LICENSE" "$OUT/LICENSE"
echo "$REV" >"$OUT/.ok"
log "listo: $OUT ($(ls "$OUT/lib" | wc -l | tr -d ' ') bibliotecas estaticas)"
