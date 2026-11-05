# mk/tablero-web.mk — El tablero web (QML compilada a WebAssembly).
# Lo incluye el Makefile de la raíz (las variables comunes están allá).

.PHONY: deps-wasm tablero-web

# --- El tablero web (src/tablero-web, docs/funciones/TABLERO-WEB.md) --------------------------
# El dashboard de la app en el navegador: la MISMA QML compilada con el Qt de la app
# (QT6_VER) para WebAssembly. Todo en extern/ (nada en el sistema): Qt para WebAssembly
# (aqt, sin hilos: el servidor no necesita COOP/COEP), el emscripten con que se armó ese Qt
# (EMSDK_VER, en extern/emsdk-<versión>)
# y el Qt del host para moc/rcc. El .wasm es el mismo para las tres plataformas; en Docker
# (Linux) se arma igual. `make tablero-web` deja build-tablero-web/salida/, que el bundle
# lleva a Web/control/app (cmake/BaTableroWeb.cmake).

.PHONY: deps-wasm tablero-web
deps-wasm: deps-qt-host
	@if [ -f "$(QT6_WASM_SENTINEL)" ]; then echo "[deps] Qt $(QT6_VER) para WebAssembly: ya presente"; else \
	  test -x "$(PY)" || python3 -m venv .venv; \
	  "$(PIP)" install -q aqtinstall || { echo "[deps] ERROR: no se pudo instalar aqtinstall" >&2; exit 1; }; \
	  echo "[deps] Qt $(QT6_VER) para WebAssembly (wasm_singlethread) en $(QT6_WASM_DEST)"; \
	  "$(PY)" -m aqt install-qt all_os wasm $(QT6_VER) wasm_singlethread -O "$(QT6_WASM_DEST)" \
	    || { echo "[deps] ERROR: falló aqt (Qt para WebAssembly)" >&2; exit 1; }; \
	  test -f "$(QT6_WASM_SENTINEL)" || { echo "[deps] ERROR: falta $(QT6_WASM_SENTINEL)" >&2; exit 1; }; \
	fi
	@mkdir -p "$(DL_DIR)"
	@if [ -f "$(EMSDK_DEST)/upstream/emscripten/emcc" ]; then echo "[deps] emscripten $(EMSDK_VER): ya presente"; else \
	  f="$(DL_DIR)/emsdk-$(EMSDK_VER).tar.gz"; \
	  [ -f "$$f" ] && [ "$$($(SHA256) "$$f" | cut -d' ' -f1)" = "$(EMSDK_SHA)" ] || curl -fL --retry 3 -o "$$f" "$(EMSDK_URL)"; \
	  printf '%s  %s\n' "$(EMSDK_SHA)" "$$f" | $(SHA256_CHECK) || { echo "[deps] ERROR emsdk: hash" >&2; exit 1; }; \
	  rm -rf "$(EMSDK_DEST)" && mkdir -p "$(EMSDK_DEST)" && tar xzf "$$f" -C "$(EMSDK_DEST)" --strip-components 1; \
	  echo "[deps] emscripten $(EMSDK_VER): instalando adentro de $(EMSDK_DEST)"; \
	  cd "$(EMSDK_DEST)" && EMSDK_PYTHON="$$(command -v python3)" ./emsdk install $(EMSDK_VER) >/dev/null \
	    && EMSDK_PYTHON="$$(command -v python3)" ./emsdk activate --embedded $(EMSDK_VER) >/dev/null \
	    || { echo "[deps] ERROR: falló emsdk install $(EMSDK_VER)" >&2; exit 1; }; \
	fi

tablero-web: deps-wasm
	JOBS=$(JOBS) QT_VER=$(QT6_VER) QT_HOST_DIR=$(QT6_HOST_DIR) EMSDK_DIR=$(abspath $(EMSDK_DEST)) tools/deps/tablero-web.sh
