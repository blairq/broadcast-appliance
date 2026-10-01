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
JOBS      ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
# Hace falta el sha256sum de GNU (coreutils): el que trae macOS 15+ es el de
# BSD y no acepta `-c --quiet`. Si no es GNU, `shasum -a 256`, que usa el
# mismo formato de salida y el mismo -c.
SHA256       := $(shell sha256sum --version 2>/dev/null | grep -q GNU && echo sha256sum || echo "shasum -a 256")
SHA256_CHECK := $(SHA256) -c --quiet
# macOS: libobs pide Xcode completo. Si xcode-select apunta a las Command Line
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

.PHONY: modelo-mac deps-mlx-swift-lm publicar subir dist-linux-docker dist-win-docker publicar-macos publicar-linux publicar-win app-macos dmg-macos check-macos-tools deps-macos deps-obs-src deps-obs-macos deps-lsp-macos macos release-macos dist-macos deps-lsp-win deps-clap deps-obs-deps-win deps-ndi-src deps-linux test-win deps-win deps-win-toolchain deps-webrtc-win release-win dist-win dist-linux deps-lwip deps-cacert help dev build prod test test-unit pytest test-all lint docs release win deps-cloudflared-host deps-qt-host \
        clean clean-all \
        deps deps-obs deps-qt deps-webrtc deps-spout deps-cloudflared deps-vcredist \
        deps-lsp deps-status deps-clean deps-libreoffice deps-libreoffice-linux deps-libreoffice-win deps-libreoffice-mac \
        deps-msitools mcp-inventario deps-llamacpp bateria-local corpus-entrenamiento \
        venv-entrenar vector-control venv-mlx deps-llamacpp-convertir modelo modelo-fabricar \
        modelo-corpus modelo-inventario modelo-etiquetas \
        corpus-descubrir \
        deps-src deps-mbedtls deps-nlohmann deps-samplerate deps-argon2 deps-rostros _ba_baja

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
	@echo "  make win        Windows x64 desde Linux (clang-cl, ABI MSVC): baja todo y compila en build/win"
	@echo "                  (requiere BA_ACCEPT_MSVC_LICENSE=1 la primera vez: CRT + SDK de Microsoft)"
	@echo "  make release-win  Bundle portable de Windows en release-win/"
	@echo "  make dist-win   dist/BroadcastAppliance-<ver>-windows-x64.zip"
	@echo "  make test-win   Corre los tests de Windows bajo Wine portable"
	@echo "  make dist-linux dist/BroadcastAppliance-<ver>-linux-x64.tar.gz"
	@echo "  make deps-macos     Todo lo del build de macOS (en una Mac Apple Silicon)"
	@echo "  make macos          macOS: compila y corre los tests"
	@echo "  make dmg-macos      dist/BroadcastAppliance-<ver>-macos-arm64.dmg (la .app)"
	@echo "  make publicar       TODO: Linux y Windows (Docker) + .dmg (en Mac) y los sube a GitHub"
	@echo "  make dist-linux-docker / dist-win-docker  Un bundle en Docker (con su .sha256)"
	@echo "  make subir          Sube lo que haya en dist/ y verifica tamaño y sha256 en GitHub"
	@echo "  make publicar-macos / publicar-linux / publicar-win  Una plataforma, compilando aca"
	@echo "  make deps-linux Todo lo del build completo de Linux (libobs desde fuente, Qt, libwebrtc...)"
	@echo "  make clean      Borra $(BUILD_DIR)/"
	@echo "  make clean-all  Borra $(BUILD_DIR)/, release/ y docs/_build/"
	@echo ""
	@echo "Dependencias de terceros (extern/, gitignored, idempotente):"
	@echo "  make deps             Descarga TODO (runtime + fuentes de build)"
	@echo "  make deps-src         Fuentes de BUILD (mbedtls+nlohmann+samplerate+argon2) -> extern/"
	@echo "  make deps-obs         Runtime OBS 31.0.0 (libobs+CEF+Qt) -> extern/obs-studio"
	@echo "  make deps-qt          SDK Qt 6.6.3 win64_msvc2019_64 (build de Windows) -> extern/qt6"
	@echo "  make deps-win         Todo lo del build de Windows (toolchain, Qt, OBS, libwebrtc...)"
	@echo "  make deps-webrtc      SDK libwebrtc M123 -> extern/webrtc (BA_WEBRTC_TARGET=windows|linux)"
	@echo "  make deps-lwip        lwIP 2.2.0 -> extern/lwip (tunel WireGuard del gateway)"
	@echo "  make deps-cacert      raices de Mozilla (curl.se) -> extern/cacert (HTTPS propio: YouTube)"
	@echo "  make deps-rostros     ONNX Runtime + YuNet + SFace -> extern/rostros (reconocer caras)"
	@echo "  make deps-spout       SDK Spout2 (2.007h) -> extern/Spout2"
	@echo "  make deps-cloudflared cloudflared 2024.11.0 -> extern/cloudflared"
	@echo "  make deps-vcredist    Redist VC++ -> extern/vcredist"
	@echo "  make deps-lsp         Modulos LSP 1.2.35 (CLAP, Linux) -> extern/lsp"
	@echo "  make deps-lsp-win     Modulos LSP 1.2.35 para Windows (compilados desde fuente) -> extern/lsp"
	@echo "  make deps-clap        Headers de CLAP 1.2.6 -> extern/clap"
	@echo "  make deps-libreoffice LibreOffice headless (PowerPoint -> PDF) y sus fuentes"
	@echo "  make deps-msitools    abrir el .msi de Windows desde Linux (lo pide deps-libreoffice-win)"
	@echo "  make mcp-inventario   docs/mcp.json: todas las herramientas MCP (con el aparato andando)"
	@echo "  make deps-llamacpp    llama.cpp (CPU) para probar un modelo chico local"
	@echo "  make bateria-local    cuanto tarda y cuanto acierta ese modelo (prefijo cacheado)"
	@echo "  make corpus-entrenamiento  arma datos/entrenamiento desde las semillas"
	@echo "  make modelo PERFIL=reactivo BASE=Qwen/Qwen3.5-4B   el modelo reactivo al dia con el MCP:"
	@echo "                        inventario, equivalencias, bucle del corpus y fabrica (MLX q4 + GGUF)"
	@echo "  make modelo-fabricar NOMBRE=x  solo entrenar/medir/cuantizar/empaquetar con el corpus actual"
	@echo "  make venv-entrenar         el entorno para entrenar (torch, unsloth, gguf)"
	@echo "  make vector-control MODELO=... el vector que empuja a derivar, y su curva"
	@echo "  make corpus-descubrir      que herramientas nuevas no tienen frases"
	@echo "                        -> extern/libreoffice. OPCIONAL y pesado (~280 MB por"
	@echo "                        plataforma): solo hace falta si alguien trae .pptx."
	@echo "  make deps-mbedtls     mbedtls v3.6.2 (fuente de build) -> extern/mbedtls"
	@echo "  make deps-nlohmann    nlohmann/json v3.11.3 (fuente de build) -> extern/nlohmann-json"
	@echo "  make deps-samplerate  libsamplerate 0.2.2 (fuente de build) -> extern/libsamplerate"
	@echo "  make deps-argon2      argon2 20190702 (fuente de build) -> extern/argon2"
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
	cmake --install $(BUILD_DIR) --prefix release --component bundle
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

# Ya no hay exclusiones: los headers aproximados que vivian en
# `src/obs-module/obs/` se eliminaron cuando el modulo paso a compilarse contra
# libobs real, asi que todo `src/` es codigo propio y se lintea entero.
lint:
	@if [ -z "$(CF)" ]; then \
		echo "ERROR: clang-format no encontrado. Instalarlo con: sudo apt install clang-format" >&2; \
		exit 1; \
	fi
	@echo "Lint: $(CF) sobre src/ (dry-run, -Werror)..."
	@find src \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -print0 \
		| xargs -0 -r $(CF) --dry-run -Werror

# --- Bundle portable (layout §5.1) ------------------------------------------
# Instala el esqueleto del layout portable sobre release (bin/64bit,
# obs-plugins, data/libobs, Config, Logs, Recordings, Web/guest).
release:
	cmake --install $(BUILD_DIR) --prefix release --component bundle
	@# Linux: el bundle no puede depender de bibliotecas de terceros del sistema.
	@if [ "$$(uname -s)" = Linux ]; then tools/deps/check-portable.sh release; fi
	@# Sin manifest el launcher verifica cero archivos y la comprobacion de
	@# integridad no sirve de nada. Solo lo que no cambia al usar el programa.
	tools/deploy --solo-manifest release

# --- Windows x64 desde Linux (ABI MSVC: clang-cl + lld-link) ---------------
# `make win` baja TODO lo necesario (idempotente, con hash) y compila el bundle
# de Windows en build/win; `make release-win` lo instala en release-win/ y
# `make dist-win` arma el .zip de distribucion en dist/.
#
# El CRT y el Windows SDK son de Microsoft: bajarlos (xwin) implica aceptar la
# licencia de Visual Studio Build Tools -> BA_ACCEPT_MSVC_LICENSE=1.
WIN_BUILD_DIR ?= build/win
WIN_RELEASE   ?= release-win
WIN_TOOLCHAIN := $(CURDIR)/cmake/toolchains/windows-clang-cl.cmake
QT6_HOST_PATH = $(CURDIR)/$(EXTERNS_DIR)/qt6-host/$(QT6_VER)/gcc_64

deps-win-toolchain:
	tools/deps/win-toolchain.sh

deps-webrtc-win:
	$(MAKE) deps-webrtc BA_WEBRTC_TARGET=windows

# Todo lo que el build de Windows necesita. Qt de Windows (msvc2019_64) mas el
# Qt del host (moc, rcc, qmlcachegen corren en Linux), OBS (runtime + fuentes
# para los headers), libwebrtc de Windows y las fuentes compartidas.
deps-win: deps-win-toolchain deps-qt deps-qt-host deps-obs deps-obs-deps-win deps-ndi-src deps-webrtc-win \
          deps-src deps-lwip deps-cacert deps-libvterm deps-fonts deps-cloudflared deps-clap deps-lsp-win \
          deps-libreoffice-win

# Plugins LSP (CLAP) para Windows: compilados desde fuente (tools/deps/lsp-win.sh).
deps-lsp-win: deps-win-toolchain
	tools/deps/lsp-win.sh $(JOBS)

# FFmpeg para compilar contra el del runtime de OBS (NDI decodifica SpeedHQ con
# libavcodec): headers + bibliotecas de importacion del obs-deps OFICIAL que fija
# el propio OBS (extern/obs-src/buildspec.json, mismo hash). Es FFmpeg n7.0.2,
# el mismo de las DLL de extern/obs-studio (que son las que viajan).
OBS_DEPS_WIN_VER := 2024-09-12
OBS_DEPS_WIN_SHA := d4a4f194591766891ad3c0b267deec3c4b85239c8fe557273559927456aeedbb
OBS_DEPS_WIN_ZIP = $(DL_DIR)/windows-deps-$(OBS_DEPS_WIN_VER)-x64.zip
deps-obs-deps-win:
	@mkdir -p "$(DL_DIR)"
	@if [ -f "$(EXTERNS_DIR)/obs-deps-win/lib/avcodec.lib" ]; then echo "[deps] obs-deps (Windows): ya presente"; else \
	  [ -f "$(OBS_DEPS_WIN_ZIP)" ] && [ "$$($(SHA256) "$(OBS_DEPS_WIN_ZIP)" | cut -d' ' -f1)" = "$(OBS_DEPS_WIN_SHA)" ] || \
	    curl -fL --retry 3 -o "$(OBS_DEPS_WIN_ZIP)" "https://github.com/obsproject/obs-deps/releases/download/$(OBS_DEPS_WIN_VER)/windows-deps-$(OBS_DEPS_WIN_VER)-x64.zip"; \
	  printf '%s  %s\n' "$(OBS_DEPS_WIN_SHA)" "$(OBS_DEPS_WIN_ZIP)" | $(SHA256_CHECK) || { echo "[deps] ERROR obs-deps: hash" >&2; exit 1; }; \
	  rm -rf "$(EXTERNS_DIR)/obs-deps-win"; \
	  unzip -q -o "$(OBS_DEPS_WIN_ZIP)" 'include/libavcodec/*' 'include/libavutil/*' 'lib/avcodec.lib' 'lib/avutil.lib' \
	    -d "$(EXTERNS_DIR)/obs-deps-win"; \
	  echo "[deps] obs-deps (Windows): FFmpeg (headers + .lib) en $(EXTERNS_DIR)/obs-deps-win"; fi

