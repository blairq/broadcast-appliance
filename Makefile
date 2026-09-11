# ============================================================================
# Makefile — interfaz de build de Broadcast Appliance Portable v2
#
# Envuelve a CMake/CTest para que el flujo de desarrollo sea simple. Este
# archivo NO compila nada por sí solo: delega todo en CMake (el único sistema
# de build real). ccache queda activado automáticamente vía el bloque guardado
# del CMakeLists raíz, así que cada build nativo o cross cachea las TU.
#
# Flujo típico:
#   make deps     -> descarga los assets de terceros a extern/ (idempotente)
#   make dev      -> build de desarrollo (Debug) en build/
#   make prod     -> build de producción (Release) + bundle portable
#   make docs     -> genera la documentación (Sphinx/HTML)
#   make test     -> corre ctest (tests unitarios de ba_core)
#   make help     -> muestra estos targets
#
# Variables (sobrescribibles desde la línea de comando):
#   BUILD_DIR  -> directorio de build (default: build)
#   JOBS       -> paralelismo (default: nproc)
#   BA_WEBRTC_TARGET -> artefacto de libwebrtc a bajar: windows (default) | linux
#
# Compatible con make POSIX (no usa extensiones GNU de make).
# ============================================================================

BUILD_DIR ?= build
JOBS      ?= $(nproc)
PY        ?= .venv/bin/python
PIP       ?= .venv/bin/pip

.PHONY: help dev build prod test test-unit lint docs release win clean clean-all \
        deps deps-obs deps-webrtc deps-spout deps-cloudflared deps-vcredist \
        deps-lsp deps-status deps-clean

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
	@echo "Dependencias de terceros (extern/, gitignored, idempotente):"
	@echo "  make deps             Descarga TODO (obs+webrtc+spout+cloudflared+vcredist+lsp)"
	@echo "  make deps-obs         Runtime OBS 31.0.0 (libobs+CEF+Qt) -> extern/obs-studio"
	@echo "  make deps-webrtc      SDK libwebrtc M123 -> extern/webrtc (BA_WEBRTC_TARGET=windows|linux)"
	@echo "  make deps-spout       SDK Spout2 (2.007h) -> extern/Spout2"
	@echo "  make deps-cloudflared cloudflared 2024.11.0 -> extern/cloudflared"
	@echo "  make deps-vcredist    Redist VC++ -> extern/vcredist"
	@echo "  make deps-lsp         Bundles .vst3 de LSP 1.2.35 -> extern/lsp"
	@echo "  make deps-status      Muestra qué assets hay y su estado"
	@echo "  make deps-clean       Borra extern/"
	@echo ""
	@echo "Variables: BUILD_DIR?=$(BUILD_DIR)  JOBS?=$(JOBS)  BA_WEBRTC_TARGET?=windows"

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

# ============================================================================
# Dependencias de terceros (extern/) — descarga reproducible e idempotente
#
# `make deps` descarga TODOS los assets de terceros que el build/deploy
# necesitan, los verifica por SHA-256 y los extrae en `extern/` (gitignored:
# nunca se commitea). Es IDEMPOTENTE: si el artefacto ya está y su hash
# coincide, se salta la descarga (y la extracción si ya está hecha).
#
# Las VERSIONES se leen de versions.cmake (única fuente de pins). Los
# identificadores de descarga EXACTOS (tag de release, nombre de archivo) y
# los SHA-256 viven aquí porque son metadatos de descarga, no "pins".
#
#   make deps             -> todo (obs + webrtc + spout + cloudflared + vcredist + lsp)
#   make deps-obs         -> runtime OBS 31.0.0 (libobs.dll + CEF + Qt) a extern/obs-studio
#   make deps-webrtc      -> SDK libwebrtc M123 a extern/webrtc (= BA_WEBRTC_ROOT)
#   make deps-spout       -> SDK Spout2 (tag real 2.007h) a extern/Spout2
#   make deps-cloudflared -> cloudflared 2024.11.0 a extern/cloudflared
#   make deps-vcredist    -> redist VC++ a extern/vcredist (msvcp140_atomic_wait.dll)
#   make deps-lsp         -> bundles .vst3 de LSP 1.2.35 a extern/lsp
#   make deps-status      -> muestra qué assets hay y su estado
#   make deps-clean       -> borra extern/
#
# BA_WEBRTC_TARGET: `windows` (default, webrtc.lib) o `linux` (libwebrtc.a).
# El build enlaza libwebrtc solo en Windows (if(WIN32) en src/gateway), por lo
# que el artefacto por defecto es el de Windows.
# ============================================================================

