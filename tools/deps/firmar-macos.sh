#!/usr/bin/env bash
# ============================================================================
# tools/deps/firmar-macos.sh — firma los Mach-O del bundle, solo los que cambiaron
#
# Lo llama cmake/BaMacBundle.cmake al final de `cmake --install`. Antes firmaba
# TODOS los binarios (775) uno por uno en cada instalación, y además firmar
# modifica el archivo: en la instalación siguiente cmake lo veía distinto del
# original y lo volvía a copiar (854 archivos), y había que firmarlos de nuevo.
#
# Ahora:
#   * cache (fecha de modificación con nanosegundos|tamaño|ruta) de cómo quedó
#     cada archivo la última vez que se firmó: solo se miran los nuevos o los
#     que cambiaron. No sirve el ctime: cmake se lo cambia a TODOS en cada
#     instalación (vuelve a poner los permisos aunque el archivo esté al día).
#     cmake solo vuelve a copiar un archivo si cambió su fecha, así que una
#     copia nueva siempre trae otra fecha;
#   * después de firmar se le devuelve al archivo su fecha de modificación: así
#     cmake lo sigue viendo «Up-to-date» y no lo vuelve a copiar;
#   * las firmas van en paralelo.
# Sin cache (release/ recién borrado) firma todo, como antes.
#
# Uso: firmar-macos.sh <raiz> <identidad|-> <cache> [<lista-de-firmados>]
# ============================================================================
set -uo pipefail

ROOT="${1:?uso: $0 <raiz> <identidad> <cache> [lista]}"
FIRMA="${2:?falta la identidad}"
CACHE="${3:?falta el archivo de cache}"
LISTA="${4:-/dev/null}"
export LC_ALL=C
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

estado() { # fecha|tamaño|ruta de todos los archivos comunes (no symlinks), ordenado
    find "$ROOT" -type f -print0 | xargs -0 /usr/bin/stat -f '%Fm|%z|%N' | sort
}

estado >"$TMP/ahora"
[ -f "$CACHE" ] || : >"$CACHE"
sort "$CACHE" >"$TMP/antes"
# Nuevos o cambiados desde la última firma.
comm -23 "$TMP/ahora" "$TMP/antes" | cut -d'|' -f3- >"$TMP/cambiados"

# Los de adentro de una .app (LibreOffice) se firman con la .app entera, abajo.
grep -v '\.app/' "$TMP/cambiados" | tr '\n' '\0' | xargs -0 file -N -F $'\t' 2>/dev/null |
    awk -F'\t' 'index($1, " (for architecture ") == 0 && $2 ~ /Mach-O/ {print $1}' >"$TMP/macho"

# Firma conservando la fecha de modificación (ver arriba).
cat >"$TMP/firmar1.sh" <<'SH'
firma="$1"; f="$2"; ref="$(mktemp)"
touch -r "$f" "$ref"
if codesign -f -s "$firma" "$f" >/dev/null 2>&1; then echo "$f"; else echo "ba-bundle: no se pudo firmar $f" >&2; fi
touch -r "$ref" "$f"; rm -f "$ref"
SH
tr '\n' '\0' <"$TMP/macho" | xargs -0 -n 1 -P "$JOBS" bash "$TMP/firmar1.sh" "$FIRMA" 2>&1 >"$TMP/firmados" |
    grep -v '^$' >&2
n="$(wc -l <"$TMP/firmados" | tr -d ' ')"
echo "-- ba-bundle: $n binarios Mach-O firmados ($FIRMA; $(wc -l <"$TMP/cambiados" | tr -d ' ') archivos nuevos o cambiados)"

# Cada .app de primer nivel con algo cambiado adentro: entera (--deep), y con
# las fechas de modificación de lo de adentro devueltas.
find "$ROOT" -type d -name '*.app' -prune -print | while IFS= read -r app; do
    grep -qF "$app/" "$TMP/cambiados" || continue
    find "$app" -type f -print0 | xargs -0 /usr/bin/stat -f '%Sm|%N' -t '%Y%m%d%H%M.%S' >"$TMP/fechas"
    if codesign --force --deep -s "$FIRMA" "$app" >/dev/null 2>&1; then
        echo "-- ba-bundle: firmada $app"
        echo "$app" >>"$TMP/firmados"
    else
        echo "ba-bundle: no se pudo firmar $app" >&2
    fi
    while IFS='|' read -r t f; do touch -t "$t" "$f"; done <"$TMP/fechas"
done

cp "$TMP/firmados" "$LISTA" 2>/dev/null || true
# Cómo quedó todo después de firmar.
estado >"$CACHE"
