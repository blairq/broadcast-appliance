#!/usr/bin/env bash
# ============================================================================
# tools/deps/lsp-macos.sh — modulos LSP (CLAP) para macOS arm64, desde fuente
#
# Se corre EN la Mac. LSP no publica binarios de macOS pero su codigo lo
# soporta (interfaz Cocoa). En macOS pide cairo, fontconfig y freetype: se
# compilan aca desde fuente fijada como bibliotecas ESTATICAS, asi quedan
# adentro del .clap y el plugin no depende de nada instalado (Homebrew incluido).
# iconv, CoreFoundation, AudioToolbox y Cocoa son de macOS.
#
# Herramientas de build (no viajan): Xcode CLT, cmake, meson, ninja, pkg-config
# y GNU make 4 (gmake)
#   brew install cmake meson ninja pkg-config make
#
# Resultado: extern/lsp/macos-arm64/lsp-plugins.clap (+ licencias)
# ============================================================================
set -euo pipefail
command -v sha256sum >/dev/null 2>&1 || sha256sum() { shasum -a 256 "$@"; }

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DL="$ROOT/extern/.downloads"
TOOLS="$ROOT/extern/.tools"
SRC="$ROOT/extern/.build-lsp-macos"
DEPS="$ROOT/extern/.lsp-deps-macos"
OUT="$ROOT/extern/lsp/macos-arm64"
JOBS="${1:-$(sysctl -n hw.ncpu)}"
log() { echo "[lsp-macos] $*" >&2; }

LSP_VER=1.2.35
PKGS=(
    "lsp|$LSP_VER|https://github.com/lsp-plugins/lsp-plugins/releases/download/$LSP_VER/lsp-plugins-src-$LSP_VER.tar.gz|2c95ec7bb219d561ea3db36051b6c732133bcd76426fb836b1dd850dc4b5bb6c"
    "php|8.4.23|https://dl.static-php.dev/static-php-cli/common/php-8.4.23-cli-macos-aarch64.tar.gz|bba286e442796dbd420d778a016e2817b31d5036d11b3ba316d19a60de912cdc"
    "freetype|2.13.3|https://download.savannah.gnu.org/releases/freetype/freetype-2.13.3.tar.xz|0550350666d427c74daeb85d5ac7bb353acba5f76956395995311a9c6f063289"
    "fontconfig|2.15.0|https://gitlab.freedesktop.org/fontconfig/fontconfig/-/archive/2.15.0/fontconfig-2.15.0.tar.gz|cdebb4b805d33e9bdefcc0ef9743db638d2acb21139bbe1a6a85878d4c3e8c9e"
    "pixman|0.44.2|https://cairographics.org/releases/pixman-0.44.2.tar.gz|6349061ce1a338ab6952b92194d1b0377472244208d47ff25bef86fc71973466"
    "libpng|1.6.44|https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.44.tar.gz|0ef5b633d0c65f780c4fced27ff832998e71478c13b45dfb6e94f23a82f64f7c"
    "cairo|1.18.2|https://cairographics.org/releases/cairo-1.18.2.tar.xz|a62b9bb42425e844cc3d6ddde043ff39dbabedd1542eba57a2eb79f85889d45a"
)

[ "$(uname -s)" = Darwin ] && [ "$(uname -m)" = arm64 ] || { log "esto se corre en una Mac Apple Silicon"; exit 1; }
if [ "$(cat "$OUT/.version" 2>/dev/null)" = "$LSP_VER" ] && [ -e "$OUT/lsp-plugins.clap" ]; then
    log "LSP $LSP_VER para macOS: ya presente"
    exit 0
fi
for t in cmake meson ninja pkg-config gmake; do
    command -v "$t" >/dev/null || { log "falta $t (herramienta de build): brew install cmake meson ninja pkg-config make"; exit 1; }
done

