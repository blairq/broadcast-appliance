# mk/windows.mk — Windows x64 desde Linux (clang-cl): dependencias, build, tests bajo Wine y el .zip.
# Lo incluye el Makefile de la raíz (las variables comunes están allá).

.PHONY: deps-win-toolchain deps-webrtc-win deps-win deps-win-todo deps-lsp-win deps-lspdsp-win \
        deps-qt deps-ncnn-win deps-onnxruntime-win deps-sherpa-onnx-win deps-cloudflared \
        deps-pinggy-win deps-chrome-headless-win deps-craft-win deps-msitools \
        deps-llamacpp-win deps-python-win deps-herramientas-win deps-gstreamer-win \
        deps-gstreamer-win-pdb deps-gst-python-win win test-win release-win dist-win dist-win-zip

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
# El Qt del host de la máquina que compila (moc, rcc, qmlcachegen): x86_64 o arm64.
QT6_HOST_PATH = $(CURDIR)/$(EXTERNS_DIR)/qt6-host/$(QT6_VER)/$(QT6_HOST_DIR)

deps-win-toolchain:
	tools/deps/win-toolchain.sh

deps-webrtc-win:
	$(MAKE) deps-webrtc BA_WEBRTC_TARGET=windows

# Todo lo que el build de Windows necesita. Qt de Windows (msvc2022_64: SDK y
# también las DLL que viajan) mas el Qt del host (moc, rcc, qmlcachegen corren
# en Linux), GStreamer oficial de MSVC (trae su FFmpeg), libwebrtc de Windows y
# las fuentes compartidas.

deps-win:
	@$(DEPS_ATAJO) --nombre deps-win --clave-en $(EXTERNS_DIR)/.atajos/deps-win.json -- $(MAKE) deps-win-todo

deps-win-todo: deps-win-toolchain deps-qt deps-qt-host deps-ndi-src deps-webrtc-win \
          deps-src deps-estaticas-win deps-lwip deps-cacert deps-libvterm deps-fonts deps-cloudflared deps-pinggy-win deps-clap deps-lsp-win deps-lspdsp-win \
          deps-craft-win deps-fuentes-texto deps-herramientas-win deps-gstreamer-win deps-gstreamer-win-pdb deps-python-win deps-gst-python-win \
          deps-rostros deps-onnxruntime-win deps-sherpa-onnx-win deps-ncnn-win deps-llamacpp-win deps-chrome-headless-win deps-hinchada

# Plugins LSP (CLAP) para Windows: compilados desde fuente (tools/deps/lsp-win.sh).
deps-lsp-win: deps-win-toolchain
	tools/deps/lsp-win.sh $(JOBS)

# ba-lspdsp: el DSP de LSP con API en C (el Mezclador mide con eso). Solo sus
# bibliotecas, desde la fuente de LSP, con el compilador de LSP de cada sistema.
deps-lspdsp-win: deps-win-toolchain
	tools/deps/lspdsp.sh windows-x86_64 $(JOBS)

# Qt 6 (win64_msvc2022_64): SDK de BUILD (headers + import libs + CMake config).
# Vía aqtinstall (en el .venv local): baja el componente exacto y lo deja en
# extern/qt6/$(QT6_VER)/msvc2022_64/{include,lib,bin} (incl. lib/cmake/Qt6/
# Qt6Config.cmake y lib/libQt6Core.a). La vía aqt es la fiable aquí: la ruta
# oficial de descarga única (qt-opensource-...-mingw_2022-<ver>.7z) devuelve 404
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
	  echo "[deps] Qt6 $(QT6_VER): instalando SDK win64_msvc2022_64 a $(QT6_PREFIX)"; \
	  rm -rf "$(QT6_DEST)"; \
	  "$(PY)" -m aqt install-qt windows desktop $(QT6_VER) win64_msvc2022_64 -m qtpdf qtmultimedia -O "$(QT6_DEST)" \
	    || { echo "[deps] ERROR Qt6: fallo de aqt" >&2; exit 1; }; \
	  if [ -f "$(QT6_SENTINEL)" ]; then \
	    echo "[deps] Qt6 $(QT6_VER): SDK instalado ($(QT6_PREFIX))"; \
	  else \
	    echo "[deps] ERROR Qt6: sentinel ausente tras aqt ($(QT6_SENTINEL))" >&2; exit 1; \
	  fi; \
	fi

# Windows: el ONNX Runtime oficial con DirectML (tools/deps/onnxruntime-win.sh)
# y sherpa-onnx compilado con clang-cl contra él, cada uno en su carpeta.
.PHONY: deps-onnxruntime-win deps-sherpa-onnx-win deps-ncnn-win
# ncnn + Vulkan (superresolución en cualquier GPU: tools/deps/ncnn-win.sh).
deps-ncnn-win:
	tools/deps/ncnn-win.sh "$(DL_DIR)"
deps-onnxruntime-win:
	tools/deps/onnxruntime-win.sh "$(DL_DIR)"
deps-sherpa-onnx-win: deps-onnxruntime-win deps-win-toolchain
	tools/deps/sherpa-onnx.sh win

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