EXTERNS_DIR ?= extern
DL_DIR      := $(EXTERNS_DIR)/.downloads

# Artefacto de libwebrtc a bajar: windows (default) o linux.
BA_WEBRTC_TARGET ?= windows

# --- Versiones (sincronizadas con versions.cmake) ---------------------------
# Se leen de versions.cmake para que el pin sea la única fuente. Si la línea
# no se encuentra, se cae a un default conocido (robustez ante reorden).
OBS_TAG        := $(shell awk -F'"' '/OBS_STUDIO_TAG/{print $$2; exit}' versions.cmake 2>/dev/null)
OBS_TAG        ?= 31.0.0
CLOUDFLARED_VER:= $(shell awk -F'"' '/CLOUDFLARED_VERSION/{print $$2; exit}' versions.cmake 2>/dev/null)
CLOUDFLARED_VER?= 2024.11.0
LSP_VER        := $(shell awk -F'"' '/LSP_VERSION/{print $$2; exit}' versions.cmake 2>/dev/null)
LSP_VER        ?= 1.2.35

# --- Identificadores de descarga exactos (metadatos, no pins) ---------------
# libwebrtc: el tag de Shiguredo que corresponde a WEBRTC_MILESTONE=M123 /
# WEBRTC_VERSION=123.0.6312.5 (versions.cmake). NO es derivable por sustitución
# (Shiguredo usa m<maj>.<build>.<x>.<y>; aquí 6312.3.5), así que se fija aquí.
WEBRTC_TAG := m123.6312.3.5
# Spout2: el pin nominal SPOUT2_VERSION=2.0.0.0 (versions.cmake) NO es un tag
# real del repo leadedge/Spout2; el tag estable más reciente es 2.007h.
SPOUT2_TAG := 2.007h

# --- URLs + SHA-256 por asset -----------------------------------------------
# OBS 31.0.0 (Windows): runtime portable (bin/64bit + obs-plugins/64bit con
# libcef.dll/chrome_elf.dll + data/libobs). Es la raíz que consume tools/deploy
# vía BA_DEPLOY_OBS. Trae también las DLLs de Qt del runtime.
OBS_URL  := https://github.com/obsproject/obs-studio/releases/download/$(OBS_TAG)/OBS-Studio-$(OBS_TAG)-Windows.zip
OBS_SHA  := 0ea3f71bd4d8a611ef763d2591aaf12b61e6f563284a3a14ddff485621fe4c41
OBS_ZIP  := $(DL_DIR)/OBS-Studio-$(OBS_TAG)-Windows.zip
OBS_DEST := $(EXTERNS_DIR)/obs-studio

# libwebrtc M123 (Shiguredo). El zip/tar trae un top-dir `webrtc/` con
# include/ + lib/; se extrae sobre extern/ -> extern/webrtc/{include,lib}.
ifeq ($(BA_WEBRTC_TARGET),linux)
  WEBRTC_URL := https://github.com/shiguredo-webrtc-build/webrtc-build/releases/download/$(WEBRTC_TAG)/webrtc.ubuntu-22.04_x86_64.tar.gz
  WEBRTC_SHA := 5858e94f14d11da0612b24bd139664281db8e227885587ef0d2ca614fb9d693
  WEBARC     := $(DL_DIR)/webrtc.ubuntu-22.04_x86_64.tar.gz
  WEB_KIND   := tar
  WEB_LIB    := libwebrtc.a
else
  WEBRTC_URL := https://github.com/shiguredo-webrtc-build/webrtc-build/releases/download/$(WEBRTC_TAG)/webrtc.windows_x86_64.zip
  WEBRTC_SHA := a56f41f40fb97e7016e5b2d0e0bf0394bcfc3a0833f5de2984eece8951f101c0
  WEBARC     := $(DL_DIR)/webrtc.windows_x86_64.zip
  WEB_KIND   := zip
  WEB_LIB    := webrtc.lib
endif
WEB_DEST := $(EXTERNS_DIR)/webrtc

