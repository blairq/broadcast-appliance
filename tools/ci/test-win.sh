#!/usr/bin/env bash
# ============================================================================
# tools/ci/test-win.sh — corre los tests de Windows (build/win) bajo Wine
#
# Wine portable (build de Kron4ek, fijado por hash) en extern/toolchain/wine:
# no se instala nada en el sistema. Corre cada test_*.exe de los dos arboles
# de Windows (el /MD y el sub-build /MT) con el PATH que tendria en el bundle
# (Qt, GStreamer y el CRT de MSVC). Los tests de la parte /MT usan el CRT de
# Microsoft enlazado estatico, asi que tambien atrapan diferencias del CRT
# real que el ucrtbase propio de Wine taparia.
#
# Con caché por contenido (tools/ci/tests-con-cache.py): un test que ya pasó
# con el mismo .exe, las mismas DLL (el cierre de importaciones), los mismos
# datos, el mismo entorno y el mismo Wine no se vuelve a correr («pasó
# (caché)»); si no queda ninguno por correr, ni se arranca Wine.
# TESTS_SIN_CACHE=1 (o SIN_CACHE=1): todos. TESTS_SIN_LENTOS=1: sin los `lento`.
#
# Uso: tools/ci/test-win.sh [--solo-wine] [build/win]
# ============================================================================
set -uo pipefail
# macOS no trae sha256sum (shasum -a 256 da el mismo formato).
command -v sha256sum >/dev/null 2>&1 || sha256sum() { shasum -a 256 "$@"; }

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
# --solo-wine: solo deja Wine listo en extern/toolchain/wine (lo usa
# tools/deps/gst-python-win.sh para correr el Python de Windows).
SOLO_WINE=0
[ "${1:-}" = --solo-wine ] && { SOLO_WINE=1; shift; mkdir -p "$ROOT/build/win"; }
B="$(cd "${1:-$ROOT/build/win}" && pwd)"
TC="$ROOT/extern/toolchain"
DL="$ROOT/extern/.downloads"

WINE_VER=11.17
# wow64: todo de 64 bits (los programas de 32 bits van por el WoW64 de Wine),
# sin cargador ni bibliotecas i386 en el host (los runners de GitHub no las traen).
WINE_NAME="wine-${WINE_VER}-amd64-wow64"
WINE_URL="https://github.com/Kron4ek/Wine-Builds/releases/download/${WINE_VER}/${WINE_NAME}.tar.xz"
WINE_SHA=3211db09086bc6fbd769c0286f27790b32d6fc6fb0bc610dad6b59796f07d462

if [ "$(cat "$TC/wine/.version" 2>/dev/null)" != "$WINE_NAME" ]; then
    mkdir -p "$DL" "$TC"
    f="$DL/${WINE_NAME}.tar.xz"
    if ! [ -f "$f" ] || [ "$(sha256sum "$f" | cut -d' ' -f1)" != "$WINE_SHA" ]; then
        curl -fL --retry 3 -o "$f.part" "$WINE_URL" || exit 1
        [ "$(sha256sum "$f.part" | cut -d' ' -f1)" = "$WINE_SHA" ] || { echo "ERROR: hash de Wine" >&2; exit 1; }
        mv "$f.part" "$f"
    fi
    rm -rf "$TC/wine" && mkdir -p "$TC/wine"
    tar xJf "$f" -C "$TC/wine" --strip-components=1 --no-same-owner
    rm -rf "$B/wineprefix" # un prefijo de otra variante de Wine no sirve
    echo "$WINE_NAME" >"$TC/wine/.version"
fi
[ "$SOLO_WINE" = 1 ] && exit 0

export WINEPREFIX="$B/wineprefix" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" DISPLAY=
# Como ctest (cmake/BaTestsCache.cmake): los tests esperan los textos en el original, no en
# el idioma de la máquina (sin esto, test_mcp leía «isn't open» en vez de «no esta abierto»).
export BA_IDIOMA=es
export WINEPATH="$ROOT/extern/qt6/6.6.3/msvc2019_64/bin;$ROOT/extern/gstreamer-win/bin;$ROOT/extern/onnxruntime-win/lib;$ROOT/extern/python-win;$ROOT/extern/msvc/redist"
# Los tests de audio cargan el LSP de Windows (tools/deps/lsp-win.sh) por ruta Windows.
if [ -f "$ROOT/extern/lsp/windows-x86_64/lsp-plugins.clap" ]; then
    export BA_LSP_CLAP="Z:${ROOT//\//\\}\\extern\\lsp\\windows-x86_64\\lsp-plugins.clap"
fi
W="$TC/wine/bin/wine"
mkdir -p "$B/wine-logs"
RES="$B/wine-logs/.resultados"
PEND="$B/wine-logs/.pendientes"
rm -f "$RES" && : >"$RES"

# Qué hay que correr: lo que no está en la caché.
find "$B/src" "$B/win-mt/src" -name 'test_*.exe' 2>/dev/null | sort |
    python3 "$ROOT/tools/ci/tests-con-cache.py" wine-preparar --build "$B" --herramienta "$WINE_NAME" \
        --ruta "$WINEPATH" --pendientes "$PEND" || exit 1

