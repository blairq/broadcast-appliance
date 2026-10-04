#!/bin/bash
# ============================================================================
# tools/deps/gst-python-mac.sh — los bindings de GStreamer para el Python de
# los plugins (macOS)
#
# Los oficiales del GStreamer.framework están compilados para Python 3.9 (el
# del sistema de Apple) y el de los plugins es el 3.12 (extern/python): una
# extensión de otra versión no carga. Se compilan acá, con versiones fijadas:
#   - PyGObject 3.50.2 (la que usa GStreamer 1.28): `import gi`
#   - gst-python (la misma versión que el framework): los overrides de Gst
#     (`from gi.repository import Gst` con la API cómoda) y libgstpython (el
#     plugin que deja escribir ELEMENTOS de GStreamer en Python)
# contra las bibliotecas del framework (glib, girepository, libffi).
#
# Quedan:
#   gi/ (con overrides de Gst)  -> extern/python/lib/python3.12/site-packages
#   libgstpython.dylib          -> extern/gst-python-mac/ (el bundle lo lleva a
#                                  gst-plugins/, junto a los plugins propios)
# Los .typelib vienen en el framework (lib/girepository-1.0); en ejecución
# GI_TYPELIB_PATH apunta ahí (platform/GstEntorno).
#
# RPATH relativos al bundle: gi/_gi.so busca el framework en
# ../../../../../bin/64bit/GStreamer.framework/Versions/1.0/lib (y en extern/
# al correr desde el árbol de build).
#
# Uso: tools/deps/gst-python-mac.sh <dir de descargas>
# ============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DL="${1:-$ROOT/extern/.downloads}"
PY="$ROOT/extern/python/bin/python3"
FW="$ROOT/extern/gstreamer-macos/GStreamer.framework/Versions/1.0"
OUT="$ROOT/extern/gst-python-mac"
SITE="$("$PY" -c 'import sysconfig; print(sysconfig.get_paths()["purelib"])')"
log() { printf '\033[1;36m[gst-python]\033[0m %s\n' "$*"; }
morir() { log "ERROR: $*"; exit 1; }
command -v sha256sum >/dev/null 2>&1 || sha256sum() { shasum -a 256 "$@"; }

GST_VER="$(grep -E '^Version:' "$FW/lib/pkgconfig/gstreamer-1.0.pc" | awk '{print $2}')"
PYGO_VER=3.50.2
PYGO_SHA=ece6b860aab77cb649fdfc6e88d8a83765e7a62f7ffd39a628d6e2a0d397a7ff
GSTPY_SHA_1_28_7=9c23c5dddfd2c9b94bb1b4e4f5b90ebc6400d84277c77cf91b1dbdb4a8b451f9
[ "$GST_VER" = 1.28.7 ] || morir "el framework es $GST_VER; fijá el sha256 de gst-python-$GST_VER"
[ -d "$("$PY" -c 'import sysconfig; print(sysconfig.get_paths()["include"])')" ] || morir "el Python no tiene cabeceras (make deps-python)"

if [ -f "$OUT/.version" ] && [ "$(cat "$OUT/.version")" = "$PYGO_VER $GST_VER" ] && [ -d "$SITE/gi" ]; then
    log "ya están ($OUT)"
    exit 0
fi

baja() { # url archivo sha
    [ -f "$DL/$2" ] && [ "$(sha256sum "$DL/$2" | cut -d' ' -f1)" = "$3" ] && return 0
    curl -fL --retry 3 -o "$DL/$2.part" "$1"
    [ "$(sha256sum "$DL/$2.part" | cut -d' ' -f1)" = "$3" ] || morir "sha256 de $2"
    mv "$DL/$2.part" "$DL/$2"
}
baja "https://download.gnome.org/sources/pygobject/3.50/pygobject-$PYGO_VER.tar.gz" "pygobject-$PYGO_VER.tar.gz" "$PYGO_SHA"
baja "https://origin.gstreamer.freedesktop.org/src/gst-python/gst-python-$GST_VER.tar.xz" "gst-python-$GST_VER.tar.xz" "$GSTPY_SHA_1_28_7"

T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
# meson y ninja para compilar, en un entorno aparte (no tocan el Python del bundle).
"$PY" -m venv "$T/venv"
"$T/venv/bin/pip" install -q --disable-pip-version-check "meson==1.9.1" "ninja==1.13.0"
export PATH="$T/venv/bin:$PATH"
command -v pkg-config >/dev/null || "$T/venv/bin/pip" install -q "pkgconf==2.4.3.post1"
STAGE="$T/stage"
export PKG_CONFIG_PATH="$STAGE/lib/pkgconfig:$FW/lib/pkgconfig"
export LDFLAGS="-L$FW/lib -Wl,-headerpad_max_install_names"
export CFLAGS="-I$FW/Headers"

