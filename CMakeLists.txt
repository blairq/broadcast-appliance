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
# Las bibliotecas estáticas repetidas en la línea de enlace (ba_core, mbedtls… que piden
# varias bibliotecas de la app): con CMP0156 en NEW, CMake las repite solo si el enlazador
# lo necesita (GNU ld sí; ld64 de Apple y el de MSVC/lld-link no), y en la Mac deja de avisar
# «ignoring duplicate libraries». Lo que se enlaza es lo mismo. Con un CMake anterior a 3.29
# (sin la política) queda como antes.
if(POLICY CMP0156)
    cmake_policy(SET CMP0156 NEW)
endif()

# --- Proyecto ---------------------------------------------------------------
project(
    BroadcastAppliance
    VERSION 2.1.0
    DESCRIPTION "Broadcast Appliance Portable v2 (motor GStreamer + Gateway WebRTC + Supervisor)"
    LANGUAGES C CXX
)

# compile_commands.json siempre (lo que se compila no cambia: solo se escribe el json).
# Lo usan la vigilancia del diseño físico (test_headers_solos_*, cmake/BaNiveles.cmake:
# sin el json no se registra y nadie se entera), tools/dev/chequear.py y
# `make lint-includes`. Va antes de crear cualquier target (la propiedad
# EXPORT_COMPILE_COMMANDS se toma al crearlo). Quien lo apague a propósito
# (-DCMAKE_EXPORT_COMPILE_COMMANDS=OFF o la variable de entorno) manda. Ojo: con Ninja y
# Makefiles, CMake ya dejó la variable en la caché VACÍA al llegar acá (DEFINED da
# verdadero): «nadie la fijó» es «vacía».
if("${CMAKE_EXPORT_COMPILE_COMMANDS}" STREQUAL "" AND "$ENV{CMAKE_EXPORT_COMPILE_COMMANDS}" STREQUAL "")
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Escribir compile_commands.json (ver el CMakeLists raíz)" FORCE)
endif()

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

# El detector de acoples (src/core/diag/Acoples.hpp, docs/arquitectura/detector-de-acoples.md):
# el «modo sabotaje» de la interfaz para las pruebas. Prendido en los builds de desarrollo y
# de pruebas; los paquetes que se PUBLICAN salen sin él (`make paquetes`/`publicar` pasan
# DIAG_ACOPLES=0): con la opción apagada no se define BA_DIAG_ACOPLES, BA_ACOPLE queda en
# ((void)0), no hay herramienta MCP diag_acoples y no queda ningún símbolo del detector.
option(BA_DIAG_ACOPLES "El detector de acoples de la interfaz (desarrollo y pruebas; no en lo que se publica)" ON)
# Las SONDAS del camino de los cuadros (src/core/diag/SondasCuadros.hpp, docs/local/camino-del-cuadro.md):
# el ritmo de cada video en cada tramo (red, decodificador, grilla, publicación, interfaz), con cada
# cuadro. Para encontrar dónde un video pierde la fluidez; nunca en lo que se usa en un evento.
# `make app SONDAS=1` (mejor en otro BUILD_DIR: cambiarla recompila todo). Apagada, BA_SONDA(…) no
# genera nada; queda solo lo del final del camino (ui/Fluidez: lo que se dibuja).
option(BA_SONDAS_CUADROS "Sondas del ritmo de los cuadros en cada tramo (build de diagnóstico)" OFF)
if(BA_SONDAS_CUADROS)
    message(STATUS "BA_SONDAS_CUADROS: build con las sondas del camino de los cuadros")
