#!/usr/bin/env bash
# ============================================================================
# tools/deps/llamacpp-lib.sh — llama.cpp como BIBLIOTECA, para el ba-modelo de
# Windows y Linux (src/modelo-llama): el modelo local fuera de la Mac.
#
# En la Mac el modelo local corre con MLX (src/modelo-mac); en Windows y Linux
# no había motor (ModelosLocales lo decía y apagaba la función). Este script
# deja la biblioteca de llama.cpp con la que se enlaza el ba-modelo de C++, que
# habla el MISMO protocolo que el de la Mac.
#
# De dónde sale (la misma versión que tools/deps/llamacpp.sh y el conversor, la
# que exige la tarjeta del modelo para la arquitectura qwen35):
#   - los binarios OFICIALES de la release (Vulkan en Windows y Linux: anda en
#     AMD, NVIDIA e Intel; Metal en la Mac, solo para desarrollar);
#   - los encabezados, del fuente de la misma versión (la API es C: el binario
#     de MSVC/gcc sirve con clang-cl/clang);
#   - en Windows, las bibliotecas de importación (.lib) se arman de las DLL con
#     llvm-dlltool (la release no las trae).
#
# Uso: tools/deps/llamacpp-lib.sh win|linux|mac     (idempotente)
#      deja extern/llamacpp-<plataforma>/{include,lib,bin}
# ============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DL="$ROOT/extern/.downloads"
PLAT="${1:?uso: llamacpp-lib.sh win|linux|mac}"
VER="b11255"
case "$PLAT" in
    win)   ARCH="llama-$VER-bin-win-vulkan-x64.zip"; SHA="3dae91806111ac4a464fb5a6db45188ba870a7e89bfa0a203750115fcef20a31" ;;
    linux) ARCH="llama-$VER-bin-ubuntu-vulkan-x64.tar.gz"; SHA="751edacb2ec7d2cf4cfe2ecb5e931b71e1b4df6e8a199147fe303f803fa546a5" ;;
    mac)   ARCH="llama-$VER-bin-macos-arm64.tar.gz"; SHA="108c641ea8a279e046faddc6f38b7b24736f83f70a3e33c71eafc8d7bebeb777" ;;
    *) echo "plataforma: win, linux o mac" >&2; exit 2 ;;
esac
FUENTE="llama.cpp-$VER-fuente.tar.gz"
SHA_FUENTE="cb1b964a6c2782ba35cb939de2797dae782c9210682bd7de8295084ac48cc77e"
OUT="$ROOT/extern/llamacpp-$PLAT"

log()  { printf '\033[36m[llamacpp-lib]\033[0m %s\n' "$*"; }
morir() { printf '\033[31m[llamacpp-lib] ERROR:\033[0m %s\n' "$*" >&2; exit 1; }
sha() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
bajar() { # archivo url sha
    if [ ! -f "$DL/$1" ] || [ "$(sha "$DL/$1")" != "$3" ]; then
        log "bajando $1"
        curl -fL --retry 3 --no-progress-meter -o "$DL/$1.parcial" "$2" || morir "no se pudo bajar $2"
        [ "$(sha "$DL/$1.parcial")" = "$3" ] || morir "el hash de $1 no coincide"
        mv "$DL/$1.parcial" "$DL/$1"
    fi
}

if [ "$(cat "$OUT/.version" 2>/dev/null)" = "$VER" ]; then log "ya estaba ($PLAT, $VER)"; exit 0; fi
mkdir -p "$DL"
bajar "$ARCH" "https://github.com/ggml-org/llama.cpp/releases/download/$VER/$ARCH" "$SHA"
bajar "$FUENTE" "https://github.com/ggml-org/llama.cpp/archive/refs/tags/$VER.tar.gz" "$SHA_FUENTE"

tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
rm -rf "$OUT"; mkdir -p "$OUT/include" "$OUT/lib" "$OUT/bin"
# Los encabezados: llama.h, ggml*.h y mtmd*.h (la visión).
tar xzf "$DL/$FUENTE" -C "$tmp"
src="$tmp/llama.cpp-$VER"
cp "$src/include/"*.h "$OUT/include/"
cp "$src/ggml/include/"*.h "$OUT/include/"
cp "$src/tools/mtmd/mtmd.h" "$src/tools/mtmd/mtmd-helper.h" "$OUT/include/"
# Los binarios, enteros: cargan backends que no se llaman llama-* (ggml-cpu-*,
# ggml-vulkan) y se eligen al arrancar según el procesador.
mkdir -p "$tmp/bin"
case "$ARCH" in
    *.zip) (cd "$tmp/bin" && unzip -q "$DL/$ARCH") ;;
    *) tar xzf "$DL/$ARCH" -C "$tmp/bin" ;;
esac
raiz="$tmp/bin"; [ "$(find "$raiz" -maxdepth 1 -mindepth 1 | wc -l | tr -d " ")" = 1 ] && raiz="$(find "$raiz" -maxdepth 1 -mindepth 1 -type d | head -1)"
case "$PLAT" in
    win)
        # Solo las DLL (las herramientas llama-*.exe no viajan): las de llama, ggml y mtmd.
        find "$raiz" -maxdepth 1 -iname "*.dll" -exec cp {} "$OUT/bin/" \;
        # Bibliotecas de importación desde las exportaciones de cada DLL.
        LLVM="$ROOT/extern/toolchain/llvm/bin"
        [ -x "$LLVM/llvm-dlltool" ] || morir "falta llvm-dlltool (tools/deps/win-toolchain.sh)"
        for dll in llama ggml ggml-base mtmd; do
            [ -f "$OUT/bin/$dll.dll" ] || morir "la release no trae $dll.dll"
            { echo "LIBRARY $dll.dll"; echo "EXPORTS";
              "$LLVM/llvm-readobj" --coff-exports "$OUT/bin/$dll.dll" | sed -n 's/^ *Name: \(.*\)$/\1/p' | grep -v '^$'; } > "$tmp/$dll.def"
            "$LLVM/llvm-dlltool" -m i386:x86-64 -d "$tmp/$dll.def" -l "$OUT/lib/$dll.lib"
        done
        ;;
    linux)
        find "$raiz" -maxdepth 1 \( -name "*.so" -o -name "*.so.*" \) -exec cp -a {} "$OUT/lib/" \;
        ;;
    mac)
        find "$raiz" -maxdepth 1 -name "*.dylib" -exec cp -a {} "$OUT/lib/" \;
        ;;
esac
[ -n "$(ls "$OUT/lib")" ] || morir "no quedó ninguna biblioteca en $OUT/lib"
echo "$VER" > "$OUT/.version"
log "listo: extern/llamacpp-$PLAT ($VER): $(ls "$OUT/lib" | wc -l | tr -d ' ') en lib, $(ls "$OUT/bin" | wc -l | tr -d ' ') en bin"
