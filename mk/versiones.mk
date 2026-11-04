# mk/versiones.mk — Versiones, URLs y SHA-256 de lo de terceros (extern/), y para qué plataforma se baja cada cosa.
# Lo incluye el Makefile de la raíz (las variables comunes están allá).

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

# --- URLs + SHA-256 por asset -----------------------------------------------
# Qt 6.6.3 (win64_msvc2019_64): SDK (headers + import libs + CMake config + moc)
# y también el runtime: cmake/BaQtDeployWin.cmake copia de su bin/ las DLL que
# ba-app importa de verdad.
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
# binario), NO assets de runtime como Qt/Spout. Mismo patrón idempotente:
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
LIBDEFLATE_URL  := https://codeload.github.com/ebiggers/libdeflate/tar.gz/refs/tags/v1.22
LIBDEFLATE_SHA  := 7f343c7bf2ba46e774d8a632bf073235e1fd27723ef0a12a90f8947b7fe851d6
LIBDEFLATE_TAR  := $(DL_DIR)/libdeflate-1.22.tar.gz
LIBDEFLATE_DEST := $(EXTERNS_DIR)/libdeflate
LIBDEFLATE_SENT := $(LIBDEFLATE_DEST)/CMakeLists.txt
# zxing-cpp 2.3.0 (Apache-2.0): el lector de códigos de barras (DNI, QR). Se
# construye desde la fuente (cmake/BaZxing.cmake); la versión y el hash, también
# en versions.cmake.
ZXING_URL  := https://codeload.github.com/zxing-cpp/zxing-cpp/tar.gz/refs/tags/v2.3.0
ZXING_SHA  := 64e4139103fdbc57752698ee15b5f0b0f7af9a0331ecbdc492047e0772c417ba
ZXING_TAR  := $(DL_DIR)/zxing-cpp-2.3.0.tar.gz
ZXING_DEST := $(EXTERNS_DIR)/zxing-cpp
ZXING_SENT := $(ZXING_DEST)/CMakeLists.txt
ARGON2_URL  := https://codeload.github.com/P-H-C/phc-winner-argon2/tar.gz/refs/tags/20190702
ARGON2_SHA  := daf972a89577f8772602bf2eb38b6a3dd3d922bf5724d45e7f9589b5e830442c
ARGON2_TAR  := $(DL_DIR)/argon2-20190702.tar.gz
ARGON2_DEST := $(EXTERNS_DIR)/argon2
ARGON2_SENT := $(ARGON2_DEST)/Makefile

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
# (ver docs/funciones/CARRUSEL.md). Se eligio sobre MuPDF porque usa el mismo vendoreo que
# ya existe, anda igual en Linux y Windows, y no agrega otro build.
QT6_HOST_PDF := $(QT6_HOST_DEST)/$(QT6_VER)/$(QT6_HOST_DIR)/lib/cmake/Qt6Pdf/Qt6PdfConfig.cmake
# Multimedia (qtmultimedia), con su propio centinela por lo mismo que el PDF.
# Es para VER un archivo antes de ponerlo al aire: doble clic en la pestaña
# Medios. Su motor ES ffmpeg —Qt lo trae adentro desde 6.5— asi que no hace
# falta ni VLC ni un ffmpeg del sistema ni levantar otro proceso: es el mismo
# vendoreo de Qt que ya usamos, igual en Linux y en Windows.
QT6_HOST_MM := $(QT6_HOST_DEST)/$(QT6_VER)/$(QT6_HOST_DIR)/lib/cmake/Qt6Multimedia/Qt6MultimediaConfig.cmake

QT6_WASM_DEST     := $(EXTERNS_DIR)/qt6-wasm
QT6_WASM_SENTINEL := $(QT6_WASM_DEST)/$(QT6_VER)/wasm_singlethread/bin/qt-cmake
EMSDK_VER         := 3.1.37
EMSDK_SHA         := c3c80dfe82fb448751d1c48200e5143aeca0f0db1d021db4b63655a9d9470cd8
EMSDK_URL         := https://github.com/emscripten-core/emsdk/archive/refs/tags/$(EMSDK_VER).tar.gz
EMSDK_DEST        := $(EXTERNS_DIR)/emsdk

# lwIP 2.2.0 (BSD): pila IP en espacio de usuario para el tunel WireGuard del
# gateway (src/gateway/net). El zip de Savannah es un archivo estatico: se fija
# su hash. Mismo codigo que el tag STABLE-2_2_0_RELEASE de GitHub (verificado).
LWIP_URL := https://download.savannah.nongnu.org/releases/lwip/lwip-2.2.0.zip
LWIP_SHA := e12c769be5a1da9a1edf1b8f38d645c6c87d52a26a636172cea4b8c63ec04994
LWIP_ZIP := $(DL_DIR)/lwip-2.2.0.zip

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