fetch() { # <nombre> -> imprime la ruta del archivo verificado
    local line n ver url sha file
    for line in "${PKGS[@]}"; do
        IFS='|' read -r n ver url sha <<<"$line"
        [ "$n" = "$1" ] || continue
        file="$DL/$(basename "$url")"
        [ "$n" = libpng ] && file="$DL/libpng-$ver.tar.gz"
        if ! [ -f "$file" ] || [ "$(sha256sum "$file" | cut -d' ' -f1)" != "$sha" ]; then
            log "$n $ver: descargando"
            curl -fsSL --retry 3 -o "$file.part" "$url"
            [ "$(sha256sum "$file.part" | cut -d' ' -f1)" = "$sha" ] || { log "ERROR: hash de $n"; exit 1; }
            mv "$file.part" "$file"
        fi
        echo "$file"
        return 0
    done
    log "ERROR: paquete desconocido $1"
    exit 1
}
unpack() { # <nombre> -> directorio con la fuente
    local f d
    f="$(fetch "$1")"
    d="$SRC/$1"
    rm -rf "$d" && mkdir -p "$d"
    tar xf "$f" -C "$d" --strip-components=1
    echo "$d"
}

mkdir -p "$DL" "$SRC" "$DEPS/lib/pkgconfig" "$TOOLS"
export PKG_CONFIG_PATH="$DEPS/lib/pkgconfig"
export PKG_CONFIG_LIBDIR="$DEPS/lib/pkgconfig" # nada del sistema/Homebrew
export MACOSX_DEPLOYMENT_TARGET=13.0
export CFLAGS="-arch arm64 -mmacosx-version-min=13.0" CXXFLAGS="-arch arm64 -mmacosx-version-min=13.0"
MESON=(--prefix="$DEPS" --libdir=lib --buildtype=release --default-library=static -Db_staticpic=true
    --wrap-mode=nofallback)

# expat y zlib: los de macOS (SDK). Se describen con .pc propios.
SDK="$(xcrun --show-sdk-path)"
cat >"$DEPS/lib/pkgconfig/expat.pc" <<EOF
Name: expat
Description: expat del SDK de macOS
Version: 2.5.0
Libs: -lexpat
Cflags: -I$SDK/usr/include
EOF
cat >"$DEPS/lib/pkgconfig/zlib.pc" <<EOF
Name: zlib
Description: zlib del SDK de macOS
Version: 1.2.12
Libs: -lz
Cflags: -I$SDK/usr/include
EOF

if ! [ -f "$DEPS/lib/libfreetype.a" ]; then
    d="$(unpack freetype)"; log "freetype (estatica)"
    meson setup "$d/b" "$d" "${MESON[@]}" -Dbrotli=disabled -Dbzip2=disabled -Dharfbuzz=disabled -Dpng=disabled \
        -Dzlib=enabled >/dev/null && ninja -C "$d/b" install >/dev/null
fi
if ! [ -f "$DEPS/lib/libfontconfig.a" ]; then
    d="$(unpack fontconfig)"; log "fontconfig (estatica)"
    meson setup "$d/b" "$d" "${MESON[@]}" -Ddoc=disabled -Dtests=disabled -Dtools=disabled \
        -Dcache-build=disabled -Dnls=disabled >/dev/null && ninja -C "$d/b" install >/dev/null
fi
if ! [ -f "$DEPS/lib/libpixman-1.a" ]; then
    d="$(unpack pixman)"; log "pixman (estatica)"
    meson setup "$d/b" "$d" "${MESON[@]}" -Dtests=disabled -Ddemos=disabled -Dgtk=disabled -Dlibpng=disabled \
        >/dev/null && ninja -C "$d/b" install >/dev/null
fi
if ! [ -f "$DEPS/lib/libpng16.a" ]; then
    d="$(unpack libpng)"; log "libpng (estatica)"
    cmake -S "$d" -B "$d/b" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$DEPS" \
        -DCMAKE_OSX_ARCHITECTURES=arm64 -DPNG_SHARED=OFF -DPNG_TESTS=OFF -DPNG_TOOLS=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON >/dev/null && cmake --build "$d/b" --target install >/dev/null
