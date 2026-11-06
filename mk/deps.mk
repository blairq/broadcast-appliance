# mk/deps.mk — Dependencias de terceros comunes a las tres plataformas (descarga reproducible e idempotente).
# Lo incluye el Makefile de la raíz (las variables comunes están allá).

.PHONY: deps deps-ndi-src deps-qt-host deps-webrtc deps-libyuv deps-lwip deps-sherpa-onnx \
        deps-rostros _ba_baja deps-libvterm deps-fonts deps-cacert deps-pinggy deps-hinchada \
        deps-cloudflared-host deps-chrome-headless deps-fuentes-texto deps-clap deps-python \
        deps-herramientas deps-craft deps-src deps-mbedtls deps-nlohmann deps-samplerate deps-argon2 \
        deps-libdeflate deps-zxing deps-estaticas deps-estaticas-mac deps-estaticas-linux deps-estaticas-win \
        deps-status deps-clean

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
#   make deps             -> lo de esta plataforma (deps-mac, deps-linux, deps-windows)
#   make deps-webrtc      -> SDK libwebrtc M123 a extern/webrtc (= BA_WEBRTC_ROOT)
#   make deps-cloudflared -> cloudflared 2024.11.0 a extern/cloudflared
#   make deps-lsp         -> modulos LSP 1.2.35 (CLAP, Linux) a extern/lsp
#   make deps-status      -> muestra qué assets hay y su estado
#   make deps-clean       -> borra extern/
#
# BA_WEBRTC_TARGET: `windows` (default, webrtc.lib) o `linux` (libwebrtc.a).
# El build enlaza libwebrtc solo en Windows (if(WIN32) en src/gateway), por lo
# que el artefacto por defecto es el de Windows.
# ============================================================================

# Cada target enlaza el bloque idempotente de descarga (presente+sha -> skip;
# si no, curl a <archivo>.part, mv y verificacion `$(SHA256) -c`). Se repite
# por asset a proposito: es mas robusto y depurable que un macro define/call/
# eval (este ultimo, dentro de una receta, se interpreta como contenido de
# makefile y rompe el parseo).

# --- deps (agregado) ---------------------------------------------------------
# `make deps`: lo de esta plataforma (deps-mac, deps-linux o deps-windows).
deps:
	$(MAKE) deps-$(PLATAFORMA)
# Atajo: si no cambiaron las versiones fijadas ni las recetas (Makefile,
# versions.cmake, tools/deps/, el toolchain) ni lo que hay en extern/ (qué
# dependencias y sus .version), no se vuelve a verificar nada (eran ~23 s de
# sha256 de archivos bajados en cada build). SIN_CACHE=1 verifica todo.
# hinchada no tiene versión fijada: sale de los paquetes de ../hinchada (HINCHADA_DIR), así que
# la clave lleva su contenido (HINCHADA_HUELLA). Sin eso, un paquete nuevo de hinchada no
# entraba nunca (el atajo veía todo igual y deps-hinchada no corría: el .zip de Windows salía
# con la hinchada vieja).
DEPS_ATAJO = python3 tools/ci/huellas.py atajo --entrada Makefile --entrada mk --entrada versions.cmake --entrada tools/deps \
	--entrada cmake/estaticas --entrada cmake/mbedtls_user_config.h \
	--entrada cmake/toolchains --centinelas $(EXTERNS_DIR) --texto "BA_ACCEPT_MSVC_LICENSE=$(BA_ACCEPT_MSVC_LICENSE)" \
	--texto "hinchada $(HINCHADA_HUELLA)"

# Recepcion NDI abierta: libNoIDea + libmicrodns por commit fijado.
deps-ndi-src:
	tools/deps/ndi-src.sh

