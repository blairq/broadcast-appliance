#!/usr/bin/env bash
# ============================================================================
# tools/deps/gst-python-win.sh — los bindings de GStreamer para el Python de
# los plugins (Windows), compilados desde Linux
#
# Los oficiales del .msi de GStreamer son para Python 3.13 y el de los plugins
# es el 3.12 (extern/python-win): una extensión de otra versión no carga. Se
# compilan acá con clang-cl (el mismo toolchain que el resto de Windows, ABI de
# MSVC), contra el GStreamer oficial (extern/gstreamer-win):
#   - PyGObject 3.50.2 con los parches que aplica GStreamer (el de
#     os.add_dll_directory: Python 3.8+ no busca las DLL por el PATH; en el
#     bundle la carpeta va en PYGI_DLL_DIRS, platform/GstEntorno)
#   - gst-python (la misma versión que el GStreamer de Windows): los overrides
#     de Gst y libgstpython (elementos de GStreamer escritos en Python)
#
# meson necesita CORRER el Python de destino para saber cómo es: corre bajo
# Wine (extern/toolchain/wine, el de los tests de Windows).
#
# Quedan:
#   gi/ (con overrides de Gst) -> extern/python-win/Lib/site-packages
#   gstpython.dll              -> extern/gst-python-win/ (el bundle lo lleva a gst-plugins/)
#
# Uso: tools/deps/gst-python-win.sh [dir de descargas]
# ============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
EXT="$ROOT/extern"
DL="${1:-$EXT/.downloads}"
PYW="$EXT/python-win"
GW="$EXT/gstreamer-win"
LLVM="$EXT/toolchain/llvm/bin"
SYS="$EXT/msvc"
WINE="$EXT/toolchain/wine/bin/wine"
OUT="$EXT/gst-python-win"
log() { printf '\033[1;36m[gst-python-win]\033[0m %s\n' "$*"; }
morir() { log "ERROR: $*"; exit 1; }

GST_VER="$(sed -n 's/^Version: *//p' "$GW/lib/pkgconfig/gstreamer-1.0.pc" | tr -d '\r')"
PYGO_VER=3.50.2
PYGO_SHA=ece6b860aab77cb649fdfc6e88d8a83765e7a62f7ffd39a628d6e2a0d397a7ff
GSTPY_SHA_1_26_11=11316ba769b56d26d8b1464c7198a9924caeae48134f7db59171faf1a73dc436
# Los parches de PyGObject que aplica GStreamer (fijados por commit de GStreamer).
PARCHES_GST=070125524a8422e29d3b69a372ed4f62fd343ffa
[ "$GST_VER" = 1.26.11 ] || morir "el GStreamer de Windows es $GST_VER; fijá el sha256 de gst-python-$GST_VER"
for f in "$PYW/python.exe" "$PYW/libs/python312.lib" "$PYW/include/Python.h" "$LLVM/clang-cl" "$SYS/VC" "$WINE"; do
    [ -e "$f" ] || morir "falta $f (make deps-python-win deps-win-toolchain test-win)"
done
if [ -f "$OUT/.version" ] && [ "$(cat "$OUT/.version")" = "$PYGO_VER $GST_VER" ] && [ -d "$PYW/Lib/site-packages/gi" ]; then
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
baja "https://origin.gstreamer.freedesktop.org/src/gst-python/gst-python-$GST_VER.tar.xz" "gst-python-$GST_VER.tar.xz" "$GSTPY_SHA_1_26_11"

T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
tar -xzf "$DL/pygobject-$PYGO_VER.tar.gz" -C "$T"
tar -xJf "$DL/gst-python-$GST_VER.tar.xz" -C "$T"
for p in 0001-gi-Call-os.add_dll_directory-on-Windows-when-possibl.patch 0002-Fix-syntax-error-for-missing.patch; do
    curl -fsL "https://raw.githubusercontent.com/GStreamer/gstreamer/$PARCHES_GST/subprojects/packagefiles/pygobject-$PYGO_VER/$p" -o "$T/$p"
    patch -d "$T/pygobject-$PYGO_VER" -p1 -s <"$T/$p"
done

# El Python de destino, bajo Wine (meson lo corre para conocerlo).
export WINEPREFIX="$T/wineprefix" WINEDEBUG=-all DISPLAY= WINEDLLOVERRIDES="mscoree,mshtml="
export BA_WINE="$WINE" BA_PYW="$PYW"
# Windows no distingue mayúsculas: el Python dice «Include» y la carpeta es «include».
[ -e "$PYW/Include" ] || ln -s include "$PYW/Include"
# meson (cruzado desde Linux) busca la biblioteca con el nombre de Unix.
[ -e "$PYW/libs/python3.12.lib" ] || ln -s python312.lib "$PYW/libs/python3.12.lib"
cp "$ROOT/tools/deps/python-wine.py" "$T/python-win"
chmod +x "$T/python-win"
"$WINE" wineboot -u >/dev/null 2>&1 || true

