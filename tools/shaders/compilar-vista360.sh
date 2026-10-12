#!/usr/bin/env bash
# tools/shaders/compilar-vista360.sh — compila el shader de D3D12 de la Vista 360
# (vista360.hlsl -> vista360_d3d12.h): ver compilar-hlsl.sh.
exec "$(dirname "$0")/compilar-hlsl.sh" vista360 Vista360
