# ============================================================================
# Broadcast Appliance Portable v2 — build raíz
#
# Proyecto principal: Windows/MSVC (x64), pero el árbol TAMBIÉN debe configurar
# y compilar en Linux (gcc/clang) para poder correr las pruebas unitarias de
# `ba_core` en el desarrollo local.
#
# Reglas de la arquitectura (docs/prompts/primer_prompt.md §14, §3, §9):
#   - C++20 obligatorio en todo el árbol.
#   - `ba_core` es el núcleo: SIN Qt, SIN libobs, testeable por unidad.
#   - El launcher (`BroadcastAppliance`) es Win32 puro con CRT estático (/MT).
#   - A (`ba-app`), B (`ba-gateway`) y el módulo (`ba-sources`) usan /MD.
#   - Los PDBs se emiten siempre.
#
# Este archivo solo monta el andamiaje de build. Los sources los aportan los
# agentes de cada componente vía file(GLOB_RECURSE ... CONFIGURE_DEPENDS),
# por lo que aquí NO se nombran archivos fuente individuales.
# ============================================================================

cmake_minimum_required(VERSION 3.21)

# --- Proyecto ---------------------------------------------------------------
project(
    BroadcastAppliance
    VERSION 2.0.0
    DESCRIPTION "Broadcast Appliance Portable v2 (libobs embebido + Gateway WebRTC + Supervisor)"
    LANGUAGES C CXX
)

# C++20 es obligatorio en todos los targets.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- ccache (caché de compilación) -----------------------------------------
# Acelerador de builds: si `ccache` está en el PATH, se usa como launcher de
# compilación para que CUALQUIER build (nativo o cross) cachee las TU ya
# compiladas. Guardado con find_program + "no ya fijado" para que:
#   - no rompa nada si ccache no existe (find_program deja la variable vacía),
#   - no anule un launcher que el usuario ya haya fijado a mano
#     (-DCMAKE_CXX_COMPILER_LAUNCHER=...),
#   - sea puramente aditivo: no toca FetchContent, el if(WIN32) del VST3 SDK,
#     los flags de MSVC ni los add_subdirectory de los componentes.
# Verificar que ccache está activo: `ccache -s` (hits/misses) o la línea STATUS
# que se imprime en el configure.
find_program(CCACHE_LAUNCHER ccache)
if(CCACHE_LAUNCHER AND NOT CMAKE_C_COMPILER_LAUNCHER AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    set(CMAKE_C_COMPILER_LAUNCHER    "${CCACHE_LAUNCHER}")
    set(CMAKE_CXX_COMPILER_LAUNCHER  "${CCACHE_LAUNCHER}")
    message(STATUS "ccache activo: ${CCACHE_LAUNCHER} (launcher de C/CXX)")
else()
    message(STATUS "ccache no activo (no encontrado o ya hay un launcher fijado)")
endif()

# --- Versiones fijadas ------------------------------------------------------
# Un único archivo concentra las versiones de librerías de terceros para que
# el build sea reproducible y auditable (spec §14 "Versiones fijadas").
include("${CMAKE_CURRENT_SOURCE_DIR}/versions.cmake")

# --- Dependencias de terceros (FetchContent) --------------------------------
# Se descargan y compilan dentro del árbol de build (self-contained, sin
# instalación global). mbedtls es una lib de C, por eso el proyecto declara
# `LANGUAGES C CXX` arriba. Se declaran ANTES de `add_subdirectory(src/core)`
# para que los targets `mbedcrypto`/`mbedx50`/`mbedtls` y
# `nlohmann_json::nlohmann_json` ya existan cuando `ba_core` los enlaza.
include(FetchContent)
FetchContent_Declare(mbedtls
    GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
    GIT_TAG ${MBEDTLS_VERSION})
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG ${NLOHMANN_JSON_VERSION})
# libsamplerate (Secret Rabbit Code, MIT): re-muestreo de audio (spec 7.4).
# Lib de C pura que compila en Linux y Windows, por eso se trae SIN if(WIN32)
# (a diferencia del VST3 SDK de más abajo). Target resultante: `samplerate`
# (alias `SampleRate::samplerate`).
FetchContent_Declare(libsamplerate
    GIT_REPOSITORY https://github.com/libsndfile/libsamplerate.git
    GIT_TAG ${LIBSAMPLERATE_VERSION})