deps-qt-host:
	@if [ -f "$(QT6_HOST_SENTINEL)" ] && [ -f "$(QT6_HOST_PDF)" ] && [ -f "$(QT6_HOST_MM)" ]; then \
	  echo "[deps] Qt6 $(QT6_VER) ($(QT6_HOST_ARCH)): ya presente con qtpdf y qtmultimedia ($(QT6_HOST_DEST))"; \
	else \
	  test -x "$(PY)" || python3 -m venv .venv; \
	  "$(PIP)" install -q aqtinstall || { echo "[deps] ERROR Qt6: no se pudo instalar aqtinstall" >&2; exit 1; }; \
	  echo "[deps] Qt6 $(QT6_VER): instalando $(QT6_HOST_AQT)/$(QT6_HOST_ARCH) (+qtpdf +qtmultimedia) en $(QT6_HOST_DEST)"; \
	  "$(PY)" -m aqt install-qt $(QT6_HOST_AQT) desktop $(QT6_VER) $(QT6_HOST_ARCH) -m qtpdf qtmultimedia -O "$(QT6_HOST_DEST)" \
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

# libyuv + libjpeg-turbo desde fuente (cmake/BaLibyuv.cmake): el núcleo del gateway escala y
# convierte con libyuv; donde el build no enlaza el SDK de libwebrtc (Linux) la compila él.
deps-libyuv:
	@command -v curl >/dev/null 2>&1 || { echo "[deps] ERROR: falta curl" >&2; exit 1; }
	@mkdir -p "$(DL_DIR)" "$(EXTERNS_DIR)"
	@if [ -f "$(LIBYUV_DEST)/include/libyuv.h" ] && [ "$$(cd $(LIBYUV_DEST) && find . -type f | LC_ALL=C sort | xargs $(SHA256) | $(SHA256) | cut -d' ' -f1)" = "$(LIBYUV_TREE_SHA)" ]; then \
	  echo "[deps] libyuv: ya presente y verificado ($(LIBYUV_DEST))"; \
	else \
	  echo "[deps] libyuv: bajando $(LIBYUV_COMMIT)"; \
	  rm -rf "$(LIBYUV_DEST)"; mkdir -p "$(LIBYUV_DEST)"; \
	  curl -fsSL --retry 5 --retry-delay 3 "$(LIBYUV_URL)" | tar xz -C "$(LIBYUV_DEST)" \
	    || { echo "[deps] ERROR libyuv: fallo de descarga" >&2; rm -rf "$(LIBYUV_DEST)"; exit 1; }; \
	  test "$$(cd $(LIBYUV_DEST) && find . -type f | LC_ALL=C sort | xargs $(SHA256) | $(SHA256) | cut -d' ' -f1)" = "$(LIBYUV_TREE_SHA)" \
	    || { echo "[deps] ERROR libyuv: el contenido no coincide con el hash fijado" >&2; rm -rf "$(LIBYUV_DEST)"; exit 1; }; \
	fi
	@if [ -f "$(LIBJPEG_TURBO_TAR)" ] && [ "$$($(SHA256) "$(LIBJPEG_TURBO_TAR)" | cut -d' ' -f1)" = "$(LIBJPEG_TURBO_SHA)" ]; then \
	  echo "[deps] libjpeg-turbo: ya presente y verificado ($(LIBJPEG_TURBO_TAR))"; \
	else \
	  echo "[deps] libjpeg-turbo: descargando $(LIBJPEG_TURBO_URL)"; \
	  curl -fL --retry 3 --connect-timeout 30 -o "$(LIBJPEG_TURBO_TAR).part" "$(LIBJPEG_TURBO_URL)" \
	    && mv "$(LIBJPEG_TURBO_TAR).part" "$(LIBJPEG_TURBO_TAR)" \
	    && printf '%s  %s\n' "$(LIBJPEG_TURBO_SHA)" "$(LIBJPEG_TURBO_TAR)" | $(SHA256_CHECK) \
	    || { echo "[deps] ERROR libjpeg-turbo: fallo de descarga o hash" >&2; rm -f "$(LIBJPEG_TURBO_TAR).part"; exit 1; }; \
	fi
	@if [ -f "$(LIBJPEG_TURBO_DEST)/.version" ] && [ "$$(cat $(LIBJPEG_TURBO_DEST)/.version)" = "$(LIBJPEG_TURBO_VER)" ]; then \
	  echo "[deps] libjpeg-turbo: ya extraído en $(LIBJPEG_TURBO_DEST)"; \
	else \
	  echo "[deps] libjpeg-turbo: extrayendo a $(LIBJPEG_TURBO_DEST)"; \
	  rm -rf "$(LIBJPEG_TURBO_DEST)"; mkdir -p "$(LIBJPEG_TURBO_DEST)"; \
	  tar xzf "$(LIBJPEG_TURBO_TAR)" -C "$(LIBJPEG_TURBO_DEST)" --strip-components=1 \
	    && echo "$(LIBJPEG_TURBO_VER)" > "$(LIBJPEG_TURBO_DEST)/.version"; \
	fi

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

