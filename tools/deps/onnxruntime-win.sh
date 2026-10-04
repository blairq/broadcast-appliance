#!/usr/bin/env bash
# ============================================================================
# tools/deps/onnxruntime-win.sh — ONNX Runtime para Windows, con DirectML
#
# El OFICIAL de Microsoft (como el OBS, el Qt y el GStreamer de Windows): el
# paquete NuGet Microsoft.ML.OnnxRuntime.DirectML y la DirectML que pide
# (Microsoft.AI.DirectML). DirectML corre las redes en CUALQUIER GPU con
# DirectX 12 (AMD, Intel, NVIDIA), integradas incluidas: la superresolución
# ONNX (baonnxsr) la usa; los rostros y la voz corren en el procesador.
#
# Va a extern/onnxruntime-win (aparte del de Linux/macOS en extern/rostros:
# el volumen de compilar en Docker es el mismo para los dos):
#   include/   las cabeceras (con dml_provider_factory.h)
#   lib/       onnxruntime.lib (para enlazar), onnxruntime.dll y DirectML.dll
#
# Uso: tools/deps/onnxruntime-win.sh [dir de descargas]   (idempotente)
# ============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DL="${1:-$ROOT/extern/.downloads}"
OUT="$ROOT/extern/onnxruntime-win"
command -v sha256sum >/dev/null 2>&1 || sha256sum() { shasum -a 256 "$@"; }
log() { printf '\033[36m[onnxruntime-win]\033[0m %s\n' "$*"; }
morir() { printf '\033[31m[onnxruntime-win] ERROR:\033[0m %s\n' "$*" >&2; exit 1; }

ORT_VER=1.20.1
ORT_SHA=6763468507b7cfc777b1334b3e174c11a540ddacb7bd4354bc2e0ec89e56eec2
DML_VER=1.15.4
DML_SHA=4e7cb7ddce8cf837a7a75dc029209b520ca0101470fcdf275c1f49736a3615b9
MARCA="ort $ORT_VER dml $DML_VER"

if [ "$(cat "$OUT/.version" 2>/dev/null)" = "$MARCA" ]; then
    log "ya está ($OUT)"
    exit 0
fi
mkdir -p "$DL"
baja() { # paquete versión sha
    local f="$DL/$(echo "$1" | tr 'A-Z' 'a-z').$2.nupkg"
    if ! [ -f "$f" ] || [ "$(sha256sum "$f" | cut -d' ' -f1)" != "$3" ]; then
        curl -fL --retry 3 -o "$f.part" "https://www.nuget.org/api/v2/package/$1/$2" || morir "no pude bajar $1 $2"
        [ "$(sha256sum "$f.part" | cut -d' ' -f1)" = "$3" ] || { rm -f "$f.part"; morir "sha256 de $1 $2"; }
        mv "$f.part" "$f"
    fi
    echo "$f"
}
ORT_PKG="$(baja Microsoft.ML.OnnxRuntime.DirectML "$ORT_VER" "$ORT_SHA")"
DML_PKG="$(baja Microsoft.AI.DirectML "$DML_VER" "$DML_SHA")"

T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
unzip -q "$ORT_PKG" -d "$T/ort"
unzip -q "$DML_PKG" -d "$T/dml"
rm -rf "$OUT"
mkdir -p "$OUT/include" "$OUT/lib"
cp "$T"/ort/build/native/include/*.h "$OUT/include/"
cp "$T"/ort/runtimes/win-x64/native/onnxruntime.{dll,lib} "$OUT/lib/"
cp "$T"/dml/bin/x64-win/DirectML.dll "$OUT/lib/"
cp "$T/ort/LICENSE" "$OUT/LICENSE" 2>/dev/null || cp "$T"/ort/LICENSE* "$OUT/" 2>/dev/null || true
cp "$T/dml/LICENSE.txt" "$OUT/LICENSE-DirectML.txt"
echo "$MARCA" >"$OUT/.version"
log "listo: $OUT ($(du -sh "$OUT" | cut -f1))"
