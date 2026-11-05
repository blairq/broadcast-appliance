# ============================================================================
# Makefile — compilar, probar, empaquetar y publicar Broadcast Appliance
#
# No compila nada por sí solo: envuelve a CMake/CTest (el build de verdad) y a
# los scripts de tools/. `make` a secas muestra la ayuda corta; `make pasos`, todo.
#
# LOS NOMBRES: un verbo y, si hace falta, la plataforma al final, siempre con la
# misma palabra (mac, linux, windows). Sin plataforma = la de esta máquina.
#
# ESTE ARCHIVO: las variables comunes, la ayuda y los comandos del día a día. El
# resto, por tema, en mk/ (incluidos al final):
#   mk/versiones.mk    versiones, URLs y SHA-256 de lo de terceros
#   mk/deps.mk         dependencias comunes (make deps y las sueltas)
#   mk/desarrollo.mk   build genérico, tests, idiomas, documentación, lint, limpieza
#   mk/linux.mk        Linux: deps-linux, release/, el .tar.gz
#   mk/mac.mk          macOS: deps-macos, ba-modelo, la .app, el .dmg
#   mk/windows.mk      Windows desde Linux: deps-win, win, test-win, el .zip
#   mk/tablero-web.mk  el tablero web (WebAssembly)
#   mk/modelos.mk      el modelo local (corpus, entrenamiento, fábrica)
#   mk/publicar.mk     Docker, subir y publicar
#
# LO ÓPTIMO, SOLO (nada de flags a mano):
#   - JOBS: bajo el árbitro de los agentes (~/.claude/arbitro/bin/turno) el -j
#     que toca (`turno jobs`); si no, los núcleos. JOBS=n lo fija a mano.
#   - CMake se configura solo si hace falta (tools/ci/preparar-build.sh): si no
#     está configurado o cambió una opción pedida. Si no, cmake --build vuelve a
#     configurar solo cuando cambia un CMakeLists.
#   - En la Mac, siempre Ninja, arm64 y macOS 13.3 (sin eso, un worktree no
#     comparte la caché de ccache con el principal) y ZERO_AR_DATE=1.
#   - En un worktree de git: extern/ y build/modelo-mac, enlaces a los del principal.
#   - ccache: lo pone el CMakeLists si está en el PATH (también en Docker y el CI).
#   - `make test`: compila lo que cambió y corre solo los tests cuya huella cambió.
#
# Variables: BUILD_DIR (build), JOBS, SIN_CACHE=1 (todo de cero), PERFILAR=1
# (build que mide CPU y GPU; los paquetes salen con «-perfil»), DIAG_ACOPLES=0
# (sin el detector de acoples: lo ponen solos `paquetes` y `publicar`).
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
# INCOMPLETO=1: configurar aunque falte una parte del programa (BA_PERMITIR_INCOMPLETO): solo
# la validación nativa arm64 en Docker (lo que no tiene par arm64 queda en la lista de
# faltantes). Explícito siempre: un build no hereda el permiso del caché.
INCOMPLETO ?= 0
BA_CMAKE_PERFIL = -DBA_PERFILAR=$(if $(filter 1,$(PERFILAR)),ON,OFF) \
	-DBA_DIAG_ACOPLES=$(if $(filter 0,$(DIAG_ACOPLES)),OFF,ON) \
	-DBA_PERMITIR_INCOMPLETO=$(if $(filter 1,$(INCOMPLETO)),ON,OFF)
BA_SUFIJO = $(if $(filter 1,$(PERFILAR)),-perfil,)
export PERFILAR
export DIAG_ACOPLES
export INCOMPLETO

# El almacén de símbolos (para leer los volcados: tools/caidas/analizar.py) es UNO por
# máquina: dist/simbolos del árbol principal, también desde un worktree (cada worktree
# con el suyo eran ~16 GB por corrida de Windows). Se llena al publicar (SIMBOLOS=1, lo
# pone `make publicar`); un paquete de prueba que va a la PC de pruebas, con SIMBOLOS=1
# a mano. BA_SIMBOLOS cambia dónde vive. La Mac guarda sus .dSYM siempre (por UUID: lo
# que ya está no se repite).
SIMBOLOS ?= 0
BA_SIMBOLOS ?= $(abspath $(shell git rev-parse --path-format=absolute --git-common-dir 2>/dev/null || echo .git)/..)/dist/simbolos
export SIMBOLOS
export BA_SIMBOLOS

