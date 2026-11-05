#!/usr/bin/env bash
# ============================================================================
# tools/deps/gst-plugins-rs-linux.sh — los plugins de gst-plugins-rs que usa el
# programa, para el GStreamer de Linux (extern/gstreamer-linux)
#
# El framework de macOS y el instalador de Windows traen gst-plugins-rs; el
# GStreamer de Linux se compila desde fuente (tools/deps/gstreamer-linux.sh) y
# no lo traía: sin intersink/intersrc el motor no puede armar el programa (sus
# pipelines se unen con ellos) y ~20 tests del motor se salteaban. Acá se
# compilan, con cargo, desde la misma versión que la del framework de macOS
# (la etiqueta gstreamer-1.28.7 del repositorio de gst-plugins-rs, 0.15.3) y con
# su Cargo.lock (--locked: cada crate con la versión y el hash de ahí):
#
#   inter          intersink, intersrc (el motor: unir sus pipelines)
#   audiofx        ebur128level, audioloudnorm, audiornnoise, hrtfrender, rsaudioecho
#   livesync       livesync (tapar microcortes)
#   fallbackswitch fallbackswitch, fallbacksrc
#   closedcaption  tttocea608, cea608overlay, ccdetect… (subtítulos 608/708)
#   videofx        colordetect, roundedcorners, videocompare (con dssim)
#   hsv            hsvfilter, hsvdetector
#   onvif          onvifmetadataoverlay y compañía
#   gopbuffer      gopbuffer
#
# (Los módulos de video del catálogo que vienen de gst-plugins-rs, más lo que el
# programa usa por nombre. Afuera: los que no usa nada: transporte, IA de
# terceros, gtk4paintablesink —GTK 4 no está en la base de Linux—.)
#
# Rust: el instalador oficial de 1.92.0 (la que pide gst-plugins-rs 0.15) con
# su sha256, en extern/.tools/rust-<versión>: es para compilar, no viaja.
# Los .so van a extern/gstreamer-linux/lib/gstreamer-1.0 con el mismo RPATH que
# los demás plugins; quedan anotados en extern/gstreamer-linux/.version-rs.
# Si gstreamer-linux.sh rehace extern/gstreamer-linux, la marca se va con él y
# esto vuelve a instalarlos.
#
# Uso: tools/deps/gst-plugins-rs-linux.sh [JOBS]   (idempotente)
# ============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
JOBS="${1:-$(nproc)}"
RS_VER=0.15.3
RS_COMMIT=e9229628528b19e9b42e52620be4e79a2e054363 # etiqueta gstreamer-1.28.7
RUST_VER=1.92.0
RUST_SHA256=d2ccef59dd9f7439f2c694948069f789a044dc1addcc0803613232af8f88ee0c
# Subir RECETA al cambiar la lista o las opciones: si no, «ya está» y no recompila.
RECETA=1
PAQUETES=(gst-plugin-inter gst-plugin-audiofx gst-plugin-livesync gst-plugin-fallbackswitch
          gst-plugin-closedcaption gst-plugin-videofx gst-plugin-hsv gst-plugin-onvif gst-plugin-gopbuffer)
FUNCIONES=(gst-plugin-videofx/dssim)
# Lo que tiene que quedar (el nombre de cada biblioteca de los paquetes de arriba).
LIBS=(gstrsinter gstrsaudiofx gstlivesync gstfallbackswitch gstrsclosedcaption gstrsvideofx gsthsv gstrsonvif gstgopbuffer)
URL=https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs.git
PREFIX="$ROOT/extern/gstreamer-linux"
DEPS="$ROOT/extern/terceros-linux"
SRC="$ROOT/extern/.build-gst-plugins-rs"
RUST="$ROOT/extern/.tools/rust-$RUST_VER"
MARCA="$PREFIX/.version-rs"
QUE="$RS_VER $RS_COMMIT rust$RUST_VER r$RECETA ${PAQUETES[*]} ${FUNCIONES[*]}"
log() { printf '\033[1;36m[gst-plugins-rs-linux]\033[0m %s\n' "$*"; }

[ -f "$PREFIX/lib/libgstreamer-1.0.so" ] || { log "ERROR: falta extern/gstreamer-linux (make deps-gstreamer-linux)"; exit 1; }
if [ -f "$MARCA" ] && [ "$(cat "$MARCA")" = "$QUE" ]; then
    log "ya está ($PREFIX/lib/gstreamer-1.0)"
    exit 0
fi

