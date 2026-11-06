#!/usr/bin/env bash
# ============================================================================
# tools/ci/receta-docker.sh — lo que corre ADENTRO del contenedor de
# tools/ci/compilar-en-docker.sh, ya en el checkout del commit (/trabajo/ba)
#
# Linux:   deps-linux → compilar (Release) → ctest → pytest → release → .tar.gz
# Windows: deps-win → win → tests bajo Wine → release-win → .zip → símbolos
#
# Con todo al día no se hace nada: cada paso mira sus entradas y dice qué salteó y por
# qué («[atajo]», «[configurar]», «[tests-cache]», «[pytest-cache]», «[instalar]»). Lo
# que queda es lo que no se puede saltear sin mirar: ninja/make comprueban que no haya
# nada que compilar, la caché de tests calcula sus claves, cmake --install mira las fechas.
#
# Variables (las pone compilar-en-docker.sh): SHA QUE CORRIDA PERFILAR SIN_CACHE T0
# PYTEST_ARGS (argumentos de más para pytest, p. ej. un --deselect para medir).
# VALIDAR=1 (la validación nativa arm64): compilar y probar, sin paquetes (ni .tar.gz, ni .zip,
# ni release/), con INCOMPLETO=1 (lo que no tiene par arm64 queda en la lista de faltantes).
# ETAPA=tests-win: solo los tests de Windows bajo Wine (el contenedor x86_64 de la validación
# arm64, sobre lo que compiló el arm64); en un host arm64 la receta de Windows no los corre.
# ============================================================================
set -euo pipefail
cd /trabajo/ba
T0="${T0:-$(date +%s)}"; TE="${TE:-$T0}"; RESUMEN="${RESUMEN:-}"
# Cuánto tarda cada etapa (al final, un resumen): para saber dónde se va el tiempo.
etapa() { local t; t=$(date +%s); RESUMEN="$RESUMEN$(printf "  %-38s %4d s" "$1" $((t - TE)))\n"; TE=$t; }
resumen() {
    printf "\n[tiempos] total %d s\n%b" $(( $(date +%s) - T0 )) "$RESUMEN"
    ccache -s 2>/dev/null | grep -iE "hits|misses|cache size" | sed "s/^/  ccache: /" || true
}
trap resumen EXIT
# Sin volcados de memoria: bajo Wine sobre Rosetta, un test que se cae dejaba un «core» de
# ~500 MB en su carpeta (build/win/src/gst-plugins), que es entrada de 24 tests: la caché los
# veía cambiados y los volvía a correr en cada corrida (y releía el volcado entero). Los
# viejos, fuera (un archivo «core» en el build no es otra cosa).
ulimit -c 0
find build -name core -type f -size +1M -delete 2>/dev/null || true
etapa "checkout"

J=$(nproc)
# Sin símbolos que guardar (SIMBOLOS=1 lo pone `make publicar`), sin información de depuración:
# solo se valida, y con ella el build de Linux ocupaba 31 GB y el de Windows 17 GB del disco de
# colima (y tardaba más en compilar y enlazar). Lo que se publica sale con ella.
[ "${SIMBOLOS:-0}" = 1 ] || export DEPURACION=0
VALIDAR="${VALIDAR:-0}"
ETAPA="${ETAPA:-todo}"
[ "$VALIDAR" = 1 ] && echo "[compilar-en-docker] VALIDAR=1 ($(uname -m)): compilar + tests, sin paquetes; INCOMPLETO=${INCOMPLETO:-0}"
# Solo los paquetes de ESTA versión: el volumen guarda los de versiones viejas y el
# comodín sin versión los volvía a copiar a dist/.
VER=$(sed -n "/^project(/,/)/s/.*VERSION \([0-9.]*\).*/\1/p" CMakeLists.txt | head -1)
SUF=""; [ "$PERFILAR" = 1 ] && SUF="-perfil"   # PERFILAR=1: un build que mide (diag/Perfil)
# SONDAS=1: el build de diagnóstico de los cuadros (src/core/diag/SondasCuadros.hpp), en su propia
# carpeta (build/win-sondas, release-win-sondas: mk/windows.mk) y con su .zip (…-sondas.zip).
export SONDAS="${SONDAS:-0}"
WINB=build/win; WINR=release-win
[ "$SONDAS" = 1 ] && SUF="$SUF-sondas" && WINB=build/win-sondas && WINR=release-win-sondas
# TESTS_WIN=0: sin los tests de Windows bajo Wine (un paquete para probar a mano en una PC).
TESTS_WIN="${TESTS_WIN:-1}"
[ "$SIN_CACHE" != 0 ] && echo "[compilar-en-docker] SIN_CACHE=1: todo de cero (sin caché de tests ni atajos)"
# A dist/ de la Mac: solo si cambió (el .zip son 800 MB por el volumen compartido).
# El .commit, siempre: el mismo contenido es lo que sale de este commit.
llevar() {
    local f="$1" n; n="$(basename "$1")"
    if [ -f "/salida/$n" ] && cmp -s "$f.sha256" "/salida/$n.sha256" 2>/dev/null; then
        echo "[atajo] copia de $n a dist/: salteada (mismo sha256 que el que ya está)"
    else
        cp "$f" "$f.sha256" /salida/
    fi
    cp "$f.commit" /salida/
}
# Los registros de lo que falló, a dist/logs/<corrida>/ de la Mac (en el volumen los pisa
# la próxima corrida).
guardar_registros() {
    mkdir -p "/salida/logs/$CORRIDA"
    find "$@" -maxdepth 1 -type f -newer /tmp/inicio-de-etapa -exec cp {} "/salida/logs/$CORRIDA/" \; 2>/dev/null || true
    echo "[compilar-en-docker] registros de lo que falló: dist/logs/$CORRIDA/"
}

