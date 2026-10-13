#!/usr/bin/env bash
# ============================================================================
# tools/deps/lspdsp.sh — ba-lspdsp: el DSP de LSP detrás de una API en C
#
# Compila, desde la fuente oficial de LSP 1.2.35 (verificada por hash), SOLO las
# bibliotecas que hacen falta (lsp-common-lib, lsp-lltl-lib, lsp-runtime-lib,
# lsp-dsp-lib, lsp-dsp-units: cada Makefile de módulo arma ese módulo, unos
# segundos) y les suma src/lspdsp/ba_lspdsp.cpp, con el MISMO compilador y las
# mismas opciones que LSP, en una biblioteca compartida que viaja en el bundle
# (LGPL: aparte y reemplazable). La app la carga en tiempo de ejecución.
# No depende del build de los plugins (en Linux usamos el CLAP oficial).
# Ver docs/planes/PLAN-MEZCLADOR.md (L0).
#
# Uso: tools/deps/lspdsp.sh <plataforma> [jobs]
#   plataforma: macos-arm64 (en la Mac) | linux-x86_64 | windows-x86_64 (en el
#   contenedor de build; Windows con el toolchain de tools/deps/win-toolchain.sh)
# Resultado: extern/lsp/<plataforma>/dsp/{libba-lspdsp.dylib|libba-lspdsp.so|ba-lspdsp.dll, ba_lspdsp.h}
# ============================================================================
set -euo pipefail
command -v sha256sum >/dev/null 2>&1 || sha256sum() { shasum -a 256 "$@"; }
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PLAT="${1:?uso: $0 <macos-arm64|linux-x86_64|windows-x86_64> [jobs]}"
JOBS="${2:-4}"
log() { echo "[lspdsp] $*" >&2; }
LSP_VER=1.2.35
LSP_SHA=2c95ec7bb219d561ea3db36051b6c732133bcd76426fb836b1dd850dc4b5bb6c
DL="$ROOT/extern/.downloads"
SRC="$ROOT/extern/.build-lspdsp-$PLAT"
OUT="$ROOT/extern/lsp/$PLAT/dsp"
MODS=(lsp-common-lib lsp-lltl-lib lsp-runtime-lib lsp-dsp-lib lsp-dsp-units)

# Si no cambió nada (LSP, el envoltorio, este script), no se rehace.
SELLO="$LSP_VER+$(cat "$ROOT/src/lspdsp/ba_lspdsp.cpp" "$ROOT/src/lspdsp/ba_lspdsp.h" "$0" | sha256sum | cut -c1-16)"
case "$PLAT" in
    macos-arm64) LIB=libba-lspdsp.dylib ;;
    linux-x86_64) LIB=libba-lspdsp.so ;;
    windows-x86_64) LIB=ba-lspdsp.dll ;;
    *) log "plataforma desconocida: $PLAT"; exit 1 ;;
esac
if [ "$(cat "$OUT/.version" 2>/dev/null)" = "$SELLO" ] && [ -f "$OUT/$LIB" ]; then
    log "ba-lspdsp ($PLAT): ya presente"
    exit 0
fi

# --- La fuente ----------------------------------------------------------------
mkdir -p "$DL"
f="$DL/lsp-plugins-src-$LSP_VER.tar.gz"
if ! [ -f "$f" ] || [ "$(sha256sum "$f" | cut -d' ' -f1)" != "$LSP_SHA" ]; then
    log "descargando la fuente de LSP $LSP_VER"
    curl -fL --retry 3 -o "$f.part" "https://github.com/lsp-plugins/lsp-plugins/releases/download/$LSP_VER/lsp-plugins-src-$LSP_VER.tar.gz"
    [ "$(sha256sum "$f.part" | cut -d' ' -f1)" = "$LSP_SHA" ] || { log "ERROR: hash de la fuente de LSP"; rm -f "$f.part"; exit 1; }
    mv "$f.part" "$f"
fi
rm -rf "$SRC" && mkdir -p "$SRC" && tar xzf "$f" -C "$SRC"
cd "$SRC/lsp-plugins"

