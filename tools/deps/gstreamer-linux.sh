#!/usr/bin/env bash
# ============================================================================
# tools/deps/gstreamer-linux.sh — GStreamer para Linux, DESDE FUENTE
#
# Para Linux no hay binarios oficiales: se compila el repositorio único de
# GStreamer (núcleo, base, good, bad, ugly, libav y el servidor RTSP) en la
# MISMA versión que el framework de macOS, con commit fijado, hacia
# extern/gstreamer-linux. El bundle lo lleva en bin/64bit/gstreamer (lib/,
# lib/gstreamer-1.0/ y libexec/ con gst-plugin-scanner): ahí lo busca
# ba::platform::gstRaiz.
#
# Qué usa del sistema: solo la base (tools/deps/linux-baseline.txt: glib, GL,
# X11/Wayland, ALSA/Pulse, udev, drm). FFmpeg (gst-libav), x264, SRT, libva,
# libpng y cairo salen de extern/obs-deps-linux (los mismos que usa libobs y
# que ya viajan en bin/64bit). ORC (acelera videoconvert y compañía) se
# compila como subproyecto fijado del propio GStreamer. Las demás
# dependencias opcionales que no estén se apagan solas (auto): lo que queda
# afuera lo dice el resumen de meson en el registro. Los códecs que la imagen
# de compilar tiene pero un Linux cualquiera no (ogg, vorbis, opus, FLAC, bz2,
# mpg123) se compilan como subproyectos fijados del
# propio GStreamer (--force-fallback-for) y viajan en gstreamer/lib.
#
# Python: la introspección (los .typelib y libgirepository) y los bindings
# (PyGObject y gst-python, con libgstpython para escribir elementos en Python),
# compilados con el Python 3.12 de Ubuntu 24.04: la misma versión que el de
# los plugins (extern/python), así que las extensiones le sirven. Quedan en
# lib/python3.12/site-packages y lib/girepository-1.0.
#
# RPATH: las bibliotecas se encuentran entre sí ($ORIGIN), los plugins suben a
# lib/ ($ORIGIN/..) y a bin/64bit ($ORIGIN/../../..) para FFmpeg y compañía.
#
# Uso: tools/deps/gstreamer-linux.sh [JOBS]   (idempotente)
# ============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
JOBS="${1:-$(nproc)}"
VER=1.28.7
COMMIT=070125524a8422e29d3b69a372ed4f62fd343ffa
URL=https://gitlab.freedesktop.org/gstreamer/gstreamer.git
SRC="$ROOT/extern/.build-gstreamer"
PREFIX="$ROOT/extern/gstreamer-linux"
DEPS="$ROOT/extern/obs-deps-linux"
log() { printf '\033[1;36m[gstreamer-linux]\033[0m %s\n' "$*"; }

if [ -f "$PREFIX/.version" ] && [ "$(cat "$PREFIX/.version")" = "$VER $COMMIT" ]; then
    log "ya está ($PREFIX)"
    exit 0
fi
[ -d "$DEPS/lib" ] || { log "ERROR: falta extern/obs-deps-linux (make deps-obs-linux)"; exit 1; }
export PATH="$ROOT/extern/.tools/bin:$PATH"
command -v meson >/dev/null || { log "ERROR: falta meson (lo deja tools/deps/obs-deps-linux.sh en extern/.tools)"; exit 1; }

if [ ! -d "$SRC/.git" ]; then
    rm -rf "$SRC"
    git clone -q --filter=blob:none --no-checkout "$URL" "$SRC"
fi
git -C "$SRC" fetch -q origin "$COMMIT" 2>/dev/null || git -C "$SRC" fetch -q origin "refs/tags/$VER"
git -C "$SRC" checkout -q -f "$COMMIT"
[ "$(git -C "$SRC" rev-parse HEAD)" = "$COMMIT" ] || { log "ERROR: el commit no coincide"; exit 1; }

# Lo de extern/obs-deps-linux primero; después, el sistema (la base). SRT está
# compilado estático (va dentro de libobs): su .pc deja en Libs.private lo que
# pide (mbedTLS, la de C++), y el plugin no lo enlazaba. Un .pc corregido, solo
# para esta compilación, lo pone en Libs.
PC="$SRC/pc"
mkdir -p "$PC"
for n in srt haisrt; do
    [ -f "$DEPS/lib/pkgconfig/$n.pc" ] || continue
    sed -e 's/^Libs: \(.*\)$/Libs: \1 -lmbedtls -lmbedx509 -lmbedcrypto -lstdc++ -lm -lpthread/' -e '/^Libs.private:/d' \
        "$DEPS/lib/pkgconfig/$n.pc" >"$PC/$n.pc"
