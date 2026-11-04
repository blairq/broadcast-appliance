# mk/linux.mk — Linux: dependencias, el bundle portable (release/) y el .tar.gz.
# Lo incluye el Makefile de la raíz (las variables comunes están allá).

.PHONY: deps-lspdsp-linux deps-linux deps-linux-todo deps-terceros-linux deps-lsp \
        deps-chrome-headless-linux deps-craft-linux deps-llamacpp-linux deps-gstreamer-linux release \
        dist-linux dist-linux-tar

deps-lspdsp-linux:
	tools/deps/lspdsp.sh linux-x86_64 $(JOBS)

# Todo lo que el build completo de Linux necesita (las bibliotecas de terceros
# y GStreamer desde fuente, Qt del host, libwebrtc de Linux, fuentes
# compartidas...).
deps-linux:
	@$(DEPS_ATAJO) --nombre deps-linux --clave-en $(EXTERNS_DIR)/.atajos/deps-linux.json -- $(MAKE) deps-linux-todo

deps-linux-todo: deps-terceros-linux deps-qt-host deps-src deps-lwip deps-cacert deps-libvterm deps-fonts deps-cloudflared-host deps-pinggy \
            deps-clap deps-lsp deps-lspdsp-linux deps-craft-linux deps-fuentes-texto deps-sherpa-onnx deps-herramientas deps-gstreamer-linux deps-python \
            deps-llamacpp-linux deps-chrome-headless-linux deps-hinchada
	$(MAKE) deps-webrtc BA_WEBRTC_TARGET=linux

# --- Bibliotecas de terceros de Linux (compiladas desde fuente) -------------
# FFmpeg, x264, SRT, libva, libv4l2, cairo, libsndfile, libmicrodns... desde
# fuente fijada (tools/deps/terceros-linux.sh): el bundle no usa bibliotecas de
# terceros del sistema. Las usan GStreamer (deps-gstreamer-linux), los plugins
# LSP y la búsqueda por mDNS. Quedan en extern/terceros-linux (antes
# extern/obs-deps-linux: el script muda un árbol viejo y deja un enlace).
deps-terceros-linux:
	tools/deps/terceros-linux.sh $(JOBS)

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
	  echo "[deps] LSP: CLAP de Linux en $(LSP_DEST)/linux-x86_64"; \
	fi
	@# Siempre (es idempotente): un CLAP extraído antes del cambio de nombre
	@# de extern/obs-deps-linux también queda apuntando a extern/terceros-linux.
	@if command -v patchelf >/dev/null 2>&1; then \
	  patchelf --set-rpath '$$ORIGIN:$$ORIGIN/../../terceros-linux/lib' "$(LSP_DEST)/linux-x86_64/lsp-plugins.clap"; fi

deps-chrome-headless-linux:
	tools/deps/chrome-headless.sh linux64

deps-craft-linux:
	tools/deps/craft.sh linux

deps-llamacpp-linux:
	tools/deps/llamacpp-lib.sh linux

# GStreamer de Linux: desde fuente, la misma versión (tools/deps/gstreamer-linux.sh).
.PHONY: deps-gstreamer-linux
deps-gstreamer-linux: deps-terceros-linux
	bash tools/deps/gstreamer-linux.sh $(JOBS)

# --- Bundle portable (layout §5.1) ------------------------------------------
# Instala el esqueleto del layout portable sobre release (bin/64bit,
# gst-plugins, data, Config, Logs, Recordings, Web/guest).
release:
	@# Incremental: solo se copia lo que cambió y se borra lo que ya no se instala
	@# (lo de una receta vieja no queda). SIN_CACHE=1: de cero.
	tools/ci/instalar-incremental.sh $(BUILD_DIR) release
	@# Linux: la depuración afuera (dist/simbolos/linux) y el bundle sin ella, ANTES del
	@# manifest (antes iba después, en dist-linux: el manifest llevaba el sha256 de los
	@# binarios sin strip y el .tar.gz los de con strip). Deja la fecha de cada binario como
	@# la instaló cmake: la próxima instalación los ve al día y no los vuelve a copiar.
	@# Solo si cambió el bundle.
	@if [ "$$(uname -s)" = Linux ]; then python3 tools/ci/huellas.py atajo --nombre "release (strip de los símbolos)" \
	  --clave-en $(BUILD_DIR)/atajos/simbolos-linux.json --memo $(BUILD_DIR)/atajos/huellas.json \
	  --entrada release --excluir manifest.json --entrada tools/caidas/simbolos-linux.sh --salida dist/simbolos/linux \
	  -- tools/caidas/simbolos-linux.sh release dist/simbolos/linux; fi
	@# Linux: el bundle no puede depender de bibliotecas de terceros del sistema.
	@# Sin manifest el launcher verifica cero archivos y la comprobacion de
	@# integridad no sirve de nada. Solo lo que no cambia al usar el programa.
	@# Los dos, solo si cambió el contenido del bundle (tools/ci/huellas.py).
	@python3 tools/ci/huellas.py atajo --nombre "release (chequeo + manifest)" \
	  --clave-en $(BUILD_DIR)/atajos/release.json --memo $(BUILD_DIR)/atajos/huellas.json \
	  --entrada release --excluir manifest.json --entrada tools/deploy --entrada tools/deps/check-portable.sh \
	  --entrada tools/deps/linux-baseline.txt \
	  --salida release/manifest.json -- sh -c '\
	    if [ "$$(uname -s)" = Linux ]; then tools/deps/check-portable.sh release || exit 1; fi; \
	    BA_MANIFEST_CACHE=$(BUILD_DIR)/atajos/manifest-cache.txt tools/deploy --solo-manifest release'

# Configurado en Release y compilado: lo que arma el paquete. La receta de Docker
# (tools/ci/receta-docker.sh) lo hace una vez y después le pasa `-o compilado-release`
# a dist-linux: cada «nada que compilar» de make bajo Rosetta son ~40 s.
.PHONY: compilado-release
compilado-release:
	@$(CONFIGURAR) $(BUILD_DIR) $(BA_CMAKE_PERFIL) $(BA_CMAKE_PLATAFORMA) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) -j$(JOBS)

dist-linux: compilado-release
	@# Incremental (`make release`; SIN_CACHE=1 lo arma de cero).
	$(MAKE) release
	@mkdir -p dist
	@# El .tar.gz, solo si el contenido del bundle cambió desde el último (clave al lado).
	@python3 tools/ci/huellas.py atajo --nombre "tar.gz de Linux" --clave-en $(DIST_LINUX).clave \
	  --memo $(BUILD_DIR)/atajos/huellas.json --entrada release --texto "$(DIST_LINUX) tar -czf" \
	  --salida $(DIST_LINUX) --salida $(DIST_LINUX).sha256 -- $(MAKE) dist-linux-tar
	@echo "$(ESTE_COMMIT)" > $(DIST_LINUX).commit
	@echo "$(DIST_LINUX)"

dist-linux-tar:
	@mkdir -p dist
	tar -C release -czf dist/BroadcastAppliance-$(BA_VERSION)-linux-x64$(BA_SUFIJO).tar.gz --transform 's,^\.,BroadcastAppliance,' .
	@cd dist && $(SHA256) BroadcastAppliance-$(BA_VERSION)-linux-x64$(BA_SUFIJO).tar.gz > BroadcastAppliance-$(BA_VERSION)-linux-x64$(BA_SUFIJO).tar.gz.sha256
