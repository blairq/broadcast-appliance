#!/usr/bin/env bash
# ============================================================================
# tools/deps/llamacpp-convertir.sh — el conversor HF -> GGUF de llama.cpp
#
# Los binarios (tools/deps/llamacpp.sh) no traen convert_hf_to_gguf.py ni su
# biblioteca gguf-py: viven en el codigo fuente. Se bajan de la MISMA version
# que los binarios (b11255), porque el formato que escribe el conversor tiene
# que ser el que lee el llama-server que lo va a correr. b11255 ya convierte
# Qwen3.5 (Qwen3_5ForCausalLM, conversion/qwen.py) y su vision (qwen3vl.py).
#
# Uso: tools/deps/llamacpp-convertir.sh        (idempotente)
#      deja extern/llamacpp-convertir/{convert_hf_to_gguf.py,conversion,gguf-py}
# ============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DL="$ROOT/extern/.downloads"
OUT="$ROOT/extern/llamacpp-convertir"
VER="b11255"
TAR="llama.cpp-$VER-fuente.tar.gz"
URL="https://github.com/ggml-org/llama.cpp/archive/refs/tags/$VER.tar.gz"
SHA="cb1b964a6c2782ba35cb939de2797dae782c9210682bd7de8295084ac48cc77e"

log()  { printf '\033[36m[llamacpp-convertir]\033[0m %s\n' "$*"; }
morir() { printf '\033[31m[llamacpp-convertir] ERROR:\033[0m %s\n' "$*" >&2; exit 1; }

if [ -f "$OUT/convert_hf_to_gguf.py" ] && [ "$(cat "$OUT/.version" 2>/dev/null)" = "$VER" ]; then
    log "ya estaba ($VER)"
    exit 0
fi
mkdir -p "$DL"
if [ ! -f "$DL/$TAR" ] || [ "$(sha256sum "$DL/$TAR" | cut -d' ' -f1)" != "$SHA" ]; then
    log "bajando el fuente de $VER"
    curl -fL --retry 3 --no-progress-meter -o "$DL/$TAR.parcial" "$URL" || morir "no se pudo bajar"
    echo "$SHA  $DL/$TAR.parcial" | sha256sum -c --quiet - || morir "el hash no coincide"
    mv "$DL/$TAR.parcial" "$DL/$TAR"
fi
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
tar xzf "$DL/$TAR" -C "$tmp" --wildcards \
    "llama.cpp-$VER/convert_hf_to_gguf.py" "llama.cpp-$VER/conversion/*" "llama.cpp-$VER/gguf-py/*"
rm -rf "$OUT"; mkdir -p "$OUT"
cp -a "$tmp/llama.cpp-$VER/." "$OUT/"
echo "$VER" > "$OUT/.version"
log "listo: extern/llamacpp-convertir ($VER)"