# Spout2 (leadedge/Spout2, tag 2.007h): SDK completo (headers SPOUTSDK/ +
# lib estáticas BUILD/Binaries/x64/). Top-dir `Spout2-2.007h/` -> se strippea.
SPOUT_URL  := https://codeload.github.com/leadedge/Spout2/tar.gz/refs/tags/$(SPOUT2_TAG)
SPOUT_SHA  := 9cad3bbfed3f13b154e92a32eaff429236402080d720551bb491757a9febec28
SPOUT_TAR  := $(DL_DIR)/Spout2-$(SPOUT2_TAG).tar.gz
SPOUT_DEST := $(EXTERNS_DIR)/Spout2

# cloudflared 2024.11.0 (Windows, el que shippea el appliance en bin/64bit/).
# Binario plano (sin extraer): se descarga directo a su destino final.
CFL_URL  := https://github.com/cloudflare/cloudflared/releases/download/$(CLOUDFLARED_VER)/cloudflared-windows-amd64.exe
CFL_SHA  := f287dc99f9abe8f49510c78270b13fbb7a3fa0e22e53d1e061455a4d82901298
CFL_EXE  := $(EXTERNS_DIR)/cloudflared/cloudflared.exe

# Redist VC++ (evergreen aka.ms): aporta msvcp140_atomic_wait.dll (requerido
# por std::atomic::wait de C++20). Es un objetivo MOVIENTE (sin tag), por lo
# que NO se fija su hash: solo se asegura su presencia.
VCREDIST_URL := https://aka.ms/vs/17/release/vc_redist.x64.exe
VCREDIST_EXE := $(EXTERNS_DIR)/vcredist/VC_redist.x64.exe

# LSP 1.2.35 (asset de despliegue, no se compila): bundles .vst3 a extern/lsp/
# (el paso de deploy los copia a assets/audio-plugins/). 7z de Linux x86_64.
LSP_URL  := https://github.com/lsp-plugins/lsp-plugins/releases/download/$(LSP_VER)/lsp-plugins-$(LSP_VER)-Linux-x86_64.7z
LSP_SHA  := 4ef3b2f63b29a522fdaf3a18f879c1c85c82f0bdd1e0b3c849a5f3c4b08d2550
LSP_7Z   := $(DL_DIR)/lsp-plugins-$(LSP_VER)-Linux-x86_64.7z
LSP_DEST := $(EXTERNS_DIR)/lsp

# Cada target enlaza el bloque idempotente de descarga (presente+sha -> skip;
# si no, curl a <archivo>.part, mv y verificacion `sha256sum -c`). Se repite
# por asset a proposito: es mas robusto y depurable que un macro define/call/
# eval (este ultimo, dentro de una receta, se interpreta como contenido de
# makefile y rompe el parseo).

# --- deps (agregado) ---------------------------------------------------------
deps: deps-obs deps-webrtc deps-spout deps-cloudflared deps-vcredist deps-lsp

deps-obs:
	@command -v curl >/dev/null 2>&1 || { echo "[deps] ERROR: falta curl" >&2; exit 1; }
	@mkdir -p "$(DL_DIR)" "$(OBS_DEST)"
	@if [ -f "$(OBS_ZIP)" ] && [ "$$(sha256sum "$(OBS_ZIP)" | cut -d' ' -f1)" = "$(OBS_SHA)" ]; then \
	  echo "[deps] OBS: ya presente y verificado ($(OBS_ZIP))"; \
	else \
	  echo "[deps] OBS: descargando $(OBS_URL)"; \
	  curl -fL --retry 3 --connect-timeout 30 -o "$(OBS_ZIP).part" "$(OBS_URL)" \
	    && mv "$(OBS_ZIP).part" "$(OBS_ZIP)" \
	    && printf '%s  %s\n' "$(OBS_SHA)" "$(OBS_ZIP)" | sha256sum -c --quiet \
	    || { echo "[deps] ERROR OBS: fallo de descarga o hash" >&2; rm -f "$(OBS_ZIP).part"; exit 1; }; \
	fi
	@if [ -f "$(OBS_DEST)/obs-plugins/64bit/libcef.dll" ]; then \
	  echo "[deps] OBS: ya extraído en $(OBS_DEST)"; \
	else \
	  echo "[deps] OBS: extrayendo a $(OBS_DEST)"; \
	  unzip -q -o "$(OBS_ZIP)" -d "$(OBS_DEST)"; \
	fi

