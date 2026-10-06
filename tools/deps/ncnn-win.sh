#!/usr/bin/env bash
# ============================================================================
# tools/deps/ncnn-win.sh — ncnn con Vulkan para Windows (superresolución en la GPU)
#
# ncnn (Tencent, BSD-3) corre redes en CUALQUIER GPU con Vulkan —AMD, Intel,
# NVIDIA—; es lo que usa realesrgan-ncnn-vulkan. El motor «vulkan» de la
# superresolución (bancnnsr) lo usa. Todo viaja en el bundle:
#   ncnn.dll          el oficial (versión compartida de la release). FIJADO en
#                     20231027: en una Radeon RX 5700 XT las releases de 2026
#                     corren la misma red ~60 veces más lento (12 s contra 190 ms
#                     por cuadro, 960x540 x4); la de 2023 anda como realesrgan.
#   vulkan-1.dll      el cargador de Vulkan de Khronos/LunarG (Apache-2.0): ncnn
#                     lo importa; va junto al programa en bin/64bit. Del sistema
#                     solo se usa el driver de la placa (como D3D12)
#   vulkaninfo.exe    para el diagnóstico (qué ve Vulkan en esta PC)
#   pnnx.exe          convierte un modelo ONNX a ncnn (el laboratorio lo usa
#                     para «compilar» la red para Vulkan)
#
# Va a extern/ncnn-win:  include/ncnn  lib/ncnn.lib  bin/*.dll  herramientas/pnnx.exe
# Uso: tools/deps/ncnn-win.sh [dir de descargas]   (idempotente)
# ============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DL="${1:-$ROOT/extern/.downloads}"
OUT="$ROOT/extern/ncnn-win"
command -v sha256sum >/dev/null 2>&1 || sha256sum() { shasum -a 256 "$@"; }
log() { printf '\033[36m[ncnn-win]\033[0m %s\n' "$*" >&2; }
morir() { printf '\033[31m[ncnn-win] ERROR:\033[0m %s\n' "$*" >&2; exit 1; }

NCNN_VER=20231027
NCNN_SHA=e78c066af35769930207a2452e2672719b3e62e55eb0a2301ada82402f97ddaf
VK_VER=1.4.363.0
VK_SHA=a25a927aa8b9f0371048f1861cf88ac3b9bc9b1fb332c42d897c8ab32695769a
PNNX_VER=20260704
PNNX_SHA=7472941823186e4ce8f829d54267c0e9f50e616e3214a80a524d4aa1e76d7fc8
MARCA="ncnn $NCNN_VER vulkan $VK_VER pnnx $PNNX_VER"

if [ "$(cat "$OUT/.version" 2>/dev/null)" = "$MARCA" ]; then
    log "ya está ($OUT)"
    exit 0
fi
mkdir -p "$DL"
baja() { # url archivo sha
    local f="$DL/$2"
    if ! [ -f "$f" ] || [ "$(sha256sum "$f" | cut -d' ' -f1)" != "$3" ]; then
        log "bajando $2"
        curl -fL --retry 3 -o "$f.part" "$1" || morir "no pude bajar $1"
        [ "$(sha256sum "$f.part" | cut -d' ' -f1)" = "$3" ] || { rm -f "$f.part"; morir "sha256 de $2"; }
        mv "$f.part" "$f"
    fi
    echo "$f"
}
NCNN_ZIP="$(baja "https://github.com/Tencent/ncnn/releases/download/$NCNN_VER/ncnn-$NCNN_VER-windows-vs2022-shared.zip" \
    "ncnn-$NCNN_VER-windows-vs2022-shared.zip" "$NCNN_SHA")"
VK_ZIP="$(baja "https://sdk.lunarg.com/sdk/download/$VK_VER/windows/vulkan-runtime-components.zip" \
    "vulkan-runtime-components-$VK_VER.zip" "$VK_SHA")"
PNNX_ZIP="$(baja "https://github.com/pnnx/pnnx/releases/download/$PNNX_VER/pnnx-$PNNX_VER-windows.zip" \
    "pnnx-$PNNX_VER-windows.zip" "$PNNX_SHA")"

T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
unzip -q "$NCNN_ZIP" -d "$T/ncnn"
unzip -q "$VK_ZIP" -d "$T/vk"
unzip -q "$PNNX_ZIP" -d "$T/pnnx"
N="$T/ncnn/ncnn-$NCNN_VER-windows-vs2022-shared/x64"
V="$(echo "$T"/vk/VulkanRT-X64-*-Components/x64)"
rm -rf "$OUT"
mkdir -p "$OUT/include" "$OUT/lib" "$OUT/bin" "$OUT/herramientas"
cp -R "$N/include/ncnn" "$OUT/include/"
cp "$N/lib/ncnn.lib" "$OUT/lib/"
cp "$N/bin/ncnn.dll" "$OUT/bin/"
cp "$V/vulkan-1.dll" "$V/vulkaninfo.exe" "$OUT/bin/"
cp "$T"/pnnx/pnnx-*/pnnx.exe "$OUT/herramientas/"
cp "$T"/vk/VulkanRT-X64-*-Components/VulkanRT-License.txt "$OUT/LICENSE-Vulkan.txt"
curl -fsL -o "$OUT/LICENSE-ncnn.txt" "https://raw.githubusercontent.com/Tencent/ncnn/$NCNN_VER/LICENSE.txt" || true
echo "$MARCA" >"$OUT/.version"
log "listo: $OUT"