# La voz (docs/funciones/AUDIO-TTS.md): sherpa-onnx contra el MISMO ONNX Runtime de los
# rostros. Los modelos de voz NO: se bajan a pedido desde la app.
deps-sherpa-onnx: deps-rostros
	tools/deps/sherpa-onnx.sh

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

# libpinggy (el túnel de pinggy.io de ba-pinggy, src/pinggy): binario oficial
# fijado por versión y SHA-256 (tools/deps/pinggy.sh). deps-pinggy baja la del
# host (macOS universal, Linux x86_64/aarch64); deps-pinggy-win la de Windows
# (se compila desde Linux).
deps-pinggy:
	tools/deps/pinggy.sh

# hinchada: el probador de carga WebRTC (prueba de sala y de asamblea, docs/funciones/PRUEBA-SALA.md y PRUEBA-ASAMBLEA.md).
# Es un proyecto hermano (../hinchada, o HINCHADA_DIR): se toman sus paquetes ya armados
# (`make -C ../hinchada paquete`) y quedan en extern/hinchada/<plataforma>; el bundle lo
# lleva en bin/64bit/hinchada/. La Mac copia la carpeta del paquete; Linux y Windows, sus .tar.gz.
HINCHADA_DIR ?= $(abspath $(CURDIR)/../hinchada)
# Lo que hay en sus paquetes (para la clave de DEPS_ATAJO): los .tar.gz y el VERSION del de la Mac.
HINCHADA_HUELLA = $(shell cat $(HINCHADA_DIR)/dist/hinchada-*.tar.gz $(HINCHADA_DIR)/dist/hinchada-macos-arm64/VERSION 2>/dev/null | $(SHA256) | cut -c1-16)
ifeq ($(shell uname -s)-$(shell uname -m),Linux-aarch64)
# hinchada no arma paquete para Linux arm64 (sus paquetes: macos-arm64, linux-x86_64,
# windows-x86_64): se dice y se sigue; CMake lo marca como faltante (ba_falta).
deps-hinchada:
	@echo "[deps] hinchada: SALTEADO en linux-aarch64: no hay paquete de hinchada para Linux arm64"
else
deps-hinchada:
	@test -d "$(HINCHADA_DIR)/dist" || { echo "[deps] ERROR hinchada: no está $(HINCHADA_DIR)/dist ('make -C $(HINCHADA_DIR) paquete' o HINCHADA_DIR=…)" >&2; exit 1; }
	@mkdir -p "$(EXTERNS_DIR)/hinchada"
	@# La prueba de sala usa --modo grabado y --hablan; la de asamblea, --servidor integrantes
	@# (--integrantes): un paquete viejo no los conoce.
	@if [ -x "$(HINCHADA_DIR)/dist/hinchada-macos-arm64/hinchada" ] && [ "$$(uname -s)" = Darwin ] && \
	   ! "$(HINCHADA_DIR)/dist/hinchada-macos-arm64/hinchada" ayuda 2>&1 | grep -q -- "--integrantes"; then \
	  echo "[deps] ERROR hinchada: el paquete es viejo (sin --servidor integrantes): 'make -C $(HINCHADA_DIR) paquete'" >&2; exit 1; fi
	@if [ -d "$(HINCHADA_DIR)/dist/hinchada-macos-arm64" ]; then \
	  rm -rf "$(EXTERNS_DIR)/hinchada/macos-arm64" && cp -R "$(HINCHADA_DIR)/dist/hinchada-macos-arm64" "$(EXTERNS_DIR)/hinchada/macos-arm64" && \
	  echo "[deps] hinchada macos-arm64: $$(cat "$(EXTERNS_DIR)/hinchada/macos-arm64/VERSION" 2>/dev/null)"; fi
	@for p in linux-x86_64 windows-x86_64; do \
	  t="$(HINCHADA_DIR)/dist/hinchada-$$p.tar.gz"; \
	  if [ -f "$$t" ]; then rm -rf "$(EXTERNS_DIR)/hinchada/$$p" "$(EXTERNS_DIR)/hinchada/hinchada-$$p" && \
	    tar -xzf "$$t" -C "$(EXTERNS_DIR)/hinchada" && mv "$(EXTERNS_DIR)/hinchada/hinchada-$$p" "$(EXTERNS_DIR)/hinchada/$$p" && \
	    echo "[deps] hinchada $$p: $$(cat "$(EXTERNS_DIR)/hinchada/$$p/VERSION" 2>/dev/null)"; fi; \
	done