deps-webrtc:
	@command -v curl >/dev/null 2>&1 || { echo "[deps] ERROR: falta curl" >&2; exit 1; }
	@mkdir -p "$(DL_DIR)" "$(EXTERNS_DIR)"
	@if [ -f "$(WEBARC)" ] && [ "$$(sha256sum "$(WEBARC)" | cut -d' ' -f1)" = "$(WEBRTC_SHA)" ]; then \
	  echo "[deps] libwebrtc-$(BA_WEBRTC_TARGET): ya presente y verificado ($(WEBARC))"; \
	else \
	  echo "[deps] libwebrtc-$(BA_WEBRTC_TARGET): descargando $(WEBRTC_URL)"; \
	  curl -fL --retry 3 --connect-timeout 30 -o "$(WEBARC).part" "$(WEBRTC_URL)" \
	    && mv "$(WEBARC).part" "$(WEBARC)" \
	    && printf '%s  %s\n' "$(WEBRTC_SHA)" "$(WEBARC)" | sha256sum -c --quiet \
	    || { echo "[deps] ERROR libwebrtc: fallo de descarga o hash" >&2; rm -f "$(WEBARC).part"; exit 1; }; \
	fi
	@if [ -f "$(WEB_DEST)/lib/$(WEB_LIB)" ]; then \
	  echo "[deps] libwebrtc: ya extraído en $(WEB_DEST) ($(WEB_LIB))"; \
	else \
	  echo "[deps] libwebrtc: extrayendo a $(WEB_DEST)"; \
	  rm -rf "$(WEB_DEST)"; \
	  if [ "$(WEB_KIND)" = "tar" ]; then tar xzf "$(WEBARC)" -C "$(EXTERNS_DIR)"; \
	  else unzip -q -o "$(WEBARC)" -d "$(EXTERNS_DIR)"; fi; \
	fi

deps-spout:
	@command -v curl >/dev/null 2>&1 || { echo "[deps] ERROR: falta curl" >&2; exit 1; }
	@mkdir -p "$(DL_DIR)" "$(EXTERNS_DIR)"
	@if [ -f "$(SPOUT_TAR)" ] && [ "$$(sha256sum "$(SPOUT_TAR)" | cut -d' ' -f1)" = "$(SPOUT_SHA)" ]; then \
	  echo "[deps] Spout2: ya presente y verificado ($(SPOUT_TAR))"; \
	else \
	  echo "[deps] Spout2: descargando $(SPOUT_URL)"; \
	  curl -fL --retry 3 --connect-timeout 30 -o "$(SPOUT_TAR).part" "$(SPOUT_URL)" \
	    && mv "$(SPOUT_TAR).part" "$(SPOUT_TAR)" \
	    && printf '%s  %s\n' "$(SPOUT_SHA)" "$(SPOUT_TAR)" | sha256sum -c --quiet \
	    || { echo "[deps] ERROR Spout2: fallo de descarga o hash" >&2; rm -f "$(SPOUT_TAR).part"; exit 1; }; \
	fi
	@if [ -f "$(SPOUT_DEST)/SPOUTSDK/SpoutDirectX/SpoutDX/SpoutDX.h" ]; then \
	  echo "[deps] Spout2: ya extraído en $(SPOUT_DEST)"; \
	else \
	  echo "[deps] Spout2: extrayendo a $(SPOUT_DEST)"; \
	  rm -rf "$(SPOUT_DEST)"; \
	  mkdir -p "$(SPOUT_DEST)"; \
	  tar xzf "$(SPOUT_TAR)" -C "$(SPOUT_DEST)" --strip-components=1; \
	fi

deps-cloudflared:
	@command -v curl >/dev/null 2>&1 || { echo "[deps] ERROR: falta curl" >&2; exit 1; }
	@mkdir -p "$(dir $(CFL_EXE))"
	@if [ -f "$(CFL_EXE)" ] && [ "$$(sha256sum "$(CFL_EXE)" | cut -d' ' -f1)" = "$(CFL_SHA)" ]; then \
	  echo "[deps] cloudflared: ya presente y verificado ($(CFL_EXE))"; \
	else \
	  echo "[deps] cloudflared: descargando $(CFL_URL)"; \
	  curl -fL --retry 3 --connect-timeout 30 -o "$(CFL_EXE).part" "$(CFL_URL)" \
	    && mv "$(CFL_EXE).part" "$(CFL_EXE)" \
	    && printf '%s  %s\n' "$(CFL_SHA)" "$(CFL_EXE)" | sha256sum -c --quiet \
	    || { echo "[deps] ERROR cloudflared: fallo de descarga o hash" >&2; rm -f "$(CFL_EXE).part"; exit 1; }; \
	fi

