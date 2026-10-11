#!/usr/bin/env bash
# ============================================================================
# tools/caidas/simbolos-mac.sh — los .dSYM de lo nuestro, al almacén
#
# En macOS -g deja la información de depuración en los .o del build; dsymutil
# la junta en un .dSYM por binario. Se guarda por el UUID del binario (lo que
# trae el informe de caídas de macOS, ~/Library/Logs/DiagnosticReports/*.ips):
#   dist/simbolos/macos/<binario>/<UUID>/<binario>.dSYM
# Para leer una pila: atos -o <ese .dSYM>/Contents/Resources/DWARF/<binario> -l <base> <dirección>
#
# Uso: tools/caidas/simbolos-mac.sh <release> [destino]
# ============================================================================
set -euo pipefail
REL="$1"
DEST="${2:-$(cd "$(dirname "$0")/../.." && pwd)/dist/simbolos/macos}"
n=0
while IFS= read -r -d '' f; do
    file -b "$f" | grep -q "Mach-O" || continue
    uuid="$(dwarfdump --uuid "$f" 2>/dev/null | awk '{print $2; exit}')"
    [ -n "$uuid" ] || continue
    nom="$(basename "$f")"
    d="$DEST/$nom/$uuid"
    [ -d "$d/$nom.dSYM" ] && continue
    mkdir -p "$d"
    dsymutil "$f" -o "$d/$nom.dSYM" 2>/dev/null || { rm -rf "$d"; continue; }
    n=$((n + 1))
done < <({ find "$REL/bin" -type f \( -name 'ba-*' -o -name 'BroadcastAppliance' \) -print0
           find "$REL/gst-plugins" -type f \( -name '*.dylib' -o -name '*.so' \) -print0; } 2>/dev/null)
echo "[simbolos-mac] $n .dSYM nuevos en $DEST"
