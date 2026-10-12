# ============================================================================
# versions.cmake — Versiones FIJADAS (pins) de dependencias de terceros
#
# Único lugar donde se fijan las versiones (spec §14 "Versiones fijadas").
# Todo el build consume estas variables para ser reproducible y auditable.
#
# ⚠️  ESTAS SON LAS PINS FIJADAS. No cambiarlas sin una decisión registrada:
#     cada una arrastra una versión concreta de headers/binarios en `extern/`
#     y una superficie de compatibilidad (GStreamer, Qt, Spout2, libwebrtc).
#     Si hay que mover una, se documenta en el changelog y se re-valida el
#     preflight (spec §10) contra el tag resultante.
# ============================================================================

# --- Qt 6 ------------------------------------------------------------------
# Qt 6 Widgets para la UI del Proceso A. Se usa la LTS 6.6.x (estable,
# mantenida). El runtime se despliega con windeployqt (spec §5.2).
set(QT6_VERSION "6.6.3" CACHE STRING "Versión de Qt 6")
mark_as_advanced(QT6_VERSION)

# --- Spout2 ----------------------------------------------------------------
# SDK de Spout2 (API DX11, clase `spoutDX`). Se enlaza estático; no hace falta
# Spout2.dll (spec §4.2). Se fija por tag de release del SDK.
set(SPOUT2_VERSION "2.0.0.0" CACHE STRING "Versión/tag del SDK Spout2")
mark_as_advanced(SPOUT2_VERSION)

# --- libwebrtc -------------------------------------------------------------
# Milestone de libwebrtc (binarios precompilados, p. ej. webrtc-build de
# Shiguredo). Se enlaza estático en el Proceso B (spec §8.1, §5.1). Se fija
# por milestone M + versión de Chromium base para reproducibilidad.
set(WEBRTC_MILESTONE "M123" CACHE STRING "Milestone de libwebrtc")
set(WEBRTC_VERSION "123.0.6312.5" CACHE STRING "Versión base de Chromium de libwebrtc")
mark_as_advanced(WEBRTC_MILESTONE WEBRTC_VERSION)

# --- cloudflared -----------------------------------------------------------
# Túnel de Cloudflare que supervisa el Proceso B (spec §8.8). Versión
# date-based (AAAA.N.P). Se copia en bin/64bit/ en la versión fijada.
set(CLOUDFLARED_VERSION "2024.11.0" CACHE STRING "Versión de cloudflared")
mark_as_advanced(CLOUDFLARED_VERSION)

# --- mbedTLS ---------------------------------------------------------------
# Criptografía (TLS, X.509, primitivas) para el núcleo. Se compila estática
# (sin tests/programs/examples) vía FetchContent en el CMakeLists raíz. Se fija
# por tag de release 3.6.x (estable, mantenida).
set(MBEDTLS_VERSION "v3.6.2" CACHE STRING "Versión/tag de mbedTLS")
mark_as_advanced(MBEDTLS_VERSION)

# --- nlohmann/json ---------------------------------------------------------
# JSON header-only para serializar/deserializar en el núcleo. Se enlaza como
# target `nlohmann_json::nlohmann_json` (header-only, sin binario). Se fija por
# tag de release.
set(NLOHMANN_JSON_VERSION "v3.11.3" CACHE STRING "Versión/tag de nlohmann/json")
mark_as_advanced(NLOHMANN_JSON_VERSION)

# --- Argon2 (referencia, dominio público) ----------------------------------
# KDF para el cifrado de `Config/secrets.json` (spec §12, decisión D7):
# Argon2id de la passphrase maestra + salt aleatorio -> clave AES-256-GCM.
# Se usa la implementación de referencia del Password Hashing Competition
# (P-H-C/phc-winner-argon2, CC0/Apache-2.0, C pura que compila en Linux y
# Windows). ⚠️  El repo NO trae CMakeLists (es Makefile): se declara con
# FetchContent y se arma el target `argon2` a mano en el CMakeLists raíz con
# la lista de sources del Makefile oficial (argon2.c core.c ref.c thread.c
# encoding.c blake2/blake2b.c). Tag real verificado con `git ls-remote --tags`
# (el último es `20190702`; no existe tag con prefijo `v`).
set(ARGON2_VERSION "20190702" CACHE STRING "Versión/tag de Argon2 (referencia P-H-C)")
mark_as_advanced(ARGON2_VERSION)