# ¿El test dijo «<test>: OK» al terminar? (ver el reintento de abajo)
dijo_ok() { tail -n 3 "$2" 2>/dev/null | grep -q "^$1: OK"; }
# ¿Se cayó por la aserción AVX de Rosetta? (ver el reintento de abajo)
rosetta_avx() { grep -q "x86_avx_state_ptr->xsave_header" "$1" 2>/dev/null; }

if [ -s "$PEND" ]; then
timeout 300 "$W" wineboot -u >/dev/null 2>&1

# En paralelo (como ctest -j en la Mac: los tests ya se bancan correr a la vez). En
# serie eran 13 minutos de 17 en un build de Windows sin nada que compilar: los del
# motor son pipelines en tiempo real que, bajo Wine sobre Rosetta, esperan más de lo
# que calculan. TESTS_WIN_JOBS lo fija (1 = en serie, para buscar un test que molesta).
JOBS="${TESTS_WIN_JOBS:-$(nproc)}"
uno() {
    local t="$1" n tree log rc t0
    n="$(basename "$t" .exe)"
    tree="$(basename "$(dirname "$(dirname "$t")")")"
    log="$B/wine-logs/$tree-$n.log"
    t0=$(date +%s)
    (cd "$(dirname "$t")" && timeout 180 "$W" "$t" >"$log" 2>&1)
    rc=$?
    echo "$rc $(( $(date +%s) - t0 )) $n $log $t" >>"$RES"
}
export -f uno
export B W RES
xargs -P "$JOBS" -I{} bash -c 'uno "$1"' _ {} <"$PEND"
# rc=133 (128+5): el proceso wine murió por SIGTRAP: lo mató el emulador (Wine sobre
# Rosetta, con la máquina llena de tests a la vez), no el test (un test que se cae bajo Wine
# sale con el código de la excepción de Windows, no con una señal). Se reintentan de a uno.
# Igual los que dijeron «<test>: OK» y aun así salieron con otro código: el programa
# terminó bien y algo se rompió al cerrarse el proceso bajo Wine. Y los que se cayeron por
# la aserción de Rosetta al volver de una señal con estado AVX («assertion failed
# [x86_avx_state_ptr->xsave_header…] (ThreadContextSignals.cpp … rt_sigreturn)»): es del
# emulador y cae en un test distinto cada vez (test_mcp, test_ipstack, test_sse_epoch el
# 2026-11-04), en el proceso del test o en uno que lanza.
: >"$B/wine-logs/.reintentar"
: >"$RES.tmp"
while read -r rc seg n log t; do
    if [ "$rc" = 133 ] || { [ "$rc" != 0 ] && [ "$rc" != 77 ] && { dijo_ok "$n" "$log" || rosetta_avx "$log"; }; }; then
        echo "$t" >>"$B/wine-logs/.reintentar"
    else
        echo "$rc $seg $n $log $t" >>"$RES.tmp"
    fi
done <"$RES"
mv "$RES.tmp" "$RES"
while read -r t; do uno "$t"; done <"$B/wine-logs/.reintentar"
"$TC/wine/bin/wineserver" -k 2>/dev/null
# Los que pasaron (o se saltearon), a la caché.
python3 "$ROOT/tools/ci/tests-con-cache.py" wine-guardar --build "$B" --resultados "$RES"
fi

pass=0
skip=0
failed=()
entorno=()
while read -r rc seg n log _; do
    case $rc in
        0) pass=$((pass + 1)) ;;
        77) skip=$((skip + 1)) ;;
        133) entorno+=("$n"); echo "---- $n: rc=133 dos veces (SIGTRAP del emulador: Wine sobre Rosetta; del entorno, no del test)" ;;
        *) if dijo_ok "$n" "$log"; then
               entorno+=("$n"); echo "---- $n: dijo OK y Wine devolvió rc=$rc dos veces (al cerrarse el proceso; confirmar en Windows)"
               continue
           fi
           if rosetta_avx "$log"; then
               entorno+=("$n"); echo "---- $n: rc=$rc dos veces por la aserción AVX de Rosetta (del emulador; confirmar en Windows)"
               continue
           fi
           failed+=("$n($rc)"); echo "---- $n (rc=$rc, ${seg} s)"; grep -E "FALLO|FAIL" "$log" | head -5 ;;
    esac
done < <(sort -k3 "$RES")
if [ -s "$RES" ]; then
    echo "Los más lentos bajo Wine:"
    sort -k2 -rn "$RES" | head -8 | awk '{printf "  %4d s  %s\n", $2, $3}'
fi
sin_correr=$(( $(wc -l <"$B/wine-logs/.claves") - $(wc -l <"$PEND") ))
echo "Windows (Wine): ${pass} ok, ${skip} saltados, ${#failed[@]} fallidos ${failed[*]:-}, ${#entorno[@]} del entorno (Wine) ${entorno[*]:-} (${sin_correr} sin correr: caché o lentos afuera)"
[ "${#failed[@]}" -eq 0 ]
