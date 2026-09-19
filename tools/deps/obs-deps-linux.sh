#!/usr/bin/env bash
# ============================================================================
# tools/deps/obs-deps-linux.sh — Dependencias de libobs para Linux, DESDE FUENTE
#
# Proposito:
#   El bundle portable no puede depender de bibliotecas "de terceros" del
#   sistema (FFmpeg, x264, jansson, curl, ...): en otra maquina no estan, o
#   estan en otra version y la app no arranca. Este script compila cada una
#   desde un tarball FIJADO (URL + SHA-256, o commit git) hacia
#   `extern/obs-deps-linux/`, que es de donde `make deps-obs-linux` compila
#   libobs. Lo que queda afuera es solo la BASE del sistema (ver
#   tools/deps/linux-baseline.txt): glibc, drivers de video (GL/EGL/VA
#   drivers), servidor grafico (X11/xcb/Wayland), audio (ALSA/Pulse), udev,
#   fuentes (fontconfig/freetype) y lo que exige Qt oficial (glib, dbus, zlib).
#
#   Estatico (-fPIC, queda dentro de libobs o del plugin): jansson, libuuid,
#   speexdsp, libpci, libcurl (solo HTTP, sin TLS: las actualizaciones de
#   servicios estan apagadas), mbedTLS.
#   Compartido (se copia a bin/64bit): FFmpeg, x264, libva (cargador: los
#   drivers VA son del sistema), libv4l2/libv4lconvert.
#
#   Herramientas de build que no hay en el sistema (nasm, meson) tambien se
#   compilan/extraen desde fuente fijada, en `extern/.tools/`.
#
# Uso: tools/deps/obs-deps-linux.sh [JOBS]   (idempotente: centinelas por paquete)
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DL="$ROOT/extern/.downloads"
SRC="$ROOT/extern/.build-obs-deps"
PREFIX="$ROOT/extern/obs-deps-linux"
TOOLS="$ROOT/extern/.tools"
JOBS="${1:-$(nproc)}"

mkdir -p "$DL" "$SRC" "$PREFIX" "$TOOLS/bin"
export PATH="$TOOLS/bin:$PATH"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig"
export CFLAGS="-O2 -fPIC"
export CXXFLAGS="-O2 -fPIC"
# Las .so propias se encuentran entre si junto a ellas (bin/64bit del bundle).
export LDFLAGS="-Wl,-rpath,\$ORIGIN"

