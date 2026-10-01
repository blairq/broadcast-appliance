#!/usr/bin/env bash
# ============================================================================
# tools/deps/lsp-win.sh — plugins LSP (CLAP) para Windows, compilados desde fuente
#
# LSP no publica binarios de Windows (solo Linux/FreeBSD), pero su codigo
# soporta Windows x86_64 por completo. Se compila aca, desde Linux, con el
# llvm-mingw de extern/toolchain (el plugin es un .dll con ABI de C: lo carga
# el host CLAP de la app, que es MSVC, sin problema). En Windows LSP solo usa
# bibliotecas del sistema (Direct2D, DirectWrite, GDI, WinMM...).
#
# Todo fijado por hash: fuente de LSP (release oficial) y PHP estatico (lo
# usan los generadores de LSP al compilar).
# LSP une los objetos de cada modulo con `ld -r` (lld no lo hace para PE, y
# el ld de GNU rompe las tablas de excepciones al descartar COMDATs repetidos:
# el .dll resultante revienta al desenrollar una excepcion). En su lugar cada
# "union" es un archivo delgado (llvm-ar T, sin copiar nada) y el enlace final
# los toma enteros (--whole-archive): mismo resultado que `ld -r`.
# El build de LSP compila tambien herramientas para el HOST (Linux) que
# necesitan los headers de libsndfile y cairo (de extern/obs-deps-linux si
# estan; si no, los del sistema: libsndfile1-dev libcairo2-dev) y X11/GL.
#
# Resultado: extern/lsp/windows-x86_64/{lsp-plugins.clap, lsp-r3d-wgl-lib-*.dll}
# ============================================================================
set -euo pipefail
# macOS no trae sha256sum (shasum -a 256 da el mismo formato).
command -v sha256sum >/dev/null 2>&1 || sha256sum() { shasum -a 256 "$@"; }

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DL="$ROOT/extern/.downloads"
TOOLS="$ROOT/extern/.tools"
SRC="$ROOT/extern/.build-lsp"
OUT="$ROOT/extern/lsp/windows-x86_64"
LLVM="$ROOT/extern/toolchain/llvm/bin"
JOBS="${1:-$(nproc)}"
log() { echo "[lsp-win] $*" >&2; }

LSP_VER=1.2.35
LSP_SHA=2c95ec7bb219d561ea3db36051b6c732133bcd76426fb836b1dd850dc4b5bb6c
PHP_VER=8.4.23
PHP_SHA=1aeed5bc7967977ca5b1da7163acd91bf9ba3ac56037045d4e91ee2ff2712bb7

# La versión de LSP más la huella de nuestro módulo (si cambia, se recompila).
SELLO="$LSP_VER+$("$ROOT/tools/deps/lsp-modulo-propio.sh" --huella)"
if [ "$(cat "$OUT/.version" 2>/dev/null)" = "$SELLO" ] && [ -f "$OUT/lsp-plugins.clap" ]; then
    log "LSP $LSP_VER para Windows: ya presente"
    exit 0
fi
[ -x "$LLVM/x86_64-w64-mingw32-clang++" ] || { log "falta el toolchain: tools/deps/win-toolchain.sh"; exit 1; }
mkdir -p "$DL" "$TOOLS"

fetch() { # url archivo sha
    if [ -f "$2" ] && [ "$(sha256sum "$2" | cut -d' ' -f1)" = "$3" ]; then return 0; fi
    log "descargando $1"
    curl -fL --retry 3 -o "$2.part" "$1"
    [ "$(sha256sum "$2.part" | cut -d' ' -f1)" = "$3" ] || { log "ERROR: hash de $2"; rm -f "$2.part"; exit 1; }
    mv "$2.part" "$2"
}

# --- PHP estatico (herramienta de build de LSP) ---------------------------------
PHP="$TOOLS/php/php"
if ! [ -x "$PHP" ]; then
    f="$DL/php-$PHP_VER-cli-linux-x86_64.tar.gz"
    fetch "https://dl.static-php.dev/static-php-cli/common/php-$PHP_VER-cli-linux-x86_64.tar.gz" "$f" "$PHP_SHA"
    mkdir -p "$TOOLS/php" && tar xzf "$f" -C "$TOOLS/php"
fi

# --- Union de objetos (LD) y enlace final (CXX) ------------------------------
WRAP="$TOOLS/lsp-win-bin"
mkdir -p "$WRAP"
cat >"$WRAP/ld" <<EOS
#!/usr/bin/env bash
# ld -r [-m emul] [-T script] -o salida entradas... -> archivo delgado
set -e
out=; ins=()
while [ \$# -gt 0 ]; do
    case "\$1" in
        -o) out="\$2"; shift 2 ;;
        -m|-T) shift 2 ;;
        -*) shift ;;
        *) ins+=("\$1"); shift ;;
    esac