# El -j: el que da el árbitro de los agentes si está (los núcleos repartidos entre
# los que compilan en este momento: solo, la máquina entera), si no los núcleos.
# Una vez por make (`:=`), no en cada receta. JOBS=n (línea o entorno) manda.
TURNO ?= $(wildcard $(HOME)/.claude/arbitro/bin/turno)
ifeq ($(origin JOBS),undefined)
  JOBS := $(or $(if $(TURNO),$(shell $(TURNO) jobs 2>/dev/null)),$(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4))
endif

# Hace falta el sha256sum de GNU (coreutils): el que trae macOS 15+ es el de
# BSD y no acepta `-c --quiet`. Si no es GNU, `shasum -a 256`, que usa el
# mismo formato de salida y el mismo -c.
SHA256       := $(shell sha256sum --version 2>/dev/null | grep -q GNU && echo sha256sum || echo "shasum -a 256")
SHA256_CHECK := $(SHA256) -c --quiet
PY        ?= .venv/bin/python
PIP       ?= .venv/bin/pip

# La plataforma de esta máquina (la de `make compilar`, `make app`, `make deps`).
ifeq ($(shell uname -s),Darwin)
  PLATAFORMA := mac
else
  PLATAFORMA := linux
endif

ifeq ($(PLATAFORMA),mac)
  # ba-modelo (MLX, xcodebuild) pide Xcode completo. Si xcode-select apunta a las Command
  # Line Tools pero Xcode esta instalado, se usa ese sin tocar la configuracion del
  # sistema (lo mismo que `sudo xcode-select -s`, solo para este build).
  ifeq ($(DEVELOPER_DIR),)
    ifneq ($(shell xcodebuild -version >/dev/null 2>&1 && echo si),si)
      ifneq ($(wildcard /Applications/Xcode.app/Contents/Developer),)
        export DEVELOPER_DIR := /Applications/Xcode.app/Contents/Developer
      endif
    endif
  endif
  # Enlaces y archivos .a reproducibles (el ld de Apple anota la fecha de cada .o):
  # el CMakeLists ya lo pone en los enlaces; acá alcanza también a ar/libtool y a
  # lo que se compila fuera de CMake (xcodebuild, las deps).
  export ZERO_AR_DATE := 1
  # Ninja, arm64 y macOS 13.3 en TODO configure de la Mac (el principal y los
  # worktrees con las mismas opciones: comparten la caché de ccache).
  BA_CMAKE_PLATAFORMA := -G Ninja -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.3
else
  BA_CMAKE_PLATAFORMA :=
endif

# Configurar (solo si hace falta) y preparar un worktree: tools/ci/preparar-build.sh.
PREPARAR   = tools/ci/preparar-build.sh
CONFIGURAR = $(PREPARAR) configurar

BA_VERSION := $(shell awk '/^project\(/,/\)/' CMakeLists.txt | awk '/VERSION/{print $$2; exit}')
# De que commit sale cada archivo de dist/ (su .commit al lado): tools/publicar.sh
# no sube nada que no sea del commit actual. "-dirty" = habia cambios sin commitear.
ESTE_COMMIT = $(shell git rev-parse HEAD 2>/dev/null)$(shell git status --porcelain --untracked-files=no 2>/dev/null | grep -q . && echo -dirty)
DIST_LINUX = dist/BroadcastAppliance-$(BA_VERSION)-linux-x64$(BA_SUFIJO).tar.gz
DIST_WIN   = dist/BroadcastAppliance-$(BA_VERSION)-windows-x64$(BA_SUFIJO).zip
DIST_MACOS = dist/BroadcastAppliance-$(BA_VERSION)-macos-arm64$(BA_SUFIJO).dmg

EXTERNS_DIR ?= extern
DL_DIR      := $(EXTERNS_DIR)/.downloads

.PHONY: help ayuda pasos compilar compilar-mac compilar-linux compilar-windows probar probar-rapido \
        probar-todo app app-mac app-mac-de-cero app-linux paquete-mac paquete-linux paquete-windows \
        paquetes deps-mac deps-windows limpiar limpiar-todo idiomas documentacion

