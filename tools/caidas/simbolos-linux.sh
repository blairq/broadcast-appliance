#!/usr/bin/env bash
# ============================================================================
# tools/caidas/simbolos-linux.sh — Linux: la depuración afuera, el binario chico
#
# Con -g (CMakeLists raíz) la información de depuración queda ADENTRO de cada
# ELF. Acá se saca al almacén, por build-id (el formato que gdb y
# eu-stack buscan solos):  <destino>/<id[0:2]>/<id[2:]>.debug
# y al binario se le deja solo lo de siempre (el bundle no crece).
#
# Uso: tools/caidas/simbolos-linux.sh <release> [destino]
# ============================================================================
set -euo pipefail
REL="$1"
DEST="${2:-dist/simbolos/linux}"
mkdir -p "$DEST"
n=0
while IFS= read -r -d '' f; do
    file -b "$f" | grep -q "^ELF" || continue
    id="$(readelf -n "$f" 2>/dev/null | awk '/Build ID:/{print $3; exit}')"
    [ -n "$id" ] || continue
    dbg="$DEST/${id:0:2}/${id:2}.debug"
    if [ ! -f "$dbg" ]; then
        mkdir -p "$(dirname "$dbg")"
        objcopy --only-keep-debug "$f" "$dbg" && n=$((n + 1))
    fi
    objcopy --strip-debug "$f"
done < <({ find "$REL/bin" -type f \( -name 'ba-*' -o -name 'BroadcastAppliance' \) -print0
           find "$REL/gst-plugins" -type f -name '*.so' -print0; } 2>/dev/null)
echo "[simbolos-linux] $n .debug nuevos en $DEST"
