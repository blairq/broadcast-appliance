# ============================================================================
# Broadcast Appliance Portable v2 — build raíz
#
# Proyecto principal: Windows/MSVC (x64), pero el árbol TAMBIÉN debe configurar
# y compilar en Linux (gcc/clang) para poder correr las pruebas unitarias de
# `ba_core` en el desarrollo local.
#
# Reglas de la arquitectura (docs/prompts/primer_prompt.md §14, §3, §9):
#   - C++20 obligatorio en todo el árbol.
#   - `ba_core` es el núcleo: SIN Qt, SIN motor de video, testeable por unidad.
#   - El launcher (`BroadcastAppliance`) es Win32 puro con CRT estático (/MT).
#   - A (`ba-app`) usa /MD; el gateway va /MT con el launcher (cmake/BaWinStaticPart.cmake).
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
    DESCRIPTION "Broadcast Appliance Portable v2 (motor GStreamer + Gateway WebRTC + Supervisor)"
    LANGUAGES C CXX
)

# Windows (MSVC/clang-cl): el launcher y el gateway se compilan con la CRT
# ESTATICA en un sub-build propio (cmake/BaWinStaticPart.cmake). Con esta
# opcion en ON, este mismo arbol ES ese sub-build: solo core+launcher+gateway.
option(BA_WIN_STATIC_PART "Sub-build interno de Windows: launcher y gateway /MT" OFF)

# Un build que MIDE (diag/Perfil): la CPU de cada hilo y la GPU cada segundo, en la
# app y en el gateway, a Logs/perfil-*.jsonl y la herramienta MCP `perfil`; también
# las mediciones del motor (BA_MEDIR_VISTAS). Para encontrar dónde se va la CPU o la
# GPU en una máquina. `make … PERFILAR=1`. Apagado, el programa queda igual.
# Solo diag/Perfil.cpp lo ve (src/core/CMakeLists.txt): el resto pregunta
# Perfil::activo(), así cambiarlo recompila un archivo y no el programa entero.
option(BA_PERFILAR "Medir CPU por hilo y GPU (diagnóstico de rendimiento)" OFF)
if(BA_PERFILAR)
    message(STATUS "BA_PERFILAR: build que mide CPU y GPU")
endif()

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
#   - sea puramente aditivo: no toca FetchContent, los flags de MSVC ni los add_subdirectory de los componentes.
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
# CMake 4 ya no acepta cmake_minimum_required < 3.5, que es lo que declaran
# libsamplerate 0.2.2 y otras de terceros (lo trae Homebrew en macOS y lo van
# a traer los runners). Se les permite configurar con la politica de 3.5.
if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()
FetchContent_Declare(mbedtls
    GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
    GIT_TAG ${MBEDTLS_VERSION})
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG ${NLOHMANN_JSON_VERSION})
# libsamplerate (Secret Rabbit Code, MIT): re-muestreo de audio (spec 7.4).
# Lib de C pura que compila en Linux y Windows. Target resultante: `samplerate`
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
# libdeflate (MIT): el gzip de ba-web (src/web). Solo la biblioteca estática.
FetchContent_Declare(libdeflate
    URL https://codeload.github.com/ebiggers/libdeflate/tar.gz/refs/tags/v${LIBDEFLATE_VERSION}
    URL_HASH SHA256=${LIBDEFLATE_SHA256})