# Argon2 (referencia P-H-C/phc-winner-argon2, CC0/Apache-2.0): KDF Argon2id
# para el cifrado de `Config/secrets.json` (spec §12, decisión D7). C pura que
# compila en Linux y Windows, por eso se trae SIN if(WIN32) (como libsamplerate).
# ⚠️  El repo NO trae CMakeLists (es Makefile): FetchContent_MakeAvailable solo
# lo POPULA (baja el código a _deps/argon2-src) y NO crea target. Por eso se
# arma el target `argon2` a mano justo debajo, con la lista de sources del
# Makefile oficial (argon2.c core.c ref.c thread.c encoding.c blake2/blake2b.c;
# `ref.c` es la variante no optimizada por SIMD, la que compila en cualquier
# gcc/MSVC). Verificado que compila y corre en gcc 13 / Linux.
FetchContent_Declare(argon2
    GIT_REPOSITORY https://github.com/P-H-C/phc-winner-argon2.git
    GIT_TAG ${ARGON2_VERSION})
# Compilar mbedtls como lib estática, sin sus tests/programs/examples.
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
# libsamplerate trae sus propios tests (misc_test, simple_test, ...) que su
# CMakeLists registra con CTest cuando BUILD_TESTING está ON. Lo forzamos OFF
# para que no ensucien la lista de tests del proyecto: ctest debe seguir
# mostrando solo los tests de `ba_core`.
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
# MinGW (GCC 13+) emite -Wunterminated-string-initialization para los arrays de
# labels de tamaño fijo de mbedtls (ssl_tls13_keys.h) y, con -Werror, el build
# de la lib de terceros falla. mbedtls añade -Werror SOLO cuando
# MBEDTLS_FATAL_WARNINGS está ON, así que lo desactivamos. Guardado con
# if(WIN32) para NO tocar el build Linux (ya verde, 18/18), donde el gcc nativo
# no dispara esa advertencia.
if(WIN32)
    set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
endif()

