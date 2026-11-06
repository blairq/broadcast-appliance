# mk/desarrollo.mk — Desarrollo: build genérico, idiomas, documentación, tests, lint y limpieza.
# Lo incluye el Makefile de la raíz (las variables comunes están allá).

.PHONY: build _enlaces _estaticas _configurado _al-dia i18n-actualizar docs test test-rapido test-completo test-explicar pytest \
        lint lint-includes clean clean-all

# --- Build genérico ---------------------------------------------------------
# Configura solo si hace falta (tools/ci/preparar-build.sh) y compila. Sin
# CMAKE_BUILD_TYPE explícito: CMake usa su default (Release, fijado en el
# CMakeLists raíz). En la Mac, con Ninja, arm64 y 13.3 (BA_CMAKE_PLATAFORMA).
build: _enlaces _estaticas
	@$(CONFIGURAR) $(BUILD_DIR) $(BA_CMAKE_PERFIL) $(BA_CMAKE_PLATAFORMA)
	cmake --build $(BUILD_DIR) -j$(JOBS)

# $(BUILD_DIR) listo para compilar SIN tocar sus opciones (lo usan `make app` y `make
# test`: no pasan un build de publicar a desarrollo ni al revés). Si no está
# configurado: en un worktree, los enlaces al principal; en la Mac, ba-modelo
# (el CMakeLists lo exige) y la configuración de `make compilar`.
_configurado: _enlaces _estaticas
	@[ "$(PLATAFORMA)" != mac ] || [ -f "$(BUILD_DIR)/CMakeCache.txt" ] || $(MAKE) modelo-mac
	@$(CONFIGURAR) --si-falta $(BUILD_DIR) $(BA_CMAKE_PERFIL) $(BA_CMAKE_PLATAFORMA) -DCMAKE_BUILD_TYPE=Release
	@# Las sondas de los cuadros (SONDAS=1) sí se siguen: un build de diagnóstico no tiene que
	@# quedar con ellas sin querer (ni al revés). Cambiarlas recompila casi todo.
	@grep -q "^BA_SONDAS_CUADROS:BOOL=$(if $(filter 1,$(SONDAS)),ON,OFF)$$" $(BUILD_DIR)/CMakeCache.txt || \
	  $(CONFIGURAR) $(BUILD_DIR) -DBA_SONDAS_CUADROS=$(if $(filter 1,$(SONDAS)),ON,OFF)

# Las estáticas de terceros de esta plataforma (cmake/estaticas): si ya están con la misma
# huella, nada (una cuenta, ~0,1 s); si no (otra configuración de mbedtls, una versión
# nueva), se compilan una vez en extern/estaticas, compartidas con los worktrees. Sin las
# fuentes en extern/ (make deps-src) frena con el aviso de CMake.
_estaticas: _enlaces
	@tools/deps/estaticas.sh $(if $(filter mac,$(PLATAFORMA)),macos,linux) $(JOBS)

# En un worktree: extern/ y build/modelo-mac, enlaces a los del principal.
_enlaces:
	@$(PREPARAR) enlaces

# Lo que se prueba es lo compilado: antes de los tests, el build incremental.
_al-dia: _configurado
	cmake --build $(BUILD_DIR) -j$(JOBS)

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
# Primero compila lo que cambió (_al-dia): se prueba lo compilado, nunca otra cosa.
# Con caché por contenido (tools/ci/tests-con-cache.py): un test que ya pasó con
# exactamente el mismo ejecutable, las mismas bibliotecas, los mismos datos y el
# mismo entorno se informa «pasó (caché)» sin correrlo. `ctest` a secas sigue
# corriendo todo. TESTS_SIN_CACHE=1 (o SIN_CACHE=1) corre todo igual;
# TESTS_SIN_LENTOS=1 deja afuera los `lento` (cmake/BaTestsCache.cmake).
# Antes de correrlos dice por qué corre cada uno (qué entrada cambió); `make test-explicar`
# lo dice sin correr nada.
# Explicar la clave de uno: python3 tools/ci/tests-con-cache.py --test-dir build --explicar test_x
# En paralelo (-j): bajo carga algún test del motor puede fallar por tiempo (no se
# anota, y la próxima corre solo); TESTS_JOBS=1 los corre en serie.
TESTS_JOBS ?= $(JOBS)
TESTS_CACHE = python3 tools/ci/tests-con-cache.py --test-dir $(BUILD_DIR) -- --output-on-failure -j$(TESTS_JOBS)
test: _al-dia
	$(TESTS_CACHE)

test-rapido: _al-dia
	TESTS_SIN_LENTOS=1 $(TESTS_CACHE)

test-completo: _al-dia
	TESTS_SIN_CACHE=1 $(TESTS_CACHE)

# Sin correr nada: qué tests correría `make test` y por qué (qué entrada cambió desde la
# última vez que pasaron). Para auditar que no corra nada de más. Compila antes, como
# `make test`: si no, explicaría contra los ejecutables viejos.
test-explicar: _al-dia
	python3 tools/ci/tests-con-cache.py --test-dir $(BUILD_DIR) --explicar-todo

# --- Tests Python (pytest) --------------------------------------------------
# Suite Python en tests/ (22 tests), corriendo con el pytest del venv local
# (.venv). Usa `$(PY) -m pytest` (mismo patrón que `docs` con `$(PY) -m
# sphinx`) para no depender del entrypoint binario. No requiere build C++:
# los tests Python son independientes de $(BUILD_DIR).
pytest:
	$(PY) -m pytest tests/ -v

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
