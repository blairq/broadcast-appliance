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
# Uso: tools/deps/python.sh <archivo.tar.gz> <salida>   (lo llama make deps-python)
# ============================================================================
set -euo pipefail
TGZ="${1:?falta el .tar.gz}"
OUT="${2:?falta la salida}"
log() { printf '\033[36m[python]\033[0m %s\n' "$*"; }

rm -rf "$OUT.tmp" "$OUT"
mkdir -p "$OUT.tmp"
tar -xzf "$TGZ" -C "$OUT.tmp"
mv "$OUT.tmp/python" "$OUT"
rm -rf "$OUT.tmp"
# Lo que no hace falta en el programa: cabeceras, tests, IDLE/Tk, pip lo trae
# ensurepip (se usa pip para bajar bibliotecas de los plugins).
rm -rf "$OUT/include" "$OUT/share"
rm -rf "$OUT"/lib/python3.*/test "$OUT"/lib/python3.*/idlelib "$OUT"/lib/python3.*/tkinter \
       "$OUT"/lib/python3.*/turtledemo "$OUT"/lib/tcl* "$OUT"/lib/tk* "$OUT"/lib/itcl* "$OUT"/lib/thread*
# numpy viene incluido: casi todo plugin de video lo usa, y bajarlo para cada
# uno era lo más lento de probar un plugin. Versión fija.
"$OUT/bin/python3" -m pip install -q --disable-pip-version-check --no-warn-script-location "numpy==2.5.3"
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
"$OUT/bin/python3" -c 'import sys, ssl, sqlite3, ctypes; print("python", sys.version.split()[0], "ok")'
log "listo: $OUT ($(du -sh "$OUT" | cut -f1))"