endif

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

# chrome-headless-shell (Chrome for Testing): las fotos de las páginas web (web_captura).
.PHONY: deps-chrome-headless deps-chrome-headless-win deps-chrome-headless-linux
deps-chrome-headless:
	tools/deps/chrome-headless.sh $(if $(filter Darwin,$(shell uname -s)),mac-arm64,linux64)

# Las tipografías de texto (Liberation, Carlito, DejaVu, Noto…) para los gráficos y
# los rótulos: los mismos paquetes con que se arma LibreOffice. Ver tools/deps/fuentes-texto.sh.
deps-fuentes-texto:
	tools/deps/fuentes-texto.sh

deps-clap:
	@mkdir -p "$(DL_DIR)"
	@if [ -f "$(EXTERNS_DIR)/clap/include/clap/clap.h" ]; then echo "[deps] CLAP: ya presente"; else \
	  f="$(DL_DIR)/clap-$(CLAP_VER).tar.gz"; \
	  [ -f "$$f" ] && [ "$$($(SHA256) "$$f" | cut -d' ' -f1)" = "$(CLAP_SHA)" ] || \
	    curl -fL --retry 3 -o "$$f" "https://codeload.github.com/free-audio/clap/tar.gz/refs/tags/$(CLAP_VER)"; \
	  printf '%s  %s\n' "$(CLAP_SHA)" "$$f" | $(SHA256_CHECK) || { echo "[deps] ERROR CLAP: hash" >&2; exit 1; }; \
	  rm -rf "$(EXTERNS_DIR)/clap" && mkdir -p "$(EXTERNS_DIR)/clap" && tar xzf "$$f" -C "$(EXTERNS_DIR)/clap" --strip-components=1; \
	  echo "[deps] CLAP $(CLAP_VER): headers en $(EXTERNS_DIR)/clap"; fi

# El del sistema que compila (macOS o Linux) en extern/python; el de Windows,
# en extern/python-win (sus bibliotecas se bajan con el pip del de Linux).
.PHONY: deps-python deps-python-win
deps-python:
	@if [ -x "$(EXTERNS_DIR)/python/bin/python3" ] && [ "$$($(EXTERNS_DIR)/python/bin/python3 -c 'import platform; print(platform.python_version())')" = "$(PY_VER)" ] && \
	    $(EXTERNS_DIR)/python/bin/python3 -c 'import numpy, ctranslate2, sentencepiece, sacremoses, subword_nmt, onnx, PIL' 2>/dev/null && \
	    { [ "$$(uname -s)" != Darwin ] || $(EXTERNS_DIR)/python/bin/python3 -c 'import coremltools' 2>/dev/null; }; then \
		echo "Python ya está ($(EXTERNS_DIR)/python)"; exit 0; fi; \
	$(MAKE) --no-print-directory _ba_baja ARCHIVO="$(DL_DIR)/$(PY_TGZ_$(HERR_PLAT))" URL="$(PY_URL)/$(subst +,%2B,$(PY_TGZ_$(HERR_PLAT)))" SHA="$(PY_SHA_$(HERR_PLAT))" QUE="Python" && \
	bash tools/deps/python.sh "$(DL_DIR)/$(PY_TGZ_$(HERR_PLAT))" "$(EXTERNS_DIR)/python" $(HERR_PLAT)