set(LIBDEFLATE_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(LIBDEFLATE_BUILD_GZIP OFF CACHE BOOL "" FORCE)
set(LIBDEFLATE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LIBDEFLATE_ZLIB_SUPPORT OFF CACHE BOOL "" FORCE)
# Compilar mbedtls como lib estática, sin sus tests/programs/examples.
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
# Sin autotests de mbedTLS (ver el archivo: evita una carrera en ecp.c).
set(MBEDTLS_USER_CONFIG_FILE "${CMAKE_SOURCE_DIR}/cmake/mbedtls_user_config.h" CACHE FILEPATH "" FORCE)
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
# no dispara esa advertencia. Apple clang 21 (Xcode 26) la dispara igual.
if(WIN32 OR APPLE)
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
if(EXISTS "${_BA_EXTERN}/libdeflate/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_LIBDEFLATE "${_BA_EXTERN}/libdeflate")
    message(STATUS "libdeflate: usando vendored en extern/ (offline)")
else()
    message(STATUS "libdeflate: sin extern/libdeflate: se baja el tarball")
endif()
if(EXISTS "${_BA_EXTERN}/argon2/Makefile")
    set(FETCHCONTENT_SOURCE_DIR_ARGON2 "${_BA_EXTERN}/argon2")
    message(STATUS "argon2: usando vendored en extern/ (offline)")
else()
    message(STATUS "argon2: sin extern/argon2: usando git")
endif()
# Las dependencias son ESTATICAS e internas: sus headers/libs no van en el
# bundle portable. Algunas (mbedtls) instalan include/ y lib/ aunque se les
# pida que no, asi que sus reglas de install se mandan a un componente propio
# ("thirdparty") y `make release` instala solo el del producto ("bundle").
set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME thirdparty)
FetchContent_MakeAvailable(mbedtls nlohmann_json libsamplerate argon2 libdeflate)
set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME bundle)

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

# --- Config por defecto del build -------------------------------------------
# Si no se pide nada, vamos a Release (el artefacto portable es Release).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# --- Flags del compilador ---------------------------------------------------
# MSVC: flags del spec §14. /MD por defecto para A (consistencia
# con Qt y GStreamer); el launcher se pasa a /MT en su propio CMakeLists.
# Los PDBs se emiten SIEMPRE (debug info) incluso en Release.
if(MSVC)
    add_compile_options(
        /W4            # nivel alto de advertencias
        /permissive-   # modo conforme estricto
        /Zc:__cplusplus # __cplusplus refleja el estándar real
        /utf-8         # fuente y ejecutable en UTF-8
        /EHsc          # excepciones activas, estilo estándar
    )
    # CRT: /MD por defecto (CMAKE_MSVC_RUNTIME_LIBRARY); el sub-build de
    # launcher+gateway la pasa a /MT (cmake/BaWinStaticPart.cmake).
    # /Zi garantiza PDBs en todos los configurados; PDBs de enlace siempre.
    add_compile_options(/Zi)
    add_link_options(/DEBUG)
    # /DEBUG apaga por defecto /OPT:REF y /OPT:ICF (sacar lo que nadie usa y
    # juntar funciones idénticas): se vuelven a prender fuera de Debug, así el
    # código queda igual que sin símbolos. Los .pdb van aparte (dist/simbolos).
    add_link_options($<$<NOT:$<CONFIG:Debug>>:/OPT:REF> $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>)
    # Windows 10+ (ConPTY, WinRT...) y sin min/max de windows.h. NOMINMAX y
    # WIN32_LEAN_AND_MEAN van VACIOS: identicos al `#define` que ya tienen
    # varios archivos (redefinirlos igual no es un aviso).
    add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX= WIN32_LEAN_AND_MEAN=
                            _CRT_SECURE_NO_WARNINGS)
else()
    # Linux (gcc/clang): advertencias estrictas razonables.
    add_compile_options(-Wall -Wextra)
    # Símbolos siempre (también en Release): -g no cambia el código que se
    # genera, solo agrega la información para leer una pila. En macOS queda en
    # los .o y dsymutil la junta en un .dSYM; en Linux va aparte (dist/simbolos).
    add_compile_options(-g)
    # macOS: el equivalente del --as-needed que Ubuntu usa por defecto. Sin
    # esto, todo lo que enlaza ba_core arrastra las dylibs de sus dependencias
    # opcionales (ONNX Runtime) aunque no las use, y el launcher, que no tiene
    # rpath hacia bin/64bit, ni arranca.
    if(APPLE)
        add_link_options(LINKER:-dead_strip_dylibs)
    endif()
endif()