# Compilar solo si cambió algo: el árbol del commit (todo el código, los CMakeLists, el
# Makefile), lo que hay en extern/ y las opciones. Si son los mismos que la última vez que
# compiló bien, el build es el mismo: ni se le pregunta a make/ninja (bajo Rosetta, el
# «nada que compilar» de make sobre build/ son ~40 s). Uso: compilar_si_cambio <nombre>
# <carpeta del build> <argumentos de make...>
compilar_si_cambio() {
    local nombre="$1" dir="$2"; shift 2
    mkdir -p "$dir/atajos"
    python3 tools/ci/huellas.py atajo --nombre "$nombre" --clave-en "$dir/atajos/compilado.json" \
        --texto "árbol $(git rev-parse "$SHA^{tree}")" --texto "make $* DIAG_ACOPLES=${DIAG_ACOPLES:-} DEPURACION=${DEPURACION:-1}" --centinelas extern \
        --salida "$dir/CMakeCache.txt" -- make "$@"
}

# El venv de las pruebas de Python: pip solo si cambió el lock (o Python). Antes que ctest:
# los test_py_* (cmake/BaTestsPy.cmake) corren con este pytest.
[ -x .venv/bin/python ] || python3 -m venv .venv
python3 tools/ci/huellas.py atajo --nombre "pip install (requirements-dev.lock)" --clave-en .venv/.atajo-pip.json \
    --entrada requirements-dev.lock --texto "$(.venv/bin/python --version 2>&1)" --salida .venv/bin/pytest -- \
    .venv/bin/pip install -q -r requirements-dev.lock
etapa "venv (pip install)"

# Solo los tests de Windows bajo Wine (validación arm64: el contenedor x86_64).
if [ "$ETAPA" = tests-win ]; then
    tests_win=0
    touch /tmp/inicio-de-etapa
    make -o deps-win -o win test-win BA_ACCEPT_MSVC_LICENSE=1 PERFILAR=$PERFILAR || tests_win=$?
    if [ "$tests_win" != 0 ]; then
        mkdir -p "/salida/logs/$CORRIDA"
        awk '$1 != "0" && $1 != "77" { print $4 }' build/win/wine-logs/.resultados 2>/dev/null |
            while read -r r; do cp "$r" "/salida/logs/$CORRIDA/"; done
        echo "[compilar-en-docker] registros de lo que falló: dist/logs/$CORRIDA/"
    fi
    etapa "tests bajo Wine (x86_64, con caché)"
    [ "$tests_win" = 0 ] || exit 3
    exit 0
fi

if [ "$QUE" != win ]; then
    make deps-linux JOBS=$J
    etapa "deps-linux"
    # Configurado en Release y compilado UNA vez: ctest y dist-linux no lo repiten (-o): cada
    # «nada que compilar» de make bajo Rosetta son ~40 s. Y si el árbol del commit es el
    # mismo que el de la última compilación que anduvo, ni eso.
    compilar_si_cambio "compilar (linux)" build compilado-release JOBS=$J PERFILAR=$PERFILAR
    etapa "compilar (linux)"
    touch /tmp/inicio-de-etapa
    make -o _al-dia test || { guardar_registros build/Testing/Temporary; exit 1; }
    etapa "tests (ctest, con caché)"
    # pytest por archivo y con caché, sin lo que ya corrió ctest: los test_py_* y cada
    # binario de test (test_cpp_tests.py los vuelve a correr de a uno, sin el entorno de
    # ctest: 10 minutos bajo Rosetta para nada).
    touch /tmp/inicio-de-etapa
    # shellcheck disable=SC2086
    .venv/bin/python -B tools/ci/pytest-con-cache.py --build build --ctest -- \
        --deselect tests/test_cpp_tests.py::test_cpp_binary_runs_clean ${PYTEST_ARGS:-} \
        || { guardar_registros build/.cache-pytest/registros; exit 1; }
    etapa "pytest (con caché)"