# Recepcion NDI abierta: libNoIDea + libmicrodns por commit fijado.
deps-ndi-src:
	tools/deps/ndi-src.sh

win: deps-win
	cmake -S . -B $(WIN_BUILD_DIR) -G Ninja \
		-DCMAKE_TOOLCHAIN_FILE=$(WIN_TOOLCHAIN) \
		-DCMAKE_BUILD_TYPE=Release \
		-DQT_HOST_PATH=$(QT6_HOST_PATH)
	cmake --build $(WIN_BUILD_DIR) -j$(JOBS)

# Tests de Windows bajo Wine portable (tools/ci/test-win.sh).
test-win: win
	tools/ci/test-win.sh $(WIN_BUILD_DIR)

release-win: win
	rm -rf $(WIN_RELEASE)
	cmake --install $(WIN_BUILD_DIR) --prefix $(WIN_RELEASE) --component bundle
	tools/deps/check-portable-win.sh $(WIN_RELEASE)
	@# El manifest, igual que en Linux: sin el, el launcher de Windows verifica
	@# CERO archivos y la comprobacion de integridad no sirve de nada, sin que
	@# nada lo avise. El bundle de Windows salia sin manifest desde siempre.
	tools/deploy --solo-manifest $(WIN_RELEASE)

# --- Archivos de distribucion (los que sube el CI a un release) --------------
# dist/BroadcastAppliance-<version>-<plataforma>.{zip,tar.gz}: la carpeta
# portable completa, lista para descomprimir y usar (o para `--update`).
BA_VERSION := $(shell awk '/^project\(/,/\)/' CMakeLists.txt | awk '/VERSION/{print $$2; exit}')
dist-win: release-win
	@mkdir -p dist
	rm -f dist/BroadcastAppliance-$(BA_VERSION)-windows-x64.zip
	@# Con carpeta raiz (como el .tar.gz de Linux): "Extraer aqui" no desparrama
	@# los archivos del programa en la carpeta del usuario.
	rm -rf dist/.zip-stage && mkdir -p dist/.zip-stage
	cp -al $(WIN_RELEASE) dist/.zip-stage/BroadcastAppliance 2>/dev/null || cp -a $(WIN_RELEASE) dist/.zip-stage/BroadcastAppliance
	cd dist/.zip-stage && 7z a -tzip -mx=7 -bso0 ../BroadcastAppliance-$(BA_VERSION)-windows-x64.zip BroadcastAppliance
	rm -rf dist/.zip-stage
	@cd dist && $(SHA256) BroadcastAppliance-$(BA_VERSION)-windows-x64.zip > BroadcastAppliance-$(BA_VERSION)-windows-x64.zip.sha256
	@echo "$(ESTE_COMMIT)" > dist/BroadcastAppliance-$(BA_VERSION)-windows-x64.zip.commit
	@echo "dist/BroadcastAppliance-$(BA_VERSION)-windows-x64.zip"

# Todo lo que el build completo de Linux necesita (libobs y sus deps desde
# fuente, Qt del host, libwebrtc de Linux, fuentes compartidas...).
deps-linux: deps-obs-linux deps-qt-host deps-src deps-lwip deps-cacert deps-libvterm deps-fonts deps-cloudflared-host \
            deps-clap deps-lsp deps-libreoffice-linux
	$(MAKE) deps-webrtc BA_WEBRTC_TARGET=linux

# --- macOS arm64 (Apple Silicon), nativo -------------------------------------
# Todo se compila EN la Mac con Apple clang (Xcode completo: libobs lo exige).
# Mismo layout portable que Linux (bin/64bit, obs-plugins/64bit, data/) y el
# mismo criterio: nada del sistema salvo los frameworks de macOS.
#   make deps-macos     -> baja/compila todo lo de terceros en extern/
#   make macos          -> compila (build/) y corre los tests
#   make release-macos  -> bundle portable en release/ (verificado con otool)
#   make dist-macos     -> dist/BroadcastAppliance-<ver>-macos-arm64.tar.gz
#   make dmg-macos      -> dist/BroadcastAppliance-<ver>-macos-arm64.dmg (la .app, lo que publica el release)
deps-obs-src:
	@test -d extern/obs-src || git clone --depth 1 --branch $(OBS_TAG) \
		--recurse-submodules --shallow-submodules \
		https://github.com/obsproject/obs-studio.git extern/obs-src

deps-obs-macos: check-macos-tools deps-obs-src
	tools/deps/obs-macos.sh $(JOBS)

deps-lsp-macos:
	tools/deps/lsp-macos.sh $(JOBS)

check-macos-tools:
	@[ "$$(uname -s)" = Darwin ] && [ "$$(uname -m)" = arm64 ] || { echo "ERROR: esto es para una Mac Apple Silicon" >&2; exit 1; }
	@xcodebuild -version >/dev/null 2>&1 || { echo "ERROR: falta Xcode completo (App Store) y: sudo xcode-select -s /Applications/Xcode.app/Contents/Developer" >&2; exit 1; }
	@missing=""; for t in cmake ninja meson pkg-config git curl python3; do command -v $$t >/dev/null 2>&1 || missing="$$missing $$t"; done; \
	  if [ -n "$$missing" ]; then echo "ERROR: faltan herramientas de build:$$missing" >&2; \
	  echo "  brew install cmake ninja meson pkg-config make" >&2; exit 1; fi
	@echo "[macos] herramientas OK ($$(xcodebuild -version | head -1), $$(cmake --version | head -1))"

# deps-ndi-src: el fuente de libmicrodns (buscar televisores por mDNS); en
# Linux viene compilada con obs-deps.
deps-macos: check-macos-tools deps-obs-macos deps-qt-host deps-src deps-lwip deps-cacert deps-libvterm deps-fonts \
            deps-cloudflared-host deps-clap deps-ndi-src deps-libreoffice-mac
	$(MAKE) deps-webrtc BA_WEBRTC_TARGET=macos
	@# LSP (modulos del rack de audio) es opcional en macOS mientras se valida:
	@# sin el, el rack funciona con el analizador y el generador.
	-$(MAKE) deps-lsp-macos

macos: modelo-mac
	cmake -S . -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
		-DCMAKE_OSX_DEPLOYMENT_TARGET=13.3
	cmake --build $(BUILD_DIR) -j$(JOBS)
	ctest --test-dir $(BUILD_DIR) --output-on-failure