.PHONY: deps-herramientas
deps-herramientas:
	@if [ -f "$(EXTERNS_DIR)/herramientas/$(HERR_PLAT)/LICENCIAS.txt" ] && grep -q "yt-dlp 2026.08.19" "$(EXTERNS_DIR)/herramientas/$(HERR_PLAT)/LICENCIAS.txt"; then \
		echo "Herramientas ya están ($(EXTERNS_DIR)/herramientas/$(HERR_PLAT))"; exit 0; fi; \
	bash tools/deps/herramientas.sh "$(HERR_PLAT)" "$(DL_DIR)" "$(EXTERNS_DIR)/herramientas/$(HERR_PLAT)"

# --- WordCraft, DeckCraft y GridCraft ----------------------------------------
# Pasan a PDF lo que trae la gente (el carrusel, docs/funciones/CARRUSEL.md, y la
# documentación del evento, evento/APdf) y, por su MCP, el asistente arma
# documentos, presentaciones y gráficos. Reemplazan a LibreOffice: se bajan los
# releases oficiales (no se compilan) y viaja solo la herramienta de línea de
# comandos de cada uno. Ver tools/deps/craft.sh.

deps-craft:
	@if [ "$$(uname -s)" = Darwin ]; then tools/deps/craft.sh mac; else tools/deps/craft.sh linux; fi

# --- Fuentes de BUILD (se compilan en el producto) ---------------------------
# Agrupado: baja y extrae los 4 árboles de código de terceros que el
# CMakeLists raíz consume vía FetchContent. Con `extern/` poblado, el configure
# es OFFLINE (prefiere la copia vendored); sin él, cae a git.
deps-src: deps-mbedtls deps-nlohmann deps-samplerate deps-argon2 deps-libdeflate deps-zxing

# mbedtls, libsamplerate, argon2, libdeflate y zxing-cpp compiladas UNA vez (no en cada
# build ni en cada worktree): extern/estaticas/<plataforma>-<huella> (cmake/estaticas,
# tools/deps/estaticas.sh). Sin ellas, la configuración de CMake frena. Si ya están (la
# misma huella), no hace nada: la llaman `make compilar` y `make test` antes de configurar.
deps-estaticas: deps-estaticas-$(PLATAFORMA)
deps-estaticas-mac: deps-src
	@tools/deps/estaticas.sh macos $(JOBS)
deps-estaticas-linux: deps-src
	@tools/deps/estaticas.sh linux $(JOBS)
deps-estaticas-win: deps-src deps-win-toolchain
	@tools/deps/estaticas.sh win $(JOBS)
	@tools/deps/estaticas.sh win-mt $(JOBS)

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

deps-libdeflate:
	@command -v curl >/dev/null 2>&1 || { echo "[deps] ERROR: falta curl" >&2; exit 1; }
	@mkdir -p "$(DL_DIR)" "$(LIBDEFLATE_DEST)"
	@if [ -f "$(LIBDEFLATE_TAR)" ] && [ "$$($(SHA256) "$(LIBDEFLATE_TAR)" | cut -d' ' -f1)" = "$(LIBDEFLATE_SHA)" ]; then \
	  echo "[deps] libdeflate: ya presente y verificado ($(LIBDEFLATE_TAR))"; \
	else \
	  echo "[deps] libdeflate: descargando $(LIBDEFLATE_URL)"; \
	  curl -fL --retry 3 --connect-timeout 30 -o "$(LIBDEFLATE_TAR).part" "$(LIBDEFLATE_URL)" \
	    && mv "$(LIBDEFLATE_TAR).part" "$(LIBDEFLATE_TAR)" \
	    && printf '%s  %s\n' "$(LIBDEFLATE_SHA)" "$(LIBDEFLATE_TAR)" | $(SHA256_CHECK) \
	    || { echo "[deps] ERROR libdeflate: fallo de descarga o hash" >&2; rm -f "$(LIBDEFLATE_TAR).part"; exit 1; }; \
	fi
	@if [ -f "$(LIBDEFLATE_SENT)" ]; then \
	  echo "[deps] libdeflate: ya extraído en $(LIBDEFLATE_DEST)"; \
	else \
	  echo "[deps] libdeflate: extrayendo a $(LIBDEFLATE_DEST)"; \
	  rm -rf "$(LIBDEFLATE_DEST)"; \
	  mkdir -p "$(LIBDEFLATE_DEST)"; \
	  tar xzf "$(LIBDEFLATE_TAR)" -C "$(LIBDEFLATE_DEST)" --strip-components=1; \
	fi