# --- Configurar LSP con el compilador de la plataforma ------------------------
FLAGS=(-std=c++17 -O2 -fvisibility=hidden -fno-exceptions -fno-rtti -fdata-sections -ffunction-sections)
case "$PLAT" in
    macos-arm64)
        MAKE=gmake
        command -v gmake >/dev/null || { log "falta GNU make 4 (brew install make)"; exit 1; }
        export MACOSX_DEPLOYMENT_TARGET=13.0
        CXX=clang++
        ARCHF="-arch arm64 -mmacosx-version-min=13.0"
        "$MAKE" config FEATURES='clap' CFLAGS="$ARCHF" CXXFLAGS="$ARCHF" >"$SRC/config.log" 2>&1
        FLAGS+=($ARCHF -fPIC)
        LINK=(-dynamiclib $ARCHF -install_name @rpath/$LIB -framework CoreFoundation -Wl,-dead_strip)
        ;;
    linux-x86_64)
        MAKE=make
        CXX="${CXX:-g++}"
        "$MAKE" config FEATURES='clap' CC="${CC:-gcc}" CXX="$CXX" >"$SRC/config.log" 2>&1
        FLAGS+=(-fPIC -march=x86-64)
        LINK=(-shared -Wl,-soname,$LIB -Wl,--gc-sections -lpthread -ldl)
        ;;
    windows-x86_64)
        MAKE=make
        LLVM="$ROOT/extern/toolchain/llvm/bin"
        [ -x "$LLVM/x86_64-w64-mingw32-clang++" ] || { log "falta el toolchain: tools/deps/win-toolchain.sh"; exit 1; }
        # Los mismos envoltorios que tools/deps/lsp-win.sh: «ld -r» de LSP como un
        # archivo fino de llvm-ar, y el enlace con --whole-archive de esos archivos.
        WRAP="$SRC/bin"
        mkdir -p "$WRAP"
        cat >"$WRAP/ld" <<EOS
#!/usr/bin/env bash
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
        CXX="$WRAP/cxx"
        "$MAKE" config PLATFORM=Windows ARCHITECTURE=x86_64 PROCESSOR_ARCHITECTURE=x86_64 \
            FEATURES='clap crosscompile' CC="$LLVM/x86_64-w64-mingw32-clang" CXX="$CXX" AR="$LLVM/llvm-ar" \
            LD="$WRAP/ld" HOST_CC=gcc HOST_CXX=g++ >"$SRC/config.log" 2>&1
        # Todo estático (libc++ de MinGW incluida): la DLL no pide nada más que Windows.
        LINK=(-shared -static -Wl,--gc-sections -lshlwapi -lole32 -luuid)
        ;;
esac
grep -q "Configured OK" "$SRC/config.log" || { tail -20 "$SRC/config.log" >&2; log "ERROR al configurar LSP"; exit 1; }

# --- Solo las bibliotecas -------------------------------------------------------
log "compilando las bibliotecas de DSP de LSP ($PLAT)"
OBJS=()
INC=()
for m in "${MODS[@]}"; do
    "$MAKE" -C "modules/$m/src" compile CONFIG="$PWD/.config.mk" -j"$JOBS" >>"$SRC/build.log" 2>&1 ||
        { tail -30 "$SRC/build.log" >&2; log "ERROR al compilar $m"; exit 1; }
    OBJS+=("$PWD/.build/target/$m/$m.o")
    INC+=("-I$PWD/modules/$m/include")
done

# --- ba-lspdsp ------------------------------------------------------------------
rm -rf "$OUT" && mkdir -p "$OUT"
log "compilando ba_lspdsp.cpp y enlazando $LIB"
"$CXX" "${FLAGS[@]}" "${INC[@]}" -c "$ROOT/src/lspdsp/ba_lspdsp.cpp" -o "$SRC/ba_lspdsp.o"
"$CXX" -o "$OUT/$LIB" "$SRC/ba_lspdsp.o" "${OBJS[@]}" "${LINK[@]}"
[ "$PLAT" = macos-arm64 ] && codesign -f -s - "$OUT/$LIB" 2>/dev/null || true
cp "$ROOT/src/lspdsp/ba_lspdsp.h" "$OUT/"
mkdir -p "$ROOT/extern/lsp/licencias"
cp COPYING "$ROOT/extern/lsp/licencias/COPYING.txt"
cp COPYING.LESSER "$ROOT/extern/lsp/licencias/COPYING.LESSER.txt"
echo "$SELLO" >"$OUT/.version"
log "listo: $OUT/$LIB"