tar -xzf "$DL/pygobject-$PYGO_VER.tar.gz" -C "$T"
tar -xJf "$DL/gst-python-$GST_VER.tar.xz" -C "$T"
log "PyGObject $PYGO_VER"
meson setup "$T/b1" "$T/pygobject-$PYGO_VER" --prefix="$STAGE" --libdir=lib --buildtype=release \
    -Dpython="$PY" -Dpycairo=disabled -Dtests=false >"$T/b1.log" 2>&1 || { tail -30 "$T/b1.log"; exit 1; }
ninja -C "$T/b1" >>"$T/b1.log" 2>&1 && meson install -C "$T/b1" --quiet >/dev/null || { tail -30 "$T/b1.log"; exit 1; }
PYSTAGE="$(find "$STAGE" -type d -name site-packages | head -1)"
[ -d "$PYSTAGE/gi" ] || morir "PyGObject no quedó instalado"
log "gst-python $GST_VER"
meson setup "$T/b2" "$T/gst-python-$GST_VER" --prefix="$STAGE" --libdir=lib --buildtype=release \
    -Dpython-exe="$PY" -Dpygi-overrides-dir="$PYSTAGE/gi/overrides" -Dlibpython-dir="$ROOT/extern/python/lib" \
    -Dplugin=enabled -Dtests=disabled >"$T/b2.log" 2>&1 || { tail -30 "$T/b2.log"; exit 1; }
ninja -C "$T/b2" >>"$T/b2.log" 2>&1 && meson install -C "$T/b2" --quiet >/dev/null || { tail -30 "$T/b2.log"; exit 1; }

# Al Python de los plugins: gi/ (con los overrides de Gst) y pygtkcompat.
rm -rf "$SITE/gi" "$SITE/pygtkcompat"
cp -R "$PYSTAGE/gi" "$SITE/"
[ -d "$PYSTAGE/pygtkcompat" ] && cp -R "$PYSTAGE/pygtkcompat" "$SITE/"
rm -rf "$OUT" && mkdir -p "$OUT"
PLUG="$(find "$STAGE" -name 'libgstpython*.dylib' | head -1)"
[ -n "$PLUG" ] && cp "$PLUG" "$OUT/libgstpython.dylib"

# RPATH relativos: del framework y de libpython, desde donde quedan en el bundle.
REL_FW_DESDE_GI="@loader_path/../../../../../bin/64bit/GStreamer.framework/Versions/1.0/lib"
for so in $(find "$SITE/gi" -name '*.so'); do
    # De la ruta absoluta del framework de compilar a @rpath.
    for dep in $(otool -L "$so" | awk 'NR>1{print $1}' | grep "$FW/lib/" || true); do
        install_name_tool -change "$dep" "@rpath/lib/$(basename "$dep")" "$so"
    done
    d="$(dirname "$so")"
    prof="$(python3 -c "import os,sys; print(os.path.relpath('$SITE', '$d'))")"
    install_name_tool -add_rpath "@loader_path/$prof/../../../../bin/64bit/GStreamer.framework/Versions/1.0" "$so" 2>/dev/null || true
    install_name_tool -add_rpath "$FW" "$so" 2>/dev/null || true
    codesign -f -s - "$so" 2>/dev/null || true
done
if [ -f "$OUT/libgstpython.dylib" ]; then
    so="$OUT/libgstpython.dylib"
    for dep in $(otool -L "$so" | awk 'NR>1{print $1}' | grep -E "$FW/lib/|$ROOT/extern/python" || true); do
        case "$dep" in
        *libpython*) install_name_tool -change "$dep" "@rpath/$(basename "$dep")" "$so" ;;
        *) install_name_tool -change "$dep" "@rpath/lib/$(basename "$dep")" "$so" ;;
        esac
    done
    # En el bundle: gst-plugins/ → ../python/lib y ../bin/64bit/GStreamer.framework/Versions/1.0
    install_name_tool -add_rpath "@loader_path/../python/lib" "$so" 2>/dev/null || true
    install_name_tool -add_rpath "@loader_path/../bin/64bit/GStreamer.framework/Versions/1.0" "$so" 2>/dev/null || true
    codesign -f -s - "$so" 2>/dev/null || true
fi
echo "$PYGO_VER $GST_VER" >"$OUT/.version"
log "listo: gi en $SITE, plugin en $OUT"
