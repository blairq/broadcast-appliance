#!/usr/bin/env bash
# ============================================================================
# tools/deps/lspdsp.sh — ba-lspdsp: el DSP de LSP detrás de una API en C
#
# Compila src/lspdsp/ba_lspdsp.cpp con el MISMO compilador y las mismas opciones
# que LSP y lo enlaza con los objetos que deja el build de LSP (lsp-common-lib,
# lsp-lltl-lib, lsp-runtime-lib, lsp-dsp-lib, lsp-dsp-units) en una biblioteca
# compartida que viaja en el bundle (LGPL: aparte y reemplazable). La app la
# carga en tiempo de ejecución. Ver docs/planes/PLAN-MEZCLADOR.md (L0).
#
# Uso: tools/deps/lspdsp.sh <arbol-de-build-de-LSP> <plataforma> <salida>
#   plataforma: macos-arm64 | windows-x86_64 | linux-x86_64
#   Lo llaman tools/deps/lsp-macos.sh, lsp-win.sh y el de Linux al terminar el
#   build de LSP (y se puede correr solo con un árbol ya compilado).
#   Variables: CXX (de fábrica: el de cada plataforma), CXXFLAGS_EXTRA.
# ============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LSP="${1:?uso: $0 <arbol-lsp> <plataforma> <salida>}"
PLAT="${2:?falta la plataforma}"
OUT="${3:?falta la salida}"
log() { echo "[lspdsp] $*" >&2; }

T="$LSP/.build/target"
OBJS=()
for m in lsp-common-lib lsp-lltl-lib lsp-runtime-lib lsp-dsp-lib lsp-dsp-units; do
    o="$T/$m/$m.o"
    [ -f "$o" ] || { log "falta $o: compilá LSP primero"; exit 1; }
    OBJS+=("$o")
done
INC=()
for m in lsp-common-lib lsp-lltl-lib lsp-runtime-lib lsp-dsp-lib lsp-dsp-units; do
    INC+=("-I$LSP/modules/$m/include")
done
# Las opciones de LSP para sus bibliotecas (sin excepciones ni RTTI).
FLAGS=(-std=c++17 -O2 -fvisibility=hidden -fno-exceptions -fno-rtti -fdata-sections -ffunction-sections)
mkdir -p "$OUT"
OBJ="$OUT/.ba_lspdsp.o"
case "$PLAT" in
    macos-arm64)
        CXX="${CXX:-clang++}"
        FLAGS+=(-arch arm64 -mmacosx-version-min=13.0 -fPIC)
        LIB="$OUT/libba-lspdsp.dylib"
        LINK=(-dynamiclib -arch arm64 -mmacosx-version-min=13.0 -install_name @rpath/libba-lspdsp.dylib
              -framework CoreFoundation -Wl,-dead_strip)
        ;;
    linux-x86_64)
        CXX="${CXX:-g++}"
        FLAGS+=(-fPIC -march=x86-64)
        LIB="$OUT/libba-lspdsp.so"
        LINK=(-shared -Wl,-soname,libba-lspdsp.so -Wl,--gc-sections -lpthread -ldl)
        ;;
    windows-x86_64)
        CXX="${CXX:?en Windows va el mismo CXX que LSP (x86_64-w64-mingw32-clang++)}"
        LIB="$OUT/ba-lspdsp.dll"
        # Todo estático (libc++ de MinGW incluida): la DLL no pide nada más que Windows.
        LINK=(-shared -static -Wl,--gc-sections -Wl,--out-implib,"$OUT/ba-lspdsp.dll.a" -lshlwapi -lole32 -luuid)
        ;;
    *) log "plataforma desconocida: $PLAT"; exit 1 ;;
esac
log "compilando ba_lspdsp.cpp ($PLAT)"
"$CXX" "${FLAGS[@]}" ${CXXFLAGS_EXTRA:-} "${INC[@]}" -c "$ROOT/src/lspdsp/ba_lspdsp.cpp" -o "$OBJ"
log "enlazando $(basename "$LIB")"
"$CXX" -o "$LIB" "$OBJ" "${OBJS[@]}" "${LINK[@]}"
rm -f "$OBJ"
cp "$ROOT/src/lspdsp/ba_lspdsp.h" "$OUT/"
log "listo: $LIB"
