# mk/mac.mk — macOS arm64: dependencias, ba-modelo (MLX), la .app y el .dmg.
# Lo incluye el Makefile de la raíz (las variables comunes están allá).

.PHONY: deps-lsp-macos check-macos-tools deps-macos macos deps-lspdsp-macos deps-craft-mac \
        deps-gst-python-mac deps-gstreamer-macos deps-mlx-swift-lm modelo-mac release-macos \
        app-macos app-macos-rapida dmg-macos dist-macos

# --- macOS arm64 (Apple Silicon), nativo -------------------------------------
# Todo se compila EN la Mac con Apple clang (Xcode completo: ba-modelo lo exige).
# Mismo layout portable que Linux (bin/64bit, gst-plugins, data/) y el
# mismo criterio: nada del sistema salvo los frameworks de macOS.
#   make deps-macos     -> baja/compila todo lo de terceros en extern/
#   make macos          -> compila (build/) y corre los tests
#   make release-macos  -> bundle portable en release/ (verificado con otool)
#   make dist-macos     -> dist/BroadcastAppliance-<ver>-macos-arm64.tar.gz
#   make dmg-macos      -> dist/BroadcastAppliance-<ver>-macos-arm64.dmg (la .app, lo que publica el release)
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
# Linux viene compilada con las de terceros (deps-terceros-linux).
deps-macos: check-macos-tools deps-qt-host deps-src deps-lwip deps-cacert deps-libvterm deps-fonts deps-chrome-headless \
            deps-cloudflared-host deps-pinggy deps-clap deps-ndi-src deps-craft-mac deps-fuentes-texto deps-sherpa-onnx deps-gstreamer-macos deps-python deps-gst-python-mac deps-herramientas deps-hinchada
	$(MAKE) deps-webrtc BA_WEBRTC_TARGET=macos
	@# LSP (modulos del rack de audio) es opcional en macOS mientras se valida:
	@# sin el, el rack funciona con el analizador y el generador.
	-$(MAKE) deps-lsp-macos
	$(MAKE) deps-lspdsp-macos

macos: compilar-mac
	$(TESTS_CACHE)

deps-lspdsp-macos:
	tools/deps/lspdsp.sh macos-arm64 $(JOBS)

deps-craft-mac:
	tools/deps/craft.sh mac

# Los bindings de Python de GStreamer para el Python 3.12 del bundle (macOS).
.PHONY: deps-gst-python-mac
deps-gst-python-mac: deps-gstreamer-macos deps-python
	bash tools/deps/gst-python-mac.sh "$(DL_DIR)"

.PHONY: deps-gstreamer-macos
deps-gstreamer-macos:
	@if [ -f "$(EXTERNS_DIR)/gstreamer-macos/GStreamer.framework/Versions/1.0/lib/libgstreamer-1.0.dylib" ]; then \
		echo "GStreamer ya está ($(EXTERNS_DIR)/gstreamer-macos)"; exit 0; fi; \
	$(MAKE) --no-print-directory _ba_baja ARCHIVO="$(DL_DIR)/$(GST_RT)" URL="$(GST_URL)/$(GST_RT)" SHA="$(GST_RT_SHA)" QUE="GStreamer" && \
	$(MAKE) --no-print-directory _ba_baja ARCHIVO="$(DL_DIR)/$(GST_DEV)" URL="$(GST_URL)/$(GST_DEV)" SHA="$(GST_DEV_SHA)" QUE="GStreamer (cabeceras)" && \
	bash tools/deps/gstreamer-macos.sh "$(DL_DIR)/$(GST_RT)" "$(DL_DIR)/$(GST_DEV)" "$(EXTERNS_DIR)/gstreamer-macos"

