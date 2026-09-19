#!/usr/bin/env bash
# ============================================================================
# tools/deps/check-portable-macos.sh — el bundle de macOS es autocontenido
#
# Recorre todos los Mach-O del bundle y mira sus dependencias (otool -L):
#   - /usr/lib/... y /System/Library/...: base de macOS, ok;
#   - @rpath/X, @executable_path/X, @loader_path/X: tiene que existir un X
#     (archivo o framework) dentro del bundle;
#   - cualquier otra ruta absoluta (Homebrew, /opt, /Users, extern/ de la
#     maquina de build...): ERROR, en otra Mac no existe.
# Ademas verifica que cada binario tenga firma valida (Apple Silicon no corre
# codigo sin firmar).
#
# Uso: tools/deps/check-portable-macos.sh <release>
# ============================================================================
set -uo pipefail

BUNDLE="$(cd "${1:?uso: $0 <bundle>}" && pwd)"
fail=0
count=0

# Nombres disponibles en el bundle (archivos, enlaces y frameworks), uno por
# linea. Sin arrays asociativos: macOS trae bash 3.2.
HAVE="$(mktemp)"
trap 'rm -f "$HAVE"' EXIT
{
    find "$BUNDLE" \( -type f -o -type l \) -exec basename {} \;
    find "$BUNDLE" -type d -name '*.framework' -exec basename {} \;
} | sort -u >"$HAVE"

while IFS= read -r f; do
    file -b "$f" | grep -q "Mach-O" || continue
    count=$((count + 1))
    rel="${f#"$BUNDLE"/}"
    if ! codesign -v "$f" 2>/dev/null; then
        echo "SIN FIRMA: $rel"
        fail=1
    fi
    # Primera linea de otool -L: el propio archivo (en dylibs, su install name).
    while IFS= read -r dep; do
        case "$dep" in
            /usr/lib/* | /System/Library/*) continue ;;
            @rpath/* | @executable_path/* | @loader_path/*)
                tail="${dep#@*/}"
                # Framework: basta con que el .framework este en el bundle.
                if [[ "$tail" == *.framework/* ]]; then
                    name="${tail%%.framework/*}.framework"
                    name="${name##*/}"
                else
                    name="$(basename "$tail")"
                fi
                if ! grep -Fxq "$name" "$HAVE"; then
                    echo "FALTA: $rel necesita $dep (no esta en el bundle)"
                    fail=1
                fi
                ;;
            *)
                echo "RUTA DE LA MAQUINA DE BUILD: $rel -> $dep"
                fail=1
                ;;
        esac
    done < <(otool -L "$f" 2>/dev/null | tail -n +2 | awk '{print $1}' | grep -v "^$(basename "$f")$")
done < <(find "$BUNDLE" -type f)

echo "Mach-O revisados: $count"
if [ "$fail" -ne 0 ]; then
    echo "ERROR: el bundle de macOS depende de cosas que no lleva (o sin firma)." >&2
    exit 1
fi
echo "OK: el bundle de macOS solo usa lo propio y la base de macOS."