# --- Offline-first: preferir las fuentes vendored en extern/ -----------------
# Si `make deps-src` (o `make deps`) ya pobló extern/ con los árboles de
# código de terceros, se apunta FetchContent a esa copia local vía
# FETCHCONTENT_SOURCE_DIR_<NOMBRE> (en mayúsculas) y el configure NO toca la
# red: es el requisito de producto portable/offline. Si extern/ no está
# poblado (CI, clon fresco sin `make deps`), se deja el FetchContent_Declare
# (GIT_REPOSITORY) de arriba como FALLBACK online. NO se borran los Declare ni
# el target `argon2` de más abajo: solo se sobreescribe la source dir cuando el
# sentinel de extern/ existe. Con la override, `${argon2_SOURCE_DIR}` resuelve
# a `extern/argon2` y el target a mano sigue compilando los mismos sources.
set(_BA_EXTERN "${CMAKE_CURRENT_SOURCE_DIR}/extern")
if(EXISTS "${_BA_EXTERN}/mbedtls/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_MBEDTLS "${_BA_EXTERN}/mbedtls")
    message(STATUS "mbedtls: usando vendored en extern/ (offline)")
else()
    message(STATUS "mbedtls: sin extern/mbedtls: usando git")
endif()
if(EXISTS "${_BA_EXTERN}/nlohmann-json/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON "${_BA_EXTERN}/nlohmann-json")
    message(STATUS "nlohmann_json: usando vendored en extern/ (offline)")
else()
    message(STATUS "nlohmann_json: sin extern/nlohmann-json: usando git")
endif()
if(EXISTS "${_BA_EXTERN}/libsamplerate/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_LIBSAMPLERATE "${_BA_EXTERN}/libsamplerate")
    message(STATUS "libsamplerate: usando vendored en extern/ (offline)")
else()
    message(STATUS "libsamplerate: sin extern/libsamplerate: usando git")
endif()
if(EXISTS "${_BA_EXTERN}/argon2/Makefile")
    set(FETCHCONTENT_SOURCE_DIR_ARGON2 "${_BA_EXTERN}/argon2")
    message(STATUS "argon2: usando vendored en extern/ (offline)")
else()
    message(STATUS "argon2: sin extern/argon2: usando git")
endif()
FetchContent_MakeAvailable(mbedtls nlohmann_json libsamplerate argon2)

# Target `argon2` a mano (el repo no trae CMakeLists): lib estática con los
# sources del Makefile oficial. `ref.c` = implementación de referencia (sin
# SIMD), válida en cualquier plataforma. Los headers públicos viven en
# include/ (argon2.h) y se propagan como PUBLIC para que `ba_core` los vea.
add_library(argon2 STATIC
    ${argon2_SOURCE_DIR}/src/argon2.c
    ${argon2_SOURCE_DIR}/src/core.c
    ${argon2_SOURCE_DIR}/src/ref.c
    ${argon2_SOURCE_DIR}/src/thread.c
    ${argon2_SOURCE_DIR}/src/encoding.c
    ${argon2_SOURCE_DIR}/src/blake2/blake2b.c)
target_include_directories(argon2 PUBLIC ${argon2_SOURCE_DIR}/include)

# --- VST3 SDK (Steinberg, MIT) — SOLO Windows -------------------------------
# Base del host de plugins (spec D6/§7.5, LSP como componente CORE). Se trae
# SOLO en Windows: en Linux el build es OFFLINE y el módulo `ba_plugin_host`
# compila solo con std::filesystem (sin SDK, sin red). Guardado con if(WIN32)
# para que el configure de Linux no toque la red ni compile el SDK.
if(WIN32)
    # El SDK trae, además de las librerías que usamos (base/pluginterfaces/
    # sdk_hosting), sus ejemplos de hosting. El ejemplo `VST3Inspector` se crea
    # vía `vstgui_add_executable`, que SOLO crea el target bajo MSVC/LINUX/
    # CMAKE_HOST_APPLE pero llama a target_link_libraries SIEMPRE: en un
    # cross-compile MinGW (host=Linux, target=Windows) ninguna de esas ramas
    # coincide, el target no existe y el configure falla. Solo necesitamos las
    # librerías del SDK, no sus ejemplos GUI, así que desactivamos el soporte
    # de VSTGUI (que elimina `vstgui`/`vstgui_standalone` y con ello saltea
    # `inspectorapp`) y los ejemplos. `validator`/`moduleinfotool` solo enlazan
    # `sdk_hosting` (no vstgui) y son cross-platform, por lo que siguen bien.
    set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
    set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
    # El sample `validator` del SDK arranca su propio binario en POST_BUILD
    # (-selftest). En un cross-compile MinGW (host=Linux) ese PE no corre bajo
    # /bin/sh ("Exec format error"), por lo que se desactiva: solo necesitamos
    # las librerías del SDK, no sus samples ni su selftest.
    set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(vst3sdk
        GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
        GIT_TAG ${VST3_SDK_VERSION}
        GIT_SHALLOW TRUE)
    # Offline-first (mismo criterio que las 4 nativas de arriba): si extern/
    # trae el SDK vendored, se usa esa copia y el configure no toca la red;
    # si no, cae al git de arriba.
    if(EXISTS "${_BA_EXTERN}/vst3-sdk/CMakeLists.txt")
        set(FETCHCONTENT_SOURCE_DIR_VST3SDK "${_BA_EXTERN}/vst3-sdk")
        message(STATUS "vst3sdk: usando vendored en extern/ (offline)")
    else()
        message(STATUS "vst3sdk: sin extern/vst3-sdk: usando git")
    endif()
    FetchContent_MakeAvailable(vst3sdk)

    # --- Excluir del build los targets del SDK que NO usamos ------------------
    # Solo enlazamos `base`, `pluginterfaces`, `sdk_hosting` y `sdk_common`
    # (ver src/plugin-host). El resto de targets que el SDK crea por defecto
    # se compilan dentro de `all` y, en un cross-compile MinGW, fallan en
    # `public.sdk/source/vst/utility/alignedalloc.h:79` (`std::aligned_alloc`
    # no existe en el libstdc++ de MinGW):
    #   - `sdk`        : lib completa del SDK; `dataexchange.cpp` usa
    #                     `Steinberg::Vst::aligned_alloc`. Ningún target de
    #                     producto la enlaza (solo la usan los samples).
    #   - `validator`  : test-host de Steinberg (samples/vst-hosting/validator);
    #                     NO se crea bajo ninguna variable del SDK (audiohost/
    #                     editorhost/inspectorapp sí respetan
    #                     SMTG_ENABLE_VST3_HOSTING_EXAMPLES, validator no).
    #   - `moduleinfotool` : util de samples/vst-utilities; la desactiva
    #                     `SMTG_ADD_VST3_UTILITIES` (ON por defecto).
    # `EXCLUDE_FROM_ALL` los saca del target `all` (y de `cmake --build`) sin
    # borrar su definición, por lo que sigue siendo enlazable si algún día se
    # quisiera. Solo se aplica en Windows: en Linux el SDK no se trae
    # (if(WIN32)) y estos targets no existen.
    foreach(_ba_vst3_excl_target sdk validator moduleinfotool)
        if(TARGET ${_ba_vst3_excl_target})
            set_target_properties(${_ba_vst3_excl_target}
                PROPERTIES EXCLUDE_FROM_ALL ON)
            message(STATUS "VST3 SDK: target '${_ba_vst3_excl_target}' -> EXCLUDE_FROM_ALL (no lo usamos; evita el fallo de aligned_alloc en cross MinGW)")
        endif()
    endforeach()
endif()

# --- Config por defecto del build -------------------------------------------
# Si no se pide nada, vamos a Release (el artefacto portable es Release).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# --- Flags del compilador ---------------------------------------------------
# MSVC: flags del spec §14. /MD por defecto para A, B y el módulo (consistencia
# con libobs y Qt); el launcher se pasa a /MT en su propio CMakeLists.
# Los PDBs se emiten SIEMPRE (debug info) incluso en Release.
if(MSVC)
    add_compile_options(
        /W4            # nivel alto de advertencias
        /permissive-   # modo conforme estricto
        /Zc:__cplusplus # __cplusplus refleja el estándar real
        /utf-8         # fuente y ejecutable en UTF-8
        /EHsc          # excepciones activas, estilo estándar
    )
    # /MD por defecto (CRT dinámica). El launcher lo anula a /MT localmente.
    # /Zi garantiza PDBs en todos los configurados.
    add_compile_options(
        $<$<CONFIG:Debug>:/MDd>
        $<$<CONFIG:Release>:/MD>
        $<$<CONFIG:RelWithDebInfo>:/MD>
        $<$<CONFIG:MinSizeRel>:/MD>
        /Zi
    )
    # PDBs de enlace siempre.
    add_link_options(
        $<$<CONFIG:Debug>:/MDd>
        $<$<CONFIG:Release>:/MD>
        $<$<CONFIG:RelWithDebInfo>:/MD>
        $<$<CONFIG:MinSizeRel>:/MD>
        /DEBUG
    )
else()
    # Linux (gcc/clang): advertencias estrictas razonables.
    add_compile_options(-Wall -Wextra)
    # Debug info por defecto para poder correr los unit tests con sanitizers.
    add_compile_options($<$<CONFIG:Debug>:-g>)
endif()

# --- Testing ----------------------------------------------------------------
# enable_testing() + include(CTest) habilitan `ctest` y el target `test`.
enable_testing()
include(CTest)

# --- Componentes ------------------------------------------------------------
# Cada subdirectorio declara su target. `ba_core` es la base; el resto enlaza
# contra ella. El launcher solo existe en Windows (Win32 puro); el resto se
# configura en ambas plataformas.
add_subdirectory(src/core)
add_subdirectory(src/launcher)
add_subdirectory(src/app)
add_subdirectory(src/obs-module)
add_subdirectory(src/gateway)
add_subdirectory(src/plugin-host)

# --- Reglas de instalación (layout 5.1) -------------------------------------
# El layout espeja el portable de OBS (spec §5.1). Las rutas de destino son
# relativas a CMAKE_INSTALL_PREFIX. Aquí se crean los directorios del layout
# y se instalan los targets propios; los runtime de terceros (OBS, CEF, Qt,
# VC++, cloudflared) los copia `tools/deploy` sobre este esqueleto.
#
#   <prefix>/
#   ├── bin/64bit/            # ejecutables y DLLs propias
#   ├── obs-plugins/64bit/    # plugins de libobs (incluye ba-sources)
#   ├── data/libobs/          # shaders (*.effect) de libobs
#   ├── Config/               # configuración del usuario
#   ├── Logs/                 # logs rotativos y dumps
#   ├── Recordings/           # grabación de respaldo
#   └── Web/guest/            # app web del invitado (la sirve B)

# Layout portable: por defecto el prefix es la raíz del disco.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    # No forzar un prefix concreto; el deploy decide. Solo documentamos.
endif()

# Directorios del layout (se crean vacíos para que el árbol exista en el disco).
# `install(DIRECTORY DESTINATION ...)` sin fuente crea el directorio vacío.
install(DIRECTORY
    DESTINATION bin/64bit
)
install(DIRECTORY
    DESTINATION obs-plugins/64bit
)
install(DIRECTORY
    DESTINATION data/libobs
)
install(DIRECTORY
    DESTINATION Config
)
install(DIRECTORY
    DESTINATION Logs
)
install(DIRECTORY
    DESTINATION Recordings
)
install(DIRECTORY
    DESTINATION Web/guest
)

# --- Target de instalación de los ejecutables/DLLs propios ------------------
# Cada componente declara su propio install(TARGETS ...) en su CMakeLists para
# controlar el directorio de destino exacto. Aquí se centraliza solo la
# documentación de a dónde va cada cosa.