deps-zxing:
	@command -v curl >/dev/null 2>&1 || { echo "[deps] ERROR: falta curl" >&2; exit 1; }
	@mkdir -p "$(DL_DIR)" "$(ZXING_DEST)"
	@if [ -f "$(ZXING_TAR)" ] && [ "$$($(SHA256) "$(ZXING_TAR)" | cut -d' ' -f1)" = "$(ZXING_SHA)" ]; then \
	  echo "[deps] zxing-cpp: ya presente y verificado ($(ZXING_TAR))"; \
	else \
	  echo "[deps] zxing-cpp: descargando $(ZXING_URL)"; \
	  curl -fL --retry 3 --connect-timeout 30 -o "$(ZXING_TAR).part" "$(ZXING_URL)" \
	    && mv "$(ZXING_TAR).part" "$(ZXING_TAR)" \
	    && printf '%s  %s\n' "$(ZXING_SHA)" "$(ZXING_TAR)" | $(SHA256_CHECK) \
	    || { echo "[deps] ERROR zxing-cpp: fallo de descarga o hash" >&2; rm -f "$(ZXING_TAR).part"; exit 1; }; \
	fi
	@if [ -f "$(ZXING_SENT)" ]; then \
	  echo "[deps] zxing-cpp: ya extraído en $(ZXING_DEST)"; \
	else \
	  echo "[deps] zxing-cpp: extrayendo a $(ZXING_DEST)"; \
	  rm -rf "$(ZXING_DEST)"; \
	  mkdir -p "$(ZXING_DEST)"; \
	  tar xzf "$(ZXING_TAR)" -C "$(ZXING_DEST)" --strip-components=1; \
	fi

# --- Estado / limpieza de dependencias --------------------------------------
deps-status:
	@echo "Assets de terceros en $(EXTERNS_DIR)/:"
	@echo "  Qt6 $(QT6_VER) (win64_msvc2022_64): $$( [ -f '$(QT6_SENTINEL)' ] && echo OK || echo ausente )"
	@echo "  libwebrtc M123 ($(BA_WEBRTC_TARGET)): $$( [ -f '$(WEB_DEST)/lib/$(WEB_LIB)' ] && echo OK || echo ausente )"
	@echo "  cloudflared $(CLOUDFLARED_VER)     : $$( [ -f '$(CFL_EXE)' ] && echo OK || echo ausente )"
	@echo "  LSP $(LSP_VER)           : $$( [ -f '$(LSP_DEST)/linux-x86_64/lsp-plugins.clap' ] && echo OK || echo ausente )"
	@echo "  --- Fuentes de BUILD (se compilan en el producto) ---"
	@echo "  mbedtls v3.6.2        : $$( [ -f '$(MBEDTLS_SENT)' ] && echo OK || echo ausente )"
	@echo "  nlohmann v3.11.3      : $$( [ -f '$(NLOHMANN_SENT)' ] && echo OK || echo ausente )"
	@echo "  libsamplerate 0.2.2   : $$( [ -f '$(SAMPLERATE_SENT)' ] && echo OK || echo ausente )"
	@echo "  argon2 20190702       : $$( [ -f '$(ARGON2_SENT)' ] && echo OK || echo ausente )"

deps-clean:
	@echo "[deps] borrando $(EXTERNS_DIR)/"
	@rm -rf "$(EXTERNS_DIR)"
