#!/usr/bin/env bash
# ============================================================================
# tools/deps/tablero-web.sh — arma el tablero web (src/tablero-web: el dashboard de la app
# en Qt para WebAssembly) y deja en build-tablero-web/salida/ lo que va a
# Web/control/app del bundle (cmake/BaTableroWeb.cmake).
#
# Usa SOLO lo de extern/ (make deps-wasm): Qt para WebAssembly (extern/qt6-wasm), el Qt del
# host para las herramientas (moc, rcc, qmlimportscanner: extern/qt6-host) y emscripten
# (extern/emsdk-<versión>). Nada se instala en el sistema. Anda igual en la Mac y en Linux/Docker: el
# .wasm es el mismo para las tres plataformas.
#
#   JOBS=N   cuántos procesos de compilación (de fábrica 4)
#   QT_VER, QT_HOST_DIR, EMSDK_DIR   los pone mk/tablero-web.mk (de mk/versiones.mk)
# ============================================================================
set -euo pipefail
RAIZ="$(cd "$(dirname "$0")/../.." && pwd)"
EXT="$RAIZ/extern"
: "${QT_VER:?falta QT_VER: correlo con make tablero-web}"
: "${QT_HOST_DIR:?falta QT_HOST_DIR: correlo con make tablero-web}"
: "${EMSDK_DIR:?falta EMSDK_DIR: correlo con make tablero-web}"
QTW="$EXT/qt6-wasm/$QT_VER/wasm_singlethread"
QTH="$EXT/qt6-host/$QT_VER/$QT_HOST_DIR"
JOBS="${JOBS:-4}"
B="$RAIZ/build-tablero-web"
SALIDA="$B/salida"
# Los objetos, por versión de Qt: el caché de CMake de otra versión apuntaba a su Qt.
OBJ="$B/obj-$QT_VER"

for x in "$QTW/bin/qt-cmake" "$QTH/bin" "$EMSDK_DIR/emsdk_env.sh"; do
  [ -e "$x" ] || { echo "[tablero-web] falta $x: make deps-wasm" >&2; exit 1; }
done

# emscripten del extern (no el del sistema), sin que ensucie la salida.
export EMSDK_QUIET=1
export EMSDK_PYTHON="${EMSDK_PYTHON:-$(command -v python3)}"
# shellcheck disable=SC1091
source "$EMSDK_DIR/emsdk_env.sh" >/dev/null

echo "[tablero-web] configurando (Qt $QT_VER para WebAssembly, $(emcc --version | head -1))"
sh "$QTW/bin/qt-cmake" -S "$RAIZ/src/tablero-web" -B "$OBJ" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DQT_HOST_PATH="$QTH" >/dev/null
echo "[tablero-web] compilando (-j$JOBS)"
cmake --build "$OBJ" -j"$JOBS"

# --- La salida ---------------------------------------------------------------------------------
rm -rf "$SALIDA.nueva"
mkdir -p "$SALIDA.nueva"
cp "$OBJ/ba-tablero.wasm" "$OBJ/ba-tablero.js" "$SALIDA.nueva/"
cp "$OBJ/qtloader.js" "$SALIDA.nueva/" 2>/dev/null || cp "$QTW/plugins/platforms/qtloader.js" "$SALIDA.nueva/"
cp "$RAIZ/src/tablero-web/web/puente.js" "$RAIZ/src/tablero-web/web/tablero.css" "$SALIDA.nueva/"
# La versión: el hash del .wasm y los .js (otra versión = otra URL; el navegador guarda la vieja para siempre).
VERSION="$(cat "$SALIDA.nueva/ba-tablero.wasm" "$SALIDA.nueva/ba-tablero.js" "$SALIDA.nueva/puente.js" "$SALIDA.nueva/qtloader.js" \
  | { shasum -a 256 2>/dev/null || sha256sum; })"
VERSION="${VERSION:0:12}"
sed "s/@VERSION@/$VERSION/g" "$RAIZ/src/tablero-web/web/index.html" > "$SALIDA.nueva/index.html"
# Comprimido de antemano: .gz para HTTP (el navegador no pide br sin TLS) y .br para HTTPS.
for f in ba-tablero.wasm ba-tablero.js qtloader.js; do
  gzip -9 -k -n "$SALIDA.nueva/$f"
  if command -v brotli >/dev/null 2>&1; then brotli -q 11 -k "$SALIDA.nueva/$f"; fi
done

# --- Licencia (LGPL de Qt: el .wasm va estático; hay que poder volver a enlazarlo) ------------
# Van los objetos propios y el comando para enlazarlos contra otro Qt para WebAssembly.
mkdir -p "$SALIDA.nueva/licencia"
cp "$RAIZ/src/tablero-web/LICENCIA-QT.txt" "$SALIDA.nueva/licencia/LEEME.txt"
# El comando de enlace tal cual (rutas de esta máquina: se cambian por las del otro Qt).
(cd "$OBJ" && ninja -t commands ba-tablero | tail -1 > "$B/enlazar.txt") || true
(cd "$OBJ" && find . -name '*.o' | LC_ALL=C sort > "$B/objetos.txt")
cp "$B/enlazar.txt" "$OBJ/enlazar.txt"
tar -czf "$SALIDA.nueva/licencia/ba-tablero-objetos.tar.gz" -C "$OBJ" -T "$B/objetos.txt" enlazar.txt

rm -rf "$SALIDA"
mv "$SALIDA.nueva" "$SALIDA"
du -h "$SALIDA"/ba-tablero.wasm* | sed 's/^/[tablero-web] /'
echo "[tablero-web] listo: $SALIDA (versión $VERSION)"
