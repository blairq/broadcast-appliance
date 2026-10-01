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
    # Solo lo que se carga (ejecutables, dylibs, bundles): un .o suelto (el de
    # Python de LibreOffice, para compilar extensiones) no se firma ni se carga.
    file -b "$f" | grep -qE "Mach-O.*(executable|dynamically linked|bundle)" || continue
    count=$((count + 1))
    rel="${f#"$BUNDLE"/}"
    if ! codesign -v "$f" 2>/dev/null; then
        echo "SIN FIRMA: $rel"
        fail=1
    fi
    # Solo la porcion arm64 (Qt viene universal y otool imprime un encabezado
    # "archivo (architecture X):" por cada una). En dylibs, otool -L repite el
    # propio install name como primera dependencia: se saltea.
    self="$(otool -arch arm64 -D "$f" 2>/dev/null | grep -v ':$' | head -1)"
    is_exe=0
    file -b "$f" | grep -q "Mach-O.*executable" && is_exe=1
    exe_rpaths=""
    [ "$is_exe" = 1 ] && exe_rpaths="$(otool -arch arm64 -l "$f" 2>/dev/null | awk '/cmd LC_RPATH/{getline; getline; print $2}')"
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
                # Un ejecutable tiene que poder resolver sus @rpath con sus
                # propios LC_RPATH (los de una dylib dependen de quien la carga).
                if [ "$is_exe" = 1 ]; then
                    if [[ "$dep" == @rpath/* ]]; then
                        found=0
                        while IFS= read -r rp; do
                            [ -n "$rp" ] || continue
                            rp="${rp//@executable_path/$(dirname "$f")}"
                            rp="${rp//@loader_path/$(dirname "$f")}"
                            [ -e "$rp/${dep#@rpath/}" ] && { found=1; break; }
                        done <<<"$exe_rpaths"
                        if [ "$found" -ne 1 ]; then
                            echo "NO RESUELVE: $rel necesita $dep y sus rpath no lo encuentran"
                            fail=1
                        fi
                    fi
                fi
                ;;
            *)
                echo "RUTA DE LA MAQUINA DE BUILD: $rel -> $dep"
                fail=1
                ;;
        esac
    done < <(otool -arch arm64 -L "$f" 2>/dev/null | grep -v ':$' | awk '{print $1}' | grep -vxF "${self:-/}")
done < <(find "$BUNDLE" -type f)

echo "Mach-O revisados: $count"
if [ "$fail" -ne 0 ]; then
    echo "ERROR: el bundle de macOS depende de cosas que no lleva (o sin firma)." >&2
    exit 1
fi
echo "OK: el bundle de macOS solo usa lo propio y la base de macOS."