# ba-modelo: el modelo local con MLX (src/modelo-mac). xcodebuild y no `swift
# build`: este no compila los shaders de Metal de MLX. Las dependencias de
# Swift quedan fijadas en src/modelo-mac/Package.resolved; mlx-swift-lm va en
# extern/ con los parches de src/modelo-mac/parches/ (deps-mlx-swift-lm). Necesita el
# componente Metal de Xcode (xcodebuild -downloadComponent MetalToolchain).
MLX_SWIFT_LM_REV := ac6f5651a423b6ad9005332eed583dd336e59a53
deps-mlx-swift-lm:
	@set -e; d=extern/mlx-swift-lm; sello=$$d/.parches-$$(cat src/modelo-mac/parches/*.patch | $(SHA256) | cut -c1-12); \
	if [ -f "$$sello" ]; then echo "[mlx-swift-lm] al dia ($$sello)"; exit 0; fi; \
	[ -d $$d/.git ] || git clone -q https://github.com/ml-explore/mlx-swift-lm $$d; \
	git -C $$d fetch -q origin $(MLX_SWIFT_LM_REV) 2>/dev/null || true; \
	git -C $$d checkout -q -f $(MLX_SWIFT_LM_REV); git -C $$d clean -qfd; \
	for p in src/modelo-mac/parches/*.patch; do git -C $$d apply "$(CURDIR)/$$p"; echo "[mlx-swift-lm] $$p"; done; \
	touch "$$sello"

modelo-mac: deps-mlx-swift-lm
	@xcrun metal --version >/dev/null 2>&1 || { echo "ERROR: falta el compilador de Metal: xcodebuild -downloadComponent MetalToolchain" >&2; exit 1; }
	cd src/modelo-mac && xcodebuild -scheme ba-modelo -destination 'platform=macOS,arch=arm64' \
		-configuration Release -derivedDataPath $(CURDIR)/build/modelo-mac \
		-skipPackagePluginValidation -skipMacroValidation -quiet build

release-macos:
	rm -rf release
	cmake --install $(BUILD_DIR) --prefix release --component bundle
	tools/deps/check-portable-macos.sh release
	@# El manifest, como en Linux y Windows: sin el, el launcher verifica cero
	@# archivos y la comprobacion de integridad no sirve de nada.
	tools/deploy --solo-manifest release

# La .app (el bundle portable en Contents/) y el .dmg que publica el release.
# Firma ad-hoc: ver tools/deps/app-macos.sh.
app-macos: release-macos
	@mkdir -p dist
	tools/deps/app-macos.sh release dist/BroadcastAppliance.app

dmg-macos: release-macos
	@mkdir -p dist
	tools/deps/app-macos.sh release dist/BroadcastAppliance.app dist/BroadcastAppliance-$(BA_VERSION)-macos-arm64.dmg
	@cd dist && $(SHA256) BroadcastAppliance-$(BA_VERSION)-macos-arm64.dmg > BroadcastAppliance-$(BA_VERSION)-macos-arm64.dmg.sha256
	@echo "$(ESTE_COMMIT)" > dist/BroadcastAppliance-$(BA_VERSION)-macos-arm64.dmg.commit
	@echo "dist/BroadcastAppliance-$(BA_VERSION)-macos-arm64.dmg"

# ============================================================================
# Releases desde una maquina propia (el CI de GitHub corre solo a mano).
#
#   make publicar            TODO: controles, Linux y Windows (en Docker), el
#                            .dmg si esto es una Mac, y la subida a GitHub.
#
# O por partes (cada dist-* deja el archivo y su .sha256 en dist/):
#   make dist-linux-docker   .tar.gz de Linux, en Docker (Ubuntu 24.04 x86_64)
#   make dist-win-docker     .zip de Windows, en Docker (cross con clang-cl)
#   make dmg-macos           .dmg de macOS (solo en una Mac)
#   make subir               sube lo que haya en dist/ de esta version y verifica
#                            en GitHub tamaño y sha256 (tools/publicar.sh)
#
# El release sale del commit (v<version>-<commit>): lo armado en distintas
# maquinas desde el mismo commit cae junto. Desde master queda "Latest"; si
# no, pre-release. Antes de compilar se chequea: sin cambios sin commitear,
# commit subido a GitHub, release del mismo commit. FORZAR=1 saltea; TAG=...
# elige otro release.
#
# En Linux sin Docker: make publicar-linux / publicar-win (compilan aca).
# ============================================================================
# De que commit sale cada archivo de dist/ (su .commit al lado): tools/publicar.sh
# no sube nada que no sea del commit actual. "-dirty" = habia cambios sin commitear.
ESTE_COMMIT = $(shell git rev-parse HEAD 2>/dev/null)$(shell git status --porcelain --untracked-files=no 2>/dev/null | grep -q . && echo -dirty)
DIST_LINUX = dist/BroadcastAppliance-$(BA_VERSION)-linux-x64.tar.gz
DIST_WIN   = dist/BroadcastAppliance-$(BA_VERSION)-windows-x64.zip
DIST_MACOS = dist/BroadcastAppliance-$(BA_VERSION)-macos-arm64.dmg

dist-linux-docker:
	tools/ci/compilar-en-docker.sh linux

# Los tests de Windows corren bajo Wine. En una Mac, Wine va encima de Rosetta
# y algunos fallan sin que el programa ande mal (en el runner del CI pasan):
# ahi el fallo se AVISA pero no frena. En un Linux de verdad, frena.
dist-win-docker:
	@rc=0; tools/ci/compilar-en-docker.sh win || rc=$$?; \
	if [ $$rc = 3 ] && [ "$$(uname -s)" = Darwin ]; then \
	  echo ""; echo "AVISO: tests de Windows bajo Wine fallaron (Wine sobre Rosetta): ver arriba. El .zip se armo." >&2; echo ""; \
	elif [ $$rc != 0 ]; then exit $$rc; fi

subir:
	@archivos="$$(ls $(DIST_LINUX) $(DIST_WIN) $(DIST_MACOS) 2>/dev/null)"; \
	  [ -n "$$archivos" ] || { echo "ERROR: no hay nada armado en dist/ para la version $(BA_VERSION)" >&2; exit 1; }; \
	  tools/publicar.sh $$archivos

publicar:
	tools/publicar.sh --solo-chequear
	rm -f $(DIST_LINUX)* $(DIST_WIN)* $(DIST_MACOS)*
	$(MAKE) dist-linux-docker
	$(MAKE) dist-win-docker
	@if [ "$$(uname -s)" = Darwin ]; then $(MAKE) macos && $(MAKE) dmg-macos; \
	  else echo "[publicar] esto no es una Mac: el .dmg se suma despues con 'make publicar-macos' en una Mac"; fi
	$(MAKE) subir

# Una plataforma sola, compilando en ESTA maquina (sin Docker).
publicar-macos:
	tools/publicar.sh --solo-chequear
	$(MAKE) macos
	$(MAKE) dmg-macos
	tools/publicar.sh $(DIST_MACOS)

publicar-linux:
	tools/publicar.sh --solo-chequear
	$(MAKE) build
	$(MAKE) test
	$(MAKE) dist-linux
	tools/publicar.sh $(DIST_LINUX)

publicar-win:
	tools/publicar.sh --solo-chequear
	$(MAKE) test-win BA_ACCEPT_MSVC_LICENSE=1
	$(MAKE) dist-win BA_ACCEPT_MSVC_LICENSE=1
	tools/publicar.sh $(DIST_WIN)

dist-macos: release-macos
	@mkdir -p dist
	tar -C release -czf dist/BroadcastAppliance-$(BA_VERSION)-macos-arm64.tar.gz -s ',^\.,BroadcastAppliance,' .
	@cd dist && $(SHA256) BroadcastAppliance-$(BA_VERSION)-macos-arm64.tar.gz > BroadcastAppliance-$(BA_VERSION)-macos-arm64.tar.gz.sha256
	@echo "dist/BroadcastAppliance-$(BA_VERSION)-macos-arm64.tar.gz"

dist-linux:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) -j$(JOBS)
	rm -rf release
	$(MAKE) release
	@mkdir -p dist
	tar -C release -czf dist/BroadcastAppliance-$(BA_VERSION)-linux-x64.tar.gz --transform 's,^\.,BroadcastAppliance,' .
	@cd dist && $(SHA256) BroadcastAppliance-$(BA_VERSION)-linux-x64.tar.gz > BroadcastAppliance-$(BA_VERSION)-linux-x64.tar.gz.sha256
	@echo "$(ESTE_COMMIT)" > dist/BroadcastAppliance-$(BA_VERSION)-linux-x64.tar.gz.commit
	@echo "dist/BroadcastAppliance-$(BA_VERSION)-linux-x64.tar.gz"

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
#   make deps-lsp         -> modulos LSP 1.2.35 (CLAP, Linux) a extern/lsp
#   make deps-status      -> muestra qué assets hay y su estado
#   make deps-clean       -> borra extern/
#
# BA_WEBRTC_TARGET: `windows` (default, webrtc.lib) o `linux` (libwebrtc.a).
# El build enlaza libwebrtc solo en Windows (if(WIN32) en src/gateway), por lo
# que el artefacto por defecto es el de Windows.
# ============================================================================

EXTERNS_DIR ?= extern
DL_DIR      := $(EXTERNS_DIR)/.downloads

# Artefacto de libwebrtc a bajar: el del sistema donde se compila (linux en
# Linux) o windows (`make deps-webrtc-win`, lo usa `make win`).
ifeq ($(shell uname -s),Linux)
  BA_WEBRTC_TARGET ?= linux
else ifeq ($(shell uname -s),Darwin)
  BA_WEBRTC_TARGET ?= macos
else
  BA_WEBRTC_TARGET ?= windows
endif

# --- Versiones (sincronizadas con versions.cmake) ---------------------------
# Se leen de versions.cmake para que el pin sea la única fuente. Si la línea
# no se encuentra, se cae a un default conocido (robustez ante reorden).
OBS_TAG        := $(shell awk -F'"' '/OBS_STUDIO_TAG/{print $$2; exit}' versions.cmake 2>/dev/null)
OBS_TAG        ?= 31.0.0
CLOUDFLARED_VER:= $(shell awk -F'"' '/CLOUDFLARED_VERSION/{print $$2; exit}' versions.cmake 2>/dev/null)
CLOUDFLARED_VER?= 2024.11.0
LSP_VER        := $(shell awk -F'"' '/LSP_VERSION/{print $$2; exit}' versions.cmake 2>/dev/null)
LSP_VER        ?= 1.2.35
QT6_VER        := $(shell awk -F'"' '/QT6_VERSION/{print $$2; exit}' versions.cmake 2>/dev/null)
QT6_VER        ?= 6.6.3

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
OBS_ZIP  := $(EXTERNS_DIR)/.downloads/OBS-Studio-$(OBS_TAG)-Windows.zip
OBS_DEST := $(EXTERNS_DIR)/obs-studio

# Qt 6.6.3 (win64_msvc2019_64): SDK de BUILD (headers + import libs + CMake config +
# moc), NO el runtime (ese lo trae el zip de OBS en extern/obs-studio/bin/64bit).
# Se instala con `aqtinstall` (en el .venv local) en lugar de curl: aqt baja el
# componente exacto y lo deja en extern/qt6/$(QT6_VER)/msvc2019_64/{include,lib,bin}
# (incl. lib/cmake/Qt6/Qt6Config.cmake y lib/libQt6Core.a). La ruta oficial de
# descarga única (qt-opensource-...-mingw_2022-6.6.3.7z) no está disponible en
# los mirrors actuales (404), por eso aqt es la vía fiable. El SDK es grande
# (~1 GB); es idempotente: si el sentinel (Qt6Config.cmake) ya está, se salta.
# El hash que se pinnea es el del sentinel (aqt no baja un único archivo con
# hash conocido): es la verificación de integridad del layout.
QT6_DEST     := $(EXTERNS_DIR)/qt6
QT6_PREFIX   := $(QT6_DEST)/$(QT6_VER)/msvc2019_64
QT6_SENTINEL := $(QT6_PREFIX)/lib/cmake/Qt6/Qt6Config.cmake
QT6_SHA      := 381542ca9904f8595cccfa3a45a035e188924180f815e44afa77f296a9bee323

# libwebrtc M123 (Shiguredo). El zip/tar trae un top-dir `webrtc/` con
# include/ + lib/; se extrae sobre extern/ -> extern/webrtc/{include,lib}.
ifeq ($(BA_WEBRTC_TARGET),linux)
  WEBRTC_URL := https://github.com/shiguredo-webrtc-build/webrtc-build/releases/download/$(WEBRTC_TAG)/webrtc.ubuntu-22.04_x86_64.tar.gz
  WEBRTC_SHA := 5858e94f14d11da0612b24bd139664281db8e2278885587ef0d2ca614fb9d693
  WEBARC     := $(DL_DIR)/webrtc.ubuntu-22.04_x86_64.tar.gz
  WEB_KIND   := tar
  WEB_LIB    := libwebrtc.a
else ifeq ($(BA_WEBRTC_TARGET),macos)
  # macOS arm64: compilado contra la libc++ del sistema (std::__1), se enlaza
  # directo con Apple clang (src/gateway/CMakeLists.txt, rama APPLE).
  WEBRTC_URL := https://github.com/shiguredo-webrtc-build/webrtc-build/releases/download/$(WEBRTC_TAG)/webrtc.macos_arm64.tar.gz
  WEBRTC_SHA := e88e16db18a4b0a6b3d21e332dc2937cb8b63ca52bf0ed6876da67c796be7a1e
  WEBARC     := $(DL_DIR)/webrtc.macos_arm64.tar.gz
  WEB_KIND   := tar
  WEB_LIB    := libwebrtc.a
else
  WEBRTC_URL := https://github.com/shiguredo-webrtc-build/webrtc-build/releases/download/$(WEBRTC_TAG)/webrtc.windows_x86_64.zip
  WEBRTC_SHA := a56f41f40fb97e7016e5b2d0e0bf0394bcfc3a0833f5de2984eece8951f101c0
  WEBARC     := $(DL_DIR)/webrtc.windows_x86_64.zip
  WEB_KIND   := zip
  WEB_LIB    := webrtc.lib
endif
# Cada SDK en su carpeta: se pueden tener los dos (Linux + cross a Windows).
ifeq ($(BA_WEBRTC_TARGET),linux)
  WEB_DEST := $(EXTERNS_DIR)/webrtc
else ifeq ($(BA_WEBRTC_TARGET),macos)
  WEB_DEST := $(EXTERNS_DIR)/webrtc
else
  WEB_DEST := $(EXTERNS_DIR)/webrtc-win
endif
# libc++ de Chromium para compilar contra libwebrtc de Linux (commits de
# extern/webrtc/VERSIONS: WEBRTC_SRC_THIRD_PARTY_LIBCXX_SRC_COMMIT y
# WEBRTC_SRC_BUILDTOOLS_COMMIT).
LIBCXX_COMMIT     := 9d119c1f4a097b7d27210874f4eba3fc91a83a4e
LIBCXX_URL        := https://chromium.googlesource.com/external/github.com/llvm/llvm-project/libcxx/+archive/$(LIBCXX_COMMIT)/include.tar.gz
LIBCXX_TREE_SHA   := 501fb1f30f3d690019f488461af2453d9c5f508f3dd7ee12145da6e63151e425
BUILDTOOLS_COMMIT := f35a7d885ace0b7dd8e8ac2376ca759d3905f4dc
BUILDTOOLS_URL    := https://chromium.googlesource.com/chromium/src/buildtools/+/$(BUILDTOOLS_COMMIT)/third_party/libc++
LIBCXX_CFG_SHA    := 6feb8f5430ffa14ca3c32638ac55f40b8f513f615ed650a6f4bb99f35d92ca8e
LIBCXX_ASSERT_SHA := 84a4c12bc4870c0f5d23b940ce5f38248bc2d2abfdcd4462f81d3a2761e6f29a

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

# cloudflared para el SISTEMA DEL HOST (Linux/macOS): el que corre el gateway
# cuando el appliance se ejecuta en esta maquina. Hashes de los binarios
# oficiales 2024.11.0.
CFL_HOST_OS   := $(shell uname -s)
CFL_HOST_ARCH := $(shell uname -m)
CFL_HOST_BIN  := $(EXTERNS_DIR)/cloudflared/cloudflared
ifeq ($(CFL_HOST_OS),Linux)
  ifeq ($(CFL_HOST_ARCH),aarch64)
    CFL_HOST_ASSET := cloudflared-linux-arm64
    CFL_HOST_SHA   := 45a4e6d4c15ffc493ce685d3548e02c59da10a22b67d262be8e32611c173f707
  else
    CFL_HOST_ASSET := cloudflared-linux-amd64
    CFL_HOST_SHA   := cd7095b33a861ccb055606d512fc74d81865f46fe9c2c7c84452ee60383db13c
  endif
else ifeq ($(CFL_HOST_OS),Darwin)
  CFL_HOST_ASSET := cloudflared-darwin-arm64.tgz
  CFL_HOST_SHA   := 9be7398e5cfaa8b5883bdd31185acc9931c53be34796a9ba8ef94ab7a8293206
endif
CFL_HOST_URL := https://github.com/cloudflare/cloudflared/releases/download/$(CLOUDFLARED_VER)/$(CFL_HOST_ASSET)


# Redist VC++ (evergreen aka.ms): aporta msvcp140_atomic_wait.dll (requerido
# por std::atomic::wait de C++20). Es un objetivo MOVIENTE (sin tag), por lo
# que NO se fija su hash: solo se asegura su presencia.
VCREDIST_URL := https://aka.ms/vs/17/release/vc_redist.x64.exe
VCREDIST_EXE := $(EXTERNS_DIR)/vcredist/VC_redist.x64.exe

# LSP 1.2.35: el CLAP oficial de Linux x86_64 (7z) a extern/lsp/ (el de
# Windows se compila desde la fuente: deps-lsp-win).
LSP_URL  := https://github.com/lsp-plugins/lsp-plugins/releases/download/$(LSP_VER)/lsp-plugins-$(LSP_VER)-Linux-x86_64.7z
LSP_SHA  := 4ef3b2f63b29a522fdaf3a18f879c1c85c82f0bdd1e0b3c849a5f3c4b08d2550
LSP_7Z   := $(DL_DIR)/lsp-plugins-$(LSP_VER)-Linux-x86_64.7z
LSP_DEST := $(EXTERNS_DIR)/lsp

# ============================================================================
# FUENTES DE BUILD (se COMPILEN en el producto) — distinto de los assets de
# runtime de arriba. Estos 4 son los árboles de código de terceros que el
# CMakeLists raíz consume vía FetchContent (mbedtls, nlohmann/json,
# libsamplerate y argon2). Se vendorean a `extern/` para que un
# configure con `extern/` poblado sea OFFLINE (cero red): el CMake prefiere la
# copia vendored y solo cae a git si falta. Son DEPS DE BUILD (entran en el
# binario), NO assets de runtime como OBS/Qt/Spout. Mismo patrón idempotente:
# presente+SHA -> skip; si no, curl a <tar>.part, mv, $(SHA256) -c, y se
# extrae strippeando el top-dir de codeload.
# ============================================================================

# mbedtls v3.6.2 (Mbed-TLS/mbedtls). CMakeLists en la raíz del tar.
MBEDTLS_URL  := https://codeload.github.com/Mbed-TLS/mbedtls/tar.gz/refs/tags/v3.6.2
MBEDTLS_SHA  := f4a876b1f6921ad0aefb445f974ef62414d33928640b2c45555c5e64a196a1a8
MBEDTLS_TAR  := $(DL_DIR)/mbedtls-v3.6.2.tar.gz
MBEDTLS_DEST := $(EXTERNS_DIR)/mbedtls
MBEDTLS_SENT := $(MBEDTLS_DEST)/CMakeLists.txt
# Submodule `framework` de mbedtls v3.6.2: el gitlink del repo lo fija en el
# commit 94599c0e3b5036e086446a51a3f79640f70f22f6 (Mbed-TLS/mbedtls-framework).
# Codeload NO incluye submodules (el tar deja framework/ vacío) y el
# CMakeLists de mbedtls hace FATAL_ERROR si framework/CMakeLists.txt no existe,
# así que se vendorea aparte y se extrae dentro de extern/mbedtls/framework.
MBEDTLS_FW_URL  := https://codeload.github.com/Mbed-TLS/mbedtls-framework/tar.gz/94599c0e3b5036e086446a51a3f79640f70f22f6
MBEDTLS_FW_SHA  := 632d5aa4dd2e4179f9d648be12af9a9c3158d7f503a93e70e651751b5f31257c
MBEDTLS_FW_TAR  := $(DL_DIR)/mbedtls-framework-94599c0e.tar.gz
MBEDTLS_FW_DEST := $(MBEDTLS_DEST)/framework
MBEDTLS_FW_SENT := $(MBEDTLS_FW_DEST)/CMakeLists.txt

# nlohmann/json v3.11.3 (nlohmann/json). Header-only; CMakeLists en la raíz.
NLOHMANN_URL  := https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.11.3
NLOHMANN_SHA  := 0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406
NLOHMANN_TAR  := $(DL_DIR)/nlohmann-json-v3.11.3.tar.gz
NLOHMANN_DEST := $(EXTERNS_DIR)/nlohmann-json
NLOHMANN_SENT := $(NLOHMANN_DEST)/CMakeLists.txt

# libsamplerate 0.2.2 (libsndfile/libsamplerate). CMakeLists en la raíz.
SAMPLERATE_URL  := https://codeload.github.com/libsndfile/libsamplerate/tar.gz/refs/tags/0.2.2
SAMPLERATE_SHA  := 16e881487f184250deb4fcb60432d7556ab12cb58caea71ef23960aec6c0405a
SAMPLERATE_TAR  := $(DL_DIR)/libsamplerate-0.2.2.tar.gz
SAMPLERATE_DEST := $(EXTERNS_DIR)/libsamplerate
SAMPLERATE_SENT := $(SAMPLERATE_DEST)/CMakeLists.txt

# argon2 20190702 (P-H-C/phc-winner-argon2). ⚠️  SIN CMakeLists: el target se
# arma a mano en el CMakeLists raíz desde ${argon2_SOURCE_DIR}/src/*.c, por eso
# el sentinel es el Makefile oficial (no un CMakeLists).
ARGON2_URL  := https://codeload.github.com/P-H-C/phc-winner-argon2/tar.gz/refs/tags/20190702
ARGON2_SHA  := daf972a89577f8772602bf2eb38b6a3dd3d922bf5724d45e7f9589b5e830442c
ARGON2_TAR  := $(DL_DIR)/argon2-20190702.tar.gz
ARGON2_DEST := $(EXTERNS_DIR)/argon2
ARGON2_SENT := $(ARGON2_DEST)/Makefile

# Cada target enlaza el bloque idempotente de descarga (presente+sha -> skip;
# si no, curl a <archivo>.part, mv y verificacion `$(SHA256) -c`). Se repite
# por asset a proposito: es mas robusto y depurable que un macro define/call/
# eval (este ultimo, dentro de una receta, se interpreta como contenido de
# makefile y rompe el parseo).

# --- deps (agregado) ---------------------------------------------------------
deps: deps-obs deps-qt deps-webrtc deps-lwip deps-cacert deps-libvterm deps-fonts deps-spout deps-cloudflared deps-vcredist deps-lsp deps-src

# --- libobs para Linux (compilada desde fuente hacia extern/) ---------------
# El motor de emision necesita libobs. En Windows se usa el runtime prebuilt
# que baja `make deps-obs`; en Linux no hay tal cosa, asi que se compila desde
# el tag fijado en versions.cmake hacia `extern/obs-linux`, que es de donde el
# build del proyecto la toma (nunca del sistema).
#
# Requiere las bibliotecas de desarrollo de terceros listadas en el README
# ("Requisitos de build"). uthash y nv-codec-headers se vendorean aca mismo.
# Sin parentesis dentro del $(shell ...): make empareja parentesis y una
# expresion sed con ellos le corta la llamada a la mitad.
OBS_TAG := $(shell grep OBS_STUDIO_TAG versions.cmake | head -1 | cut -d'"' -f2)

# Dependencias de libobs (FFmpeg, x264, jansson, curl, ...) DESDE FUENTE fijada:
# el bundle no usa bibliotecas de terceros del sistema.
deps-obs-deps-linux:
	tools/deps/obs-deps-linux.sh $(JOBS)

deps-obs-linux: deps-obs-deps-linux
	@echo "==> libobs $(OBS_TAG) desde fuente hacia extern/obs-linux"
	@test -d extern/obs-src || git clone --depth 1 --branch $(OBS_TAG) \
		--recurse-submodules --shallow-submodules \
		https://github.com/obsproject/obs-studio.git extern/obs-src
	@test -d extern/uthash || git clone --depth 1 --branch v2.3.0 \
		https://github.com/troydhanson/uthash.git extern/uthash
	@test -d extern/nv-codec-headers || git clone --depth 1 --branch n12.2.72.0 \
		https://github.com/FFmpeg/nv-codec-headers.git extern/nv-codec-headers
	rm -rf extern/obs-build extern/obs-linux
	PKG_CONFIG_PATH=$(CURDIR)/extern/obs-deps-linux/lib/pkgconfig \
	cmake -S extern/obs-src -B extern/obs-build \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_PREFIX_PATH=$(CURDIR)/extern/obs-deps-linux \
		-DENABLE_NEW_MPEGTS_OUTPUT=OFF -DENABLE_SERVICE_UPDATES=OFF \
		-DCMAKE_DISABLE_FIND_PACKAGE_Gio=ON -DCMAKE_COMPILE_WARNING_AS_ERROR=OFF \
		-DCMAKE_EXE_LINKER_FLAGS="-Wl,-rpath-link,$(CURDIR)/extern/obs-deps-linux/lib" \
		-DCMAKE_SHARED_LINKER_FLAGS="-Wl,-rpath-link,$(CURDIR)/extern/obs-deps-linux/lib" \
		-DCMAKE_MODULE_LINKER_FLAGS="-Wl,-rpath-link,$(CURDIR)/extern/obs-deps-linux/lib" \
		-DENABLE_UI=OFF -DENABLE_SCRIPTING=OFF -DENABLE_BROWSER=OFF \
		-DENABLE_WEBSOCKET=OFF -DENABLE_VLC=OFF -DENABLE_VST=OFF \
		-DENABLE_AJA=OFF -DENABLE_DECKLINK=OFF -DENABLE_PIPEWIRE=OFF \
		-DENABLE_QSV11=OFF -DENABLE_VIRTUALCAM=OFF -DENABLE_WEBRTC=OFF \
		-DUthash_INCLUDE_DIR=$(CURDIR)/extern/uthash/include \
		-DFFnvcodec_INCLUDE_DIR=$(CURDIR)/extern/nv-codec-headers/include \
		-DCMAKE_INSTALL_PREFIX=$(CURDIR)/extern/obs-linux
	cmake --build extern/obs-build -j$(JOBS)
	cmake --install extern/obs-build
	@# Uso desde el arbol de build (tests): libobs y sus plugins encuentran las
	@# dependencias vendoreadas. En el bundle, BaPortableBundle fija $$ORIGIN.
	@for f in extern/obs-linux/lib/*.so.* extern/obs-linux/bin/obs-ffmpeg-mux; do \
	  [ -f "$$f" ] && [ ! -L "$$f" ] && patchelf --add-rpath '$$ORIGIN/../../obs-deps-linux/lib' "$$f"; done; true
	@for f in extern/obs-linux/lib/obs-plugins/*.so; do \
	  patchelf --add-rpath '$$ORIGIN/../../../obs-deps-linux/lib' "$$f"; done
	@echo "==> libobs lista en extern/obs-linux"

deps-obs:
	@command -v curl >/dev/null 2>&1 || { echo "[deps] ERROR: falta curl" >&2; exit 1; }
	@mkdir -p "$(DL_DIR)" "$(OBS_DEST)"
	@if [ -f "$(OBS_ZIP)" ] && [ "$$($(SHA256) "$(OBS_ZIP)" | cut -d' ' -f1)" = "$(OBS_SHA)" ]; then \
	  echo "[deps] OBS: ya presente y verificado ($(OBS_ZIP))"; \
	else \
	  echo "[deps] OBS: descargando $(OBS_URL)"; \
	  curl -fL --retry 3 --connect-timeout 30 -o "$(OBS_ZIP).part" "$(OBS_URL)" \
	    && mv "$(OBS_ZIP).part" "$(OBS_ZIP)" \
	    && printf '%s  %s\n' "$(OBS_SHA)" "$(OBS_ZIP)" | $(SHA256_CHECK) \
	    || { echo "[deps] ERROR OBS: fallo de descarga o hash" >&2; rm -f "$(OBS_ZIP).part"; exit 1; }; \
	fi
	@# Headers de libobs (el zip trae solo binarios): el arbol de fuentes del tag.
	@test -d extern/obs-src || git clone --depth 1 --branch $(OBS_TAG) \
		--recurse-submodules --shallow-submodules \
		https://github.com/obsproject/obs-studio.git extern/obs-src
	@if [ -f "$(OBS_DEST)/obs-plugins/64bit/libcef.dll" ]; then \
	  echo "[deps] OBS: ya extraído en $(OBS_DEST)"; \
	else \
	  echo "[deps] OBS: extrayendo a $(OBS_DEST)"; \
	  unzip -q -o "$(OBS_ZIP)" -d "$(OBS_DEST)"; \
	fi

# Qt 6 (win64_msvc2019_64): SDK de BUILD (headers + import libs + CMake config).
# Vía aqtinstall (en el .venv local): baja el componente exacto y lo deja en
# extern/qt6/$(QT6_VER)/msvc2019_64/{include,lib,bin} (incl. lib/cmake/Qt6/
# Qt6Config.cmake y lib/libQt6Core.a). La vía aqt es la fiable aquí: la ruta
# oficial de descarga única (qt-opensource-...-mingw_2022-6.6.3.7z) devuelve 404
# en los mirrors actuales. IDEMPOTENTE: si el sentinel (Qt6Config.cmake) ya
# está y su SHA coincide, se salta (ni pip ni aqt). El SDK es grande (~1 GB);
# aqt lo baja por componentes, así que si falla a mitad se re-baja limpio.
deps-qt:
	@command -v curl >/dev/null 2>&1 || { echo "[deps] ERROR: falta curl" >&2; exit 1; }
	@mkdir -p "$(QT6_DEST)"
	@if [ -f "$(QT6_SENTINEL)" ] && [ "$$($(SHA256) "$(QT6_SENTINEL)" | cut -d' ' -f1)" = "$(QT6_SHA)" ] \
	    && [ -f "$(QT6_PREFIX)/lib/cmake/Qt6Pdf/Qt6PdfConfig.cmake" ] \
	    && [ -f "$(QT6_PREFIX)/lib/cmake/Qt6Multimedia/Qt6MultimediaConfig.cmake" ]; then \
	  echo "[deps] Qt6 $(QT6_VER): ya presente y verificado, con qtpdf y qtmultimedia ($(QT6_PREFIX))"; \
	else \
	  echo "[deps] Qt6 $(QT6_VER): instalando aqtinstall en el .venv..."; \
	  test -x "$(PY)" || python3 -m venv .venv; \
	  "$(PIP)" install -q aqtinstall \
	    || { echo "[deps] ERROR Qt6: no se pudo instalar aqtinstall" >&2; exit 1; }; \
	  echo "[deps] Qt6 $(QT6_VER): instalando SDK win64_msvc2019_64 a $(QT6_PREFIX)"; \
	  rm -rf "$(QT6_DEST)"; \
	  "$(PY)" -m aqt install-qt windows desktop $(QT6_VER) win64_msvc2019_64 -m qtpdf qtmultimedia -O "$(QT6_DEST)" \
	    || { echo "[deps] ERROR Qt6: fallo de aqt" >&2; exit 1; }; \
	  if [ -f "$(QT6_SENTINEL)" ]; then \
	    echo "[deps] Qt6 $(QT6_VER): SDK instalado ($(QT6_PREFIX))"; \
	  else \
	    echo "[deps] ERROR Qt6: sentinel ausente tras aqt ($(QT6_SENTINEL))" >&2; exit 1; \
	  fi; \
	fi

# Qt 6 para el SISTEMA DEL HOST (Linux gcc_64 / macOS clang_64), vendoreado en
# extern/qt6-host: la interfaz grafica de ba-app compila SOLO contra esta copia
# y el bundle se la lleva (portable: nunca el Qt del sistema). Se instala con
# aqtinstall dentro del .venv local (nada global). ~1,2 GB en extern/.
QT6_HOST_DEST := $(EXTERNS_DIR)/qt6-host
ifeq ($(shell uname -s),Darwin)
  QT6_HOST_OS   := mac
  QT6_HOST_ARCH := clang_64
  QT6_HOST_DIR  := macos
else
  QT6_HOST_OS   := linux
  QT6_HOST_ARCH := gcc_64
  QT6_HOST_DIR  := gcc_64
endif
QT6_HOST_SENTINEL := $(QT6_HOST_DEST)/$(QT6_VER)/$(QT6_HOST_DIR)/lib/cmake/Qt6/Qt6Config.cmake
# El modulo PDF (qtpdf) se pide APARTE y tiene su PROPIO centinela. El de arriba
# es Qt6Config.cmake, que NO cambia al agregar un modulo: sin este segundo
# chequeo, en una maquina que ya tenia Qt el `qtpdf` no se bajaria nunca y nadie
# se enteraria hasta que un PDF no se pueda abrir.
# Renderiza las paginas de un PDF a imagen, que es de donde sale un carrusel
# (ver docs/CARRUSEL.md). Se eligio sobre MuPDF porque usa el mismo vendoreo que
# ya existe, anda igual en Linux y Windows, y no agrega otro build.
QT6_HOST_PDF := $(QT6_HOST_DEST)/$(QT6_VER)/$(QT6_HOST_DIR)/lib/cmake/Qt6Pdf/Qt6PdfConfig.cmake
# Multimedia (qtmultimedia), con su propio centinela por lo mismo que el PDF.
# Es para VER un archivo antes de ponerlo al aire: doble clic en la pestaña
# Medios. Su motor ES ffmpeg —Qt lo trae adentro desde 6.5— asi que no hace
# falta ni VLC ni un ffmpeg del sistema ni levantar otro proceso: es el mismo
# vendoreo de Qt que ya usamos, igual en Linux y en Windows.
QT6_HOST_MM := $(QT6_HOST_DEST)/$(QT6_VER)/$(QT6_HOST_DIR)/lib/cmake/Qt6Multimedia/Qt6MultimediaConfig.cmake

deps-qt-host:
	@if [ -f "$(QT6_HOST_SENTINEL)" ] && [ -f "$(QT6_HOST_PDF)" ] && [ -f "$(QT6_HOST_MM)" ]; then \
	  echo "[deps] Qt6 $(QT6_VER) ($(QT6_HOST_ARCH)): ya presente con qtpdf y qtmultimedia ($(QT6_HOST_DEST))"; \
	else \
	  test -x "$(PY)" || python3 -m venv .venv; \
	  "$(PIP)" install -q aqtinstall || { echo "[deps] ERROR Qt6: no se pudo instalar aqtinstall" >&2; exit 1; }; \
	  echo "[deps] Qt6 $(QT6_VER): instalando $(QT6_HOST_OS)/$(QT6_HOST_ARCH) (+qtpdf +qtmultimedia) en $(QT6_HOST_DEST)"; \
	  "$(PY)" -m aqt install-qt $(QT6_HOST_OS) desktop $(QT6_VER) $(QT6_HOST_ARCH) -m qtpdf qtmultimedia -O "$(QT6_HOST_DEST)" \
	    || { echo "[deps] ERROR Qt6: fallo de aqt" >&2; exit 1; }; \
	  test -f "$(QT6_HOST_SENTINEL)" || { echo "[deps] ERROR Qt6: falta $(QT6_HOST_SENTINEL)" >&2; exit 1; }; \
	  test -f "$(QT6_HOST_PDF)" || { echo "[deps] ERROR Qt6: aqt no dejo el modulo qtpdf" >&2; exit 1; }; \
	  test -f "$(QT6_HOST_MM)" || { echo "[deps] ERROR Qt6: aqt no dejo el modulo qtmultimedia" >&2; exit 1; }; \
	fi

deps-webrtc:
	@command -v curl >/dev/null 2>&1 || { echo "[deps] ERROR: falta curl" >&2; exit 1; }
	@mkdir -p "$(DL_DIR)" "$(EXTERNS_DIR)"
	@if [ -f "$(WEBARC)" ] && [ "$$($(SHA256) "$(WEBARC)" | cut -d' ' -f1)" = "$(WEBRTC_SHA)" ]; then \
	  echo "[deps] libwebrtc-$(BA_WEBRTC_TARGET): ya presente y verificado ($(WEBARC))"; \
	else \
	  echo "[deps] libwebrtc-$(BA_WEBRTC_TARGET): descargando $(WEBRTC_URL)"; \
	  curl -fL --retry 3 --connect-timeout 30 -o "$(WEBARC).part" "$(WEBRTC_URL)" \
	    && mv "$(WEBARC).part" "$(WEBARC)" \
	    && printf '%s  %s\n' "$(WEBRTC_SHA)" "$(WEBARC)" | $(SHA256_CHECK) \
	    || { echo "[deps] ERROR libwebrtc: fallo de descarga o hash" >&2; rm -f "$(WEBARC).part"; exit 1; }; \
	fi
	@if [ -f "$(WEB_DEST)/lib/$(WEB_LIB)" ]; then \
	  echo "[deps] libwebrtc: ya extraído en $(WEB_DEST) ($(WEB_LIB))"; \
	else \
	  echo "[deps] libwebrtc: extrayendo a $(WEB_DEST)"; \
	  rm -rf "$(WEB_DEST)"; \
	  rm -rf "$(EXTERNS_DIR)/.webrtc-x" && mkdir -p "$(EXTERNS_DIR)/.webrtc-x"; \
	  if [ "$(WEB_KIND)" = "tar" ]; then tar xzf "$(WEBARC)" -C "$(EXTERNS_DIR)/.webrtc-x"; \
	  else unzip -q -o "$(WEBARC)" -d "$(EXTERNS_DIR)/.webrtc-x"; fi; \
	  mv "$(EXTERNS_DIR)/.webrtc-x/webrtc" "$(WEB_DEST)" && rm -rf "$(EXTERNS_DIR)/.webrtc-x"; \
	fi
ifeq ($(BA_WEBRTC_TARGET),linux)
	@# libwebrtc de Linux usa la libc++ de Chromium (std::__Cr). El SDK trae solo
	@# los .h de libc++: se bajan los headers completos y la configuracion de
	@# Chromium (__config_site/__assertion_handler) en los commits de VERSIONS,
	@# verificados por hash del CONTENIDO (los tar de gitiles no son byte-estables).
	@if [ -f "$(WEB_DEST)/libcxx/include/vector" ] && [ "$$(cd $(WEB_DEST)/libcxx/include && find . -type f | LC_ALL=C sort | xargs $(SHA256) | $(SHA256) | cut -d' ' -f1)" = "$(LIBCXX_TREE_SHA)" ]; then \
	  echo "[deps] libc++ (Chromium): headers ya presentes y verificados"; \
	else \
	  echo "[deps] libc++ (Chromium): bajando headers ($(LIBCXX_COMMIT))"; \
	  rm -rf "$(WEB_DEST)/libcxx"; mkdir -p "$(WEB_DEST)/libcxx/include"; \
	  curl -fsSL --retry 5 --retry-delay 3 "$(LIBCXX_URL)" | tar xz -C "$(WEB_DEST)/libcxx/include" \
	    || { echo "[deps] ERROR libc++: fallo de descarga" >&2; exit 1; }; \
	  test "$$(cd $(WEB_DEST)/libcxx/include && find . -type f | LC_ALL=C sort | xargs $(SHA256) | $(SHA256) | cut -d' ' -f1)" = "$(LIBCXX_TREE_SHA)" \
	    || { echo "[deps] ERROR libc++: el contenido no coincide con el hash fijado" >&2; exit 1; }; \
	fi
	@mkdir -p "$(WEB_DEST)/buildtools-libcxx"
	@for f in __config_site:$(LIBCXX_CFG_SHA) __assertion_handler:$(LIBCXX_ASSERT_SHA); do \
	  n=$${f%%:*}; h=$${f##*:}; d="$(WEB_DEST)/buildtools-libcxx/$$n"; \
	  if [ -f "$$d" ] && [ "$$($(SHA256) "$$d" | cut -d' ' -f1)" = "$$h" ]; then continue; fi; \
	  echo "[deps] libc++ (Chromium): bajando $$n ($(BUILDTOOLS_COMMIT))"; \
	  curl -fsSL --retry 5 --retry-delay 3 "$(BUILDTOOLS_URL)/$$n?format=TEXT" | base64 -d > "$$d.part" \
	    && [ "$$($(SHA256) "$$d.part" | cut -d' ' -f1)" = "$$h" ] && mv "$$d.part" "$$d" \
	    || { echo "[deps] ERROR libc++: $$n no coincide con el hash fijado" >&2; rm -f "$$d.part"; exit 1; }; \
	done
endif

# lwIP 2.2.0 (BSD): pila IP en espacio de usuario para el tunel WireGuard del
# gateway (src/gateway/net). El zip de Savannah es un archivo estatico: se fija
# su hash. Mismo codigo que el tag STABLE-2_2_0_RELEASE de GitHub (verificado).
LWIP_URL := https://download.savannah.nongnu.org/releases/lwip/lwip-2.2.0.zip
LWIP_SHA := e12c769be5a1da9a1edf1b8f38d645c6c87d52a26a636172cea4b8c63ec04994
LWIP_ZIP := $(DL_DIR)/lwip-2.2.0.zip
deps-lwip:
	@command -v curl >/dev/null 2>&1 || { echo "[deps] ERROR: falta curl" >&2; exit 1; }
	@mkdir -p "$(DL_DIR)" "$(EXTERNS_DIR)"
	@if [ -f "$(LWIP_ZIP)" ] && [ "$$($(SHA256) "$(LWIP_ZIP)" | cut -d' ' -f1)" = "$(LWIP_SHA)" ]; then \
	  echo "[deps] lwIP: ya presente y verificado"; \
	else \
	  echo "[deps] lwIP: descargando $(LWIP_URL)"; \
	  curl -fL --retry 3 -o "$(LWIP_ZIP).part" "$(LWIP_URL)" && mv "$(LWIP_ZIP).part" "$(LWIP_ZIP)" \
	    && printf '%s  %s\n' "$(LWIP_SHA)" "$(LWIP_ZIP)" | $(SHA256_CHECK) \
	    || { echo "[deps] ERROR lwIP: fallo de descarga o hash" >&2; rm -f "$(LWIP_ZIP).part"; exit 1; }; \
	fi
	@if [ -f "$(EXTERNS_DIR)/lwip/src/Filelists.cmake" ]; then \
	  echo "[deps] lwIP: ya extraido en $(EXTERNS_DIR)/lwip"; \
	else \
	  rm -rf "$(EXTERNS_DIR)/lwip" "$(EXTERNS_DIR)/lwip-2.2.0"; \
	  unzip -q "$(LWIP_ZIP)" -d "$(EXTERNS_DIR)" && mv "$(EXTERNS_DIR)/lwip-2.2.0" "$(EXTERNS_DIR)/lwip"; \
	fi

# --- Reconocimiento de rostros (ONNX Runtime + dos modelos) ---------------------
# PARA QUE: reconocer a alguien que ya estuvo —que el lobby sepa quien llego,
# que el rotulo salga con su nombre y su cargo sin tipearlo de nuevo—. Corre
# EN EL APARATO: rapido, sin red, sin costo por uso y siempre el mismo
# resultado. Un modelo remoto no sirve para esto: tarda segundos, cuesta en
# cada consulta y no contesta igual dos veces.
#
# LOS ROSTROS NO SALEN DE ACA. Se guarda un vector de numeros (el "embedding"),
# no la foto, y queda en la carpeta portable junto con lo demas.
#
# Todo MIT / Apache-2.0, fijado por hash. Pesa poco para lo que hace:
#   onnxruntime  1.20.1   6 MB (Linux) / 62 MB el zip de Windows / 7,5 MB macOS
#   YuNet        detector    228 KB
#   SFace int8   embedding   9,4 MB   (el fp32 son 37 MB y no hace falta)
ORT_VER := 1.20.1
ORT_LINUX_SHA := 67db4dc1561f1e3fd42e619575c82c601ef89849afc7ea85a003abbac1a1a105
ORT_WIN_SHA   := 78d447051e48bd2e1e778bba378bec4ece11191c9e538cf7b2c4a4565e8f5581
ORT_MAC_SHA   := b678fc3c2354c771fea4fba420edeccfba205140088334df801e7fc40e83a57a
ZOO := https://github.com/opencv/opencv_zoo/raw/main/models
YUNET_URL := $(ZOO)/face_detection_yunet/face_detection_yunet_2023mar.onnx
YUNET_SHA := 8f2383e4dd3cfbb4553ea8718107fc0423210dc964f9f4280604804ed2552fa4
SFACE_URL := $(ZOO)/face_recognition_sface/face_recognition_sface_2021dec_int8.onnx
SFACE_SHA := 2b0e941e6f16cc048c20aee0c8e31f569118f65d702914540f7bfdc14048d78a

# El paquete del runtime segun para donde se compile (BA_ORT_TARGET).
# Por defecto, el del sistema donde se compila (make win pasa windows).
ifeq ($(shell uname -s),Darwin)
  BA_ORT_TARGET ?= macos
else
  BA_ORT_TARGET ?= linux
endif
ifeq ($(BA_ORT_TARGET),windows)
  ORT_PKG := onnxruntime-win-x64-$(ORT_VER).zip
  ORT_SHA := $(ORT_WIN_SHA)
else ifeq ($(BA_ORT_TARGET),macos)
  ORT_PKG := onnxruntime-osx-arm64-$(ORT_VER).tgz
  ORT_SHA := $(ORT_MAC_SHA)
else
  ORT_PKG := onnxruntime-linux-x64-$(ORT_VER).tgz
  ORT_SHA := $(ORT_LINUX_SHA)
endif
ORT_URL := https://github.com/microsoft/onnxruntime/releases/download/v$(ORT_VER)/$(ORT_PKG)

deps-rostros:
	@command -v curl >/dev/null 2>&1 || { echo "[deps] ERROR: falta curl" >&2; exit 1; }
	@mkdir -p "$(DL_DIR)" "$(EXTERNS_DIR)/rostros/modelos"
	@$(MAKE) --no-print-directory _ba_baja ARCHIVO="$(DL_DIR)/$(ORT_PKG)" URL="$(ORT_URL)" SHA="$(ORT_SHA)" QUE="onnxruntime"
	@$(MAKE) --no-print-directory _ba_baja ARCHIVO="$(EXTERNS_DIR)/rostros/modelos/yunet.onnx" URL="$(YUNET_URL)" SHA="$(YUNET_SHA)" QUE="YuNet (detector)"
	@$(MAKE) --no-print-directory _ba_baja ARCHIVO="$(EXTERNS_DIR)/rostros/modelos/sface.onnx" URL="$(SFACE_URL)" SHA="$(SFACE_SHA)" QUE="SFace (embedding)"
	@if [ -d "$(EXTERNS_DIR)/rostros/onnxruntime/include" ]; then 	  echo "[deps] rostros: onnxruntime ya extraido"; 	else 	  rm -rf "$(EXTERNS_DIR)/rostros/onnxruntime" "$(EXTERNS_DIR)/rostros/.tmp"; 	  mkdir -p "$(EXTERNS_DIR)/rostros/.tmp"; 	  case "$(ORT_PKG)" in 	    *.zip) unzip -q "$(DL_DIR)/$(ORT_PKG)" -d "$(EXTERNS_DIR)/rostros/.tmp" ;; 	    *) tar xzf "$(DL_DIR)/$(ORT_PKG)" -C "$(EXTERNS_DIR)/rostros/.tmp" ;; 	  esac; 	  mv "$(EXTERNS_DIR)/rostros/.tmp/"* "$(EXTERNS_DIR)/rostros/onnxruntime"; 	  rmdir "$(EXTERNS_DIR)/rostros/.tmp"; 	fi
	@echo "[deps] rostros: listo en $(EXTERNS_DIR)/rostros"

# Bajar y verificar, o no dejar nada: un archivo a medias que pasa por bueno
# es peor que no tenerlo (se descubre al usarlo, lejos de aca).
_ba_baja:
	@if [ -f "$(ARCHIVO)" ] && [ "$$($(SHA256) "$(ARCHIVO)" | cut -d' ' -f1)" = "$(SHA)" ]; then 	  echo "[deps] $(QUE): ya presente y verificado"; 	else 	  echo "[deps] $(QUE): descargando"; 	  curl -fL --retry 3 -o "$(ARCHIVO).part" "$(URL)" 	    && printf '%s  %s\n' "$(SHA)" "$(ARCHIVO).part" | $(SHA256_CHECK) 	    && mv "$(ARCHIVO).part" "$(ARCHIVO)" 	    || { echo "[deps] ERROR $(QUE): fallo de descarga o hash" >&2; rm -f "$(ARCHIVO).part"; exit 1; }; 	fi

# --- Raices de certificados (HTTPS propio de A: API de YouTube) -----------------
# El cliente HTTPS usa mbedtls (vendoreado): nada de libssl del sistema. Las
# raices son las de Mozilla empaquetadas por curl, con fecha y hash fijos.
# libvterm (MIT): emulador de terminal de la pestaña Asistente (la TUI del
# agente de IA corre adentro). C puro, se compila estatico en ba-app.
LIBVTERM_URL := https://launchpad.net/libvterm/trunk/v0.3/+download/libvterm-0.3.3.tar.gz
LIBVTERM_SHA := 09156f43dd2128bd347cbeebe50d9a571d32c64e0cf18d211197946aff7226e0
# JetBrains Mono (OFL 1.1): fuente monoespaciada de la terminal.
FONTS_URL := https://github.com/JetBrains/JetBrainsMono/releases/download/v2.304/JetBrainsMono-2.304.zip
FONTS_SHA := 6f6376c6ed2960ea8a963cd7387ec9d76e3f629125bc33d1fdcd7eb7012f7bbf

deps-libvterm:
	@mkdir -p "$(DL_DIR)"
	@if [ -f "$(EXTERNS_DIR)/libvterm/src/vterm.c" ]; then echo "[deps] libvterm: ya presente"; else \
	  f="$(DL_DIR)/libvterm-0.3.3.tar.gz"; \
	  [ -f "$$f" ] && [ "$$($(SHA256) "$$f" | cut -d' ' -f1)" = "$(LIBVTERM_SHA)" ] || curl -fL --retry 3 -o "$$f" "$(LIBVTERM_URL)"; \
	  printf '%s  %s\n' "$(LIBVTERM_SHA)" "$$f" | $(SHA256_CHECK) || { echo "[deps] ERROR libvterm: hash" >&2; exit 1; }; \
	  rm -rf "$(EXTERNS_DIR)/libvterm" && mkdir -p "$(EXTERNS_DIR)/libvterm" && tar xzf "$$f" -C "$(EXTERNS_DIR)/libvterm" --strip-components=1; \
	  echo "[deps] libvterm: listo"; fi

deps-fonts:
	@mkdir -p "$(DL_DIR)"
	@if [ -f "$(EXTERNS_DIR)/fonts/JetBrainsMono-Regular.ttf" ]; then echo "[deps] fuentes: ya presentes"; else \
	  f="$(DL_DIR)/JetBrainsMono-2.304.zip"; \
	  [ -f "$$f" ] && [ "$$($(SHA256) "$$f" | cut -d' ' -f1)" = "$(FONTS_SHA)" ] || curl -fL --retry 3 -o "$$f" "$(FONTS_URL)"; \
	  printf '%s  %s\n' "$(FONTS_SHA)" "$$f" | $(SHA256_CHECK) || { echo "[deps] ERROR fuentes: hash" >&2; exit 1; }; \
	  mkdir -p "$(EXTERNS_DIR)/fonts" && unzip -j -o -q "$$f" "fonts/ttf/JetBrainsMono-Regular.ttf" "fonts/ttf/JetBrainsMono-Bold.ttf" \
	    "fonts/ttf/JetBrainsMono-Italic.ttf" "fonts/ttf/JetBrainsMono-BoldItalic.ttf" "OFL.txt" -d "$(EXTERNS_DIR)/fonts"; \
	  echo "[deps] fuentes: listas"; fi

CACERT_URL := https://curl.se/ca/cacert-2026-08-13.pem
CACERT_SHA := f66dff1bdf8f96060b8177976f8b7d9254bc89bc4db933d769f7384d28480bc9
deps-cacert:
	@command -v curl >/dev/null 2>&1 || { echo "[deps] ERROR: falta curl" >&2; exit 1; }
	@mkdir -p "$(EXTERNS_DIR)/cacert"
	@if [ -f "$(EXTERNS_DIR)/cacert/cacert.pem" ] && [ "$$($(SHA256) "$(EXTERNS_DIR)/cacert/cacert.pem" | cut -d' ' -f1)" = "$(CACERT_SHA)" ]; then \
	  echo "[deps] cacert: ya presente y verificado"; \
	else \
	  echo "[deps] cacert: descargando $(CACERT_URL)"; \
	  curl -fL --retry 3 -o "$(EXTERNS_DIR)/cacert/cacert.pem.part" "$(CACERT_URL)" \
	    && printf '%s  %s\n' "$(CACERT_SHA)" "$(EXTERNS_DIR)/cacert/cacert.pem.part" | $(SHA256_CHECK) \
	    && mv "$(EXTERNS_DIR)/cacert/cacert.pem.part" "$(EXTERNS_DIR)/cacert/cacert.pem" \
	    || { echo "[deps] ERROR cacert: fallo de descarga o hash" >&2; rm -f "$(EXTERNS_DIR)/cacert/cacert.pem.part"; exit 1; }; \
	fi

deps-spout:
	@command -v curl >/dev/null 2>&1 || { echo "[deps] ERROR: falta curl" >&2; exit 1; }
	@mkdir -p "$(DL_DIR)" "$(EXTERNS_DIR)"
	@if [ -f "$(SPOUT_TAR)" ] && [ "$$($(SHA256) "$(SPOUT_TAR)" | cut -d' ' -f1)" = "$(SPOUT_SHA)" ]; then \
	  echo "[deps] Spout2: ya presente y verificado ($(SPOUT_TAR))"; \
	else \
	  echo "[deps] Spout2: descargando $(SPOUT_URL)"; \
	  curl -fL --retry 3 --connect-timeout 30 -o "$(SPOUT_TAR).part" "$(SPOUT_URL)" \
	    && mv "$(SPOUT_TAR).part" "$(SPOUT_TAR)" \
	    && printf '%s  %s\n' "$(SPOUT_SHA)" "$(SPOUT_TAR)" | $(SHA256_CHECK) \
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
	@if [ -f "$(CFL_EXE)" ] && [ "$$($(SHA256) "$(CFL_EXE)" | cut -d' ' -f1)" = "$(CFL_SHA)" ]; then \
	  echo "[deps] cloudflared: ya presente y verificado ($(CFL_EXE))"; \
	else \
	  echo "[deps] cloudflared: descargando $(CFL_URL)"; \
	  curl -fL --retry 3 --connect-timeout 30 -o "$(CFL_EXE).part" "$(CFL_URL)" \
	    && mv "$(CFL_EXE).part" "$(CFL_EXE)" \
	    && printf '%s  %s\n' "$(CFL_SHA)" "$(CFL_EXE)" | $(SHA256_CHECK) \
	    || { echo "[deps] ERROR cloudflared: fallo de descarga o hash" >&2; rm -f "$(CFL_EXE).part"; exit 1; }; \
	fi

# cloudflared del host (Linux amd64/arm64 o macOS arm64), verificado por
# SHA-256. En macOS el asset es un .tgz con el binario adentro.
deps-cloudflared-host:
	@command -v curl >/dev/null 2>&1 || { echo "[deps] ERROR: falta curl" >&2; exit 1; }
	@test -n "$(CFL_HOST_ASSET)" || { echo "[deps] cloudflared: host $(CFL_HOST_OS) sin binario conocido" >&2; exit 1; }
	@mkdir -p "$(dir $(CFL_HOST_BIN))" "$(DL_DIR)"
	@if [ -x "$(CFL_HOST_BIN)" ]; then \
	  echo "[deps] cloudflared ($(CFL_HOST_ASSET)): ya presente ($(CFL_HOST_BIN))"; \
	else \
	  echo "[deps] cloudflared: descargando $(CFL_HOST_URL)"; \
	  curl -fL --retry 3 --connect-timeout 30 -o "$(DL_DIR)/$(CFL_HOST_ASSET)" "$(CFL_HOST_URL)" \
	    && printf '%s  %s\n' "$(CFL_HOST_SHA)" "$(DL_DIR)/$(CFL_HOST_ASSET)" | $(SHA256_CHECK) \
	    || { echo "[deps] ERROR cloudflared: fallo de descarga o hash" >&2; rm -f "$(DL_DIR)/$(CFL_HOST_ASSET)"; exit 1; }; \
	  case "$(CFL_HOST_ASSET)" in \
	    *.tgz) tar -xzf "$(DL_DIR)/$(CFL_HOST_ASSET)" -C "$(dir $(CFL_HOST_BIN))" ;; \
	    *) cp "$(DL_DIR)/$(CFL_HOST_ASSET)" "$(CFL_HOST_BIN)" ;; \
	  esac; \
	  chmod +x "$(CFL_HOST_BIN)"; \
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
	@command -v 7z >/dev/null 2>&1 || { echo "[deps] ERROR: falta 7z (p7zip) para LSP" >&2; exit 1; }
	@mkdir -p "$(DL_DIR)"
	@if [ -f "$(LSP_7Z)" ] && [ "$$($(SHA256) "$(LSP_7Z)" | cut -d' ' -f1)" = "$(LSP_SHA)" ]; then \
	  echo "[deps] LSP: ya presente y verificado ($(LSP_7Z))"; \
	else \
	  echo "[deps] LSP: descargando $(LSP_URL)"; \
	  curl -fL --retry 3 --connect-timeout 30 -o "$(LSP_7Z).part" "$(LSP_URL)" \
	    && mv "$(LSP_7Z).part" "$(LSP_7Z)" \
	    && printf '%s  %s\n' "$(LSP_SHA)" "$(LSP_7Z)" | $(SHA256_CHECK) \
	    || { echo "[deps] ERROR LSP: fallo de descarga o hash" >&2; rm -f "$(LSP_7Z).part"; exit 1; }; \
	fi
	@# Solo el formato CLAP (lo que carga el host) y su licencia.
	@if [ -f "$(LSP_DEST)/linux-x86_64/lsp-plugins.clap" ]; then \
	  echo "[deps] LSP: CLAP de Linux ya extraido en $(LSP_DEST)/linux-x86_64"; \
	else \
	  rm -rf "$(LSP_DEST)/linux-x86_64" "$(LSP_DEST)/.x" && mkdir -p "$(LSP_DEST)/linux-x86_64"; \
	  7z x -y -o"$(LSP_DEST)/.x" "$(LSP_7Z)" 'lsp-plugins-$(LSP_VER)-Linux-x86_64/CLAP/*' \
	    'lsp-plugins-$(LSP_VER)-Linux-x86_64/COPYING*.txt' 'lsp-plugins-$(LSP_VER)-Linux-x86_64/LICENSE_OFL.txt' >/dev/null \
	    && mv "$(LSP_DEST)/.x/lsp-plugins-$(LSP_VER)-Linux-x86_64/CLAP/"* "$(LSP_DEST)/linux-x86_64/" \
	    && mkdir -p "$(LSP_DEST)/licencias" \
	    && mv "$(LSP_DEST)/.x/lsp-plugins-$(LSP_VER)-Linux-x86_64/"*.txt "$(LSP_DEST)/licencias/"; \
	  rm -rf "$(LSP_DEST)/.x"; \
	  patchelf --set-rpath '$$ORIGIN:$$ORIGIN/../../obs-deps-linux/lib' "$(LSP_DEST)/linux-x86_64/lsp-plugins.clap"; \
	  echo "[deps] LSP: CLAP de Linux en $(LSP_DEST)/linux-x86_64"; \
	fi

# CLAP (free-audio/clap, MIT): headers del formato de plugins que carga el host.
CLAP_VER := 1.2.6
CLAP_SHA := 6548b5831ad9e92551c752753e535b18fead4001165ef1df8e643efefa7133b7
# --- LibreOffice (opcional) --------------------------------------------------
# Solo para convertir .pptx a PDF y de ahi a imagenes (ver docs/CARRUSEL.md).
# NO entra en `make deps`: hay que pedirlo, porque son ~420 MB por plataforma y
# el que recibe PDF no lo necesita. El bundle lo incluye solo si esta.
# En la Mac, el de macOS (el .dmg se monta con hdiutil: solo se arma ahi).
deps-libreoffice:
	@if [ "$$(uname -s)" = Darwin ]; then tools/deps/libreoffice.sh mac; else tools/deps/libreoffice.sh todo; fi

deps-libreoffice-mac:
	tools/deps/libreoffice.sh mac

deps-libreoffice-linux:
	tools/deps/libreoffice.sh linux

deps-libreoffice-win:
	tools/deps/libreoffice.sh win

# Para abrir el .msi de Windows desde Linux. Lo llama deps-libreoffice-win solo
# si hace falta; esta aparte para poder pedirlo suelto. Es una herramienta de
# COMPILACION: no viaja en el bundle y no se instala en el sistema.
deps-msitools:
	tools/deps/msitools.sh

# El inventario del MCP a docs/mcp.json: que herramientas hay, con su ayuda,
# sus etiquetas de busqueda, en que archivo viven y que roles las pueden usar.
# NECESITA EL APARATO ANDANDO: la mitad del dato (descripciones y esquemas) sale
# de preguntarle a el, que es lo que el modelo ve de verdad. Sacarlo del codigo
# obligaria a interpretar literales de C++ partidos en pedazos.
mcp-inventario:
	tools/mcp-inventario.py

# llama.cpp (CPU) para medir un modelo chico corriendo ACA. Ver el comentario
# del script: la GPU no esta libre, se la come libobs.
deps-llamacpp:
	tools/deps/llamacpp.sh

# Cuanto tarda y cuanto acierta ese modelo chico, con el prefijo cacheado.
# Hace falta un .gguf en extern/modelos (no viaja en el repo: son gigas).
bateria-local:
	tools/bateria-local.py --modelo extern/modelos/Qwen3-1.7B-Q4_K_M.gguf --solo-nombre

# El conjunto de entrenamiento del modelo chico, desde las semillas escritas a
# mano. El examen (el corpus de siempre) queda intacto: es el unico numero
# honesto que hay.
corpus-entrenamiento:
	tools/corpus-entrenamiento.py

## El entorno de entrenamiento, por script: nada se instala a mano.
venv-entrenar:
	tools/entrenar/venv.sh

## El vector de control y el barrido de su escala. MODELO= el ya entrenado.
## Sale un .gguf que llama.cpp aplica con --control-vector-scaled.
vector-control:
	@test -n "$(MODELO)" || { echo "uso: make vector-control MODELO=<ruta>"; exit 1; }
	.venv-entrenar/bin/python tools/entrenar/vector-control.py \
		--modelo "$(MODELO)" --salida "$(MODELO)-derivar.gguf"
	PYTHON=.venv-entrenar/bin/python tools/entrenar/barrer-escala.sh \
		"$(MODELO)" "$(MODELO)-derivar.gguf"

# Que herramientas nuevas del MCP no tienen frases, y si el prompt cambio
# —que es cuando el modelo entrenado quedo viejo aunque las frases esten—.
# Con --generar propone frases desde las etiquetas del buscador, PARA REVISAR.
corpus-descubrir:
	tools/corpus-descubrir.py

## EL MODELO REACTIVO, AUTOMATICO. Cada vez que el MCP crece: inventario, json de
## equivalencias, bucle del corpus (cobertura, sentido, CDNV, Frobenius, margen)
## y fabrica (entrenar, medir, MLX q4, GGUF Q4_K_M con la cabeza MTP entrenada,
## carpetas con forma de repo de Hugging Face; no sube nada).
##   PERFIL: datos/perfiles/<perfil>.json (que herramientas, por categoria)
##   BASE:   Qwen/Qwen3.5-2B | -4B | -9B | unsloth/Qwen3.5-4B-MTP-GGUF ... (se detecta sola)
##   HF_USUARIO=x SUBIR=1: sube los repos (MLX q4 y GGUF) como privados al terminar
PERFIL ?= reactivo
BASE ?= Qwen/Qwen3.5-4B
VUELTAS ?= 2
modelo: venv-entrenar venv-mlx deps-llamacpp deps-llamacpp-convertir
	.venv-entrenar/bin/python tools/entrenar/actualizar-modelo.py --perfil $(PERFIL) --base $(BASE) --vueltas $(VUELTAS) \
		$(if $(HF_USUARIO),--hf-usuario $(HF_USUARIO)) $(if $(SUBIR),--subir)

## Solo la fabrica, con el corpus que hay (NOMBRE= la carpeta en ~/entrenamientos/fabrica).
modelo-fabricar: venv-entrenar venv-mlx deps-llamacpp deps-llamacpp-convertir
	@test -n "$(NOMBRE)" || { echo "uso: make modelo-fabricar NOMBRE=<nombre> [PERFIL=] [BASE=]"; exit 1; }
	.venv-entrenar/bin/python tools/entrenar/fabricar-modelo.py --perfil $(PERFIL) --base $(BASE) --nombre $(NOMBRE)

## Solo el bucle del corpus (NOMBRE= la corrida; MEDIDOR= un adaptador para medir, si hay).
modelo-corpus: venv-entrenar
	@test -n "$(NOMBRE)" || { echo "uso: make modelo-corpus NOMBRE=<nombre> [PERFIL=] [BASE=] [MEDIDOR=]"; exit 1; }
	.venv-entrenar/bin/python tools/entrenar/bucle-corpus-completo.py --perfil $(PERFIL) --base $(BASE) \
		--nombre $(NOMBRE) --vueltas $(VUELTAS) $(if $(MEDIDOR),--medidor $(MEDIDOR))

## Que herramientas hay y cuales entran en el perfil.
modelo-inventario:
	.venv-entrenar/bin/python tools/entrenar/inventario-mcp.py --perfil $(PERFIL)

## El json de equivalencias al dia (si el perfil contesta con etiquetas).
modelo-etiquetas: venv-entrenar
	.venv-entrenar/bin/python tools/entrenar/completar-etiquetas.py --perfil $(PERFIL)

## MLX (para la Mac) y el conversor HF -> GGUF de llama.cpp, por script.
venv-mlx:
	tools/entrenar/venv-mlx.sh
deps-llamacpp-convertir:
	tools/deps/llamacpp-convertir.sh

deps-clap:
	@mkdir -p "$(DL_DIR)"
	@if [ -f "$(EXTERNS_DIR)/clap/include/clap/clap.h" ]; then echo "[deps] CLAP: ya presente"; else \
	  f="$(DL_DIR)/clap-$(CLAP_VER).tar.gz"; \
	  [ -f "$$f" ] && [ "$$($(SHA256) "$$f" | cut -d' ' -f1)" = "$(CLAP_SHA)" ] || \
	    curl -fL --retry 3 -o "$$f" "https://codeload.github.com/free-audio/clap/tar.gz/refs/tags/$(CLAP_VER)"; \
	  printf '%s  %s\n' "$(CLAP_SHA)" "$$f" | $(SHA256_CHECK) || { echo "[deps] ERROR CLAP: hash" >&2; exit 1; }; \
	  rm -rf "$(EXTERNS_DIR)/clap" && mkdir -p "$(EXTERNS_DIR)/clap" && tar xzf "$$f" -C "$(EXTERNS_DIR)/clap" --strip-components=1; \
	  echo "[deps] CLAP $(CLAP_VER): headers en $(EXTERNS_DIR)/clap"; fi

# --- Fuentes de BUILD (se compilan en el producto) ---------------------------
# Agrupado: baja y extrae los 4 árboles de código de terceros que el
# CMakeLists raíz consume vía FetchContent. Con `extern/` poblado, el configure
# es OFFLINE (prefiere la copia vendored); sin él, cae a git.
deps-src: deps-mbedtls deps-nlohmann deps-samplerate deps-argon2

deps-mbedtls:
	@command -v curl >/dev/null 2>&1 || { echo "[deps] ERROR: falta curl" >&2; exit 1; }
	@mkdir -p "$(DL_DIR)" "$(MBEDTLS_DEST)"
	@if [ -f "$(MBEDTLS_TAR)" ] && [ "$$($(SHA256) "$(MBEDTLS_TAR)" | cut -d' ' -f1)" = "$(MBEDTLS_SHA)" ]; then \
	  echo "[deps] mbedtls: ya presente y verificado ($(MBEDTLS_TAR))"; \
	else \
	  echo "[deps] mbedtls: descargando $(MBEDTLS_URL)"; \
	  curl -fL --retry 3 --connect-timeout 30 -o "$(MBEDTLS_TAR).part" "$(MBEDTLS_URL)" \
	    && mv "$(MBEDTLS_TAR).part" "$(MBEDTLS_TAR)" \
	    && printf '%s  %s\n' "$(MBEDTLS_SHA)" "$(MBEDTLS_TAR)" | $(SHA256_CHECK) \
	    || { echo "[deps] ERROR mbedtls: fallo de descarga o hash" >&2; rm -f "$(MBEDTLS_TAR).part"; exit 1; }; \
	fi
	@if [ -f "$(MBEDTLS_SENT)" ]; then \
	  echo "[deps] mbedtls: ya extraído en $(MBEDTLS_DEST)"; \
	else \
	  echo "[deps] mbedtls: extrayendo a $(MBEDTLS_DEST)"; \
	  rm -rf "$(MBEDTLS_DEST)"; \
	  mkdir -p "$(MBEDTLS_DEST)"; \
	  tar xzf "$(MBEDTLS_TAR)" -C "$(MBEDTLS_DEST)" --strip-components=1; \
	fi
	# Submodule `framework`: codeload no lo incluye (queda framework/ vacío) y
	# el CMakeLists de mbedtls hace FATAL_ERROR si framework/CMakeLists.txt no
	# existe, así que se vendorea aparte y se extrae dentro de
	# extern/mbedtls/framework. Idempotente: sentinel + tar verificado -> skip.
	@if [ -f "$(MBEDTLS_FW_SENT)" ] && [ -f "$(MBEDTLS_FW_TAR)" ] && [ "$$($(SHA256) "$(MBEDTLS_FW_TAR)" | cut -d' ' -f1)" = "$(MBEDTLS_FW_SHA)" ]; then \
	  echo "[deps] mbedtls-framework: ya presente y verificado ($(MBEDTLS_FW_TAR))"; \
	else \
	  echo "[deps] mbedtls-framework: descargando $(MBEDTLS_FW_URL)"; \
	  curl -fL --retry 3 --connect-timeout 30 -o "$(MBEDTLS_FW_TAR).part" "$(MBEDTLS_FW_URL)" \
	    && mv "$(MBEDTLS_FW_TAR).part" "$(MBEDTLS_FW_TAR)" \
	    && printf '%s  %s\n' "$(MBEDTLS_FW_SHA)" "$(MBEDTLS_FW_TAR)" | $(SHA256_CHECK) \
	    || { echo "[deps] ERROR mbedtls-framework: fallo de descarga o hash" >&2; rm -f "$(MBEDTLS_FW_TAR).part"; exit 1; }; \
	  echo "[deps] mbedtls-framework: extrayendo a $(MBEDTLS_FW_DEST)"; \
	  rm -rf "$(MBEDTLS_FW_DEST)"; \
	  mkdir -p "$(MBEDTLS_FW_DEST)"; \
	  tar xzf "$(MBEDTLS_FW_TAR)" -C "$(MBEDTLS_FW_DEST)" --strip-components=1; \
	fi

deps-nlohmann:
	@command -v curl >/dev/null 2>&1 || { echo "[deps] ERROR: falta curl" >&2; exit 1; }
	@mkdir -p "$(DL_DIR)" "$(NLOHMANN_DEST)"
	@if [ -f "$(NLOHMANN_TAR)" ] && [ "$$($(SHA256) "$(NLOHMANN_TAR)" | cut -d' ' -f1)" = "$(NLOHMANN_SHA)" ]; then \
	  echo "[deps] nlohmann: ya presente y verificado ($(NLOHMANN_TAR))"; \
	else \
	  echo "[deps] nlohmann: descargando $(NLOHMANN_URL)"; \
	  curl -fL --retry 3 --connect-timeout 30 -o "$(NLOHMANN_TAR).part" "$(NLOHMANN_URL)" \
	    && mv "$(NLOHMANN_TAR).part" "$(NLOHMANN_TAR)" \
	    && printf '%s  %s\n' "$(NLOHMANN_SHA)" "$(NLOHMANN_TAR)" | $(SHA256_CHECK) \
	    || { echo "[deps] ERROR nlohmann: fallo de descarga o hash" >&2; rm -f "$(NLOHMANN_TAR).part"; exit 1; }; \
	fi
	@if [ -f "$(NLOHMANN_SENT)" ]; then \
	  echo "[deps] nlohmann: ya extraído en $(NLOHMANN_DEST)"; \
	else \
	  echo "[deps] nlohmann: extrayendo a $(NLOHMANN_DEST)"; \
	  rm -rf "$(NLOHMANN_DEST)"; \
	  mkdir -p "$(NLOHMANN_DEST)"; \
	  tar xzf "$(NLOHMANN_TAR)" -C "$(NLOHMANN_DEST)" --strip-components=1; \
	fi

deps-samplerate:
	@command -v curl >/dev/null 2>&1 || { echo "[deps] ERROR: falta curl" >&2; exit 1; }
	@mkdir -p "$(DL_DIR)" "$(SAMPLERATE_DEST)"
	@if [ -f "$(SAMPLERATE_TAR)" ] && [ "$$($(SHA256) "$(SAMPLERATE_TAR)" | cut -d' ' -f1)" = "$(SAMPLERATE_SHA)" ]; then \
	  echo "[deps] libsamplerate: ya presente y verificado ($(SAMPLERATE_TAR))"; \
	else \
	  echo "[deps] libsamplerate: descargando $(SAMPLERATE_URL)"; \
	  curl -fL --retry 3 --connect-timeout 30 -o "$(SAMPLERATE_TAR).part" "$(SAMPLERATE_URL)" \
	    && mv "$(SAMPLERATE_TAR).part" "$(SAMPLERATE_TAR)" \
	    && printf '%s  %s\n' "$(SAMPLERATE_SHA)" "$(SAMPLERATE_TAR)" | $(SHA256_CHECK) \
	    || { echo "[deps] ERROR libsamplerate: fallo de descarga o hash" >&2; rm -f "$(SAMPLERATE_TAR).part"; exit 1; }; \
	fi
	@if [ -f "$(SAMPLERATE_SENT)" ]; then \
	  echo "[deps] libsamplerate: ya extraído en $(SAMPLERATE_DEST)"; \
	else \
	  echo "[deps] libsamplerate: extrayendo a $(SAMPLERATE_DEST)"; \
	  rm -rf "$(SAMPLERATE_DEST)"; \
	  mkdir -p "$(SAMPLERATE_DEST)"; \
	  tar xzf "$(SAMPLERATE_TAR)" -C "$(SAMPLERATE_DEST)" --strip-components=1; \
	fi

deps-argon2:
	@command -v curl >/dev/null 2>&1 || { echo "[deps] ERROR: falta curl" >&2; exit 1; }
	@mkdir -p "$(DL_DIR)" "$(ARGON2_DEST)"
	@if [ -f "$(ARGON2_TAR)" ] && [ "$$($(SHA256) "$(ARGON2_TAR)" | cut -d' ' -f1)" = "$(ARGON2_SHA)" ]; then \
	  echo "[deps] argon2: ya presente y verificado ($(ARGON2_TAR))"; \
	else \
	  echo "[deps] argon2: descargando $(ARGON2_URL)"; \
	  curl -fL --retry 3 --connect-timeout 30 -o "$(ARGON2_TAR).part" "$(ARGON2_URL)" \
	    && mv "$(ARGON2_TAR).part" "$(ARGON2_TAR)" \
	    && printf '%s  %s\n' "$(ARGON2_SHA)" "$(ARGON2_TAR)" | $(SHA256_CHECK) \
	    || { echo "[deps] ERROR argon2: fallo de descarga o hash" >&2; rm -f "$(ARGON2_TAR).part"; exit 1; }; \
	fi
	@if [ -f "$(ARGON2_SENT)" ]; then \
	  echo "[deps] argon2: ya extraído en $(ARGON2_DEST)"; \
	else \
	  echo "[deps] argon2: extrayendo a $(ARGON2_DEST)"; \
	  rm -rf "$(ARGON2_DEST)"; \
	  mkdir -p "$(ARGON2_DEST)"; \
	  tar xzf "$(ARGON2_TAR)" -C "$(ARGON2_DEST)" --strip-components=1; \
	fi

# --- Estado / limpieza de dependencias --------------------------------------
deps-status:
	@echo "Assets de terceros en $(EXTERNS_DIR)/:"
	@echo "  OBS $(OBS_TAG)          : $$( [ -f '$(OBS_DEST)/obs-plugins/64bit/libcef.dll' ] && echo OK || echo ausente )"
	@echo "  Qt6 $(QT6_VER) (win64_msvc2019_64): $$( [ -f '$(QT6_SENTINEL)' ] && echo OK || echo ausente )"
	@echo "  libwebrtc M123 ($(BA_WEBRTC_TARGET)): $$( [ -f '$(WEB_DEST)/lib/$(WEB_LIB)' ] && echo OK || echo ausente )"
	@echo "  Spout2 $(SPOUT2_TAG)          : $$( [ -f '$(SPOUT_DEST)/SPOUTSDK/SpoutDirectX/SpoutDX/SpoutDX.h' ] && echo OK || echo ausente )"
	@echo "  cloudflared $(CLOUDFLARED_VER)     : $$( [ -f '$(CFL_EXE)' ] && echo OK || echo ausente )"
	@echo "  VC++ redist           : $$( [ -f '$(VCREDIST_EXE)' ] && echo OK || echo ausente )"
	@echo "  LSP $(LSP_VER)           : $$( [ -f '$(LSP_DEST)/.deps-lsp-$(LSP_VER)' ] && echo OK || echo ausente )"
	@echo "  --- Fuentes de BUILD (se compilan en el producto) ---"
	@echo "  mbedtls v3.6.2        : $$( [ -f '$(MBEDTLS_SENT)' ] && echo OK || echo ausente )"
	@echo "  nlohmann v3.11.3      : $$( [ -f '$(NLOHMANN_SENT)' ] && echo OK || echo ausente )"
	@echo "  libsamplerate 0.2.2   : $$( [ -f '$(SAMPLERATE_SENT)' ] && echo OK || echo ausente )"
	@echo "  argon2 20190702       : $$( [ -f '$(ARGON2_SENT)' ] && echo OK || echo ausente )"

deps-clean:
	@echo "[deps] borrando $(EXTERNS_DIR)/"
	@rm -rf "$(EXTERNS_DIR)"

# --- Limpieza ----------------------------------------------------------------
clean:
	rm -rf $(BUILD_DIR)

clean-all:
	rm -rf $(BUILD_DIR) release docs/_build