done
export PKG_CONFIG_PATH="$PC:$DEPS/lib/pkgconfig:$DEPS/share/pkgconfig"
export CFLAGS="-O2" CXXFLAGS="-O2"
export LDFLAGS="-L$DEPS/lib"

B="$SRC/b"
rm -rf "$PREFIX"
log "configurando $VER ($COMMIT)"
# Si ya hay una carpeta de compilación, se reconfigura (solo se recompila lo que cambia).
# (Sin la caché de dependencias: si no, sigue usando lo que encontró antes.)
if [ -f "$B/build.ninja" ]; then RECONF=--reconfigure; meson configure --clearcache "$B" >/dev/null; else RECONF=; fi
meson setup $RECONF "$B" "$SRC" --prefix="$PREFIX" --libdir=lib --libexecdir=libexec --buildtype=release \
    --wrap-mode=default -Dauto_features=auto -Dpkg_config_path="$PKG_CONFIG_PATH" \
    --force-fallback-for=ogg,vorbis,opus,flac,bzip2,mpg123,gobject-introspection,pygobject \
    -Dbase=enabled -Dgood=enabled -Dbad=enabled -Dugly=enabled -Dlibav=enabled -Drtsp_server=enabled \
    -Dgpl=enabled -Dorc=enabled \
    -Dpython=enabled -Dintrospection=enabled -Ddevtools=disabled -Dges=disabled -Drs=disabled -Dgst-examples=disabled -Dsharp=disabled \
    -Dlibnice=disabled -Dgtk=disabled -Dqt5=disabled -Dqt6=disabled -Dtls=disabled -Dwebrtc=disabled \
    -Dtests=disabled -Dexamples=disabled -Ddoc=disabled -Dnls=disabled -Dbenchmarks=disabled \
    -Dgst-plugins-base:gl=enabled -Dgst-plugins-base:x11=enabled -Dgst-plugins-base:alsa=enabled \
    -Dgst-plugins-good:pulse=enabled -Dgst-plugins-good:v4l2=enabled \
    -Dgst-plugins-bad:srt=enabled -Dgst-plugins-bad:va=enabled \
    -Dgst-plugins-ugly:x264=enabled \
    2>&1 | tee "$SRC/configurar.log" | tail -60
log "compilando ($JOBS hilos)"
ninja -C "$B" -j"$JOBS" >"$SRC/compilar.log" 2>&1 || { tail -40 "$SRC/compilar.log"; exit 1; }
meson install -C "$B" --quiet >/dev/null

# Lo que no va en el bundle: cabeceras y .pc quedan (los usa el build), el
# resto de herramientas de desarrollo no.
rm -rf "$PREFIX/share/gtk-doc" "$PREFIX/share/man" "$PREFIX/lib/cmake"

# RPATH relativos (meson deja el del árbol de build o ninguno).
log "RPATH relativos"
find "$PREFIX/lib" -maxdepth 1 -name '*.so*' -type f -exec patchelf --set-rpath '$ORIGIN:$ORIGIN/../..' {} \;
find "$PREFIX/lib/gstreamer-1.0" -name '*.so' -type f -exec patchelf --set-rpath '$ORIGIN/..:$ORIGIN/../../..' {} \;
find "$PREFIX/libexec" -type f -perm -u+x -exec sh -c 'file "$1" | grep -q ELF && patchelf --set-rpath "\$ORIGIN/../../lib:\$ORIGIN/../../.." "$1"' _ {} \;
find "$PREFIX/bin" -type f -perm -u+x -exec sh -c 'file "$1" | grep -q ELF && patchelf --set-rpath "\$ORIGIN/../lib:\$ORIGIN/../.." "$1"' _ {} \;
strip --strip-unneeded "$PREFIX"/lib/*.so* "$PREFIX"/lib/gstreamer-1.0/*.so 2>/dev/null || true

echo "$VER $COMMIT" >"$PREFIX/.version"
log "listo: $PREFIX ($(du -sh "$PREFIX" | cut -f1), $(ls "$PREFIX/lib/gstreamer-1.0" | wc -l) plugins)"
