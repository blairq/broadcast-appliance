#!/usr/bin/env bash
# ============================================================================
# tools/deps/check-portable-win.sh — el bundle de Windows es autocontenido
#
# Recorre TODOS los .exe/.dll del bundle y mira su tabla de importaciones
# (llvm-objdump). Cada DLL importada tiene que estar en el bundle (junto al
# que la importa o en bin/64bit, donde viven A, libobs y Qt) o ser parte de
# Windows 10+ (tools/deps/windows-baseline.txt). Si algo depende de un
# instalable (VC++ Redistributable, un driver, otro programa), falla.
#
# Uso: tools/deps/check-portable-win.sh <release-win>
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUNDLE="${1:?uso: $0 <bundle>}"
OBJDUMP="$ROOT/extern/toolchain/llvm/bin/llvm-objdump"
BASELINE="$ROOT/tools/deps/windows-baseline.txt"

# Nombres (en minusculas) de todas las DLL del bundle.
declare -A have=()
while IFS= read -r f; do
    have["$(basename "$f" | tr '[:upper:]' '[:lower:]')"]=1
done < <(find "$BUNDLE" -type f \( -iname '*.dll' -o -iname '*.clap' -o -iname '*.pyd' \))

declare -A base=()
while IFS= read -r l; do
    l="${l%%#*}"
    l="$(echo "$l" | tr -d '[:space:]' | tr '[:upper:]' '[:lower:]')"
    [ -n "$l" ] && base["$l"]=1
done <"$BASELINE"
# Lo que ademas puede pedir LibreOffice, y solo el (ver el archivo).
declare -A base_lo=()
if [ -f "$ROOT/tools/deps/windows-baseline-libreoffice.txt" ]; then
    while IFS= read -r l; do
        l="${l%%#*}"
        l="$(echo "$l" | tr -d '[:space:]' | tr '[:upper:]' '[:lower:]')"
        [ -n "$l" ] && base_lo["$l"]=1
    done <"$ROOT/tools/deps/windows-baseline-libreoffice.txt"
fi

fail=0
count=0
while IFS= read -r pe; do
    count=$((count + 1))
    # LibreOffice corre desde bin/libreoffice/program/ y Windows busca sus DLL
    # AHI (la carpeta del programa) o en el sistema: que este en bin/64bit no
    # le sirve. Asi se escapo el runtime de Visual C++, que el .msi pone en
    # System32 y no en program/.
    es_lo=0
    case "$pe" in "$BUNDLE"/bin/libreoffice/*) es_lo=1 ;; esac
    while IFS= read -r dll; do
        k="$(echo "$dll" | tr '[:upper:]' '[:lower:]')"
        [ -n "${base[$k]:-}" ] && continue
        case "$k" in api-ms-win-*|ext-ms-win-*) continue ;; esac
        if [ "$es_lo" = 1 ]; then
            [ -n "${base_lo[$k]:-}" ] && continue
            # Su carpeta, o la del .exe que arranco el proceso (program/).
            ls "$(dirname "$pe")" "$BUNDLE/bin/libreoffice/program" 2>/dev/null | grep -qixF "$dll" && continue
            echo "FALTA: ${pe#"$BUNDLE"/} importa $dll (ni en su carpeta, ni en program/, ni en Windows)"
            fail=1
            continue
        fi
        [ -n "${have[$k]:-}" ] && continue
        case "$k" in api-ms-win-*|ext-ms-win-*) continue ;; esac
        echo "FALTA: ${pe#"$BUNDLE"/} importa $dll (ni en el bundle ni en Windows)"
        fail=1
    done < <("$OBJDUMP" -p "$pe" 2>/dev/null | awk '/DLL Name:/{print $3}')
done < <(find "$BUNDLE" -type f \( -iname '*.exe' -o -iname '*.dll' -o -iname '*.clap' \))

echo "PE revisados: $count"
if [ "$fail" -ne 0 ]; then
    echo "ERROR: el bundle de Windows depende de DLLs que no lleva." >&2
    exit 1
fi
echo "OK: el bundle de Windows solo usa DLLs propias y del sistema (tools/deps/windows-baseline.txt)."
