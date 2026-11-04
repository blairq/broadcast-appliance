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
etapa "checkout"

J=$(nproc)
# Solo los paquetes de ESTA versión: el volumen guarda los de versiones viejas y el
# comodín sin versión los volvía a copiar a dist/.
VER=$(sed -n "/^project(/,/)/s/.*VERSION \([0-9.]*\).*/\1/p" CMakeLists.txt | head -1)
SUF=""; [ "$PERFILAR" = 1 ] && SUF="-perfil"   # PERFILAR=1: un build que mide (diag/Perfil)
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
        --texto "árbol $(git rev-parse "$SHA^{tree}")" --texto "make $* DIAG_ACOPLES=${DIAG_ACOPLES:-}" --centinelas extern \
        --salida "$dir/CMakeCache.txt" -- make "$@"
}

# El venv de las pruebas de Python: pip solo si cambió el lock (o Python). Antes que ctest:
# los test_py_* (cmake/BaTestsPy.cmake) corren con este pytest.
[ -x .venv/bin/python ] || python3 -m venv .venv
python3 tools/ci/huellas.py atajo --nombre "pip install (requirements-dev.lock)" --clave-en .venv/.atajo-pip.json \
    --entrada requirements-dev.lock --texto "$(.venv/bin/python --version 2>&1)" --salida .venv/bin/pytest -- \
    .venv/bin/pip install -q -r requirements-dev.lock
etapa "venv (pip install)"

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
    make -o compilado-release dist-linux JOBS=$J PERFILAR=$PERFILAR
    etapa "dist-linux (instalar, strip, manifest, tar)"
    for f in dist/*-$VER-linux-x64$SUF.tar.gz; do llevar "$f"; done
    mkdir -p /salida/simbolos && cp -rn dist/simbolos/linux /salida/simbolos/ 2>/dev/null || true
    etapa "copia a dist/ (linux)"
fi
if [ "$QUE" != linux ]; then
    make deps-win JOBS=$J BA_ACCEPT_MSVC_LICENSE=1
    etapa "deps-win"
    compilar_si_cambio "compilar (win)" build/win -o deps-win win JOBS=$J BA_ACCEPT_MSVC_LICENSE=1 PERFILAR=$PERFILAR
    etapa "compilar (win)"
    # Los tests de Windows corren bajo Wine. En una Mac, Wine va encima de
    # Rosetta y se cae de vez en cuando (rc 133) en tests que en el runner del
    # CI pasan: cada corrida, uno distinto. Por eso un fallo aca NO frena el
    # .zip, pero se informa y la corrida termina con error: no es verde.
    tests_win=0
    touch /tmp/inicio-de-etapa
    make -o deps-win -o win test-win BA_ACCEPT_MSVC_LICENSE=1 PERFILAR=$PERFILAR || tests_win=$?
    if [ "$tests_win" != 0 ]; then
        # Solo los de los que fallaron (rc ni 0 ni 77; «rc segundos nombre registro exe»).
        mkdir -p "/salida/logs/$CORRIDA"
        awk '$1 != "0" && $1 != "77" { print $4 }' build/win/wine-logs/.resultados 2>/dev/null |
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
    # Los binarios y los .pdb de este build, al almacén de dist/simbolos (en la
    # Mac): para leer después las pilas de sus volcados (tools/caidas/analizar.py).
    # Si el bundle es el mismo que la última vez, ya están.
    python3 tools/ci/huellas.py atajo --nombre "símbolos de Windows" --clave-en build/win/atajos/simbolos.json \
        --memo build/win/atajos/huellas.json --entrada release-win --salida /salida/simbolos/compilaciones.jsonl -- \
        python3 tools/caidas/simbolos.py guardar --release release-win --buscar build/win extern/gstreamer-win-pdb \
        --destino /salida/simbolos --commit "$SHA" || echo "ATENCION: no se guardaron los símbolos" >&2
    etapa "símbolos (win)"
    if [ "$tests_win" != 0 ]; then
        echo "ATENCION: los tests de Windows (bajo Wine) fallaron: ver arriba. El .zip se armo igual." >&2
        exit 3
    fi
fi