# ba-modelo: el modelo local con MLX (src/modelo-mac). xcodebuild y no `swift
# build`: este no compila los shaders de Metal de MLX. Las dependencias de
# Swift quedan fijadas en src/modelo-mac/Package.resolved; mlx-swift-lm va en
# extern/ con los parches de src/modelo-mac/parches/ (deps-mlx-swift-lm; la
# versión, MLX_SWIFT_LM_REV en mk/versiones.mk). Necesita el componente Metal de
# Xcode (xcodebuild -downloadComponent MetalToolchain).
deps-mlx-swift-lm:
	@set -e; d=extern/mlx-swift-lm; sello=$$d/.parches-$$(cat src/modelo-mac/parches/*.patch | $(SHA256) | cut -c1-12); \
	if [ -f "$$sello" ]; then echo "[mlx-swift-lm] al dia ($$sello)"; exit 0; fi; \
	[ -d $$d/.git ] || git clone -q https://github.com/ml-explore/mlx-swift-lm $$d; \
	git -C $$d fetch -q origin $(MLX_SWIFT_LM_REV) 2>/dev/null || true; \
	git -C $$d checkout -q -f $(MLX_SWIFT_LM_REV); git -C $$d clean -qfd; \
	for p in src/modelo-mac/parches/*.patch; do git -C $$d apply "$(CURDIR)/$$p"; echo "[mlx-swift-lm] $$p"; done; \
	touch "$$sello"

# _enlaces primero: en un worktree, extern/ y build/modelo-mac son los del principal.
modelo-mac: _enlaces deps-mlx-swift-lm
	@xcrun metal --version >/dev/null 2>&1 || { echo "ERROR: falta el compilador de Metal: xcodebuild -downloadComponent MetalToolchain" >&2; exit 1; }
	cd src/modelo-mac && xcodebuild -scheme ba-modelo -destination 'platform=macOS,arch=arm64' \
		-configuration Release -derivedDataPath $(CURDIR)/build/modelo-mac \
		-skipPackagePluginValidation -skipMacroValidation -quiet build

release-macos:
	@# Compila primero: sin esto se empaquetaba el ultimo build que hubiera en
	@# $(BUILD_DIR), y los cambios de la app no llegaban a la .app.
	@$(CONFIGURAR) $(BUILD_DIR) $(BA_CMAKE_PERFIL) $(BA_CMAKE_PLATAFORMA)
	cmake --build $(BUILD_DIR) -j$(JOBS)
	rm -rf release
	@# De cero: también la cache de firmas (tools/deps/firmar-macos.sh).
	rm -f $(BUILD_DIR)/firmas-*.txt $(BUILD_DIR)/firmados-*.txt
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
	tools/caidas/simbolos-mac.sh release || echo "ATENCION: no se guardaron los .dSYM" >&2

# Para probar mientras se programa: lo mismo que app-macos, pero sin borrar
# release/ ni la .app. Solo se copia, firma, revisa y hashea lo que cambió
# (tools/deps/firmar-macos.sh, BA_CHECK_SOLO, BA_MANIFEST_CACHE). Si algo raro
# pasa, `make app-macos` lo arma de cero. No toca las opciones de $(BUILD_DIR)
# (_configurado: solo configura si no lo está, p. ej. en un worktree nuevo).
app-macos-rapida: _configurado
	cmake --build $(BUILD_DIR) -j$(JOBS)
	cmake --install $(BUILD_DIR) --prefix release --component bundle
	BA_CHECK_SOLO="$$(ls -t $(BUILD_DIR)/firmados-*.txt | head -1)" tools/deps/check-portable-macos.sh release
	@mkdir -p dist
	BA_FIRMADOS="$$(ls -t $(BUILD_DIR)/firmados-*.txt | head -1)" tools/deps/app-macos.sh --rapida release dist/BroadcastAppliance.app

dmg-macos: release-macos
	@mkdir -p dist
	tools/deps/app-macos.sh release dist/BroadcastAppliance.app dist/BroadcastAppliance-$(BA_VERSION)-macos-arm64$(BA_SUFIJO).dmg
	@cd dist && $(SHA256) BroadcastAppliance-$(BA_VERSION)-macos-arm64$(BA_SUFIJO).dmg > BroadcastAppliance-$(BA_VERSION)-macos-arm64$(BA_SUFIJO).dmg.sha256
	@echo "$(ESTE_COMMIT)" > dist/BroadcastAppliance-$(BA_VERSION)-macos-arm64$(BA_SUFIJO).dmg.commit
	tools/caidas/simbolos-mac.sh release || echo "ATENCION: no se guardaron los .dSYM" >&2
	@echo "dist/BroadcastAppliance-$(BA_VERSION)-macos-arm64$(BA_SUFIJO).dmg"

dist-macos: release-macos
	@mkdir -p dist
	tar -C release -czf dist/BroadcastAppliance-$(BA_VERSION)-macos-arm64$(BA_SUFIJO).tar.gz -s ',^\.,BroadcastAppliance,' .
	@cd dist && $(SHA256) BroadcastAppliance-$(BA_VERSION)-macos-arm64$(BA_SUFIJO).tar.gz > BroadcastAppliance-$(BA_VERSION)-macos-arm64$(BA_SUFIJO).tar.gz.sha256
	@echo "dist/BroadcastAppliance-$(BA_VERSION)-macos-arm64$(BA_SUFIJO).tar.gz"