# nombre|version|url|sha256
PKGS=(
  "nasm|2.16.03|https://www.nasm.us/pub/nasm/releasebuilds/2.16.03/nasm-2.16.03.tar.xz|1412a1c760bbd05db026b6c0d1657affd6631cd0a63cddb6f73cc6d4aa616148"
  "meson|1.5.2|https://github.com/mesonbuild/meson/releases/download/1.5.2/meson-1.5.2.tar.gz|f955e09ab0d71ef180ae85df65991d58ed8430323de7d77a37e11c9ea630910b"
  "jansson|2.14|https://github.com/akheron/jansson/releases/download/v2.14/jansson-2.14.tar.gz|5798d010e41cf8d76b66236cfb2f2543c8d082181d16bc3085ab49538d4b9929"
  "util-linux|2.40.2|https://www.kernel.org/pub/linux/utils/util-linux/v2.40/util-linux-2.40.2.tar.xz|d78b37a66f5922d70edf3bdfb01a6b33d34ed3c3cafd6628203b2a2b67c8e8b3"
  "speexdsp|1.2.1|https://downloads.xiph.org/releases/speex/speexdsp-1.2.1.tar.gz|8c777343e4a6399569c72abc38a95b24db56882c83dbdb6c6424a5f4aeb54d3d"
  "pciutils|3.13.0|https://mj.ucw.cz/download/linux/pci/pciutils-3.13.0.tar.gz|a03c88a0aab709b1697ca1753766f443e78297f84c7bcd80a82c1422edd7cb42"
  "curl|8.10.1|https://curl.se/download/curl-8.10.1.tar.xz|73a4b0e99596a09fa5924a4fb7e4b995a85fda0d18a2c02ab9cf134bebce04ee"
  "libva|2.22.0|https://github.com/intel/libva/releases/download/2.22.0/libva-2.22.0.tar.bz2|e3da2250654c8d52b3f59f8cb3f3d8e7fb1a2ee64378dbc400fbc5663de7edb8"
  "v4l-utils|1.28.1|https://linuxtv.org/downloads/v4l-utils/v4l-utils-1.28.1.tar.xz|0fa075ce59b6618847af6ea191b6155565ccaa44de0504581ddfed795a328a82"
  "srt|1.5.4|https://github.com/Haivision/srt/archive/refs/tags/v1.5.4.tar.gz|d0a8b600fe1b4eaaf6277530e3cfc8f15b8ce4035f16af4a5eb5d4b123640cdd"
  "ffmpeg|7.1.1|https://ffmpeg.org/releases/ffmpeg-7.1.1.tar.xz|733984395e0dbbe5c046abda2dc49a5544e7e0e1e2366bba849222ae9e3a03b1"
  # Lo que piden los plugins LSP (CLAP) y que no es base del sistema: cairo
  # (con pixman y libpng) para su interfaz y libsndfile para leer audio.
  "pixman|0.44.2|https://cairographics.org/releases/pixman-0.44.2.tar.gz|6349061ce1a338ab6952b92194d1b0377472244208d47ff25bef86fc71973466"
  "libpng|1.6.44|https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.44.tar.gz|0ef5b633d0c65f780c4fced27ff832998e71478c13b45dfb6e94f23a82f64f7c"
  "cairo|1.18.2|https://cairographics.org/releases/cairo-1.18.2.tar.xz|a62b9bb42425e844cc3d6ddde043ff39dbabedd1542eba57a2eb79f85889d45a"
  "libsndfile|1.2.2|https://github.com/libsndfile/libsndfile/releases/download/1.2.2/libsndfile-1.2.2.tar.xz|3799ca9924d3125038880367bf1468e53a1b7e3686a934f098b7e1d286cdb80e"
)
# NDI sin el SDK de Vizrt: libNoIDea (VideoLAN, LGPL, ingenieria inversa para
# interoperabilidad) + libmicrodns (descubrimiento mDNS). Por commit fijado.
NOIDEA_URL="https://code.videolan.org/jbk/libndi.git"
NOIDEA_COMMIT="40165104efd9850a4c21b4ac8c05e287fedc41bb"
MICRODNS_URL="https://github.com/videolabs/libmicrodns.git"
MICRODNS_COMMIT="deb7ed7bf05dc26802a0ca1987049b31405b8930" # 0.2.0
X264_URL="https://code.videolan.org/videolan/x264.git"
X264_COMMIT="b35605ace3ddf7c1a5d67a2eb553f034aef41d55" # rama stable

log() { printf '[obs-deps] %s\n' "$*" >&2; }

# fetch <nombre> -> deja el arbol fuente en $SRC/<nombre> y lo imprime
fetch() {
  local name="$1" line ver url sha file
  for line in "${PKGS[@]}"; do
    IFS='|' read -r n ver url sha <<<"$line"
    [ "$n" = "$name" ] || continue
    file="$DL/$(basename "$url")"
    if [ ! -f "$file" ] || [ "$(sha256sum "$file" | cut -d' ' -f1)" != "$sha" ]; then
      log "$name $ver: descargando"
      rm -f "$file.part"
      # linuxtv corta conexiones largas: se reanuda hasta completar.
      local i
      for i in $(seq 1 60); do
        curl -fsSL --http1.1 --retry 3 -C - -o "$file.part" "$url" && break || true
      done
      mv "$file.part" "$file"
    fi
    echo "$sha  $file" | sha256sum -c --quiet - || { log "ERROR: hash de $name no coincide"; exit 1; }
    if [ ! -d "$SRC/$name" ]; then
      mkdir -p "$SRC/$name"
      tar xf "$file" -C "$SRC/$name" --strip-components=1
    fi
    echo "$SRC/$name"
    return 0
  done
  log "ERROR: paquete desconocido $name"; exit 1
}

done_mark() { [ -f "$PREFIX/.done-$1" ]; }
mark() { touch "$PREFIX/.done-$1"; }

# --- herramientas de build ----------------------------------------------------
if [ ! -x "$TOOLS/bin/nasm" ]; then
  d=$(fetch nasm); log "nasm"
  (cd "$d" && ./configure --prefix="$TOOLS" >/dev/null && make -j"$JOBS" >/dev/null && make install >/dev/null)
fi
if [ ! -x "$TOOLS/bin/meson" ]; then
  d=$(fetch meson); log "meson"
  printf '#!/bin/sh\nexec python3 "%s/meson.py" "$@"\n' "$d" >"$TOOLS/bin/meson"
  chmod +x "$TOOLS/bin/meson"