# pkg-config: el de GStreamer de Windows y un .pc para el Python de destino.
mkdir -p "$T/pc"
cat >"$T/pc/python-3.12.pc" <<EOF
Name: Python
Description: Python 3.12 (Windows, extern/python-win)
Version: 3.12
Libs: -L$PYW/libs -lpython312
Cflags: -I$PYW/include
EOF
cp "$T/pc/python-3.12.pc" "$T/pc/python-3.12-embed.pc"

cat >"$T/cruzado.ini" <<EOF
[binaries]
c = ['$LLVM/clang-cl', '--target=x86_64-pc-windows-msvc', '/winsysroot', '$SYS', '-fuse-ld=lld-link']
c_ld = '$LLVM/lld-link'
ar = '$LLVM/llvm-lib'
windres = '$LLVM/llvm-rc'
pkg-config = 'pkg-config'
python = '$T/python-win'

[built-in options]
c_link_args = ['/winsysroot:$SYS', '/machine:x64', '/LIBPATH:$PYW/libs']
b_vscrt = 'md'

[properties]
pkg_config_libdir = ['$T/stage/lib/pkgconfig', '$T/pc', '$GW/lib/pkgconfig']

[host_machine]
system = 'windows'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
EOF
VENV="$T/venv"
python3 -m venv "$VENV"
"$VENV/bin/pip" install -q --disable-pip-version-check "meson==1.9.1" "ninja==1.13.0"
export PATH="$VENV/bin:$LLVM:$PATH"

log "PyGObject $PYGO_VER"
meson setup "$T/b1" "$T/pygobject-$PYGO_VER" --cross-file "$T/cruzado.ini" --prefix="$T/stage" --libdir=lib \
    --buildtype=release -Dpython="$T/python-win" -Dpycairo=disabled -Dtests=false >"$T/b1.log" 2>&1 || { tail -40 "$T/b1.log"; cp "$T/b1/meson-logs/meson-log.txt" "$EXT/.gst-python-win-meson.log" 2>/dev/null; exit 1; }
ninja -C "$T/b1" >>"$T/b1.log" 2>&1 || { tail -40 "$T/b1.log"; exit 1; }
# pygobject-3.0.pc para gst-python (meson install pondría rutas de Windows).
mkdir -p "$T/stage/lib/pkgconfig"
cat >"$T/stage/lib/pkgconfig/pygobject-3.0.pc" <<EOF
Name: PyGObject
Description: Python bindings for GObject Introspection
Version: $PYGO_VER
Requires: gobject-2.0
Cflags: -I$T/pygobject-$PYGO_VER/gi
EOF
log "gst-python $GST_VER"
meson setup "$T/b2" "$T/gst-python-$GST_VER" --cross-file "$T/cruzado.ini" --prefix="$T/stage" --libdir=lib \
    --buildtype=release -Dpython-exe="$T/python-win" -Dplugin=enabled -Dtests=disabled \
    -Dpygi-overrides-dir="$T/stage/overrides" -Dlibpython-dir="$PYW" >"$T/b2.log" 2>&1 || { tail -40 "$T/b2.log"; exit 1; }
ninja -C "$T/b2" >>"$T/b2.log" 2>&1 || { tail -40 "$T/b2.log"; exit 1; }

# Se copia a mano (meson install usaría las rutas de Windows que da el Python).
SITE="$PYW/Lib/site-packages"
rm -rf "$SITE/gi" "$SITE/pygtkcompat"
cp -R "$T/pygobject-$PYGO_VER/gi" "$SITE/gi"
find "$SITE/gi" \( -name '*.c' -o -name '*.h' -o -name 'meson.build' -o -name '*.in' \) -delete
cp "$(find "$T/b1/gi" -maxdepth 1 -name '_gi*.pyd' | head -1)" "$SITE/gi/"
[ -d "$T/pygobject-$PYGO_VER/pygtkcompat" ] && cp -R "$T/pygobject-$PYGO_VER/pygtkcompat" "$SITE/"
cp "$T/gst-python-$GST_VER"/gi/overrides/*.py "$SITE/gi/overrides/"
find "$T/b2" -name '_gi_gst*.pyd' -exec cp {} "$SITE/gi/overrides/" \;
rm -rf "$OUT" && mkdir -p "$OUT"
find "$T/b2" -name 'gstpython*.dll' -exec cp {} "$OUT/" \;
ls "$SITE/gi"/_gi*.pyd "$SITE/gi/overrides"/_gi_gst*.pyd >/dev/null || morir "faltan las extensiones compiladas"
"$VENV/bin/python" -m compileall -q "$SITE/gi" >/dev/null 2>&1 || true
echo "$PYGO_VER $GST_VER" >"$OUT/.version"
log "listo: gi en $SITE, plugin en $OUT ($(ls "$OUT"))"