# libdeflate (MIT, Eric Biggers): el gzip de ba-web (src/web). C puro, rápido, sin
# dependencias. Se baja el tarball del tag con su SHA-256 (make deps-libdeflate).
set(LIBDEFLATE_VERSION "1.22" CACHE STRING "Versión de libdeflate")
set(LIBDEFLATE_SHA256 "7f343c7bf2ba46e774d8a632bf073235e1fd27723ef0a12a90f8947b7fe851d6" CACHE STRING "SHA-256 del tarball de libdeflate")
mark_as_advanced(LIBDEFLATE_VERSION LIBDEFLATE_SHA256)

# --- libsamplerate (Secret Rabbit Code, MIT) --------------------------------
# Re-muestreo de audio (cambio de frecuencia de muestreo) para la cadena DSP
# (spec 7.4). Lib de C pura que compila en Linux y Windows; se trae por
# FetchContent en el CMakeLists raíz (sin if(WIN32)). Se fija por tag de
# release. ⚠️  El tag real del repo oficial (libsndfile/libsamplerate) es
# `0.2.2` (NO existe `v2.2.0`): verificado con `git ls-remote --tags`.
set(LIBSAMPLERATE_VERSION "0.2.2" CACHE STRING "Versión/tag de libsamplerate")
mark_as_advanced(LIBSAMPLERATE_VERSION)

# --- LSP Plugins (LGPL) -----------------------------------------------------
# Modulos de audio del rack por fuente (formato CLAP, host propio en
# src/app/audio). Linux: el CLAP oficial (`make deps-lsp`); Windows: compilado
# desde la fuente oficial (tools/deps/lsp-win.sh). Van a plugins/lsp/.
set(LSP_VERSION "1.2.35" CACHE STRING "Versión de LSP Plugins (CLAP)")
mark_as_advanced(LSP_VERSION)

# --- clang-format -----------------------------------------------------------
# Versión esperada de clang-format para `make lint` (y el job `lint` de CI).
# No se descarga nada: el binario entra por apt. El job `lint` de CI corre con
# `runs-on: ubuntu-24.04` FIJADO (noble), donde `apt install clang-format-18`
# da exactamente esta versión, y el job la aserta antes de correr `make lint`.
# En el host: `apt install clang-format-18`. (Antes se descargaba el tarball
# oficial de LLVM para pinnear la versión; se eliminó: la versión de apt de
# noble coincide y es más simple.)
set(CLANG_FORMAT_VERSION "18.1.3" CACHE STRING "Versión de clang-format")
mark_as_advanced(CLANG_FORMAT_VERSION)

# --- Resumen legible en el configure ---------------------------------------
message(STATUS "BroadcastAppliance — pins fijadas:")
message(STATUS "  Qt6         : ${QT6_VERSION}")
message(STATUS "  Spout2      : ${SPOUT2_VERSION}")
message(STATUS "  libwebrtc   : ${WEBRTC_MILESTONE} (${WEBRTC_VERSION})")
message(STATUS "  cloudflared : ${CLOUDFLARED_VERSION}")
message(STATUS "  mbedTLS     : ${MBEDTLS_VERSION}")
message(STATUS "  nlohmann    : ${NLOHMANN_JSON_VERSION}")
message(STATUS "  Argon2      : ${ARGON2_VERSION} (referencia P-H-C, KDF de secrets.json)")
message(STATUS "  libdeflate  : ${LIBDEFLATE_VERSION} (gzip de ba-web)")
message(STATUS "  libsamplerate: ${LIBSAMPLERATE_VERSION}")
message(STATUS "  LSP Plugins : ${LSP_VERSION} (CLAP)")
message(STATUS "  clang-format: ${CLANG_FORMAT_VERSION} (herramienta de lint)")