fi

# --- estaticas -----------------------------------------------------------------
if ! done_mark jansson; then
  d=$(fetch jansson); log "jansson (estatica)"
  cmake -S "$d" -B "$d/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DJANSSON_BUILD_SHARED_LIBS=OFF \
    -DJANSSON_BUILD_DOCS=OFF -DJANSSON_EXAMPLES=OFF -DJANSSON_WITHOUT_TESTS=ON >/dev/null
  cmake --build "$d/build" -j"$JOBS" >/dev/null && cmake --install "$d/build" >/dev/null
  mark jansson
fi
if ! done_mark libuuid; then
  d=$(fetch util-linux); log "libuuid (estatica)"
  (cd "$d" && ./configure --prefix="$PREFIX" --libdir="$PREFIX/lib" --disable-all-programs --enable-libuuid \
      --disable-shared --enable-static --with-pic --disable-nls --without-python --without-systemd \
      --without-udev --disable-bash-completion --disable-asciidoc >/dev/null \
    && make -j"$JOBS" libuuid.la >/dev/null && make install-uuidincHEADERS install-pkgconfigDATA install-usrlib_execLTLIBRARIES >/dev/null)
  mark libuuid
fi
if ! done_mark speexdsp; then
  d=$(fetch speexdsp); log "speexdsp (estatica)"
  (cd "$d" && ./configure --prefix="$PREFIX" --libdir="$PREFIX/lib" --disable-shared --enable-static \
      --with-pic --disable-examples >/dev/null && make -j"$JOBS" >/dev/null && make install >/dev/null)
  mark speexdsp
fi
if ! done_mark pciutils; then
  d=$(fetch pciutils); log "libpci (estatica)"
  (cd "$d" && make -j"$JOBS" PREFIX="$PREFIX" LIBDIR="$PREFIX/lib" ZLIB=no DNS=no LIBKMOD=no HWDB=no \
      SHARED=no OPT="-O2 -fPIC" lib/libpci.a >/dev/null \
    && make PREFIX="$PREFIX" LIBDIR="$PREFIX/lib" ZLIB=no DNS=no LIBKMOD=no HWDB=no SHARED=no install-lib >/dev/null)
  mark pciutils
fi
if ! done_mark curl; then
  d=$(fetch curl); log "libcurl (estatica, solo HTTP)"
  cmake -S "$d" -B "$d/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON \
    -DBUILD_CURL_EXE=OFF -DBUILD_TESTING=OFF -DBUILD_LIBCURL_DOCS=OFF -DBUILD_MISC_DOCS=OFF -DENABLE_CURL_MANUAL=OFF \
    -DHTTP_ONLY=ON -DCURL_ENABLE_SSL=OFF -DCURL_USE_LIBPSL=OFF -DCURL_USE_LIBSSH2=OFF -DCURL_USE_GSSAPI=OFF \
    -DUSE_NGHTTP2=OFF -DUSE_LIBIDN2=OFF -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF -DCURL_ZLIB=OFF -DENABLE_ARES=OFF \
    -DCURL_DISABLE_LDAP=ON >/dev/null
  cmake --build "$d/build" -j"$JOBS" >/dev/null && cmake --install "$d/build" >/dev/null
  mark curl
fi
if ! done_mark mbedtls; then
  log "mbedTLS (estatica, desde extern/mbedtls)"
  [ -f "$ROOT/extern/mbedtls/CMakeLists.txt" ] || make -C "$ROOT" deps-mbedtls >/dev/null
  cmake -S "$ROOT/extern/mbedtls" -B "$SRC/mbedtls-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF -DUSE_SHARED_MBEDTLS_LIBRARY=OFF -DUSE_STATIC_MBEDTLS_LIBRARY=ON >/dev/null
  cmake --build "$SRC/mbedtls-build" -j"$JOBS" >/dev/null && cmake --install "$SRC/mbedtls-build" >/dev/null
  mark mbedtls
fi

# --- compartidas ----------------------------------------------------------------
if ! done_mark nvcodec; then
  log "nv-codec-headers"
  make -C "$ROOT/extern/nv-codec-headers" PREFIX="$PREFIX" install >/dev/null
  mark nvcodec
