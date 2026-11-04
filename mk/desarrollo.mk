# mk/desarrollo.mk — Desarrollo: build genérico, idiomas, documentación, tests, lint y limpieza.
# Lo incluye el Makefile de la raíz (las variables comunes están allá).

.PHONY: dev build prod i18n-actualizar docs test test-unit test-rapido test-completo pytest test-all \
        lint lint-includes clean clean-all

# --- Desarrollo (Debug) -----------------------------------------------------
# Build de desarrollo: configuración Debug (debug info, sin optimizar) para
# depurar y correr los tests. ccache se activa solo vía CMakeLists.
dev:
	cmake -S . -B $(BUILD_DIR) $(BA_CMAKE_PERFIL) -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(BUILD_DIR) -j$(JOBS)

# --- Build genérico ---------------------------------------------------------
# Configura (idempotente) y compila. Sin CMAKE_BUILD_TYPE explícito: CMake usa
# su default (Release, fijado en el CMakeLists raíz).
build:
	cmake -S . -B $(BUILD_DIR) $(BA_CMAKE_PERFIL)
	cmake --build $(BUILD_DIR) -j$(JOBS)

# --- Producción (Release + bundle) ------------------------------------------
# Artefacto portable: Release + instalación del layout §5.1 en release.
prod:
	cmake -S . -B $(BUILD_DIR) $(BA_CMAKE_PERFIL) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) -j$(JOBS)
	cmake --install $(BUILD_DIR) --prefix release --component bundle
	@echo "Bundle portable: release/"

# --- Idiomas (docs/desarrollo/I18N.md) ---------------------------------------
# Pone i18n/ba_*.ts al día con el código (lupdate de extern/qt6-host): los textos
# nuevos quedan sin traducir, los que ya no están se van. Las compilaciones no lo
# necesitan: leen los .ts con tools/i18n/ts-a-json.py.
.PHONY: i18n-actualizar
i18n-actualizar:
	python3 -I tools/i18n/actualizar.py

# --- Documentación (Sphinx / ReadTheDocs) -----------------------------------
# Usa un venv local (.venv) para no instalar nada global. Instala sphinx +
# sphinx-rtd-theme desde docs/requirements.txt y genera el sitio HTML.
# (Doxygen es opcional: el Doxyfile de la raíz alimenta breathe si se corre.)
docs:
	@test -x $(PY) || python3 -m venv .venv
	$(PIP) install -q -r docs/requirements.txt
	$(PY) -m sphinx -b html docs docs/_build/html
	@echo "Docs HTML: docs/_build/html/index.html"

# --- Tests ------------------------------------------------------------------
# Con caché por contenido (tools/ci/tests-con-cache.py): un test que ya pasó con
# exactamente el mismo ejecutable, las mismas bibliotecas, los mismos datos y el
# mismo entorno se informa «pasó (caché)» sin correrlo. `ctest` a secas sigue
# corriendo todo. TESTS_SIN_CACHE=1 (o SIN_CACHE=1) corre todo igual;
# TESTS_SIN_LENTOS=1 deja afuera los `lento` (cmake/BaTestsCache.cmake).
# Explicar la clave de uno: python3 tools/ci/tests-con-cache.py --test-dir build --explicar test_x
# En paralelo (-j): bajo carga algún test del motor puede fallar por tiempo (no se
# anota, y la próxima corre solo); TESTS_JOBS=1 los corre en serie.
TESTS_JOBS ?= $(JOBS)
TESTS_CACHE = python3 tools/ci/tests-con-cache.py --test-dir $(BUILD_DIR) -- --output-on-failure -j$(TESTS_JOBS)
test:
	$(TESTS_CACHE)

test-unit: test

test-rapido:
	TESTS_SIN_LENTOS=1 $(TESTS_CACHE)

test-completo:
	TESTS_SIN_CACHE=1 $(TESTS_CACHE)

# --- Tests Python (pytest) --------------------------------------------------
# Suite Python en tests/ (22 tests), corriendo con el pytest del venv local
# (.venv). Usa `$(PY) -m pytest` (mismo patrón que `docs` con `$(PY) -m
# sphinx`) para no depender del entrypoint binario. No requiere build C++:
# los tests Python son independientes de $(BUILD_DIR).
pytest:
	$(PY) -m pytest tests/ -v

# --- Suite completa (C++ + Python) ------------------------------------------
# "Todo de una vez": build de desarrollo + tests C++ (ctest) + tests Python
# (pytest). Cada paso se invoca vía $(MAKE) para que la cadena sea estrictamente
# secuencial aunque se corra con -j (dev antes que test, test antes que pytest).
test-all:
	$(MAKE) dev
	$(MAKE) test
	$(MAKE) pytest

# --- Lint (formato) ---------------------------------------------------------
# Usa el clang-format de la distribucion (una herramienta de build, no un
# asset a descargar): `sudo apt install clang-format` en Ubuntu 24.04 da
# 18.1.3, la misma version que instala el job `lint` de CI (por apt, con
# runs-on fijado a ubuntu-24.04 para que coincida con la de dev). La version
# esperada queda documentada en versions.cmake (CLANG_FORMAT_VERSION) y CI la
# aserta. NO se descarga nada. Si clang-format no esta presente, el target
# FALA (no se salta en verde): un gate que se omite en silencio no es gate.
CF := $(shell command -v clang-format 2>/dev/null || command -v clang-format-18 2>/dev/null)

# Sin exclusiones: todo `src/` es codigo propio y se lintea entero.
lint:
	@if [ -z "$(CF)" ]; then \
		echo "ERROR: clang-format no encontrado. Instalarlo con: sudo apt install clang-format" >&2; \
		exit 1; \
	fi
	@echo "Lint: $(CF) sobre src/ (dry-run, -Werror)..."
	@find src \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -print0 \
		| xargs -0 -r $(CF) --dry-run -Werror

# misc-include-cleaner (.clang-tidy) sobre los archivos de src/ que cambiaron respecto de
# master (o ARCHIVOS="a.cpp b.hpp"; LINT_BASE=otra-rama). Con el clang-tidy vendoreado o
# CLANG_TIDY=/ruta: sin ninguno, falla y lo dice. Ver tools/diag/lint-includes.py.
LINT_BASE ?= master
.PHONY: lint-includes
lint-includes:
	python3 -B tools/diag/lint-includes.py --build $(BUILD_DIR) --base $(LINT_BASE) $(ARCHIVOS)

# --- Limpieza ----------------------------------------------------------------
clean:
	rm -rf $(BUILD_DIR)

clean-all:
	rm -rf $(BUILD_DIR) release docs/_build
