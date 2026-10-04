#!/usr/bin/env bash
# ============================================================================
# tools/deps/python.sh — el Python de los plugins (portable, vendoreado)
#
# Los plugins de equipos y de video/audio (docs/PLUGINS.md) son programas en
# Python que corren en su propio proceso. El intérprete viaja ADENTRO del
# programa: nada del sistema (ver la regla de portabilidad). Es el de
# python-build-standalone (CPython relocalizable, sin rutas absolutas), la
# variante `install_only_stripped`.
#
# Las bibliotecas que necesite cada plugin (numpy, pyusb, hidapi…) NO van acá:
# se bajan a pedido a la carpeta del propio plugin (Datos/Plugins/<id>/_libs),
# que es portable y sobrevive a las actualizaciones.
#
# Uso: tools/deps/python.sh <archivo.tar.gz> <salida> [plataforma] [python del host]
#      (lo llaman make deps-python y deps-python-win)
# ============================================================================
set -euo pipefail
TGZ="${1:?falta el .tar.gz}"
OUT="${2:?falta la salida}"
PLAT="${3:-}"
HOSTPY="${4:-}" # para Windows: el Python 3.12 del sistema que compila (baja las ruedas de win_amd64)
log() { printf '\033[36m[python]\033[0m %s\n' "$*"; }

rm -rf "$OUT.tmp" "$OUT"
mkdir -p "$OUT.tmp"
tar -xzf "$TGZ" -C "$OUT.tmp"
mv "$OUT.tmp/python" "$OUT"
rm -rf "$OUT.tmp"
# Las bibliotecas que trae: numpy (casi todo plugin de video lo usa) y la
# traducción en vivo (assets/traductor: CTranslate2 con los modelos OPUS-MT de
# Argos y sus tokenizadores; sin torch ni stanza). Versiones fijas.
LIBS=("numpy==2.5.3" "ctranslate2==4.8.2" "sentencepiece==0.2.2" "sacremoses==0.2.0" "subword-nmt==0.3.8")

if [ "$PLAT" = win-x64 ]; then
    # Python de Windows: python.exe, Lib/, DLLs/, include/ y libs/ (los .lib
    # para compilar extensiones, como los bindings de GStreamer).
    rm -rf "$OUT"/Lib/test "$OUT"/Lib/idlelib "$OUT"/Lib/tkinter "$OUT"/Lib/turtledemo "$OUT"/tcl
    [ -x "$HOSTPY" ] || { log "ERROR: para Windows hace falta el Python del sistema que compila (4º argumento)"; exit 1; }
    "$HOSTPY" -m pip install -q --disable-pip-version-check --no-warn-script-location \
        --platform win_amd64 --python-version 3.12 --implementation cp --only-binary=:all: \
        --target "$OUT/Lib/site-packages" "${LIBS[@]}"
    find "$OUT" -name '__pycache__' -type d -prune -exec rm -rf {} +
    # El bytecode es el mismo en cualquier sistema (misma versión de Python).
    "$HOSTPY" -m compileall -q -j 0 "$OUT/Lib" >/dev/null 2>&1 || true
    grep -h "^Version" "$OUT"/Lib/site-packages/ctranslate2-*.dist-info/METADATA >/dev/null
    echo "$(basename "$TGZ")" >"$OUT/.version"
    log "listo: $OUT ($(du -sh "$OUT" | cut -f1))"
    exit 0
fi

# Lo que no hace falta en el programa: tests, IDLE/Tk. Las cabeceras (include/)
# quedan para compilar extensiones (los bindings de GStreamer): el bundle no
# las lleva (cmake/BaPython.cmake).
rm -rf "$OUT/share"
rm -rf "$OUT"/lib/python3.*/test "$OUT"/lib/python3.*/idlelib "$OUT"/lib/python3.*/tkinter \
       "$OUT"/lib/python3.*/turtledemo "$OUT"/lib/tcl* "$OUT"/lib/tk* "$OUT"/lib/itcl* "$OUT"/lib/thread*
"$OUT/bin/python3" -m pip install -q --disable-pip-version-check --no-warn-script-location "${LIBS[@]}"
# El bytecode, precompilado acá: en ejecución Python corre con -B (no escribe
# nada adentro del programa: escribir en la .app rompe el sello de la firma).
find "$OUT" -name '__pycache__' -type d -prune -exec rm -rf {} +
"$OUT/bin/python3" -m compileall -q -j 0 "$OUT/lib" >/dev/null 2>&1 || true
# Apple Silicon no corre código sin firma: todo Mach-O, firmado (ad-hoc acá;
# make app-macos vuelve a sellar la .app entera).
if [ "$(uname -s)" = Darwin ]; then
    find "$OUT" -type f \( -name '*.so' -o -name '*.dylib' -o -perm -u+x \) -print0 |
        while IFS= read -r -d '' f; do
            file -b "$f" | grep -q Mach-O && codesign -f -s - "$f" 2>/dev/null || true
        done
fi
"$OUT/bin/python3" -c 'import sys, ssl, sqlite3, ctypes, ctranslate2, sentencepiece, sacremoses, subword_nmt; print("python", sys.version.split()[0], "ok")'
log "listo: $OUT ($(du -sh "$OUT" | cut -f1))"