fi
if ! done_mark x264; then
  log "x264 $X264_COMMIT"
  d="$SRC/x264"
  if [ ! -d "$d/.git" ]; then
    rm -rf "$d"; git init -q "$d"
    git -C "$d" fetch -q --depth 1 "$X264_URL" "$X264_COMMIT"
    git -C "$d" checkout -q FETCH_HEAD
  fi
  [ "$(git -C "$d" rev-parse HEAD)" = "$X264_COMMIT" ] || { log "ERROR: commit de x264 no coincide"; exit 1; }
  (cd "$d" && ./configure --prefix="$PREFIX" --libdir="$PREFIX/lib" --enable-shared --enable-pic --disable-cli \
      --extra-ldflags="$LDFLAGS" >/dev/null && make -j"$JOBS" >/dev/null && make install >/dev/null)
  mark x264
fi
if ! done_mark libva; then
  d=$(fetch libva); log "libva (cargador; drivers del sistema)"
  # Los drivers VA viven en rutas distintas segun la distro: se buscan en todas.
  meson setup "$d/build" "$d" --prefix="$PREFIX" --libdir=lib --buildtype=release \
    -Dwith_x11=no -Dwith_glx=no -Dwith_wayland=no -Ddisable_drm=false -Denable_docs=false \
    -Ddriverdir="/usr/lib/x86_64-linux-gnu/dri:/usr/lib64/dri:/usr/lib/dri:/usr/local/lib/dri" \
    -Dc_link_args="$LDFLAGS" >/dev/null
  ninja -C "$d/build" >/dev/null && ninja -C "$d/build" install >/dev/null
  mark libva
fi
if ! done_mark v4l; then
  d=$(fetch v4l-utils); log "libv4l2"
  meson setup "$d/build" "$d" --prefix="$PREFIX" --libdir=lib --buildtype=release \
    -Dv4l-utils=false -Dlibdvbv5=disabled -Djpeg=disabled -Dbpf=disabled -Dqv4l2=disabled -Dqvidcap=disabled \
    -Dgconv=disabled -Ddoxygen-doc=disabled -Dv4l-plugins=true -Dv4l-wrappers=false \
    -Dc_link_args="$LDFLAGS" -Dcpp_link_args="$LDFLAGS" >/dev/null
  ninja -C "$d/build" >/dev/null && ninja -C "$d/build" install >/dev/null
  mark v4l
fi
if ! done_mark srt; then
  d=$(fetch srt); log "libsrt (estatica, cifrado con mbedTLS)"
  cmake -S "$d" -B "$d/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DENABLE_SHARED=OFF -DENABLE_STATIC=ON \
    -DENABLE_APPS=OFF -DENABLE_UNITTESTS=OFF -DENABLE_TESTING=OFF -DUSE_ENCLIB=mbedtls \
    -DMBEDTLS_PREFIX="$PREFIX" -DMBEDTLS_INCLUDE_DIR="$PREFIX/include" \
    -DMBEDTLS_LIBRARIES="$PREFIX/lib/libmbedtls.a;$PREFIX/lib/libmbedx509.a;$PREFIX/lib/libmbedcrypto.a" >/dev/null
  cmake --build "$d/build" -j"$JOBS" >/dev/null && cmake --install "$d/build" >/dev/null
  # srt.pc pone mbedTLS ANTES de -lsrt (el enlazador estatico lo descarta):
  # orden correcto, con -l en vez de rutas.
  sed -i -e 's|^Libs:.*|Libs: -L${libdir} -lsrt|' \
         -e 's|^Libs.private:.*|Libs.private: -lmbedtls -lmbedx509 -lmbedcrypto -lstdc++ -lm -lpthread|' \
         -e '/^Requires.private/d' "$PREFIX/lib/pkgconfig/srt.pc"
  mark srt
fi
# v2: + SRT y TLS (https/rtmps/hls seguro) con mbedTLS (--enable-version3: Apache 2.0).
if ! done_mark ffmpeg2; then
  d=$(fetch ffmpeg); log "FFmpeg (sin bibliotecas externas salvo VAAPI/NVENC/zlib/SRT/mbedTLS)"
  (cd "$d" && { make distclean >/dev/null 2>&1 || true; } && ./configure --prefix="$PREFIX" --libdir="$PREFIX/lib" \
      --enable-shared --disable-static --enable-pic --enable-version3 \
      --disable-programs --disable-doc --disable-debug --disable-autodetect --disable-indevs --disable-outdevs \
      --enable-pthreads --enable-zlib --enable-vaapi --enable-libdrm --enable-ffnvcodec --enable-nvenc \
      --enable-nvdec --enable-cuvid --enable-mbedtls --enable-libsrt \
      --pkg-config-flags=--static --extra-ldflags="$LDFLAGS" >/dev/null \
    && make -j"$JOBS" >/dev/null && make install >/dev/null)
  mark ffmpeg2
