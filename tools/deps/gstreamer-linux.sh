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
# libpng y cairo salen de extern/obs-deps-linux (tools/deps/terceros-linux.sh;
# cmake/BaPortableBundle.cmake los lleva a bin/64bit). ORC (acelera videoconvert y compañía) se
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
# Subir RECETA al cambiar opciones: si no, «ya está» y no recompila.
RECETA=5
URL=https://gitlab.freedesktop.org/gstreamer/gstreamer.git
SRC="$ROOT/extern/.build-gstreamer"
PREFIX="$ROOT/extern/gstreamer-linux"
DEPS="$ROOT/extern/obs-deps-linux"
log() { printf '\033[1;36m[gstreamer-linux]\033[0m %s\n' "$*"; }

if [ -f "$PREFIX/.version" ] && [ "$(cat "$PREFIX/.version")" = "$VER $COMMIT r$RECETA" ]; then
    log "ya está ($PREFIX)"
    exit 0
fi
[ -d "$DEPS/lib" ] || { log "ERROR: falta extern/obs-deps-linux (make deps-terceros-linux)"; exit 1; }
export PATH="$ROOT/extern/.tools/bin:$PATH"
command -v meson >/dev/null || { log "ERROR: falta meson (lo deja tools/deps/terceros-linux.sh en extern/.tools)"; exit 1; }

if [ ! -d "$SRC/.git" ]; then
    rm -rf "$SRC"
    git clone -q --filter=blob:none --no-checkout "$URL" "$SRC"
fi
git -C "$SRC" fetch -q origin "$COMMIT" 2>/dev/null || git -C "$SRC" fetch -q origin "refs/tags/$VER"
git -C "$SRC" checkout -q -f "$COMMIT"
[ "$(git -C "$SRC" rev-parse HEAD)" = "$COMMIT" ] || { log "ERROR: el commit no coincide"; exit 1; }
# gobject-introspection: el subproyecto de GStreamer es la 1.84, que pide glib
# 2.84; la base del sistema es glib 2.80 (Ubuntu 24.04). Se fija la 1.80.1, la
# de glib 2.80 (también desde fuente, con su sha256).
cat >"$SRC/subprojects/gobject-introspection.wrap" <<'WRAP'
[wrap-file]
directory = gobject-introspection-1.80.1
source_url = https://download.gnome.org/sources/gobject-introspection/1.80/gobject-introspection-1.80.1.tar.xz
source_filename = gobject-introspection-1.80.1.tar.xz
source_hash = a1df7c424e15bda1ab639c00e9051b9adf5cea1a9e512f8a603b53cd199bc6d8

[provide]
dependency_names = gobject-introspection-1.0
program_names = g-ir-scanner, g-ir-compiler, g-ir-annotation-tool, g-ir-doc-tool, g-ir-inspect, g-ir-generate
WRAP

# bzip2 y mpg123: GStreamer las busca en el sistema (sin subproyecto). Desde
# fuente fijada a una carpeta aparte; sus .so se copian a lib/ al final.
LADO="$SRC/lado"
command -v sha256sum >/dev/null 2>&1 || sha256sum() { shasum -a 256 "$@"; }
baja() { # url archivo sha
    local f="$ROOT/extern/.downloads/$2"
    [ -f "$f" ] && [ "$(sha256sum "$f" | cut -d' ' -f1)" = "$3" ] && return 0
    curl -fL --retry 3 -o "$f.part" "$1"
    [ "$(sha256sum "$f.part" | cut -d' ' -f1)" = "$3" ] || { log "ERROR: sha256 de $2"; exit 1; }
    mv "$f.part" "$f"
}
if [ ! -f "$LADO/.listo-2" ]; then
    rm -rf "$LADO" && mkdir -p "$LADO/src" "$LADO/lib" "$LADO/include"
    baja https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz bzip2-1.0.8.tar.gz ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269
    # mpg123: sha256 del tarball oficial (mpg123.de), fijado al bajarlo.
    baja https://www.mpg123.de/download/mpg123-1.33.7.tar.bz2 mpg123-1.33.7.tar.bz2 31d0e35a4ca567ec9b5ebda6c3062bb4435d6d3eacd6ef0d95cadd7854dc03ee
    tar -xzf "$ROOT/extern/.downloads/bzip2-1.0.8.tar.gz" -C "$LADO/src"
    (cd "$LADO/src/bzip2-1.0.8" && make -s -f Makefile-libbz2_so CFLAGS="-O2 -fPIC -D_FILE_OFFSET_BITS=64" >/dev/null 2>&1 \
        && cp -a libbz2.so.1.0* "$LADO/lib/" && ln -sf libbz2.so.1.0 "$LADO/lib/libbz2.so" && cp bzlib.h "$LADO/include/")
    tar -xjf "$ROOT/extern/.downloads/mpg123-1.33.7.tar.bz2" -C "$LADO/src"
    (cd "$LADO/src/mpg123-1.33.7" && ./configure -q --prefix="$LADO" --libdir="$LADO/lib" --disable-static \
        --with-default-audio=dummy --with-audio=dummy >/dev/null && make -s -j"$JOBS" >/dev/null && make -s install >/dev/null)
    touch "$LADO/.listo-2"
fi

