#!/usr/bin/env bash
# ============================================================================
# tools/deps/obs-macos.sh — libobs 31 + plugins para macOS arm64, desde fuente
#
# Se corre EN la Mac (Xcode completo instalado: OBS en macOS solo compila con el
# generador de Xcode). Usa:
#   - la fuente de OBS del tag fijado (extern/obs-src, `make deps-obs` la clona);
#   - los obs-deps oficiales de macOS arm64 (FFmpeg 7, x264, mbedTLS, srt,
#     rist, freetype...), fijados por hash: los mismos que usa OBS 31.
#
# Deja en extern/obs-macos un layout listo para el bundle portable:
#   lib/        libobs.framework, libobs-opengl.so y los dylibs de obs-deps
#   plugins/    bundles .plugin (binario en Contents/MacOS, datos en Resources)
#   bin/        obs-ffmpeg-mux (grabacion)
#   data/libobs shaders .effect de libobs
# Todo se referencia por @rpath: el ejecutable del bundle lleva
# LC_RPATH @executable_path y encuentra todo junto a el (bin/64bit).
# ============================================================================
set -euo pipefail
command -v sha256sum >/dev/null 2>&1 || sha256sum() { shasum -a 256 "$@"; }

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DL="$ROOT/extern/.downloads"
SRC="$ROOT/extern/obs-src"
DEPS="$ROOT/extern/obs-deps-macos"
BUILD="$ROOT/extern/.build-obs-macos"
OUT="$ROOT/extern/obs-macos"
JOBS="${1:-$(sysctl -n hw.ncpu)}"
log() { echo "[obs-macos] $*" >&2; }

DEPS_VER=2024-09-12
DEPS_SHA=69bc0848ca93f23134c46d59e4c41496370a439e3661890d5150a13db3c1f021
DEPS_URL="https://github.com/obsproject/obs-deps/releases/download/$DEPS_VER/macos-deps-$DEPS_VER-arm64.tar.xz"
# Plugins que usa el appliance (equivalentes a los de Linux + los de Mac):
# captura de camaras (mac-avcapture), pantalla y audio del sistema
# (mac-capture), codificadores por hardware (mac-videotoolbox) y el resto.
PLUGINS=(image-source mac-avcapture mac-capture mac-videotoolbox coreaudio-encoder obs-ffmpeg obs-filters
    obs-outputs obs-transitions obs-x264 rtmp-services text-freetype2)

[ "$(uname -s)" = Darwin ] || { log "esto se corre en macOS"; exit 1; }
[ "$(uname -m)" = arm64 ] || { log "solo Apple Silicon (arm64)"; exit 1; }
xcodebuild -version >/dev/null 2>&1 || {
    log "falta Xcode completo (no alcanza con las Command Line Tools): App Store -> Xcode, y"
    log "  sudo xcode-select -s /Applications/Xcode.app/Contents/Developer"
    exit 1
}
[ -f "$SRC/libobs/obs.h" ] || { log "falta extern/obs-src: make deps-obs-src"; exit 1; }
TAG="$(git -C "$SRC" describe --tags 2>/dev/null || echo '?')"
if [ "$(cat "$OUT/.version" 2>/dev/null)" = "$TAG+$DEPS_VER" ] && [ -d "$OUT/lib/libobs.framework" ]; then
    log "libobs $TAG para macOS: ya presente"
    exit 0
fi

# --- obs-deps -----------------------------------------------------------------
mkdir -p "$DL"
f="$DL/macos-deps-$DEPS_VER-arm64.tar.xz"
if ! [ -f "$f" ] || [ "$(sha256sum "$f" | cut -d' ' -f1)" != "$DEPS_SHA" ]; then
    log "descargando obs-deps $DEPS_VER (arm64)"
    curl -fL --retry 3 -o "$f.part" "$DEPS_URL"
    [ "$(sha256sum "$f.part" | cut -d' ' -f1)" = "$DEPS_SHA" ] || { log "ERROR: hash de obs-deps"; exit 1; }
    mv "$f.part" "$f"
fi
if [ "$(cat "$DEPS/.version" 2>/dev/null)" != "$DEPS_VER" ]; then
    rm -rf "$DEPS" && mkdir -p "$DEPS"
    tar xJf "$f" -C "$DEPS"
    # Gatekeeper: lo bajado con curl no queda en cuarentena, pero por las dudas.
    xattr -dr com.apple.quarantine "$DEPS" 2>/dev/null || true
    echo "$DEPS_VER" >"$DEPS/.version"
fi