# --- La terminal del Asistente --------------------------------------------------
# libvterm (MIT): emulador de terminal de la pestaña Asistente (la TUI del
# agente de IA corre adentro). C puro, se compila estatico en ba-app.
LIBVTERM_URL := https://launchpad.net/libvterm/trunk/v0.3/+download/libvterm-0.3.3.tar.gz
LIBVTERM_SHA := 09156f43dd2128bd347cbeebe50d9a571d32c64e0cf18d211197946aff7226e0
# JetBrains Mono (OFL 1.1): fuente monoespaciada de la terminal.
FONTS_URL := https://github.com/JetBrains/JetBrainsMono/releases/download/v2.304/JetBrainsMono-2.304.zip
FONTS_SHA := 6f6376c6ed2960ea8a963cd7387ec9d76e3f629125bc33d1fdcd7eb7012f7bbf

# --- Raices de certificados (HTTPS propio de A: API de YouTube) -----------------
# El cliente HTTPS usa mbedtls (vendoreado): nada de libssl del sistema. Las
# raices son las de Mozilla empaquetadas por curl, con fecha y hash fijos.
CACERT_URL := https://curl.se/ca/cacert-2026-08-13.pem
CACERT_SHA := f66dff1bdf8f96060b8177976f8b7d9254bc89bc4db933d769f7384d28480bc9

# CLAP (free-audio/clap, MIT): headers del formato de plugins que carga el host.
CLAP_VER := 1.2.6
CLAP_SHA := 6548b5831ad9e92551c752753e535b18fead4001165ef1df8e643efefa7133b7

# --- GStreamer (macOS) ---------------------------------------------------------
# El GStreamer.framework oficial (relocalizable), desarmado sin instalarlo:
# tools/deps/gstreamer-macos.sh. Versión y sha256 fijados (los .sha256sum que
# publica gstreamer.freedesktop.org).
GST_VER := 1.28.7
GST_URL := https://origin.gstreamer.freedesktop.org/data/pkg/osx/$(GST_VER)
GST_RT := gstreamer-1.0-$(GST_VER)-universal.pkg
GST_RT_SHA := 529fdf4a4027d942e59b5b3564f6400adaa008f63ce5f3fed4ffe35d73911994
GST_DEV := gstreamer-1.0-devel-$(GST_VER)-universal.pkg
GST_DEV_SHA := 72a44870cf02472cbf6e9a84bcc25ee6807dd1c26659a112066373544d365e7a

# --- Python de los plugins (docs/funciones/PLUGINS.md) -----------------------------------
# CPython relocalizable de python-build-standalone: va adentro del programa
# (Resources/python). Versión y sha256 fijados (el digest que publica GitHub).
PY_REL := 20261001
PY_VER := 3.12.15
PY_URL := https://github.com/astral-sh/python-build-standalone/releases/download/$(PY_REL)
PY_TGZ_macos-arm64 := cpython-$(PY_VER)+$(PY_REL)-aarch64-apple-darwin-install_only_stripped.tar.gz
PY_SHA_macos-arm64 := 10cab8f6ed6202fdd81637aa6eda4af8d5b7eaa8fc42f9df3c6bea4923de0d93
PY_TGZ_linux-x64 := cpython-$(PY_VER)+$(PY_REL)-x86_64-unknown-linux-gnu-install_only_stripped.tar.gz
PY_SHA_linux-x64 := 7bb1659e3235077b7f63d5b6eb6ce653c6fcd6c5041e9d5f73b42ce10421464d
PY_TGZ_win-x64 := cpython-$(PY_VER)+$(PY_REL)-x86_64-pc-windows-msvc-install_only_stripped.tar.gz
PY_SHA_win-x64 := 52124cee54126f3f360eaa378288f6f64c402c983a3c14c95eff67f4af986aaa

# --- Herramientas de medios (ffmpeg, ffprobe, yt-dlp, deno) --------------------
# Adentro del programa (herramientas/): para arreglar o bajar un video sin
# bajar nada en el momento. Versiones y sha256 fijados en el script.
# HERR_PLAT: macos-arm64 | linux-x64 | linux-arm64 | win-x64 (por defecto, el host).
HERR_HOST := $(shell uname -s)-$(shell uname -m)
HERR_PLAT ?= $(if $(filter Darwin-arm64,$(HERR_HOST)),macos-arm64,$(if $(filter Linux-x86_64,$(HERR_HOST)),linux-x64,$(if $(filter Linux-aarch64,$(HERR_HOST)),linux-arm64,win-x64)))

# GStreamer de Windows: el oficial de MSVC, desarmado (tools/deps/gstreamer-win.sh).
GST_WIN_VER := 1.26.11
GST_WIN_URL := https://origin.gstreamer.freedesktop.org/data/pkg/windows/$(GST_WIN_VER)/msvc
GST_WIN_RT := gstreamer-1.0-msvc-x86_64-$(GST_WIN_VER).msi
GST_WIN_RT_SHA := 31cbc21fa0950b5c1e79c80959b2799805cb05a7a35953a13a9f790776137605
GST_WIN_DEV := gstreamer-1.0-devel-msvc-x86_64-$(GST_WIN_VER).msi
GST_WIN_DEV_SHA := af0a0720692052aaf5fb0cf2847ef34a87016ef0f914328d516f049e6c86f587

# mlx-swift-lm (ba-modelo, la Mac): el commit fijado; los parches, en src/modelo-mac/parches/.
MLX_SWIFT_LM_REV := ac6f5651a423b6ad9005332eed583dd336e59a53