# --- Rust (el instalador oficial, con su sha256) -------------------------------
if [ ! -x "$RUST/bin/cargo" ]; then
    mkdir -p "$ROOT/extern/.downloads"
    T="rust-$RUST_VER-x86_64-unknown-linux-gnu"
    F="$ROOT/extern/.downloads/$T.tar.xz"
    if [ ! -f "$F" ] || [ "$(sha256sum "$F" | cut -d' ' -f1)" != "$RUST_SHA256" ]; then
        log "bajando Rust $RUST_VER"
        curl -fL --retry 3 -o "$F.part" "https://static.rust-lang.org/dist/$T.tar.xz"
        [ "$(sha256sum "$F.part" | cut -d' ' -f1)" = "$RUST_SHA256" ] || { log "ERROR: sha256 de $T.tar.xz"; exit 1; }
        mv "$F.part" "$F"
    fi
    D="$SRC/rust-instalador"
    rm -rf "$D" "$RUST" && mkdir -p "$D"
    tar -xJf "$F" -C "$D"
    # Solo el compilador, cargo y la biblioteca estándar (sin documentación: ~1 GB menos).
    "$D/$T/install.sh" --prefix="$RUST" --components=rustc,cargo,rust-std-x86_64-unknown-linux-gnu \
        --disable-ldconfig >/dev/null
    rm -rf "$D"
fi
export PATH="$RUST/bin:$PATH"
log "$(rustc --version)"

# --- La fuente, en el commit fijado -------------------------------------------
if [ ! -d "$SRC/src/.git" ]; then
    rm -rf "$SRC/src"
    git clone -q --filter=blob:none --no-checkout "$URL" "$SRC/src"
fi
git -C "$SRC/src" fetch -q origin "$RS_COMMIT" 2>/dev/null || git -C "$SRC/src" fetch -q origin "refs/tags/gstreamer-1.28.7"
git -C "$SRC/src" checkout -q -f "$RS_COMMIT"
[ "$(git -C "$SRC/src" rev-parse HEAD)" = "$RS_COMMIT" ] || { log "ERROR: el commit no coincide"; exit 1; }

# --- Compilar contra el GStreamer de extern/ ----------------------------------
# Los -sys de gstreamer-rs buscan con pkg-config: primero el nuestro (y Pango/Cairo de
# extern/terceros-linux, los mismos que usa el plugin pango); glib, la del sistema (la base).
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$DEPS/lib/pkgconfig:$DEPS/share/pkgconfig"
export CARGO_HOME="$SRC/cargo"
export CARGO_TARGET_DIR="$SRC/target"
export CARGO_INCREMENTAL=0
export RUSTFLAGS="-L $PREFIX/lib -L $DEPS/lib"
args=()
for p in "${PAQUETES[@]}"; do args+=(-p "$p"); done
log "compilando ${#PAQUETES[@]} plugins ($JOBS hilos; bajo Rosetta, un rato largo)"
(cd "$SRC/src" && cargo build --release --locked -j "$JOBS" "${args[@]}" --features "${FUNCIONES[*]}") \
    >"$SRC/compilar.log" 2>&1 || { tail -60 "$SRC/compilar.log"; log "ERROR: no compiló (registro: $SRC/compilar.log)"; exit 1; }

# --- Instalar con el RPATH de los demás plugins ---------------------------------
for l in "${LIBS[@]}"; do
    so="$CARGO_TARGET_DIR/release/lib$l.so"
    [ -f "$so" ] || { log "ERROR: no quedó lib$l.so"; exit 1; }
    install -m 755 "$so" "$PREFIX/lib/gstreamer-1.0/"
    patchelf --set-rpath '$ORIGIN/..:$ORIGIN/../../..' "$PREFIX/lib/gstreamer-1.0/lib$l.so"
    strip --strip-unneeded "$PREFIX/lib/gstreamer-1.0/lib$l.so"
done
# Que carguen de verdad (con lo de extern/, como en el bundle): gst-inspect sobre cada uno.
falla=0
for l in "${LIBS[@]}"; do
    if ! LD_LIBRARY_PATH="$PREFIX/lib:$DEPS/lib" GST_PLUGIN_SYSTEM_PATH="$PREFIX/lib/gstreamer-1.0" \
        GST_REGISTRY="$SRC/registro.bin" "$PREFIX/bin/gst-inspect-1.0" "$PREFIX/lib/gstreamer-1.0/lib$l.so" >/dev/null 2>"$SRC/inspect-$l.log"; then
        log "ERROR: lib$l.so no carga:"; cat "$SRC/inspect-$l.log"; falla=1
    fi
done
[ "$falla" = 0 ] || exit 1
LD_LIBRARY_PATH="$PREFIX/lib:$DEPS/lib" GST_PLUGIN_SYSTEM_PATH="$PREFIX/lib/gstreamer-1.0" GST_REGISTRY="$SRC/registro.bin" \
    "$PREFIX/bin/gst-inspect-1.0" intersink >/dev/null || { log "ERROR: no aparece intersink"; exit 1; }
rm -f "$SRC/registro.bin"

# Lo compilado ocupa varios GB y el disco de la máquina de compilar es justo: se va. Quedan los
# .crate bajados (cargo/registry/cache) para no volver a bajarlos.
rm -rf "$CARGO_TARGET_DIR" "$CARGO_HOME/registry/src"
echo "$QUE" >"$MARCA"
log "listo: ${#LIBS[@]} plugins en $PREFIX/lib/gstreamer-1.0"
