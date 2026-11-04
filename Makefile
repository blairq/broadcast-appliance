# ============================================================================
# Makefile — compilar, probar, empaquetar y publicar Broadcast Appliance
#
# No compila nada por sí solo: envuelve a CMake/CTest (el build de verdad) y a
# los scripts de tools/. `make` a secas muestra la ayuda.
#
# LOS NOMBRES: un verbo y, si hace falta, la plataforma al final, siempre con la
# misma palabra (mac, linux, windows). Sin plataforma = la de esta máquina.
#
#   make compilar        compila en build/                  (compilar-mac, -linux, -windows)
#   make probar          los tests, con caché                (probar-rapido, probar-todo)
#   make app             el programa armado para usarlo acá  (Mac: la .app en ~25 s)
#   make paquete-mac     el .dmg            ┐
#   make paquete-linux   el .tar.gz         ├ en dist/, con su .sha256 (Linux y Windows
#   make paquete-windows el .zip            ┘ en Docker si esta máquina es una Mac)
#   make paquetes        los tres
#   make publicar        EL QUE HACE TODO: los tres paquetes de cero, con todos los
#                        tests, y los sube a un release de GitHub
#   make deps            lo de terceros para compilar acá   (deps-mac, -linux, -windows)
#
# Los nombres de antes (build, test, win, dist-win, release-macos, dmg-macos...)
# siguen: son los pasos sueltos que usan Docker (tools/ci/compilar-en-docker.sh)
# y el CI (.github/workflows). `make pasos` los lista.
#
# Variables: BUILD_DIR (build), JOBS (los núcleos), SIN_CACHE=1 (todo de cero),
# PERFILAR=1 (build que mide CPU y GPU; los paquetes salen con «-perfil»),
# DIAG_ACOPLES=0 (sin el detector de acoples: lo ponen solos `paquetes` y `publicar`).
# ============================================================================

BUILD_DIR ?= build
# PERFILAR=1: un build que mide CPU por hilo y GPU (diag/Perfil, -DBA_PERFILAR=ON);
# los paquetes salen con «-perfil» en el nombre. Sin eso, OFF explícito: un build
# normal no hereda el modo de medición del caché de CMake.
PERFILAR ?= 0
# DIAG_ACOPLES=0: sin el detector de acoples (src/core/diag/Acoples.hpp). De fábrica
# prendido (desarrollo y pruebas); lo que se publica (paquetes, paquete-*, publicar*)
# sale sin él. Explícito siempre, como PERFILAR: no se hereda del caché de CMake.
DIAG_ACOPLES ?= 1
BA_CMAKE_PERFIL = -DBA_PERFILAR=$(if $(filter 1,$(PERFILAR)),ON,OFF) \
	-DBA_DIAG_ACOPLES=$(if $(filter 0,$(DIAG_ACOPLES)),OFF,ON)
BA_SUFIJO = $(if $(filter 1,$(PERFILAR)),-perfil,)
export PERFILAR
export DIAG_ACOPLES
JOBS      ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
# Hace falta el sha256sum de GNU (coreutils): el que trae macOS 15+ es el de
# BSD y no acepta `-c --quiet`. Si no es GNU, `shasum -a 256`, que usa el
# mismo formato de salida y el mismo -c.
SHA256       := $(shell sha256sum --version 2>/dev/null | grep -q GNU && echo sha256sum || echo "shasum -a 256")
SHA256_CHECK := $(SHA256) -c --quiet
# macOS: ba-modelo (MLX, xcodebuild) pide Xcode completo. Si xcode-select apunta a las Command Line
# Tools pero Xcode esta instalado, se usa ese sin tocar la configuracion del
# sistema (lo mismo que `sudo xcode-select -s`, solo para este build).
ifeq ($(shell uname -s),Darwin)
  ifeq ($(DEVELOPER_DIR),)
    ifneq ($(shell xcodebuild -version >/dev/null 2>&1 && echo si),si)
      ifneq ($(wildcard /Applications/Xcode.app/Contents/Developer),)
        export DEVELOPER_DIR := /Applications/Xcode.app/Contents/Developer
      endif
    endif
  endif