done
rm -f "\$out"
exec "$LLVM/llvm-ar" qcT "\$out" "\${ins[@]}"
EOS
cat >"$WRAP/cxx" <<EOS
#!/usr/bin/env bash
# Los archivos delgados de la union entran enteros, como un objeto de ld -r.
args=()
for a in "\$@"; do
    if [[ "\$a" != -* && "\$a" == *.o && -f "\$a" ]] && [ "\$(head -c 7 "\$a")" = '!<thin>' ]; then
        args+=(-Wl,--whole-archive "\$a" -Wl,--no-whole-archive)
    else
        args+=("\$a")
    fi
done
exec "$LLVM/x86_64-w64-mingw32-clang++" "\${args[@]}"
EOS
chmod +x "$WRAP/ld" "$WRAP/cxx"

# --- Headers GL en minusculas (LSP incluye <gl/gl.h>, MinGW trae GL/) ------------
COMPAT="$TOOLS/mingw-compat"
mkdir -p "$COMPAT/gl"
for h in gl.h glu.h glext.h wglext.h; do
    ln -sf "$ROOT/extern/toolchain/llvm/x86_64-w64-mingw32/include/GL/$h" "$COMPAT/gl/$h"
done

# --- Fuente de LSP -----------------------------------------------------------------
f="$DL/lsp-plugins-src-$LSP_VER.tar.gz"
fetch "https://github.com/lsp-plugins/lsp-plugins/releases/download/$LSP_VER/lsp-plugins-src-$LSP_VER.tar.gz" "$f" "$LSP_SHA"
rm -rf "$SRC" && mkdir -p "$SRC" && tar xzf "$f" -C "$SRC"
cd "$SRC/lsp-plugins"
# Nuestro módulo (Matrix Mixer), con las convenciones de LSP.
"$ROOT/tools/deps/lsp-modulo-propio.sh" "$SRC/lsp-plugins"

# Herramientas del HOST: libsndfile/cairo vendoreados si estan.
HOSTINC="-DUSE_LIBSNDFILE"
HOSTLIB="-lsndfile -lcairo -lX11 -lXrandr -lfreetype -lfontconfig -lGL -ldl -lpthread -lrt"
DEPS="$ROOT/extern/obs-deps-linux"
if [ -f "$DEPS/include/sndfile.h" ]; then
    HOSTINC="-I$DEPS/include $HOSTINC"
    HOSTLIB="-L$DEPS/lib -Wl,-rpath,$DEPS/lib $HOSTLIB"
fi

log "configurando LSP $LSP_VER (Windows x86_64, CLAP + interfaz)"
make config PLATFORM=Windows ARCHITECTURE=x86_64 PROCESSOR_ARCHITECTURE=x86_64 \
    FEATURES='clap ui crosscompile' \
    CC="$LLVM/x86_64-w64-mingw32-clang" CXX="$WRAP/cxx" AR="$LLVM/llvm-ar" LD="$WRAP/ld" \
    HOST_CC=gcc HOST_CXX=g++ PHP="$PHP" \
    HOST_CFLAGS="$HOSTINC" HOST_CXXFLAGS="$HOSTINC" CFLAGS="-I$COMPAT" CXXFLAGS="-I$COMPAT" >/dev/null

# El cross de LSP asume que el host es el mismo sistema que el destino: las
# herramientas del host (Linux) no llevan las bibliotecas de Windows ni su
# script de enlace.
NOWIN=()
for l in LIBD2D1 LIBDWRITE LIBGDI32 LIBMPR LIBMSACM LIBOLE LIBOPENGL32 LIBSHLWAPI LIBUUID LIBWINCODEC LIBWINMM; do
    NOWIN+=("HOST_${l}_LDFLAGS=")
done
log "compilando (unos minutos)"
make -j"$JOBS" "${NOWIN[@]}" HOST_LD=ld HOST_LDFLAGS="-r" HOST_EXE_FLAGS="$HOSTLIB" >"$SRC/build.log" 2>&1 ||
    { tail -30 "$SRC/build.log" >&2; log "ERROR: fallo el build de LSP (log: $SRC/build.log)"; exit 1; }

rm -rf "$OUT" && mkdir -p "$OUT"
cp .build/target/lsp-plugin-fw/lsp-plugins-clap.dll "$OUT/lsp-plugins.clap"
cp .build/target/lsp-r3d-wgl-lib/lsp-r3d-wgl-lib-*.dll "$OUT/"
mkdir -p "$ROOT/extern/lsp/licencias"
cp COPYING "$ROOT/extern/lsp/licencias/COPYING.txt"
cp COPYING.LESSER "$ROOT/extern/lsp/licencias/COPYING.LESSER.txt"
echo "$SELLO" >"$OUT/.version"
log "listo: $OUT"