deps-vcredist:
	@command -v curl >/dev/null 2>&1 || { echo "[deps] ERROR: falta curl" >&2; exit 1; }
	@mkdir -p "$(dir $(VCREDIST_EXE))"
	@if [ -f "$(VCREDIST_EXE)" ]; then \
	  echo "[deps] VC++ redist: ya presente ($(VCREDIST_EXE))"; \
	else \
	  echo "[deps] VC++ redist: descargando $(VCREDIST_URL) (evergreen, sin pin de hash)"; \
	  curl -fL --retry 3 --connect-timeout 30 -o "$(VCREDIST_EXE).part" "$(VCREDIST_URL)" \
	    && mv "$(VCREDIST_EXE).part" "$(VCREDIST_EXE)" \
	    || { echo "[deps] ERROR vcredist: fallo de descarga" >&2; rm -f "$(VCREDIST_EXE).part"; exit 1; }; \
	fi

deps-lsp:
	@command -v curl >/dev/null 2>&1 || { echo "[deps] ERROR: falta curl" >&2; exit 1; }
	@command -v 7z >/dev/null 2>&1 || { echo "[deps] ERROR: falta 7z (p7zip) para LSP" >&2; exit 1; }
	@mkdir -p "$(DL_DIR)" "$(LSP_DEST)"
	@if [ -f "$(LSP_7Z)" ] && [ "$$(sha256sum "$(LSP_7Z)" | cut -d' ' -f1)" = "$(LSP_SHA)" ]; then \
	  echo "[deps] LSP: ya presente y verificado ($(LSP_7Z))"; \
	else \
	  echo "[deps] LSP: descargando $(LSP_URL)"; \
	  curl -fL --retry 3 --connect-timeout 30 -o "$(LSP_7Z).part" "$(LSP_URL)" \
	    && mv "$(LSP_7Z).part" "$(LSP_7Z)" \
	    && printf '%s  %s\n' "$(LSP_SHA)" "$(LSP_7Z)" | sha256sum -c --quiet \
	    || { echo "[deps] ERROR LSP: fallo de descarga o hash" >&2; rm -f "$(LSP_7Z).part"; exit 1; }; \
	fi
	@if [ -f "$(LSP_DEST)/.deps-lsp-$(LSP_VER)" ]; then \
	  echo "[deps] LSP: ya extraído en $(LSP_DEST)"; \
	else \
	  echo "[deps] LSP: extrayendo a $(LSP_DEST)"; \
	  7z x -y -o"$(LSP_DEST)" "$(LSP_7Z)" >/dev/null \
	    && touch "$(LSP_DEST)/.deps-lsp-$(LSP_VER)"; \
	fi

# --- Estado / limpieza de dependencias --------------------------------------
deps-status:
	@echo "Assets de terceros en $(EXTERNS_DIR)/:"
	@echo "  OBS $(OBS_TAG)          : $$( [ -f '$(OBS_DEST)/obs-plugins/64bit/libcef.dll' ] && echo OK || echo ausente )"
	@echo "  libwebrtc M123 ($(BA_WEBRTC_TARGET)): $$( [ -f '$(WEB_DEST)/lib/$(WEB_LIB)' ] && echo OK || echo ausente )"
	@echo "  Spout2 $(SPOUT2_TAG)          : $$( [ -f '$(SPOUT_DEST)/SPOUTSDK/SpoutDirectX/SpoutDX/SpoutDX.h' ] && echo OK || echo ausente )"
	@echo "  cloudflared $(CLOUDFLARED_VER)     : $$( [ -f '$(CFL_EXE)' ] && echo OK || echo ausente )"
	@echo "  VC++ redist           : $$( [ -f '$(VCREDIST_EXE)' ] && echo OK || echo ausente )"
	@echo "  LSP $(LSP_VER)           : $$( [ -f '$(LSP_DEST)/.deps-lsp-$(LSP_VER)' ] && echo OK || echo ausente )"

deps-clean:
	@echo "[deps] borrando $(EXTERNS_DIR)/"
	@rm -rf "$(EXTERNS_DIR)"

# --- Limpieza ----------------------------------------------------------------
clean:
	rm -rf $(BUILD_DIR)

clean-all:
	rm -rf $(BUILD_DIR) release docs/_build