endif
PY        ?= .venv/bin/python
PIP       ?= .venv/bin/pip

BA_VERSION := $(shell awk '/^project\(/,/\)/' CMakeLists.txt | awk '/VERSION/{print $$2; exit}')
# De que commit sale cada archivo de dist/ (su .commit al lado): tools/publicar.sh
# no sube nada que no sea del commit actual. "-dirty" = habia cambios sin commitear.
ESTE_COMMIT = $(shell git rev-parse HEAD 2>/dev/null)$(shell git status --porcelain --untracked-files=no 2>/dev/null | grep -q . && echo -dirty)
DIST_LINUX = dist/BroadcastAppliance-$(BA_VERSION)-linux-x64$(BA_SUFIJO).tar.gz
DIST_WIN   = dist/BroadcastAppliance-$(BA_VERSION)-windows-x64$(BA_SUFIJO).zip
DIST_MACOS = dist/BroadcastAppliance-$(BA_VERSION)-macos-arm64$(BA_SUFIJO).dmg

EXTERNS_DIR ?= extern
DL_DIR      := $(EXTERNS_DIR)/.downloads

# La plataforma de esta máquina (la de `make compilar`, `make app`, `make deps`).
ifeq ($(shell uname -s),Darwin)
  PLATAFORMA := mac
else
  PLATAFORMA := linux
endif

.PHONY: help ayuda pasos compilar compilar-mac compilar-linux compilar-windows probar probar-rapido \
        probar-todo app app-mac app-mac-de-cero app-linux paquete-mac paquete-linux paquete-windows \
        paquetes deps-mac deps-windows limpiar limpiar-todo idiomas documentacion

# --- Ayuda (lo que hace `make` a secas) -----------------------------------------
help: ayuda
ayuda:
	@echo "Broadcast Appliance — make <comando>  (esta máquina: $(PLATAFORMA))"
	@echo ""
	@echo "Todos los días:"
	@echo "  make compilar         compila en $(BUILD_DIR)/ (sin tests)"
	@echo "  make probar           los tests; no repite los que ya pasaron igual"
	@echo "  make probar-rapido    sin los lentos"
	@echo "  make probar-todo      todos, sin caché"
	@echo "  make app              el programa armado para usarlo acá (Mac: dist/BroadcastAppliance.app, ~25 s)"
	@echo "  make app-mac-de-cero  la .app rehecha de cero (si algo se ve raro)"
	@echo ""
	@echo "Para publicar:"
	@echo "  make publicar         TODO: los tres paquetes de cero con todos los tests, y los sube a GitHub"
	@echo "  make paquetes         los tres paquetes en dist/, sin subir"
	@echo "  make paquete-mac      dist/…-macos-arm64.dmg       (en una Mac)"
	@echo "  make paquete-linux    dist/…-linux-x64.tar.gz      (en Docker si esto es una Mac)"
	@echo "  make paquete-windows  dist/…-windows-x64.zip       (en Docker si esto es una Mac)"
	@echo "  make subir            sube lo que haya en dist/ de esta versión (y verifica tamaño y sha256)"
	@echo ""
	@echo "Una vez por máquina (o al cambiar versiones):"
	@echo "  make deps             lo de terceros para compilar acá, en extern/ ($(PLATAFORMA))"
	@echo "  make deps-mac / deps-linux / deps-windows   el de una plataforma"
	@echo ""
	@echo "Otros:"
	@echo "  make idiomas          pone i18n/ba_*.ts al día con el código (los textos nuevos, sin traducir)"
	@echo "  make documentacion    la documentación HTML (docs/_build/html)"
	@echo "  make lint             el formato del código (clang-format)"
	@echo "  make limpiar          borra $(BUILD_DIR)/      make limpiar-todo  también release/ y docs/_build/"
	@echo "  make pasos            los pasos sueltos (los que usan Docker y el CI) y las dependencias una por una"
	@echo ""
	@echo "Variables: JOBS=$(JOBS)  BUILD_DIR=$(BUILD_DIR)  SIN_CACHE=1 (de cero)  PERFILAR=1 (build que mide)"