fi
if [ "$QUE" != win ] && [ "$VALIDAR" != 1 ]; then
    make -o compilado-release dist-linux JOBS=$J PERFILAR=$PERFILAR
    etapa "dist-linux (instalar, strip, manifest, tar)"
    for f in dist/*-$VER-linux-x64$SUF.tar.gz; do llevar "$f"; done
    etapa "copia a dist/ (linux)"
    # Los .debug de este build (los saca del bundle simbolos-linux.sh, en `release`), al
    # almacén único de la Mac: solo con SIMBOLOS=1 (publicar). Lo que ya está no se copia.
    if [ "${SIMBOLOS:-0}" = 1 ]; then
        mkdir -p /simbolos && cp -rn dist/simbolos/linux /simbolos/ 2>/dev/null || true
        etapa "símbolos (linux)"
    else
        echo "[símbolos] Linux: no se guardan (SIMBOLOS=1 para guardarlos; make publicar lo pone)"
    fi
    # Y no se juntan en el volumen: dist/simbolos/linux de adentro crecía con los .debug de
    # cada build (16 GB el 2026-11-05, con el disco de colima lleno). Los de esta corrida ya
    # fueron (o no hacían falta); la próxima instalación saca los suyos de nuevo.
    rm -rf dist/simbolos/linux
fi
if [ "$QUE" != linux ]; then
    make deps-win JOBS=$J BA_ACCEPT_MSVC_LICENSE=1
    etapa "deps-win"
    compilar_si_cambio "compilar (win)" "$WINB" -o deps-win win JOBS=$J BA_ACCEPT_MSVC_LICENSE=1 PERFILAR=$PERFILAR SONDAS=$SONDAS
    etapa "compilar (win)"
    if [ "$VALIDAR" = 1 ]; then
        # Los tests bajo Wine, en el contenedor x86_64 (compilar-en-docker.sh, ETAPA=tests-win).
        echo "[compilar-en-docker] Windows compilado en $(uname -m); los tests van bajo Wine en x86_64"
        exit 0
    fi
    # Los tests de Windows corren bajo Wine. En una Mac, Wine va encima de
    # Rosetta y se cae de vez en cuando (rc 133) en tests que en el runner del
    # CI pasan: cada corrida, uno distinto. Por eso un fallo aca NO frena el
    # .zip, pero se informa y la corrida termina con error: no es verde.
    tests_win=0
    touch /tmp/inicio-de-etapa
    if [ "$TESTS_WIN" = 0 ]; then
        echo "[compilar-en-docker] TESTS_WIN=0: sin los tests de Windows bajo Wine"
    else
        make -o deps-win -o win test-win BA_ACCEPT_MSVC_LICENSE=1 PERFILAR=$PERFILAR || tests_win=$?
    fi
    if [ "$tests_win" != 0 ]; then
        # Solo los de los que fallaron (rc ni 0 ni 77; «rc segundos nombre registro exe»).
        mkdir -p "/salida/logs/$CORRIDA"
        awk '$1 != "0" && $1 != "77" { print $4 }' "$WINB/wine-logs/.resultados" 2>/dev/null |
            while read -r r; do cp "$r" "/salida/logs/$CORRIDA/"; done
        echo "[compilar-en-docker] registros de lo que falló: dist/logs/$CORRIDA/"
    fi
    etapa "tests bajo Wine (con caché)"
    make -o deps-win -o win release-win JOBS=$J BA_ACCEPT_MSVC_LICENSE=1 PERFILAR=$PERFILAR
    etapa "release-win (instalar, chequeo, manifest)"
    make -o release-win dist-win JOBS=$J BA_ACCEPT_MSVC_LICENSE=1 PERFILAR=$PERFILAR
    etapa "zip (7z)"
    for f in dist/*-$VER-windows-x64$SUF.zip; do llevar "$f"; done
    etapa "copia a dist/ (win)"
    # Los binarios y los .pdb de este build, al almacén único de la Mac (BA_SIMBOLOS,
    # montado en /simbolos): para leer después las pilas de sus volcados
    # (tools/caidas/analizar.py). Solo con SIMBOLOS=1 (publicar, o un paquete que va a
    # la PC de pruebas): cada build suma ~250 MB de lo nuestro. Si el bundle es el mismo
    # que la última vez, ya están.
    if [ "${SIMBOLOS:-0}" = 1 ]; then
        python3 tools/ci/huellas.py atajo --nombre "símbolos de Windows" --clave-en "$WINB/atajos/simbolos.json" \
            --memo "$WINB/atajos/huellas.json" --entrada "$WINR" --salida /simbolos/compilaciones.jsonl -- \
            python3 tools/caidas/simbolos.py guardar --release "$WINR" --buscar "$WINB" extern/gstreamer-win-pdb \
            --destino /simbolos --commit "$SHA" || echo "ATENCION: no se guardaron los símbolos" >&2
        etapa "símbolos (win)"
    else
        echo "[símbolos] Windows: no se guardan (SIMBOLOS=1 para guardarlos; make publicar lo pone)"
    fi
    if [ "$tests_win" != 0 ]; then
        echo "ATENCION: los tests de Windows (bajo Wine) fallaron: ver arriba. El .zip se armo igual." >&2
        exit 3
    fi
fi
