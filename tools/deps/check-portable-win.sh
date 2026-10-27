#!/usr/bin/env bash
# ============================================================================
# tools/deps/check-portable-win.sh — el bundle de Windows es autocontenido
#
# Recorre TODOS los .exe/.dll del bundle y mira su tabla de importaciones
# (llvm-objdump). Cada DLL importada tiene que estar en el bundle (junto al
# que la importa o en bin/64bit, donde viven A, GStreamer y Qt) o ser parte de
# Windows 10+ (tools/deps/windows-baseline.txt). Si algo depende de un
# instalable (VC++ Redistributable, un driver, otro programa), falla.
#
# Rápido: las importaciones de cada archivo se guardan por su sha256 en un
# caché al lado del bundle (<bundle>.importaciones), así que solo se lee lo que
# cambió, y eso en paralelo. Las comparaciones no lanzan procesos (eran decenas
# de miles de `echo | tr` bajo la emulación x86 de Docker: 6 minutos).
#
# Uso: tools/deps/check-portable-win.sh <release-win>
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUNDLE="$(cd "${1:?uso: $0 <bundle>}" && pwd)"
OBJDUMP="${OBJDUMP:-$ROOT/extern/toolchain/llvm/bin/llvm-objdump}"
BASELINE="$ROOT/tools/deps/windows-baseline.txt"
CACHE="${BA_PORTABLE_CACHE:-$BUNDLE.importaciones}"

# Todos los archivos del bundle, por ruta relativa en minúsculas (para buscar
# una DLL junto a quien la importa), y los nombres de las DLL.
declare -A archivo=() have=()
while IFS= read -r -d '' f; do
    rel="${f#"$BUNDLE"/}"
    archivo["${rel,,}"]=1
    case "${f,,}" in *.dll | *.clap | *.pyd) n="${f##*/}"; have["${n,,}"]=1 ;; esac
done < <(find "$BUNDLE" -type f -print0)

leer_base() { # $1 = archivo, $2 = nombre del arreglo
    local -n dst="$2"
    local l
    [ -f "$1" ] || return 0
    while IFS= read -r l || [ -n "$l" ]; do
        l="${l%%#*}"
        l="${l//[[:space:]]/}"
        [ -n "$l" ] && dst["${l,,}"]=1
    done <"$1"
}
declare -A base=()
leer_base "$BASELINE" base

# Los PE y su sha256.
mapfile -d '' pes < <(find "$BUNDLE" -type f \( -iname '*.exe' -o -iname '*.dll' -o -iname '*.clap' \) -print0)
declare -A sha=()
while read -r h p; do sha["$p"]="$h"; done < <(printf '%s\0' "${pes[@]}" | xargs -0 -r sha256sum)

# El caché: sha<TAB>dll dll dll
declare -A importa=()
if [ -f "$CACHE" ]; then
    while IFS=$'\t' read -r h dlls; do [ -n "$h" ] && importa["$h"]="$dlls"; done <"$CACHE"
fi
faltan=()
for p in "${pes[@]}"; do [ -n "${importa[${sha[$p]}]+x}" ] || faltan+=("$p"); done
if [ "${#faltan[@]}" -gt 0 ]; then
    # Una línea por archivo: <ruta><TAB><dll dll ...> (objdump en paralelo).
    export OBJDUMP
    while IFS=$'\t' read -r p dlls; do
        importa["${sha[$p]}"]="$dlls"
    done < <(printf '%s\0' "${faltan[@]}" | xargs -0 -r -P "$(nproc 2>/dev/null || echo 4)" -n 16 bash -c '
        for f in "$@"; do
            printf "%s\t%s\n" "$f" "$("$OBJDUMP" -p "$f" 2>/dev/null | awk "/DLL Name:/{printf \"%s \", \$3}")"
        done' _)
fi
# El caché vuelve a escribirse con lo de este bundle (lo viejo se cae solo).
tmp="$CACHE.tmp.$$"
for p in "${pes[@]}"; do printf '%s\t%s\n' "${sha[$p]}" "${importa[${sha[$p]}]}"; done | sort -u >"$tmp"
mv -f "$tmp" "$CACHE"

fail=0
for pe in "${pes[@]}"; do
    rel="${pe#"$BUNDLE"/}"
    dir="${rel%/*}"
    [ "$dir" = "$rel" ] && dir=""
    # Un programa que corre suelto fuera de bin/64bit (bin/craft: WordCraft y
    # compañía) busca sus DLL en SU carpeta o en el sistema: que esté en otro
    # lado del bundle no le sirve. Así se le escapó a LibreOffice (cuando viajaba)
    # el runtime de Visual C++.
    suelto=0
    case "$rel" in bin/craft/*) suelto=1 ;; esac
    for dll in ${importa[${sha[$pe]}]}; do
        k="${dll,,}"
        [ -n "${base[$k]:-}" ] && continue
        case "$k" in api-ms-win-* | ext-ms-win-*) continue ;; esac
        if [ "$suelto" = 1 ]; then
            [ -n "${archivo[${dir,,}/$k]:-}" ] && continue
            echo "FALTA: $rel importa $dll (ni en su carpeta ni en Windows)"
            fail=1
            continue
        fi
        [ -n "${have[$k]:-}" ] && continue
        echo "FALTA: $rel importa $dll (ni en el bundle ni en Windows)"
        fail=1
    done
done

echo "PE revisados: ${#pes[@]} (${#faltan[@]} leídos, el resto del caché)"
if [ "$fail" -ne 0 ]; then
    echo "ERROR: el bundle de Windows depende de DLLs que no lleva." >&2
    exit 1
fi
echo "OK: el bundle de Windows solo usa DLLs propias y del sistema (tools/deps/windows-baseline.txt)."