# Los pasos sueltos: los usan los comandos de arriba, Docker y el CI.
pasos:
	@echo "Pasos sueltos (los usan los comandos principales, Docker y el CI):"
	@echo "  Linux:    build  test  release  dist-linux  dist-linux-docker"
	@echo "  Windows:  win  test-win  release-win  dist-win  dist-win-docker   (desde Linux: BA_ACCEPT_MSVC_LICENSE=1)"
	@echo "  macOS:    macos (compila y prueba)  release-macos  app-macos  app-macos-rapida  dmg-macos  dist-macos  modelo-mac"
	@echo "  Publicar: publicar-macos / publicar-linux / publicar-win (una plataforma, compilando acá)"
	@echo "  Tests:    test  test-rapido  test-completo  pytest"
	@echo ""
	@echo "Dependencias una por una (extern/, idempotente, con sha256):"
	@echo "  deps-src (mbedtls, nlohmann, samplerate, argon2, libdeflate, zxing)  deps-qt  deps-qt-host  deps-webrtc"
	@echo "  deps-gstreamer-macos / -linux / -win  deps-python  deps-lwip  deps-cacert  deps-rostros  deps-sherpa-onnx"
	@echo "  deps-craft  deps-fuentes-texto  deps-cloudflared  deps-pinggy  deps-lsp  deps-clap  deps-llamacpp"
	@echo "  deps-status (qué hay)  deps-clean (borra extern/)"
	@echo ""
	@echo "Modelo local: modelo  modelo-fabricar  modelo-corpus  corpus-entrenamiento  venv-entrenar  bateria-local"
	@echo "MCP: mcp-inventario (docs/mcp.json, con el programa andando)"

# --- Los comandos principales (ver la cabecera) -----------------------------------
# Cada uno llama a los pasos de su plataforma; los pasos son los de siempre.
compilar: compilar-$(PLATAFORMA)
compilar-mac: modelo-mac
	cmake -S . -B $(BUILD_DIR) $(BA_CMAKE_PERFIL) -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
		-DCMAKE_OSX_DEPLOYMENT_TARGET=13.3
	cmake --build $(BUILD_DIR) -j$(JOBS)
compilar-linux: build
compilar-windows:
	@if [ "$$(uname -s)" = Linux ]; then $(MAKE) win; \
	  else echo "Windows se compila desde Linux (o en Docker: make paquete-windows)" >&2; exit 1; fi

probar: test
probar-rapido: test-rapido
probar-todo: test-completo

app: app-$(PLATAFORMA)
app-mac: app-macos-rapida
app-mac-de-cero: app-macos
app-linux: build release

paquete-mac:
	@[ "$$(uname -s)" = Darwin ] || { echo "El .dmg se arma en una Mac" >&2; exit 1; }
	$(MAKE) macos
	$(MAKE) dmg-macos
paquete-linux:
	@if [ "$$(uname -s)" = Linux ]; then $(MAKE) build && $(MAKE) test && $(MAKE) dist-linux; \
	  else $(MAKE) dist-linux-docker; fi
paquete-windows:
	@if [ "$$(uname -s)" = Linux ]; then $(MAKE) test-win BA_ACCEPT_MSVC_LICENSE=1 && $(MAKE) dist-win BA_ACCEPT_MSVC_LICENSE=1; \
	  else $(MAKE) dist-win-docker; fi
# Uno detrás del otro (Docker y la Mac compiten por la máquina). En Linux, sin el .dmg.
paquetes:
	$(MAKE) paquete-linux
	$(MAKE) paquete-windows
	@if [ "$$(uname -s)" = Darwin ]; then $(MAKE) paquete-mac; \
	  else echo "[paquetes] esto no es una Mac: el .dmg se arma en una Mac (make paquete-mac)"; fi

deps-mac: deps-macos
deps-windows: deps-win
limpiar: clean
limpiar-todo: clean-all
idiomas: i18n-actualizar
documentacion: docs

# --- El resto, por tema (mk/) -------------------------------------------------------
include mk/versiones.mk
include mk/deps.mk
include mk/desarrollo.mk
include mk/linux.mk
include mk/mac.mk
include mk/windows.mk
include mk/tablero-web.mk
include mk/modelos.mk
include mk/publicar.mk