# --- Ayuda (lo que hace `make` a secas) -----------------------------------------
help: ayuda
ayuda:
	@echo "Broadcast Appliance — make <comando>  (esta máquina: $(PLATAFORMA), -j$(JOBS))"
	@echo ""
	@echo "  make compilar         compila en $(BUILD_DIR)/ (configura CMake solo si hace falta)"
	@echo "  make app              el programa para usarlo acá (Mac: dist/BroadcastAppliance.app, ~14 s sin cambios)"
	@echo "  make test             compila lo que cambió y corre solo los tests que cambiaron (= make probar)"
	@echo "  make test-completo    todos los tests, sin caché (antes de publicar; = make probar-todo)"
	@echo "  make idiomas          i18n/ba_*.ts al día con el código (los textos nuevos, sin traducir)"
	@echo "  make tablero-web      el tablero web (WebAssembly) en build-tablero-web/"
	@echo "  make paquete-linux    dist/…-linux-x64.tar.gz   ┐ en Docker si esto es una Mac"
	@echo "  make paquete-windows  dist/…-windows-x64.zip    ┘ (tools/ci/compilar-en-docker.sh)"
	@echo "  make publicar         TODO: los tres paquetes de cero, con todos los tests, y los sube a GitHub"
	@echo "  make deps             lo de terceros para compilar acá, en extern/ (una vez por máquina)"
	@echo ""
	@echo "  make pasos            todo lo demás (pasos sueltos, dependencias una por una, modelo)"
	@echo "Variables: JOBS=$(JOBS)  BUILD_DIR=$(BUILD_DIR)  SIN_CACHE=1 (de cero)  PERFILAR=1 (build que mide)"

# Todo lo demás: los pasos que usan los comandos de arriba, Docker y el CI.
pasos:
	@echo "Comandos (Makefile):"
	@echo "  compilar[-mac|-linux|-windows]  probar  probar-rapido  probar-todo  app  app-mac-de-cero  app-linux"
	@echo "  paquete-mac  paquete-linux  paquete-windows  paquetes  publicar  subir"
	@echo "  deps[-mac|-linux|-windows]  idiomas  documentacion  limpiar  limpiar-todo"
	@echo ""
	@echo "Pasos sueltos (los usan los comandos principales, Docker y el CI):"
	@echo "  Linux:    build  test  release  dist-linux  dist-linux-docker                     (mk/linux.mk)"
	@echo "  Windows:  win  test-win  release-win  dist-win  dist-win-docker   (desde Linux: BA_ACCEPT_MSVC_LICENSE=1; mk/windows.mk)"
	@echo "  macOS:    macos (compila y prueba)  release-macos  app-macos  app-macos-rapida  dmg-macos  dist-macos  modelo-mac  (mk/mac.mk)"
	@echo "  Publicar: publicar-macos / publicar-linux / publicar-win (una plataforma, compilando acá)  (mk/publicar.mk)"
	@echo "  Tests:    test  test-rapido  test-completo  test-explicar  pytest  lint  lint-includes  (mk/desarrollo.mk)"
	@echo ""
	@echo "Dependencias una por una (extern/, idempotente, con sha256; mk/deps.mk y las de cada plataforma):"
	@echo "  deps-src (mbedtls, nlohmann, samplerate, argon2, libdeflate, zxing)  deps-qt  deps-qt-host  deps-webrtc"
	@echo "  deps-gstreamer-macos / -linux / -win  deps-python  deps-lwip  deps-cacert  deps-rostros  deps-sherpa-onnx"
	@echo "  deps-craft  deps-fuentes-texto  deps-cloudflared  deps-pinggy  deps-lsp  deps-clap  deps-llamacpp  deps-wasm"
	@echo "  deps-status (qué hay)  deps-clean (borra extern/)"
	@echo ""
	@echo "Modelo local (mk/modelos.mk): modelo  modelo-fabricar  modelo-corpus  corpus-entrenamiento  venv-entrenar  bateria-local"
	@echo "MCP: mcp-inventario (docs/mcp.json, con el programa andando)"

# --- Los comandos principales (ver la cabecera) -----------------------------------
# Cada uno llama a los pasos de su plataforma; los pasos son los de siempre.
compilar: compilar-$(PLATAFORMA)
compilar-mac: modelo-mac _estaticas
	@$(CONFIGURAR) $(BUILD_DIR) $(BA_CMAKE_PERFIL) $(BA_CMAKE_PLATAFORMA) -DCMAKE_BUILD_TYPE=Release
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