# Lo de extern/obs-deps-linux primero; después, el sistema (la base). SRT está
# compilado estático (va dentro de FFmpeg y del plugin): su .pc deja en Libs.private lo que
# pide (mbedTLS, la de C++), y el plugin no lo enlazaba. Un .pc corregido, solo
# para esta compilación, lo pone en Libs.
PC="$SRC/pc"
mkdir -p "$PC"
for n in srt haisrt; do
    [ -f "$DEPS/lib/pkgconfig/$n.pc" ] || continue
    sed -e 's/^Libs: \(.*\)$/Libs: \1 -lmbedtls -lmbedx509 -lmbedcrypto -lstdc++ -lm -lpthread/' -e '/^Libs.private:/d' \
        "$DEPS/lib/pkgconfig/$n.pc" >"$PC/$n.pc"
done
export PKG_CONFIG_PATH="$PC:$LADO/lib/pkgconfig:$DEPS/lib/pkgconfig:$DEPS/share/pkgconfig"
export CFLAGS="-O2 -I$LADO/include" CXXFLAGS="-O2"
export LDFLAGS="-L$LADO/lib -L$DEPS/lib"
export LIBRARY_PATH="$LADO/lib"

B="$SRC/b"
rm -rf "$PREFIX"
log "configurando $VER ($COMMIT)"
# Si ya hay una carpeta de compilación, se reconfigura (solo se recompila lo que cambia).
# (Sin la caché de dependencias: si no, sigue usando lo que encontró antes.)
if [ -f "$B/build.ninja" ]; then RECONF=--reconfigure; meson configure --clearcache "$B" >/dev/null; else RECONF=; fi
meson setup $RECONF "$B" "$SRC" --prefix="$PREFIX" --libdir=lib --libexecdir=libexec --buildtype=release \
    --wrap-mode=default -Dauto_features=auto -Dpkg_config_path="$PKG_CONFIG_PATH" \
    -Dpython.platlibdir=lib/python3.12/site-packages -Dpython.purelibdir=lib/python3.12/site-packages \
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

# Los .typelib de GLib, GObject, Gio y GModule: desde glib 2.80 los provee glib
# (no gobject-introspection), y la glib es la del sistema, que no los trae
# instalados: van los que generó la compilación.
for t in "$B"/subprojects/gobject-introspection-*/gir/{GLib,GObject,Gio,GModule,GioUnix,GLibUnix}-2.0.typelib; do
    [ -f "$t" ] && cp "$t" "$PREFIX/lib/girepository-1.0/"
done
cp -a "$LADO"/lib/libbz2.so.1.0* "$LADO"/lib/libmpg123.so* "$LADO"/lib/libout123.so* "$LADO"/lib/libsyn123.so* "$PREFIX/lib/" 2>/dev/null || true
# Lo que no va en el bundle: cabeceras y .pc quedan (los usa el build), el
# resto de herramientas de desarrollo no.
rm -rf "$PREFIX/share/gtk-doc" "$PREFIX/share/man" "$PREFIX/lib/cmake"

# RPATH relativos (meson deja el del árbol de build o ninguno).
log "RPATH relativos"
find "$PREFIX/lib" -maxdepth 1 -name '*.so*' -type f -exec patchelf --set-rpath '$ORIGIN:$ORIGIN/../..' {} \;
find "$PREFIX/lib/gstreamer-1.0" -name '*.so' -type f -exec patchelf --set-rpath '$ORIGIN/..:$ORIGIN/../../..' {} \;
find "$PREFIX/libexec" -type f -perm -u+x -exec sh -c 'file "$1" | grep -q ELF && patchelf --set-rpath "\$ORIGIN/../../lib:\$ORIGIN/../../.." "$1"' _ {} \;
find "$PREFIX/bin" -type f -perm -u+x -exec sh -c 'file "$1" | grep -q ELF && patchelf --set-rpath "\$ORIGIN/../lib:\$ORIGIN/../.." "$1"' _ {} \;
# libgstpython inicializa el Python del bundle (python/lib/libpython3.12.so):
# desde bin/64bit/gstreamer/lib/gstreamer-1.0 son cinco niveles hasta la raíz.
for f in "$PREFIX"/lib/gstreamer-1.0/libgstpython*.so; do
    [ -f "$f" ] && patchelf --set-rpath '$ORIGIN/..:$ORIGIN/../../..:$ORIGIN/../../../../../python/lib' "$f"
done
# Las extensiones de gi (_gi*.so) buscan libgirepository y GStreamer: van a
# python/lib/python3.12/site-packages/gi (y gi/overrides, un nivel más) en el bundle.
find "$PREFIX/lib/python3.12" -name '*.so' -type f -exec sh -c 'patchelf --set-rpath "\$ORIGIN/../../../../../bin/64bit/gstreamer/lib:\$ORIGIN/../../../../../bin/64bit:\$ORIGIN/../../../../../../bin/64bit/gstreamer/lib:\$ORIGIN/../../../../../../bin/64bit" "$1"' _ {} \; 2>/dev/null || true
strip --strip-unneeded "$PREFIX"/lib/*.so* "$PREFIX"/lib/gstreamer-1.0/*.so 2>/dev/null || true

echo "$VER $COMMIT r$RECETA" >"$PREFIX/.version"
log "listo: $PREFIX ($(du -sh "$PREFIX" | cut -f1), $(ls "$PREFIX/lib/gstreamer-1.0" | wc -l) plugins)"