deps-pinggy-win:
	tools/deps/pinggy.sh windows-x86_64

deps-chrome-headless-win:
	tools/deps/chrome-headless.sh win64

deps-craft-win:
	tools/deps/craft.sh win

# Para abrir el .msi de Windows desde Linux. Lo llama deps-gstreamer-win solo
# si hace falta; esta aparte para poder pedirlo suelto. Es una herramienta de
# COMPILACION: no viaja en el bundle y no se instala en el sistema.
deps-msitools:
	tools/deps/msitools.sh

# llama.cpp como BIBLIOTECA para el ba-modelo de Windows y Linux (el modelo local
# fuera de la Mac, src/modelo-llama): los binarios oficiales con Vulkan.
deps-llamacpp-win: deps-win-toolchain
	tools/deps/llamacpp-lib.sh win

deps-python-win: deps-python
	@if [ -f "$(EXTERNS_DIR)/python-win/python.exe" ] && [ -d "$(EXTERNS_DIR)/python-win/Lib/site-packages/ctranslate2" ] && \
	    [ -d "$(EXTERNS_DIR)/python-win/Lib/site-packages/onnx" ] && \
	    grep -q "$(PY_VER)" "$(EXTERNS_DIR)/python-win/.version" 2>/dev/null; then \
		echo "Python de Windows ya está ($(EXTERNS_DIR)/python-win)"; exit 0; fi; \
	$(MAKE) --no-print-directory _ba_baja ARCHIVO="$(DL_DIR)/$(PY_TGZ_win-x64)" URL="$(PY_URL)/$(subst +,%2B,$(PY_TGZ_win-x64))" SHA="$(PY_SHA_win-x64)" QUE="Python (Windows)" && \
	bash tools/deps/python.sh "$(DL_DIR)/$(PY_TGZ_win-x64)" "$(EXTERNS_DIR)/python-win" win-x64 "$(EXTERNS_DIR)/python/bin/python3"

.PHONY: deps-herramientas-win
deps-herramientas-win:
	@$(MAKE) --no-print-directory deps-herramientas HERR_PLAT=win-x64

.PHONY: deps-gstreamer-win
deps-gstreamer-win:
	@if [ "$$(cat $(EXTERNS_DIR)/gstreamer-win/.version 2>/dev/null)" = "$(GST_WIN_VER) msvc_x86_64 r4" ]; then \
		echo "GStreamer de Windows ya está ($(EXTERNS_DIR)/gstreamer-win)"; exit 0; fi; \
	$(MAKE) --no-print-directory _ba_baja ARCHIVO="$(DL_DIR)/$(GST_WIN_RT)" URL="$(GST_WIN_URL)/$(GST_WIN_RT)" SHA="$(GST_WIN_RT_SHA)" QUE="GStreamer (Windows)" && \
	$(MAKE) --no-print-directory _ba_baja ARCHIVO="$(DL_DIR)/$(GST_WIN_DEV)" URL="$(GST_WIN_URL)/$(GST_WIN_DEV)" SHA="$(GST_WIN_DEV_SHA)" QUE="GStreamer (Windows, cabeceras)" && \
	bash tools/deps/gstreamer-win.sh "$$(pwd)/$(DL_DIR)/$(GST_WIN_RT)" "$$(pwd)/$(DL_DIR)/$(GST_WIN_DEV)" "$(EXTERNS_DIR)/gstreamer-win"

# Los símbolos (.pdb) del GStreamer de Windows, del .msi de desarrollo, aparte:
# no van en el aparato, van al almacén de símbolos (tools/caidas/simbolos.py)
# para leer las pilas de los volcados.
.PHONY: deps-gstreamer-win-pdb
deps-gstreamer-win-pdb: deps-gstreamer-win
	@if [ "$$(cat $(EXTERNS_DIR)/gstreamer-win-pdb/.version 2>/dev/null)" = "$(GST_WIN_VER)" ]; then \
		echo "Símbolos de GStreamer (Windows) ya están"; exit 0; fi; \
	t="$$(mktemp -d)"; trap 'rm -rf "$$t"' EXIT; \
	for m in "$(DL_DIR)/$(GST_WIN_RT)" "$(DL_DIR)/$(GST_WIN_DEV)"; do \
		(cd "$$t" && "$(CURDIR)/$(EXTERNS_DIR)/msitools/msiextract" "$(CURDIR)/$$m" >/dev/null); done; \
	rm -rf "$(EXTERNS_DIR)/gstreamer-win-pdb" && mkdir -p "$(EXTERNS_DIR)/gstreamer-win-pdb" && \
	find "$$t" -iname '*.pdb' -exec cp -n {} "$(EXTERNS_DIR)/gstreamer-win-pdb/" \; && \
	echo "$(GST_WIN_VER)" >"$(EXTERNS_DIR)/gstreamer-win-pdb/.version" && \
	echo "Símbolos de GStreamer (Windows): $$(ls $(EXTERNS_DIR)/gstreamer-win-pdb/*.pdb | wc -l) .pdb, $$(du -sh $(EXTERNS_DIR)/gstreamer-win-pdb | cut -f1)"