fi

# --- lo que piden los plugins LSP (compartidas, van a bin/64bit) ----------------
if ! done_mark libsndfile; then
  d=$(fetch libsndfile); log "libsndfile (sin codecs externos: WAV/AIFF/...)"
  cmake -S "$d" -B "$d/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_INSTALL_LIBDIR=lib -DBUILD_SHARED_LIBS=ON -DENABLE_EXTERNAL_LIBS=OFF -DENABLE_MPEG=OFF \
    -DBUILD_PROGRAMS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DENABLE_CPACK=OFF -DINSTALL_MANPAGES=OFF \
    -DCMAKE_SHARED_LINKER_FLAGS="$LDFLAGS" >/dev/null
  cmake --build "$d/build" -j"$JOBS" >/dev/null && cmake --install "$d/build" >/dev/null
  mark libsndfile
fi
if ! done_mark pixman; then
  d=$(fetch pixman); log "pixman"
  meson setup "$d/build" "$d" --prefix="$PREFIX" --libdir=lib --buildtype=release -Dtests=disabled \
    -Ddemos=disabled -Dgtk=disabled -Dlibpng=disabled -Dc_link_args="$LDFLAGS" >/dev/null
  ninja -C "$d/build" >/dev/null && ninja -C "$d/build" install >/dev/null
  mark pixman
fi
if ! done_mark libpng; then
  d=$(fetch libpng); log "libpng"
  cmake -S "$d" -B "$d/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_INSTALL_LIBDIR=lib -DPNG_SHARED=ON -DPNG_STATIC=OFF -DPNG_TESTS=OFF -DPNG_TOOLS=OFF \
    -DPNG_FRAMEWORK=OFF -DCMAKE_SHARED_LINKER_FLAGS="$LDFLAGS" >/dev/null
  cmake --build "$d/build" -j"$JOBS" >/dev/null && cmake --install "$d/build" >/dev/null
  mark libpng
fi
if ! done_mark cairo; then
  d=$(fetch cairo); log "cairo (xlib/xcb + freetype/fontconfig del sistema)"
  meson setup "$d/build" "$d" --prefix="$PREFIX" --libdir=lib --buildtype=release \
    -Dxlib=enabled -Dxcb=enabled -Dfreetype=enabled -Dfontconfig=enabled -Dpng=enabled -Dzlib=enabled \
    -Dtests=disabled -Dglib=disabled -Dspectre=disabled -Dsymbol-lookup=disabled -Dgtk2-utils=disabled \
    -Dquartz=disabled -Dtee=disabled -Dgtk_doc=false -Dc_link_args="$LDFLAGS" >/dev/null
  ninja -C "$d/build" >/dev/null && ninja -C "$d/build" install >/dev/null
  mark cairo
fi

# git a un commit fijado (el hash ES la verificacion).
gitpin() { # <dir> <url> <commit>
  local d="$1"
  if [ ! -d "$d/.git" ]; then
    rm -rf "$d"; git init -q "$d"
    git -C "$d" fetch -q --depth 1 "$2" "$3"
    git -C "$d" checkout -q FETCH_HEAD
  fi
  [ "$(git -C "$d" rev-parse HEAD)" = "$3" ] || { log "ERROR: commit de $d no coincide"; exit 1; }
}
if ! done_mark microdns; then
  log "libmicrodns (estatica)"
  d="$SRC/libmicrodns"; gitpin "$d" "$MICRODNS_URL" "$MICRODNS_COMMIT"
  meson setup "$d/build" "$d" --prefix="$PREFIX" --libdir=lib --buildtype=release --default-library=static \
    -Db_staticpic=true -Dexamples=disabled -Dtests=disabled >/dev/null
  ninja -C "$d/build" >/dev/null && ninja -C "$d/build" install >/dev/null
  mark microdns
fi
# libNoIDea: solo el fuente fijado (se compila dentro de ba-app, ver src/app/CMakeLists.txt).
gitpin "$ROOT/extern/libnoidea" "$NOIDEA_URL" "$NOIDEA_COMMIT"

# Las .la de libtool arrastran rutas absolutas; no las usa nadie.
find "$PREFIX/lib" -name '*.la' -delete
log "listo en $PREFIX"