endif()
# BA_DEPURACION: la información de depuración (-g; /Zi y /DEBUG en Windows). Prendida: los
# volcados se leen con ella (dist/simbolos, tools/caidas). Apagada solo en las validaciones en
# Docker que no guardan símbolos (DEPURACION=0, tools/ci/receta-docker.sh sin SIMBOLOS=1): con
# ella los builds de Linux y Windows ocupaban 31 y 17 GB del disco de colima. No cambia el código.
option(BA_DEPURACION "Información de depuración (-g, /Zi): los símbolos para leer los volcados" ON)
if(NOT BA_DIAG_ACOPLES)
    message(STATUS "BA_DIAG_ACOPLES: sin el detector de acoples (paquete para publicar)")
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
#
# Lo que el build necesita de ccache va acá y no en la configuración de cada uno
# (`ccache clave=valor… compilador`, desde ccache 4.8): sloppiness
#   - pch_defines,time_macros: sin esto ccache no cachea nada que use un header
#     precompilado (cmake/BaPch.cmake; «Could not use precompiled header» en
#     `ccache -s -v`). Nadie usa __DATE__/__TIME__.
#   - include_file_mtime,include_file_ctime: un header recién escrito (los que
#     genera la configuración, el moc) no le apaga el modo directo a quien lo
#     incluye (ccache vuelve a preprocesar todo en cada acierto que no es directo).
# Se suma a la sloppiness que ya tenga la configuración de ccache (en la línea de
# comandos, una clave reemplaza a la del archivo).
find_program(CCACHE_LAUNCHER ccache)
if(CCACHE_LAUNCHER AND NOT CMAKE_C_COMPILER_LAUNCHER AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    set(_ba_ccache "${CCACHE_LAUNCHER}")
    execute_process(COMMAND "${CCACHE_LAUNCHER}" --version OUTPUT_VARIABLE _ba_ccache_v ERROR_QUIET)
    if(_ba_ccache_v MATCHES "ccache version ([0-9]+\\.[0-9]+)" AND NOT CMAKE_MATCH_1 VERSION_LESS 4.8)
        execute_process(COMMAND "${CCACHE_LAUNCHER}" --get-config sloppiness
                        OUTPUT_VARIABLE _ba_sl OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        string(REPLACE "," ";" _ba_sl "${_ba_sl}")
        string(REPLACE " " "" _ba_sl "${_ba_sl}")
        list(APPEND _ba_sl pch_defines time_macros include_file_mtime include_file_ctime)
        list(REMOVE_ITEM _ba_sl "")
        list(REMOVE_DUPLICATES _ba_sl)
        list(SORT _ba_sl)
        list(JOIN _ba_sl "," _ba_sl)
        list(APPEND _ba_ccache "sloppiness=${_ba_sl}")
    else()
        message(STATUS "ccache anterior a 4.8: sin sloppiness por línea de comandos (los headers precompilados no se cachean)")
    endif()
    set(CMAKE_C_COMPILER_LAUNCHER    "${_ba_ccache}")
    set(CMAKE_CXX_COMPILER_LAUNCHER  "${_ba_ccache}")
    message(STATUS "ccache activo: ${_ba_ccache} (launcher de C/CXX)")
else()
    message(STATUS "ccache no activo (no encontrado o ya hay un launcher fijado)")
endif()

# --- macOS: enlaces reproducibles (ZERO_AR_DATE=1) ----------------------------
# El ld de Apple anota en cada ejecutable la fecha de cada .o (las entradas N_OSO
# de la información de depuración): volver a enlazar con los MISMOS objetos daba
# otro binario. Entonces la caché de tests (tools/ci/tests-con-cache.py, por el
# contenido del ejecutable) volvía a correr todo lo que se reenlazaba aunque nada
# hubiera cambiado: tocar un .cpp del gateway sin cambiarlo eran 53 tests y 100 s.
# Con ZERO_AR_DATE=1 (lo mismo que usa Bazel) esas fechas van en cero: el código
# es el mismo, dsymutil y lldb siguen encontrando los .o (con fecha cero no la comparan).
if(APPLE AND NOT CMAKE_CXX_LINKER_LAUNCHER AND NOT CMAKE_C_LINKER_LAUNCHER)
    foreach(_lang C CXX OBJC OBJCXX)
        set(CMAKE_${_lang}_LINKER_LAUNCHER env ZERO_AR_DATE=1)
    endforeach()
endif()

# --- Versiones fijadas ------------------------------------------------------
# Un único archivo concentra las versiones de librerías de terceros para que
# el build sea reproducible y auditable (spec §14 "Versiones fijadas").
include("${CMAKE_CURRENT_SOURCE_DIR}/versions.cmake")

# La carpeta de extern/ de lo que se baja por arquitectura en Linux (lsp, hinchada…):
# linux-x86_64 (el paquete que se publica) o linux-aarch64 (la validación nativa en Docker
# sobre Apple Silicon).
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(BA_LINUX_PLAT linux-aarch64)
else()
    set(BA_LINUX_PLAT linux-x86_64)
endif()

# --- Dependencias de terceros -----------------------------------------------
# mbedtls, libsamplerate, argon2, libdeflate y zxing-cpp: ya compiladas, de
# extern/estaticas (make deps-estaticas; ver cmake/BaEstaticas.cmake). Antes de
# `add_subdirectory(src/core)`: ba_core enlaza mbedcrypto/mbedx509/mbedtls, samplerate
# y argon2.
# nlohmann/json (solo headers) sigue por FetchContent: no compila nada.
include(FetchContent)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG ${NLOHMANN_JSON_VERSION})
# Que lo de terceros no registre sus tests con CTest: ctest muestra solo los nuestros.
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

# --- Offline-first: preferir las fuentes vendored en extern/ -----------------
# Si `make deps-src` (o `make deps`) ya pobló extern/nlohmann-json, el configure NO
# toca la red: es el requisito de producto portable/offline. Si no (clon fresco sin
# `make deps`), queda el FetchContent_Declare (GIT_REPOSITORY) de arriba.
set(_BA_EXTERN "${CMAKE_CURRENT_SOURCE_DIR}/extern")
if(EXISTS "${_BA_EXTERN}/nlohmann-json/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON "${_BA_EXTERN}/nlohmann-json")
    message(STATUS "nlohmann_json: usando vendored en extern/ (offline)")
else()
    message(STATUS "nlohmann_json: sin extern/nlohmann-json: usando git")
endif()
# Las dependencias son internas: sus headers no van en el bundle portable. Sus
# reglas de install se mandan a un componente propio ("thirdparty") y `make
# release` instala solo el del producto ("bundle").
set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME thirdparty)
FetchContent_MakeAvailable(nlohmann_json)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/BaEstaticas.cmake")
set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME bundle)

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
    # /Zi garantiza PDBs en todos los configurados; PDBs de enlace siempre (salvo
    # BA_DEPURACION=OFF: las validaciones en Docker, que no guardan símbolos).
    if(BA_DEPURACION)
        add_compile_options(/Zi)
        add_link_options(/DEBUG)
    endif()
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
    # BA_DEPURACION=OFF solo en las validaciones en Docker (ver la opción).
    if(BA_DEPURACION)
        add_compile_options(-g)
    endif()
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
# Headers precompilados con lo de afuera (std, json, Qt, GStreamer): ver el archivo.
include(${CMAKE_SOURCE_DIR}/cmake/BaPch.cmake)