# Los bindings de Python de GStreamer para el Python 3.12 de Windows (clang-cl, meson bajo Wine).
.PHONY: deps-gst-python-win
ifeq ($(shell uname -m),aarch64)
# Se arman corriendo el Python de Windows bajo Wine, y Wine es solo x86_64: desde un host
# arm64 (la validación nativa en Docker) no se puede. Se dice y se sigue (CMake lo marca
# como faltante); el paquete que se publica sale del host x86_64, que sí los arma.
deps-gst-python-win:
	@echo "[deps] gst-python (Windows): SALTEADO en un host aarch64: se arma bajo Wine, que es solo x86_64"
else
deps-gst-python-win: deps-gstreamer-win deps-python-win deps-win-toolchain
	bash tools/ci/test-win.sh --solo-wine
	bash tools/deps/gst-python-win.sh "$(DL_DIR)"
endif

win: deps-win
	@$(CONFIGURAR) $(WIN_BUILD_DIR) $(BA_CMAKE_PERFIL) -G Ninja \
		-DCMAKE_TOOLCHAIN_FILE=$(WIN_TOOLCHAIN) \
		-DCMAKE_BUILD_TYPE=Release \
		-DQT_HOST_PATH=$(QT6_HOST_PATH)
	cmake --build $(WIN_BUILD_DIR) -j$(JOBS)

# Tests de Windows bajo Wine portable (tools/ci/test-win.sh).
test-win: win
	tools/ci/test-win.sh $(WIN_BUILD_DIR)

release-win: win
	@# Incremental (antes rm -rf y 2,7 GB de nuevo): ver tools/ci/instalar-incremental.sh.
	tools/ci/instalar-incremental.sh $(WIN_BUILD_DIR) $(WIN_RELEASE)
	@# El chequeo de portabilidad y el manifest, solo si cambió el contenido del
	@# bundle. El manifest, igual que en Linux: sin el, el launcher de Windows
	@# verifica CERO archivos y la comprobacion de integridad no sirve de nada.
	@python3 tools/ci/huellas.py atajo --nombre "release-win (chequeo + manifest)" \
	  --clave-en $(WIN_BUILD_DIR)/atajos/release-win.json --memo $(WIN_BUILD_DIR)/atajos/huellas.json \
	  --entrada $(WIN_RELEASE) --excluir manifest.json --entrada tools/deploy --entrada tools/deps/check-portable-win.sh \
	  --entrada tools/deps/windows-baseline.txt \
	  --salida $(WIN_RELEASE)/manifest.json -- sh -c '\
	    tools/deps/check-portable-win.sh $(WIN_RELEASE) && \
	    BA_MANIFEST_CACHE=$(WIN_BUILD_DIR)/atajos/manifest-cache.txt tools/deploy --solo-manifest $(WIN_RELEASE)'

# El .zip se saltea si el bundle es idéntico (por contenido) al del último .zip:
# la clave queda al lado (<zip>.clave). El .commit se escribe siempre: el mismo
# contenido es lo que sale de este commit.
dist-win: release-win
	@mkdir -p dist
	@python3 tools/ci/huellas.py atajo --nombre "zip de Windows" --clave-en $(DIST_WIN).clave \
	  --memo $(WIN_BUILD_DIR)/atajos/huellas.json --entrada $(WIN_RELEASE) --texto "$(DIST_WIN) 7z -tzip -mx=3" \
	  --salida $(DIST_WIN) --salida $(DIST_WIN).sha256 -- $(MAKE) dist-win-zip
	@echo "$(ESTE_COMMIT)" > $(DIST_WIN).commit
	@echo "$(DIST_WIN)"

dist-win-zip:
	@mkdir -p dist
	rm -f dist/BroadcastAppliance-$(BA_VERSION)-windows-x64$(BA_SUFIJO).zip
	@# Con carpeta raiz (como el .tar.gz de Linux): "Extraer aqui" no desparrama
	@# los archivos del programa en la carpeta del usuario.
	rm -rf dist/.zip-stage && mkdir -p dist/.zip-stage
	cp -al $(WIN_RELEASE) dist/.zip-stage/BroadcastAppliance 2>/dev/null || cp -a $(WIN_RELEASE) dist/.zip-stage/BroadcastAppliance
	@# -mx=3 y no 7: medido sobre el bundle (2,7 GB, casi todo DLLs ya comprimidas), 13 s y
	@# 826 MB contra 164 s y 814 MB; bajo Docker en una Mac el 7z corre emulado.
	cd dist/.zip-stage && 7z a -tzip -mx=3 -mcu=on -bso0 ../BroadcastAppliance-$(BA_VERSION)-windows-x64$(BA_SUFIJO).zip BroadcastAppliance
	rm -rf dist/.zip-stage
	@cd dist && $(SHA256) BroadcastAppliance-$(BA_VERSION)-windows-x64$(BA_SUFIJO).zip > BroadcastAppliance-$(BA_VERSION)-windows-x64$(BA_SUFIJO).zip.sha256