fi
if ! [ -f "$DEPS/lib/libcairo.a" ]; then
    d="$(unpack cairo)"; log "cairo (estatica, Quartz + freetype/fontconfig)"
    meson setup "$d/b" "$d" "${MESON[@]}" -Dquartz=enabled -Dxlib=disabled -Dxcb=disabled -Dfreetype=enabled \
        -Dfontconfig=enabled -Dpng=enabled -Dzlib=enabled -Dtests=disabled -Dglib=disabled -Dspectre=disabled \
        -Dsymbol-lookup=disabled -Dgtk2-utils=disabled -Dtee=disabled -Dgtk_doc=false >/dev/null &&
        ninja -C "$d/b" install >/dev/null
fi

# PHP estatico (lo usan los generadores de LSP).
PHP="$TOOLS/php-macos/php"
if ! [ -x "$PHP" ]; then
    f="$(fetch php)"
    mkdir -p "$TOOLS/php-macos" && tar xzf "$f" -C "$TOOLS/php-macos"
    xattr -d com.apple.quarantine "$PHP" 2>/dev/null || true
fi

# --- LSP ---------------------------------------------------------------------------
d="$(unpack lsp)"
cd "$d"
log "configurando LSP $LSP_VER (macOS arm64, CLAP + interfaz)"
# El build de LSP pide GNU make 4 (el de macOS es 3.81): gmake de Homebrew.
gmake config FEATURES='clap ui' PHP="$PHP" PKG_CONFIG="pkg-config --static" \
    CFLAGS="$CFLAGS" CXXFLAGS="$CXXFLAGS" >"$SRC/lsp-config.log" 2>&1 ||
    { tail -30 "$SRC/lsp-config.log" >&2; log "ERROR al configurar LSP"; exit 1; }
log "compilando (unos minutos)"
gmake -j"$JOBS" >"$SRC/lsp-build.log" 2>&1 ||
    { grep -E "error|Error" "$SRC/lsp-build.log" | tail -30 >&2; log "ERROR al compilar LSP (log: $SRC/lsp-build.log)"; exit 1; }

# El CLAP: un bundle .clap (carpeta) o un binario Mach-O; se deja con el
# nombre que busca la app (plugins/lsp/lsp-plugins.clap).
rm -rf "$OUT" && mkdir -p "$OUT"
art="$(find .build/target/lsp-plugin-fw -maxdepth 1 -name '*.clap' | head -1)"
[ -n "$art" ] || art="$(find .build/target/lsp-plugin-fw -maxdepth 1 -name '*clap*.dylib' | head -1)"
[ -n "$art" ] || { log "ERROR: no aparecio el artefacto CLAP en .build/target/lsp-plugin-fw"; exit 1; }
cp -R "$art" "$OUT/lsp-plugins.clap"
# Bibliotecas propias de LSP que acompanan al plugin (motor 3D de la interfaz).
find .build/target -maxdepth 2 -name 'lsp-r3d-*.dylib' -exec cp {} "$OUT/" \;
# Solo frameworks y /usr/lib: si algo apunta a Homebrew, el plugin no es portable.
bin="$OUT/lsp-plugins.clap"
[ -d "$bin" ] && bin="$(find "$bin/Contents/MacOS" -type f | head -1)"
# El install name trae la ruta de la carpeta de build: se lo cambia por uno
# relativo (el plugin se abre con dlopen por ruta, nadie lo enlaza por nombre).
install_name_tool -id "@rpath/$(basename "$bin")" "$bin"
# otool -L repite el install name propio como primera linea: se saltea.
self="$(otool -D "$bin" | tail -n +2)"
if otool -L "$bin" | tail -n +2 | awk '{print $1}' | grep -vxF "$self" | grep -vE '^(/usr/lib/|/System/|@)'; then
    log "ERROR: el plugin depende de bibliotecas de fuera del sistema (arriba)"
    exit 1
fi
codesign -f -s - "$OUT/lsp-plugins.clap" 2>/dev/null || true
find "$OUT" -name '*.dylib' -exec codesign -f -s - {} \; 2>/dev/null || true
mkdir -p "$ROOT/extern/lsp/licencias"
cp COPYING "$ROOT/extern/lsp/licencias/COPYING.txt"
cp COPYING.LESSER "$ROOT/extern/lsp/licencias/COPYING.LESSER.txt"
echo "$LSP_VER" >"$OUT/.version"
log "listo: $OUT"
