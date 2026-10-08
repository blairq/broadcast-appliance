#!/usr/bin/env bash
# tools/shaders/compilar-vista360.sh — compila el shader de D3D12 de la Vista 360
#
# src/gst-plugins/filtros/vista360.hlsl -> vista360_d3d12.h (DXIL firmado, como
# bytes). Usa el DXC oficial de Microsoft para Linux (con libdxil: firma el
# DXIL, así D3D12 lo acepta sin modo desarrollador), en Docker si no estamos en
# Linux. El .h va en git: compilar el programa no necesita DXC.
set -euo pipefail
DXC_TAG=v1.9.2607
DXC_TGZ=linux_dxc_2026_07_29.x86_x64.tar.gz
RAIZ="$(cd "$(dirname "$0")/../.." && pwd)"
if [[ "$(uname -s)" != Linux ]]; then
    exec docker run --rm --platform linux/amd64 -v "$RAIZ:/ba" -w /ba ubuntu:24.04 bash -c \
        "apt-get update -qq && apt-get install -y -qq curl ca-certificates >/dev/null && tools/shaders/compilar-vista360.sh"
fi
DIR="$RAIZ/extern/dxc"
if [[ ! -x "$DIR/bin/dxc" ]]; then
    mkdir -p "$DIR"
    curl -fsSL "https://github.com/microsoft/DirectXShaderCompiler/releases/download/$DXC_TAG/$DXC_TGZ" | tar xz -C "$DIR" --strip-components=1
fi
export LD_LIBRARY_PATH="$DIR/lib"
F="$RAIZ/src/gst-plugins/filtros"
T="$(mktemp -d)"
"$DIR/bin/dxc" -nologo -T vs_6_0 -E VSMain -O3 -Fh "$T/vs.h" -Vn kVista360Vs "$F/vista360.hlsl"
"$DIR/bin/dxc" -nologo -T ps_6_0 -E PSMain -O3 -Fh "$T/ps.h" -Vn kVista360Ps "$F/vista360.hlsl"
{
    echo "// Generado por tools/shaders/compilar-vista360.sh desde vista360.hlsl (DXC $DXC_TAG). No editar."
    echo "#pragma once"
    cat "$T/vs.h" "$T/ps.h"
} > "$F/vista360_d3d12.h"
rm -rf "$T"
echo "listo: $F/vista360_d3d12.h"
