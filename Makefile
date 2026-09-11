# ============================================================================
# Makefile — interfaz de build de Broadcast Appliance Portable v2
#
# Envuelve a CMake/CTest para que el flujo de desarrollo sea simple. Este
# archivo NO compila nada por sí solo: delega todo en CMake (el único sistema
# de build real). ccache queda activado automáticamente vía el bloque guardado
# del CMakeLists raíz, así que cada build nativo o cross cachea las TU.
#
# Flujo típico:
#   make dev        -> build de desarrollo (Debug) en build/
#   make prod       -> build de producción (Release) + bundle portable
#   make docs       -> genera la documentación (Sphinx/HTML)
#   make test       -> corre ctest (tests unitarios de ba_core)
#   make help       -> muestra estos targets
#
# Variables (sobrescribibles desde la línea de comando):
#   BUILD_DIR  -> directorio de build (default: build)
#   JOBS       -> paralelismo (default: nproc)
#
# Compatible con make POSIX (no usa extensiones GNU de make).
# ============================================================================

BUILD_DIR ?= build
JOBS      ?= $(nproc)
PY        ?= .venv/bin/python
PIP       ?= .venv/bin/pip

.PHONY: help dev build prod test test-unit lint docs release win clean clean-all

# --- Ayuda (target por defecto) ---------------------------------------------
help:
	@echo "Broadcast Appliance Portable v2 — targets de build:"
	@echo ""
	@echo "  make dev        Build de desarrollo (Debug) en $(BUILD_DIR)/"
	@echo "  make build      Configura y compila (build type por defecto de CMake)"
	@echo "  make prod       Build de producción (Release) + bundle portable en release"
	@echo "  make docs       Genera la documentación (Sphinx -> docs/_build/html)"
	@echo "  make test       Corre ctest (tests unitarios de ba_core)"
	@echo "  make test-unit  Alias de 'make test'"
	@echo "  make lint       Revisa el formato con clang-format del sistema (apt)"
	@echo "  make release    Instala el bundle portable (layout §5.1) en release/"
	@echo "  make win        Cross-compile MinGW-w64 a build/win (si hay cross-compiler)"
	@echo "  make clean      Borra $(BUILD_DIR)/"
	@echo "  make clean-all  Borra $(BUILD_DIR)/, release/ y docs/_build/"
	@echo ""
	@echo "Variables: BUILD_DIR?=$(BUILD_DIR)  JOBS?=$(JOBS)"

# --- Desarrollo (Debug) -----------------------------------------------------
# Build de desarrollo: configuración Debug (debug info, sin optimizar) para
# depurar y correr los tests. ccache se activa solo vía CMakeLists.
dev:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(BUILD_DIR) -j$(JOBS)

# --- Build genérico ---------------------------------------------------------
# Configura (idempotente) y compila. Sin CMAKE_BUILD_TYPE explícito: CMake usa
# su default (Release, fijado en el CMakeLists raíz).
build:
	cmake -S . -B $(BUILD_DIR)
	cmake --build $(BUILD_DIR) -j$(JOBS)

# --- Producción (Release + bundle) ------------------------------------------
# Artefacto portable: Release + instalación del layout §5.1 en release.
prod:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) -j$(JOBS)
	cmake --install $(BUILD_DIR) --prefix release
	@echo "Bundle portable: release/"

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
test:
	ctest --test-dir $(BUILD_DIR) --output-on-failure

test-unit:
	ctest --test-dir $(BUILD_DIR) --output-on-failure

# --- Lint (formato) ---------------------------------------------------------
# Usa el clang-format de la distribucion (una herramienta de build, no un
# asset a descargar): `sudo apt install clang-format` en Ubuntu 24.04 da
# 18.1.3, la misma version que instala el job `lint` de CI (por apt, con
# runs-on fijado a ubuntu-24.04 para que coincida con la de dev). La version
# esperada queda documentada en versions.cmake (CLANG_FORMAT_VERSION) y CI la
# aserta. NO se descarga nada. Si clang-format no esta presente, el target
# FALA (no se salta en verde): un gate que se omite en silencio no es gate.
CF := $(shell command -v clang-format 2>/dev/null || command -v clang-format-18 2>/dev/null)

# `src/obs-module/obs/` es VENDOR (subconjunto fiel de la API C de libobs
# 31.0.0): se excluye para que siga fiel al upstream y no se reformatee con
# nuestro .clang-format.
lint:
	@if [ -z "$(CF)" ]; then \
		echo "ERROR: clang-format no encontrado. Instalarlo con: sudo apt install clang-format" >&2; \
		exit 1; \
	fi
	@echo "Lint: $(CF) sobre src/ (dry-run, -Werror; excluye src/obs-module/obs/ vendored)..."
	@find src \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) ! -path 'src/obs-module/obs/*' -print0 \
		| xargs -0 -r $(CF) --dry-run -Werror

# --- Bundle portable (layout §5.1) ------------------------------------------
# Instala el esqueleto del layout portable sobre release (bin/64bit,
# obs-plugins, data/libobs, Config, Logs, Recordings, Web/guest).
release:
	cmake --install $(BUILD_DIR) --prefix release

# --- Cross-compile MinGW-w64 (Windows desde Linux) --------------------------
# Solo si el cross-compiler existe; si no, avisa y termina en verde. El
# cross-build va a build/win (dentro del root de build, aislado por subdirectorio).
win:
	@if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then \
		cmake -S . -B build/win \
			-DCMAKE_SYSTEM_NAME=Windows \
			-DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
			-DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
			-DCMAKE_BUILD_TYPE=Release; \
		cmake --build build/win -j$(JOBS); \
	else \
		echo "MinGW-w64 (x86_64-w64-mingw32-g++) no encontrado; se salta el target win."; \
	fi

# --- Limpieza ----------------------------------------------------------------
clean:
	rm -rf $(BUILD_DIR)

clean-all:
	rm -rf $(BUILD_DIR) release docs/_build