# --- Testing ----------------------------------------------------------------
# enable_testing() + include(CTest) habilitan `ctest` y el target `test`.
enable_testing()
include(CTest)
# Lo que el programa tiene que traer: si falta algo, la configuración frena (ver el archivo).
include(${CMAKE_SOURCE_DIR}/cmake/BaRequisitos.cmake)
# Lo que la caché de tests tiene que saber de cada uno (tools/ci/tests-con-cache.py).
include(${CMAKE_SOURCE_DIR}/cmake/BaTestsCache.cmake)

# --- Componentes ------------------------------------------------------------
# Cada subdirectorio declara su target. `ba_core` es la base; el resto enlaza
# contra ella. El launcher solo existe en Windows (Win32 puro); el resto se
# configura en ambas plataformas.
if(BA_WIN_STATIC_PART)
    add_subdirectory(src/core)
    add_subdirectory(src/launcher)
    add_subdirectory(src/gateway)
    ba_tests_registrar()
    ba_revisar_requisitos()
    return()
endif()

include(${CMAKE_SOURCE_DIR}/cmake/BaOnnxRuntime.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/BaGStreamer.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/BaPython.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/BaHerramientas.cmake)
# zxing-cpp para leer documentos (el DNI) de la cámara: antes de src/core, que arma `ba_codigos`.
include(${CMAKE_SOURCE_DIR}/cmake/BaZxing.cmake)

# macOS 26 (Xcode 26) ya no trae AGL.framework, pero el FindWrapOpenGL de Qt
# 6.6 lo enlaza igual ("framework 'AGL' not found"). Qt lo busca en la cache
# WrapOpenGL_AGL: si el SDK no lo tiene, se le da el de OpenGL (ya enlazado).
# Se mira el SDK y no el sistema: CMake 4 ya no fija CMAKE_OSX_SYSROOT, y un
# find_library encuentra el AGL de /System/Library aunque el SDK no lo tenga.
if(APPLE)
    set(_ba_sdk "${CMAKE_OSX_SYSROOT}")
    if(NOT _ba_sdk)
        execute_process(COMMAND xcrun --sdk macosx --show-sdk-path
                        OUTPUT_VARIABLE _ba_sdk OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    endif()
    if(NOT EXISTS "${_ba_sdk}/System/Library/Frameworks/AGL.framework")
        set(WrapOpenGL_AGL "-framework OpenGL" CACHE STRING "AGL ya no existe en el SDK" FORCE)
    endif()
endif()

add_subdirectory(src/core)
if(MSVC)
    # launcher + gateway: sub-build /MT (ver cmake/BaWinStaticPart.cmake).
    include(${CMAKE_SOURCE_DIR}/cmake/BaWinStaticPart.cmake)
else()
    add_subdirectory(src/launcher)
    add_subdirectory(src/gateway)
endif()
# ba-web: el servidor web público (C, estilo nginx), adelante del túnel. Lo lanza el gateway.
add_subdirectory(src/web)
# ba-pinggy (túnel de pinggy.io, lo supervisa el gateway) si se bajó libpinggy
# con `make deps-pinggy`. Fuera del sub-build /MT de Windows: no enlaza ba_core.
if(EXISTS "${CMAKE_SOURCE_DIR}/extern/pinggy")
    add_subdirectory(src/pinggy)
else()
    ba_falta("Túnel de pinggy (ba-pinggy)" "falta extern/pinggy ('make deps-pinggy')")
endif()
# El motor de emisión es GStreamer (src/motor) y sus plugins propios: la app
# los necesita (sin GStreamer, ba-app se compila sin motor: solo herramientas).
set(BA_HAVE_MOTOR ${BA_HAVE_GST})
if(BA_HAVE_GST)
    add_subdirectory(src/gst-plugins)
    add_subdirectory(src/motor)
endif()
add_subdirectory(src/app)
add_subdirectory(src/grafico)
# El modelo local fuera de la Mac (llama.cpp): ver src/modelo-llama.
add_subdirectory(src/modelo-llama)

# --- Reglas de instalación (layout 5.1) -------------------------------------
# Layout portable (spec §5.1). Las rutas de destino son relativas a
# CMAKE_INSTALL_PREFIX. Aquí se crean los directorios del layout y se instalan
# los targets propios y los runtime de terceros (GStreamer, Qt, CRT, cloudflared,
# cada uno desde su módulo de cmake/).
#
#   <prefix>/
#   ├── bin/64bit/            # ejecutables y bibliotecas (GStreamer, Qt)
#   ├── gst-plugins/          # los plugins de GStreamer propios
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
    DESTINATION Config
)
install(DIRECTORY
    DESTINATION Logs
)
install(DIRECTORY
    DESTINATION Recordings
)
install(DIRECTORY
    DESTINATION Media
)
install(DIRECTORY
    DESTINATION Web/guest
)
# Lo primero que ve quien descomprime el bundle: instrucciones (LEEME.txt en
# español, README.txt en inglés) y licencia.
install(FILES packaging/bundle/LEEME.txt packaging/bundle/README.txt LICENSE DESTINATION . COMPONENT bundle)

