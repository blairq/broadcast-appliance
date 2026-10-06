#!/usr/bin/env bash
# ============================================================================
# tools/deps/gstreamer-win.sh — GStreamer para Windows (MSVC x86_64)
#
# Los binarios OFICIALES (gstreamer.freedesktop.org), como el Qt de Windows:
# el .msi de ejecución y el de desarrollo, desarmados SIN instalar con
# msiextract (tools/deps/msitools.sh). Es la 1.26: desde la 1.28 el oficial de
# Windows viene en un instalador Inno Setup que no se puede desarmar desde
# Linux (ni innoextract ni Wine sobre Rosetta).
#
# Queda en extern/gstreamer-win con el layout que lleva el bundle en
# bin/64bit/gstreamer:
#   bin/                las DLL (el build las copia a bin/64bit, junto al .exe:
#                       Windows busca primero ahí)
#   lib/gstreamer-1.0/  los plugins
#   lib/gio/modules/    TLS de GIO (rtsps, https)
#   libexec/            gst-plugin-scanner.exe
#   etc/, share/        configuración, presets (sin las piezas de Python 3.13)
#   include/, lib/*.lib las cabeceras y bibliotecas de importación (solo build)
#
# Va TODO (también Python, GTK, los plugins de Qt y su FFmpeg, que es el único
# FFmpeg del bundle de Windows).
#
# Uso: tools/deps/gstreamer-win.sh <runtime.msi> <devel.msi> <salida>
# ============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
RT="$1"
DEV="$2"
OUT="$3"
log() { printf '\033[1;36m[gstreamer-win]\033[0m %s\n' "$*"; }
M="$ROOT/extern/msitools/msiextract"
[ -x "$M" ] || "$ROOT/tools/deps/msitools.sh"

T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
log "desarmando $(basename "$RT")"
(mkdir -p "$T/rt" && cd "$T/rt" && "$M" "$RT" >/dev/null)
log "desarmando $(basename "$DEV")"
(mkdir -p "$T/dev" && cd "$T/dev" && "$M" "$DEV" >/dev/null)
R="$T/rt/PFiles64/gstreamer/1.0/msvc_x86_64"
D="$T/dev/PFiles64/gstreamer/1.0/msvc_x86_64"
[ -d "$R/lib/gstreamer-1.0" ] || { log "ERROR: no encontré los plugins en el .msi"; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT"
# TODO el de ejecución (plugins de Python, GTK y Qt incluidos: se usan o no
# según lo que haya), y del de desarrollo las cabeceras y los .lib.
cp -a "$R"/. "$OUT/"
cp -a "$D/include" "$OUT/"
cp -a "$D"/lib/*.lib "$OUT/lib/"
mkdir -p "$OUT/lib/glib-2.0" "$OUT/lib/gstreamer-1.0"
cp -a "$D/lib/glib-2.0/include" "$OUT/lib/glib-2.0/"
cp -a "$D/lib/gstreamer-1.0/include" "$OUT/lib/gstreamer-1.0/" # gst/gl/gstglconfig.h
cp -a "$D/lib/pkgconfig" "$OUT/lib/" # para compilar los bindings de Python (gst-python-win.sh)

# Las piezas para Python 3.13 (el plugin gstpython.dll y gi en Lib/): el
# Python de los plugins es el 3.12 y sus bindings se compilan aparte
# (gst-python-win.sh, su gstpython.dll va a gst-plugins/). Dos plugins
# «python» chocarían, y el de 3.13 no carga (no hay python313.dll).
rm -f "$OUT"/lib/gstreamer-1.0/gstpython.dll "$OUT"/lib/gstreamer-1.0/gstpython.pdb
rm -rf "$OUT/Lib"
echo "1.26.11 msvc_x86_64 r4" >"$OUT/.version"
log "listo: $OUT ($(du -sh "$OUT" | cut -f1), $(ls "$OUT"/lib/gstreamer-1.0/*.dll | wc -l) plugins)"