# --- configurar y compilar (Xcode) ---------------------------------------------
log "configurando libobs $TAG (Xcode, arm64, macOS 13+)"
cmake -S "$SRC" -B "$BUILD" -G Xcode \
    -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
    -DCMAKE_PREFIX_PATH="$DEPS" \
    -DOBS_CODESIGN_IDENTITY="-" -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
    -DENABLE_UI=OFF -DENABLE_FRONTEND=OFF -DENABLE_SCRIPTING=OFF -DENABLE_BROWSER=OFF \
    -DENABLE_WEBSOCKET=OFF -DENABLE_VLC=OFF -DENABLE_VST=OFF -DENABLE_AJA=OFF -DENABLE_DECKLINK=OFF \
    -DENABLE_VIRTUALCAM=OFF -DENABLE_WEBRTC=OFF -DENABLE_SPARKLE=OFF -DENABLE_SYPHON=OFF \
    -DENABLE_NEW_MPEGTS_OUTPUT=OFF -DENABLE_SERVICE_UPDATES=OFF -DENABLE_PLUGINS=ON \
    -DCMAKE_COMPILE_WARNING_AS_ERROR=OFF >"$BUILD.configure.log" 2>&1 ||
    { tail -40 "$BUILD.configure.log" >&2; log "ERROR al configurar (log: $BUILD.configure.log)"; exit 1; }

log "compilando libobs, libobs-opengl, obs-ffmpeg-mux y ${#PLUGINS[@]} plugins"
targets=(libobs libobs-opengl obs-ffmpeg-mux "${PLUGINS[@]}")
targs=()
for t in "${targets[@]}"; do targs+=(--target "$t"); done
cmake --build "$BUILD" --config Release "${targs[@]}" -- -jobs "$JOBS" >"$BUILD.build.log" 2>&1 ||
    { grep -E "error:|BUILD FAILED" "$BUILD.build.log" | head -30 >&2; log "ERROR al compilar (log: $BUILD.build.log)"; exit 1; }

# --- juntar el resultado ---------------------------------------------------------
rm -rf "$OUT" && mkdir -p "$OUT/lib" "$OUT/plugins" "$OUT/bin" "$OUT/data"
fw="$(find "$BUILD" -type d -name libobs.framework -path '*Release*' | head -1)"
[ -n "$fw" ] || { log "ERROR: no aparecio libobs.framework"; exit 1; }
cp -R "$fw" "$OUT/lib/"
gl="$(find "$BUILD" \( -type f -o -type l \) -name 'libobs-opengl*.dylib' -path '*Release*' | head -1)"
[ -n "$gl" ] || { log "ERROR: no aparecio libobs-opengl"; exit 1; }
# El motor lo abre por ruta como libobs-opengl.so (ver BroadcastEngine).
cp -L "$gl" "$OUT/lib/libobs-opengl.so"
for p in "${PLUGINS[@]}"; do
    b="$(find "$BUILD" -type d -name "$p.plugin" -path '*Release*' | head -1)"
    if [ -n "$b" ]; then cp -R "$b" "$OUT/plugins/"; else log "aviso: no aparecio $p.plugin"; fi
done
mux="$(find "$BUILD" -type f -name obs-ffmpeg-mux -perm -u+x -path '*Release*' | head -1)"
[ -n "$mux" ] && cp "$mux" "$OUT/bin/"
cp -R "$SRC/libobs/data" "$OUT/data/libobs"
# Dylibs de obs-deps que usan libobs y los plugins (se copian con sus enlaces).
(cd "$DEPS/lib" && find . -maxdepth 1 -name '*.dylib' | while read -r d; do cp -a "$d" "$OUT/lib/"; done)
# Referencias por @rpath: el bundle las resuelve con el LC_RPATH del ejecutable.
for bin in "$OUT/lib/libobs-opengl.so" "$OUT/bin/obs-ffmpeg-mux" "$OUT"/plugins/*.plugin/Contents/MacOS/*; do
    [ -f "$bin" ] || continue
    install_name_tool -add_rpath @executable_path "$bin" 2>/dev/null || true
done
# Firma ad-hoc (Apple Silicon no ejecuta codigo sin firmar; install_name_tool la invalida).
find "$OUT" \( -name '*.dylib' -o -name '*.so' \) -type f -exec codesign -f -s - {} \; 2>/dev/null
codesign -f -s - "$OUT/lib/libobs.framework" 2>/dev/null || true
for b in "$OUT"/plugins/*.plugin; do codesign -f -s - "$b" 2>/dev/null || true; done
[ -f "$OUT/bin/obs-ffmpeg-mux" ] && codesign -f -s - "$OUT/bin/obs-ffmpeg-mux"
echo "$TAG+$DEPS_VER" >"$OUT/.version"
log "listo: $OUT ($(ls "$OUT/plugins" | wc -l | tr -d ' ') plugins)"