# El ícono viaja con el bundle: en Windows ya esta dentro del .exe, pero en
# Linux lo necesita el acceso directo, que se arma en la maquina donde se
# enchufa el disco (la ruta cambia en cada una, asi que no puede venir hecho).
install(FILES
    assets/icons/broadcast-appliance.png
    assets/icons/broadcast-appliance.svg
    assets/icons/broadcast-appliance.ico
    DESTINATION Recursos COMPONENT bundle
)
# macOS: el .icns (para la .app del .dmg y para el Finder).
if(APPLE)
    install(FILES assets/icons/broadcast-appliance.icns DESTINATION Recursos COMPONENT bundle)
endif()
if(NOT WIN32 AND NOT APPLE)
    install(PROGRAMS packaging/bundle/crear-acceso-directo.sh
        DESTINATION . COMPONENT bundle
    )
endif()

# Runtime de C++ del compilador (Linux): bin/64bit/cxx/. El launcher lo usa
# para A y B solo si es mas nuevo que el del sistema (ver ChildProcess.cpp).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    foreach(_rt libstdc++.so.6 libgcc_s.so.1)
        execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=${_rt}
                        OUTPUT_VARIABLE _rt_path OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(EXISTS "${_rt_path}")
            get_filename_component(_rt_real "${_rt_path}" REALPATH)
            install(FILES "${_rt_real}" DESTINATION bin/64bit/cxx RENAME ${_rt})
        endif()
    endforeach()
endif()

# cloudflared (tunel del gateway, spec §8.8) junto a los binarios si se bajo
# con `make deps-cloudflared` (Windows) / `make deps-cloudflared-host`.
if(WIN32)
    set(BA_CLOUDFLARED_BIN "${CMAKE_SOURCE_DIR}/extern/cloudflared/cloudflared.exe")
else()
    set(BA_CLOUDFLARED_BIN "${CMAKE_SOURCE_DIR}/extern/cloudflared/cloudflared")
endif()
if(EXISTS "${BA_CLOUDFLARED_BIN}")
    install(PROGRAMS "${BA_CLOUDFLARED_BIN}" DESTINATION bin/64bit)
    message(STATUS "ba-bundle: cloudflared incluido (${BA_CLOUDFLARED_BIN})")
else()
    ba_falta("Túnel de Cloudflare (cloudflared)" "falta el binario ('make deps-cloudflared-host')")
endif()

# chrome-headless-shell (Chrome for Testing, sin ventana): las fotos de las páginas web
# del aparato (web_captura: los manuales, y el asistente para ver lo que edita). Lo baja
# `make deps-chrome-headless` (tools/deps/chrome-headless.sh). En Linux trae en lib/ las
# bibliotecas que no están en la base (NSS, ATK…), que encuentra por su RUNPATH.
if(APPLE)
    set(BA_CHROME_HEADLESS_DIR "${CMAKE_SOURCE_DIR}/extern/chrome-headless/mac-arm64")