# --- Componentes ------------------------------------------------------------
# Cada subdirectorio declara su target. `ba_core` es la base; el resto enlaza
# contra ella. El launcher solo existe en Windows (Win32 puro); el resto se
# configura en ambas plataformas.
if(BA_WIN_STATIC_PART)
    add_subdirectory(src/core)
    add_subdirectory(src/launcher)
    add_subdirectory(src/gateway)
    ba_pch_aplicar()
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
    # El emisor aparte (F3-01): codifica, graba y transmite una salida fuera de A.
    add_subdirectory(src/emisor)
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

# hinchada: el probador de carga de la prueba de sala (docs/funciones/PRUEBA-SALA.md).
# `make deps-hinchada` lo deja en extern/hinchada/<plataforma> (o -DBA_HINCHADA_DIR=).
if(APPLE)
    set(_ba_hinchada_plat macos-arm64)
    set(_ba_hinchada_exe hinchada)
elseif(WIN32)
    set(_ba_hinchada_plat windows-x86_64)
    set(_ba_hinchada_exe hinchada.exe)
else()
    set(_ba_hinchada_plat ${BA_LINUX_PLAT})
    set(_ba_hinchada_exe hinchada)
endif()
set(BA_HINCHADA_DIR "${CMAKE_SOURCE_DIR}/extern/hinchada/${_ba_hinchada_plat}" CACHE PATH "El paquete de hinchada (prueba de sala)")
if(EXISTS "${BA_HINCHADA_DIR}/${_ba_hinchada_exe}")
    install(PROGRAMS "${BA_HINCHADA_DIR}/${_ba_hinchada_exe}" DESTINATION bin/64bit/hinchada COMPONENT bundle)
    install(FILES "${BA_HINCHADA_DIR}/cacert.pem" "${BA_HINCHADA_DIR}/README.md" DESTINATION bin/64bit/hinchada COMPONENT bundle OPTIONAL)
    message(STATUS "ba-bundle: hinchada incluido (${BA_HINCHADA_DIR})")
else()
    ba_falta("Prueba de sala con el probador propio (hinchada)" "falta el paquete ('make deps-hinchada')")
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

# Los niveles del diseño físico y los headers solos, un test por componente
# (tools/diag/niveles.txt).
include(${CMAKE_SOURCE_DIR}/cmake/BaNiveles.cmake)

# Chequeos rápidos del código que no son de C++ (segundos), cada uno con lo que lee: los
# catálogos de idiomas, un escritor a mano sobre Config/Runtime (F1-01), recorrer .items() de un
# JSON temporal (UB), el comparador de las corridas nocturnas del banco de caos (F4-03)... Los
# que recorren src/, de a un área (cmake/BaTestsPy.cmake, tests/areas.py).
include(${CMAKE_SOURCE_DIR}/cmake/BaTestsPy.cmake)

# Con todos los objetivos ya declarados: a cada uno, el header precompilado que le toca.
ba_pch_aplicar()

# Con todos los tests ya declarados: etiquetas y entradas para la caché de tests.
ba_tests_registrar()

# Al final: si a algo de lo de arriba le faltó una parte, acá frena (cmake/BaRequisitos.cmake).
ba_revisar_requisitos()