elseif(WIN32)
    set(BA_CHROME_HEADLESS_DIR "${CMAKE_SOURCE_DIR}/extern/chrome-headless/win64")
else()
    set(BA_CHROME_HEADLESS_DIR "${CMAKE_SOURCE_DIR}/extern/chrome-headless/linux64")
endif()
if(EXISTS "${BA_CHROME_HEADLESS_DIR}/.version")
    install(DIRECTORY "${BA_CHROME_HEADLESS_DIR}/" DESTINATION bin/64bit/chrome-headless USE_SOURCE_PERMISSIONS
            PATTERN ".version" EXCLUDE)
    message(STATUS "ba-bundle: chrome-headless-shell incluido (${BA_CHROME_HEADLESS_DIR})")
else()
    ba_falta("Fotos de las páginas web (chrome-headless-shell)" "falta ('make deps-chrome-headless')")
endif()

# ba-modelo (el modelo local en la Mac: MLX, ver src/modelo-mac) si se compilo
# con `make modelo-mac`. Va con su mlx-swift_Cmlx.bundle al lado: ahi esta el
# default.metallib (los kernels de Metal) y MLX lo busca junto al ejecutable.
if(APPLE)
    set(BA_MODELO_MAC_DIR "${CMAKE_SOURCE_DIR}/build/modelo-mac/Build/Products/Release")
    if(EXISTS "${BA_MODELO_MAC_DIR}/ba-modelo")
        install(PROGRAMS "${BA_MODELO_MAC_DIR}/ba-modelo" DESTINATION bin/64bit COMPONENT bundle)
        install(DIRECTORY "${BA_MODELO_MAC_DIR}/mlx-swift_Cmlx.bundle" DESTINATION bin/64bit COMPONENT bundle)
        # Swift 6.2 en macOS < 26: la compatibilidad de Span no viene con el
        # sistema y ba-modelo la busca junto a el (@executable_path).
        execute_process(COMMAND xcrun --find swiftc OUTPUT_VARIABLE _ba_swiftc OUTPUT_STRIP_TRAILING_WHITESPACE)
        get_filename_component(_ba_tc "${_ba_swiftc}" DIRECTORY)
        file(GLOB _ba_span "${_ba_tc}/../lib/swift-*/macosx/libswiftCompatibilitySpan.dylib")
        if(_ba_span)
            list(GET _ba_span 0 _ba_span)
            install(FILES "${_ba_span}" DESTINATION bin/64bit COMPONENT bundle)
        else()
            message(WARNING "ba-bundle: no encontre libswiftCompatibilitySpan.dylib en el toolchain")
        endif()
        message(STATUS "ba-bundle: ba-modelo incluido (modelo local con MLX)")
    else()
        ba_falta("Modelo local con MLX (ba-modelo)" "no está compilado ('make modelo-mac')")
    endif()
endif()

# El paso de rpath va DESPUES de copiar el runtime: reescribe lo ya instalado.
if(NOT WIN32 AND NOT APPLE)
    include(${CMAKE_SOURCE_DIR}/cmake/BaPortableBundle.cmake)
endif()

# --- Target de instalación de los ejecutables/DLLs propios ------------------
# Cada componente declara su propio install(TARGETS ...) en su CMakeLists para
# controlar el directorio de destino exacto. Aquí se centraliza solo la
# documentación de a dónde va cada cosa.

# macOS: re-firma ad-hoc de todo lo instalado. Va AL FINAL: las reglas de
# instalacion corren en el orden declarado (CMP0082) y esta tiene que ver
# todos los binarios ya en su lugar.
if(APPLE)
    include(${CMAKE_SOURCE_DIR}/cmake/BaMacBundle.cmake)
endif()

# Con todos los tests ya declarados: etiquetas y entradas para la caché de tests.
ba_tests_registrar()

# Al final: si a algo de lo de arriba le faltó una parte, acá frena (cmake/BaRequisitos.cmake).
ba_revisar_requisitos()
