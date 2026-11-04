# =====================================================================# src/app — `ba-app` (Proceso A: motor de emisión + UI de operación)
#
# El Proceso A es un ejecutable GUI (sin consola en Windows) que integra
# `ba_core` (dsp + ipc + persist) con la UI de operación (spec §6).
#
# Arquitectura:
#   - `ba_core` es el núcleo: SIN Qt, testeable por unidad.
#   - El motor de emisión es GStreamer (src/motor): libobs ya no está.
#   - `ba-app` (A) usa /MD (CRT dinámica) por consistencia con Qt y GStreamer.
#
# Dos rutas de compilación, elegidas en el configure:
#   1. Con Qt 6 vendoreado -> app COMPLETA (interfaz QML + motor). Se define
#      `BA_APP_HAVE_GUI=1`.
#   2. Sin Qt (una validación cruzada) -> modo consola (runOperate); sin
#      GStreamer, el target compila igual pero no hay motor (main.cpp lo dice).
#
# /MD (CRT dinámica) — hereda del root.
# ============================================================================

include(${CMAKE_SOURCE_DIR}/cmake/BaBiblioteca.cmake)

# --- Sources ----------------------------------------------------------------
# CONFIGURE_DEPENDS detecta los archivos nuevos sin reconfigurar a mano.
file(GLOB_RECURSE BA_APP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/tests/.*")
list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/mcp/bridge/.*") # ba-mcp es otro binario
list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/src/app/escritorio/.*") # ba-control es otro binario
list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/src/app/tools/.*") # herramientas con su propio main

# WIN32 en MSVC: subsistema GUI (sin consola). En Linux es un ejecutable normal.
# El motor es GStreamer (src/motor): sin él, el Proceso A compila sin motor.
list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/BroadcastEnginePlaca\\.cpp$")
list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/WindowingPlatform\\.cpp$")
if(NOT BA_HAVE_MOTOR)
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/emision/(BroadcastEngine|EscenasYFuentes|CapasPrograma|MezclaApp|MediosApp|VistasYMedicion|RetornoGateway|EmisionSalidas|VigiaDisco|EmisorAparte)\\.cpp$")
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/AppSettings\\.cpp$")
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/EngineSelfTest\\.cpp$")
    # AppRuntime y sus piezas (y el modo consola) necesitan el motor.
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/arranque/(AppRuntime|ModoConsola|PublicadorFoto|FotoMezcla|RepositorioEscenas|ServicioSalidas|ServicioEmision|AireDurable|Invitados|SaludProceso)\\.cpp$")
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/GuiMode\\.cpp$")
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/OperatorFeed\\.cpp$")
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/ChatOverlaySource\\.cpp$")
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/audio/MonitorVoz\\.cpp$")
endif()

# --- Qt 6 (interfaz grafica) — VENDOREADO, nunca el del sistema -------------
# El bundle es portable y se lleva su propio Qt (como GStreamer). Se busca
# SOLO en extern/ (NO_DEFAULT_PATH):
#   - Linux/macOS: extern/qt6-host/<QT6_VERSION>/<arch>  (`make deps-qt-host`)
#   - Windows:     extern/qt6/<QT6_VERSION>/<arch>       (`make deps-qt`)
# Sin Qt vendoreado, ba-app se compila sin interfaz (modo consola).
set(BA_APP_QT_OK FALSE)
if(BA_HAVE_MOTOR)
    if(WIN32)
        set(_ba_qt_root "${CMAKE_SOURCE_DIR}/extern/qt6/${QT6_VERSION}")
    else()
        set(_ba_qt_root "${CMAKE_SOURCE_DIR}/extern/qt6-host/${QT6_VERSION}")
    endif()
    file(GLOB _ba_qt_cfg "${_ba_qt_root}/*/lib/cmake/Qt6/Qt6Config.cmake")
    if(_ba_qt_cfg)
        list(GET _ba_qt_cfg 0 _ba_qt_cfg)
        get_filename_component(_ba_qt_dir "${_ba_qt_cfg}" DIRECTORY)            # .../lib/cmake/Qt6
        get_filename_component(BA_QT_PREFIX "${_ba_qt_dir}/../../.." ABSOLUTE)  # .../<arch>
        # Los subpaquetes de Qt (Qt6CoreTools, Qt6WidgetsTools...) se buscan con
        # la ruta general: el Qt vendoreado va PRIMERO para que ninguno se
        # resuelva contra un Qt del sistema.
        list(PREPEND CMAKE_PREFIX_PATH "${BA_QT_PREFIX}")
        find_package(Qt6 COMPONENTS Gui Quick Qml QuickControls2 Network Svg Pdf Multimedia CONFIG PATHS "${BA_QT_PREFIX}" NO_DEFAULT_PATH QUIET)
        set(BA_APP_QT_OK ${Qt6_FOUND})
    endif()
endif()
if(NOT BA_APP_QT_OK)
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/(GuiMode)\\.cpp$")
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/ui/.*")
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/(term|agent)/.*")
    # MCP, plataformas y cuentas son Qt (red, JSON, timers): sin Qt no van.
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/(mcp|youtube|twitch|facebook|net)/.*")
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/AccountGuides\\.cpp$")
    # Graficos, medios y la conversion del carrusel tambien son Qt.
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/(graficos|medios)/.*")
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/carrusel/Conversion\\.cpp$")
endif()
# Pseudo-terminal: una implementacion por plataforma.
if(WIN32)
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/term/Pty_posix\\.cpp$")
else()
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/term/Pty_win\\.cpp$")
endif()

# --- NDI(R) sin SDK propietario: libNoIDea (VideoLAN, LGPL) + libmicrodns -----
# Fuente fijada por commit (tools/deps/terceros-linux.sh). Se compila adentro de
# ba-app; el video SpeedHQ lo decodifica el FFmpeg del bundle.
set(BA_NOIDEA_DIR "${CMAKE_SOURCE_DIR}/extern/libnoidea")
set(BA_TERCEROS "${CMAKE_SOURCE_DIR}/extern/terceros-linux")
set(BA_HAVE_NDI OFF)
if(BA_HAVE_MOTOR AND NOT WIN32 AND NOT APPLE AND EXISTS "${BA_NOIDEA_DIR}/src/recv.c" AND EXISTS "${BA_TERCEROS}/lib/libmicrodns.a")
    add_library(ba_noidea STATIC "${BA_NOIDEA_DIR}/src/recv.c" "${BA_NOIDEA_DIR}/src/scramble.c"
                                 "${BA_NOIDEA_DIR}/extensions/discovery.c")
    target_include_directories(ba_noidea PUBLIC "${BA_NOIDEA_DIR}/include" "${BA_TERCEROS}/include")
    target_compile_definitions(ba_noidea PUBLIC NOIDEA_EXT_DISCOVERY NOIDEA_STATIC PRIVATE HAVE_POLL=1 _GNU_SOURCE)
    set_target_properties(ba_noidea PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
    target_compile_options(ba_noidea PRIVATE -w)
    find_package(Threads REQUIRED)
    target_link_libraries(ba_noidea PUBLIC "${BA_TERCEROS}/lib/libmicrodns.a" Threads::Threads)
    set(BA_HAVE_NDI ON)
    # Licencias (LGPL) junto al programa.
    install(FILES "${BA_NOIDEA_DIR}/COPYING" DESTINATION data/licencias RENAME libnoidea-LGPL.txt)
    if(EXISTS "${CMAKE_SOURCE_DIR}/extern/.build-terceros/libmicrodns/COPYING")
        install(FILES "${CMAKE_SOURCE_DIR}/extern/.build-terceros/libmicrodns/COPYING" DESTINATION data/licencias
                RENAME libmicrodns-LGPL.txt)
    endif()
endif()
# Windows: libmicrodns y libNoIDea se compilan aca (tools/deps/ndi-src.sh); el
# video lo decodifica GStreamer (bandisrc), sin FFmpeg propio.
set(BA_MDNS_DIR "${CMAKE_SOURCE_DIR}/extern/libmicrodns")

# --- mDNS para buscar Chromecast (src/app/pantallas) --------------------------
# UNA libmicrodns (ba_mdns) para los tres sistemas: la usan la búsqueda de
# televisores y NDI. En Linux, el .a de extern/terceros-linux (los terceros
# compilados, el nombre es histórico) si está; si no (y en macOS), del fuente
# vendoreado; en Windows, del mismo fuente con su compat (inet, poll) y Winsock.
# Antes Windows la compilaba aparte solo para NDI y la búsqueda quedaba afuera.
set(BA_MDNS_OK OFF)
if(WIN32 AND EXISTS "${BA_MDNS_DIR}/src/mdns.c")
    add_library(ba_mdns STATIC "${BA_MDNS_DIR}/src/mdns.c" "${BA_MDNS_DIR}/src/rr.c"
                               "${BA_MDNS_DIR}/compat/compat.c" "${BA_MDNS_DIR}/compat/inet.c"
                               "${BA_MDNS_DIR}/compat/poll.c")
    target_include_directories(ba_mdns PUBLIC "${BA_MDNS_DIR}/include"
                               PRIVATE "${CMAKE_SOURCE_DIR}/cmake/ndi-win" "${BA_MDNS_DIR}" "${BA_MDNS_DIR}/compat")
    target_compile_definitions(ba_mdns PRIVATE HAVE_CONFIG_H _UNICODE=1 UNICODE=1 _CRT_NONSTDC_NO_DEPRECATE
                                               _CRT_SECURE_NO_DEPRECATE)
    target_compile_options(ba_mdns PRIVATE -w /FIstdlib.h) # compat/ usa malloc sin incluirlo
    target_link_libraries(ba_mdns PUBLIC ws2_32 iphlpapi)
    install(FILES "${BA_MDNS_DIR}/COPYING" DESTINATION data/licencias RENAME libmicrodns-LGPL.txt)
    set(BA_MDNS_OK ON)
elseif(NOT WIN32 AND EXISTS "${BA_TERCEROS}/lib/libmicrodns.a")
    add_library(ba_mdns INTERFACE)
    target_include_directories(ba_mdns INTERFACE "${BA_TERCEROS}/include")
    target_link_libraries(ba_mdns INTERFACE "${BA_TERCEROS}/lib/libmicrodns.a")
    set(BA_MDNS_OK ON)
elseif(NOT WIN32 AND EXISTS "${BA_MDNS_DIR}/src/mdns.c")
    # compat.c: os_strerror y compania (en Windows van ademas inet.c y poll.c).
    add_library(ba_mdns STATIC "${BA_MDNS_DIR}/src/mdns.c" "${BA_MDNS_DIR}/src/rr.c"
                               "${BA_MDNS_DIR}/compat/compat.c")
    # config.h: lo que el meson.build de libmicrodns detecta en POSIX.
    target_include_directories(ba_mdns PUBLIC "${BA_MDNS_DIR}/include"
                               PRIVATE "${CMAKE_SOURCE_DIR}/cmake/microdns-posix" "${BA_MDNS_DIR}" "${BA_MDNS_DIR}/compat")
    target_compile_definitions(ba_mdns PRIVATE HAVE_CONFIG_H)
    target_compile_options(ba_mdns PRIVATE -w)
    set_target_properties(ba_mdns PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
    set(BA_MDNS_OK ON)
endif()
# macOS: NDI con la libmicrodns de arriba (compilada del fuente) y libNoIDea.
# Ya no hace falta FFmpeg: el SpeedHQ lo decodifica avdec_speedhq de GStreamer
# (NdiSource.cpp: el elemento bandisrc).
if(APPLE AND BA_HAVE_MOTOR AND BA_MDNS_OK AND EXISTS "${BA_NOIDEA_DIR}/src/recv.c")
    add_library(ba_noidea STATIC "${BA_NOIDEA_DIR}/src/recv.c" "${BA_NOIDEA_DIR}/src/scramble.c"
                                 "${BA_NOIDEA_DIR}/extensions/discovery.c")
    target_include_directories(ba_noidea PUBLIC "${BA_NOIDEA_DIR}/include" PRIVATE "${BA_NOIDEA_DIR}/src")
    target_compile_definitions(ba_noidea PUBLIC NOIDEA_EXT_DISCOVERY NOIDEA_STATIC PRIVATE HAVE_POLL=1)
    set_target_properties(ba_noidea PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
    target_compile_options(ba_noidea PRIVATE -w)
    target_link_libraries(ba_noidea PUBLIC ba_mdns)
    set(BA_HAVE_NDI ON)
    install(FILES "${BA_NOIDEA_DIR}/COPYING" DESTINATION data/licencias RENAME libnoidea-LGPL.txt COMPONENT bundle)
endif()
# El enlace con ba-app va mas abajo, cuando el objetivo ya existe.
if(BA_HAVE_MOTOR AND WIN32 AND BA_MDNS_OK AND EXISTS "${BA_NOIDEA_DIR}/src/recv.c")
    # Sin poll() en Windows: el compat/poll.c de la propia libNoIDea (el de VLC).
    add_library(ba_noidea STATIC "${BA_NOIDEA_DIR}/src/recv.c" "${BA_NOIDEA_DIR}/src/scramble.c"
                                 "${BA_NOIDEA_DIR}/extensions/discovery.c" "${BA_NOIDEA_DIR}/compat/poll.c")
    # cmake/ndi-win: pthread.h minimo sobre Win32 y un unistd.h vacio, lo que
    # libNoIDea espera de MinGW.
    target_include_directories(ba_noidea PUBLIC "${BA_NOIDEA_DIR}/include" "${CMAKE_SOURCE_DIR}/cmake/ndi-win"
                               PRIVATE "${BA_NOIDEA_DIR}/src")
    target_compile_definitions(ba_noidea PUBLIC NOIDEA_EXT_DISCOVERY NOIDEA_STATIC)
    target_compile_options(ba_noidea PRIVATE -w)
    target_link_libraries(ba_noidea PUBLIC ba_mdns ws2_32)

    set(BA_HAVE_NDI ON)
    install(FILES "${BA_NOIDEA_DIR}/COPYING" DESTINATION data/licencias RENAME libnoidea-LGPL.txt)
endif()
if(NOT BA_HAVE_NDI)
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/NdiSource\\.cpp$")
    if(BA_HAVE_MOTOR)
        ba_falta("Entradas NDI" "falta libNoIDea o libmicrodns (tools/deps/ndi-src.sh)")
    endif()
endif()

# --- Plugins de audio CLAP (LSP) ---------------------------------------------
# Headers de CLAP (MIT, `make deps-clap`). Los plugins LSP se cargan en tiempo
# de ejecucion desde el bundle (plugins/lsp/). Sin los headers o sin el motor
# (BA_HAVE_MOTOR), el rack de audio no se compila.
set(BA_CLAP_INC "${CMAKE_SOURCE_DIR}/extern/clap/include")
set(BA_HAVE_CLAP OFF)
if(BA_HAVE_MOTOR AND EXISTS "${BA_CLAP_INC}/clap/clap.h")
    set(BA_HAVE_CLAP ON)
else()
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/audio/.*")
endif()
# Plugins LSP en el bundle: plugins/lsp/ (+ sus licencias).
if(BA_HAVE_CLAP)
    if(WIN32)
        set(_ba_lsp_dir "${CMAKE_SOURCE_DIR}/extern/lsp/windows-x86_64")
    elseif(APPLE)
        set(_ba_lsp_dir "${CMAKE_SOURCE_DIR}/extern/lsp/macos-arm64")
    else()
        set(_ba_lsp_dir "${CMAKE_SOURCE_DIR}/extern/lsp/linux-x86_64")
    endif()
    if(APPLE AND IS_DIRECTORY "${_ba_lsp_dir}/lsp-plugins.clap")
        # macOS, si el .clap viene como bundle (carpeta).
        install(DIRECTORY "${_ba_lsp_dir}/lsp-plugins.clap" DESTINATION plugins/lsp USE_SOURCE_PERMISSIONS)
        file(GLOB _ba_lsp_files "${_ba_lsp_dir}/*.dylib")
    elseif(APPLE)
        # macOS: el build de LSP deja el .clap como un Mach-O suelto (y, si hay,
        # los .dylib de su motor 3D).
        file(GLOB _ba_lsp_files "${_ba_lsp_dir}/*.clap" "${_ba_lsp_dir}/*.dylib")
    else()
        file(GLOB _ba_lsp_files "${_ba_lsp_dir}/*.clap" "${_ba_lsp_dir}/*.dll" "${_ba_lsp_dir}/*.so*")
    endif()
    if(APPLE AND IS_DIRECTORY "${_ba_lsp_dir}/lsp-plugins.clap" AND NOT _ba_lsp_files)
        file(GLOB _ba_lsp_lic "${CMAKE_SOURCE_DIR}/extern/lsp/licencias/*")
        install(FILES ${_ba_lsp_lic} DESTINATION data/licencias/lsp-plugins)
    endif()
    if(_ba_lsp_files)
        install(FILES ${_ba_lsp_files} DESTINATION plugins/lsp)
        file(GLOB _ba_lsp_lic "${CMAKE_SOURCE_DIR}/extern/lsp/licencias/*")
        install(FILES ${_ba_lsp_lic} DESTINATION data/licencias/lsp-plugins)
    endif()
endif()
# ba-lspdsp: el DSP de LSP con API en C (tools/deps/lspdsp.sh), junto a los
# ejecutables (bin/64bit). La app la carga en tiempo de ejecución (audio/LspDsp).
if(WIN32)
    set(BA_LSPDSP_DIR "${CMAKE_SOURCE_DIR}/extern/lsp/windows-x86_64/dsp")
    set(_ba_lspdsp_lib "${BA_LSPDSP_DIR}/ba-lspdsp.dll")
elseif(APPLE)
    set(BA_LSPDSP_DIR "${CMAKE_SOURCE_DIR}/extern/lsp/macos-arm64/dsp")
    set(_ba_lspdsp_lib "${BA_LSPDSP_DIR}/libba-lspdsp.dylib")
else()
    set(BA_LSPDSP_DIR "${CMAKE_SOURCE_DIR}/extern/lsp/linux-x86_64/dsp")
    set(_ba_lspdsp_lib "${BA_LSPDSP_DIR}/libba-lspdsp.so")
endif()
if(BA_HAVE_CLAP AND EXISTS "${_ba_lspdsp_lib}")
    install(FILES "${_ba_lspdsp_lib}" DESTINATION bin/64bit)
endif()
# LSP para los tests (en el bundle: plugins/lsp/).
if(WIN32)
    set(BA_LSP_CLAP "${CMAKE_SOURCE_DIR}/extern/lsp/windows-x86_64/lsp-plugins.clap")
elseif(APPLE)
    set(BA_LSP_CLAP "${CMAKE_SOURCE_DIR}/extern/lsp/macos-arm64/lsp-plugins.clap")
else()
    set(BA_LSP_CLAP "${CMAKE_SOURCE_DIR}/extern/lsp/linux-x86_64/lsp-plugins.clap")
endif()

# --- libvterm (terminal de la pestaña Asistente) — VENDOREADA -----------------
# `make deps-libvterm` (URL + SHA-256). C puro, estatica dentro de ba-app.
set(BA_VTERM_DIR "${CMAKE_SOURCE_DIR}/extern/libvterm")
if(BA_APP_QT_OK)
    if(NOT EXISTS "${BA_VTERM_DIR}/src/vterm.c")
        message(FATAL_ERROR "Falta libvterm (terminal de la pestaña Asistente): make deps-libvterm")
    endif()
    file(GLOB _ba_vterm_src "${BA_VTERM_DIR}/src/*.c")
    add_library(ba_vterm STATIC ${_ba_vterm_src})
    target_include_directories(ba_vterm PUBLIC "${BA_VTERM_DIR}/include" PRIVATE "${BA_VTERM_DIR}/src")
    set_target_properties(ba_vterm PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        target_compile_options(ba_vterm PRIVATE /w)
    else()
        target_compile_options(ba_vterm PRIVATE -w)
    endif()
endif()

# --- La voz (opcional): sherpa-onnx (`make deps-sherpa-onnx`) ---------------
# Va contra el ONNX Runtime de los rostros, asi que hace falta ese tambien. Sin
# los dos, la app compila igual y la voz no aparece (BA_HAVE_VOZ). Los modelos
# de voz no viajan: se bajan a pedido.
if(WIN32)
    set(BA_SHERPA_DIR "${CMAKE_SOURCE_DIR}/extern/sherpa-onnx-win") # make deps-sherpa-onnx-win
else()
    set(BA_SHERPA_DIR "${CMAKE_SOURCE_DIR}/extern/sherpa-onnx")
endif()
set(BA_HAVE_VOZ OFF)
# (y los racks de audio: Escucha, Habla y MonitorVoz viven en audio/, que sin
# CLAP no se compila)
# (extern/ puede tener el sherpa-onnx de OTRO sistema —el volumen de Docker es
# el mismo para Linux y Windows—: se mira que estén las bibliotecas de ESTE.)
if(MSVC)
    set(_ba_sherpa_lib "${BA_SHERPA_DIR}/lib/sherpa-onnx-core.lib")
else()
    set(_ba_sherpa_lib "${BA_SHERPA_DIR}/lib/libsherpa-onnx-core.a")
endif()
if(BA_HAVE_CLAP AND EXISTS "${BA_SHERPA_DIR}/.ok" AND EXISTS "${_ba_sherpa_lib}" AND BA_HAVE_ORT)
    set(BA_HAVE_VOZ ON)
    # Las bibliotecas de sherpa-onnx (las enlazan ba-app, ba_app_audio y los tests de la voz).
    # Orden de enlace: de las APIs hacia abajo (son estaticas).
    set(BA_SHERPA_LIBS)
    foreach(_l sherpa-onnx-cxx-api sherpa-onnx-c-api sherpa-onnx-core kaldi-decoder-core sherpa-onnx-kaldifst-core
               sherpa-onnx-fstfar sherpa-onnx-fst kaldi-native-fbank-core piper_phonemize espeak-ng ucd
               kissfft-float ssentencepiece_core)
        if(MSVC)
            list(APPEND BA_SHERPA_LIBS "${BA_SHERPA_DIR}/lib/${_l}.lib")
        else()
            list(APPEND BA_SHERPA_LIBS "${BA_SHERPA_DIR}/lib/lib${_l}.a")
        endif()
    endforeach()
else()
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/audio/(Escucha|EscuchaNemotron|Habla)\\.cpp$")
endif()

# --- Lo común a todo el código de la app (ba-app y sus bibliotecas) ------------
# Las raíces de los #include "…" (las de `raiz app/` en tools/diag/niveles.txt) y las
# macros que dicen con qué se armó este build. Una sola definición: ba-app y cada
# ba_biblioteca la enlazan, así una fuente de la app (y todo .hpp que lea) se compila con
# las mismas macros en el programa, en su biblioteca y en los tests que la enlazan.
# BA_HAVE_GST, BA_HAVE_ROSTROS y BA_HAVE_CODIGOS vienen con su biblioteca (ba::gstreamer,
# ba_core, ba_codigos): quien las mira depende de ella.
add_library(ba_app_comun INTERFACE)
target_include_directories(ba_app_comun INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/motor"
                                                  "${CMAKE_SOURCE_DIR}/src/emisor" "${CMAKE_SOURCE_DIR}/src/lspdsp")
if(BA_HAVE_MOTOR)
    target_compile_definitions(ba_app_comun INTERFACE BA_HAVE_MOTOR=1)
    if(BA_HAVE_NDI)
        target_compile_definitions(ba_app_comun INTERFACE BA_HAVE_NDI=1)
    endif()
else()
    target_compile_definitions(ba_app_comun INTERFACE BA_HAVE_MOTOR=0)
endif()
if(BA_APP_QT_OK)
    target_compile_definitions(ba_app_comun INTERFACE BA_APP_HAVE_GUI=1)
    if(BA_HAVE_CLAP)
        target_compile_definitions(ba_app_comun INTERFACE BA_HAVE_CLAP=1)
    endif()
else()
    target_compile_definitions(ba_app_comun INTERFACE BA_APP_HAVE_GUI=0)
endif()
if(BA_HAVE_VOZ)
    target_compile_definitions(ba_app_comun INTERFACE BA_HAVE_VOZ=1)
endif()
if(BA_MDNS_OK)
    target_compile_definitions(ba_app_comun INTERFACE BA_HAVE_MDNS=1)
endif()

# --- Las bibliotecas de la app (docs del diseño físico: cmake/BaBiblioteca.cmake) ---------
# Cada fuente se compila una vez, en su biblioteca; ba-app y los tests la enlazan. Sin Qt
# (la validación cruzada sin interfaz) no hay bibliotecas: las fuentes quedan en ba-app.
set(BA_APP_BIBLIOTECAS "")
if(BA_APP_QT_OK)
    # Lo básico que usan todas las áreas, sin depender de ninguna (nivel 1.5, debajo de las
    # áreas): lo de comun/, las listas genéricas de la vista (ListaConClave, ListaObjetos,
    # JsonListModel, FiltroCatalogo), el contrato de errores (CapturaError) y los nombres de la
    # app para leer y guardar Config/ (arranque/ArchivoConfig). Esos archivos siguen en ui/ y
    # arranque/; tools/diag/niveles.txt los pone en el componente app/base.
    ba_biblioteca(ba_app_base NIVEL 1.5
        DEPENDE ba_app_comun ba_core Qt6::Core
        FUENTES comun/Duracion.hpp comun/FotoEntreHilos.hpp comun/HoraMonotonica.hpp comun/JsonSignals.hpp
                comun/Restante.hpp comun/UsoCpu.hpp comun/UsoPorVistas.hpp
                ui/CapturaError.hpp ui/ListaConClave.hpp ui/ListaObjetos.hpp ui/ListaObjetos.cpp
                ui/JsonListModel.hpp ui/JsonListModel.cpp ui/FiltroCatalogo.hpp ui/FiltroCatalogo.cpp
                arranque/ArchivoConfig.hpp)
    list(APPEND BA_APP_BIBLIOTECAS ba_app_base)

    # El HTTPS propio y lo que lo usa sin depender de ningún área (nivel 1.5, como
    # ba_app_base): el cliente (mbedTLS), las descargas, las guías de las cuentas, la
    # captura de páginas web y la prueba de capacidad. net/AccesoRemoto (el túnel visto
    # desde la interfaz: usa evento, sala, emision, plugins y ui) sigue en ba-app.
    ba_biblioteca(ba_app_net NIVEL 1.5
        DEPENDE ba_app_comun ba_core mbedtls mbedx509 mbedcrypto Qt6::Core Qt6::Gui
        FUENTES net/HttpsClient.hpp net/HttpsClient.cpp net/AsyncHttp.hpp net/Credentials.hpp
                net/AccountGuides.hpp net/AccountGuides.cpp net/CapturaWeb.hpp net/CapturaWeb.cpp
                net/ClienteCapacidad.hpp net/ClienteCapacidad.cpp net/Descargas.hpp net/Descargas.cpp)
    ba_biblioteca_completa(ba_app_net net "/AccesoRemoto\\.cpp$")
    list(APPEND BA_APP_BIBLIOTECAS ba_app_net)

    # El evento: el guion, la línea de tiempo, la carpeta del evento y lo que se publica de él.
    # APdf.cpp usa carrusel/Conversion (todavía en ba-app): lo resuelve quien enlaza.
    # mbedcrypto: core/crypto/Sha256.hpp (FotosConfig, CierreEvento) incluye <mbedtls/sha256.h>.
    ba_biblioteca(ba_app_evento NIVEL 2
        DEPENDE ba_app_base ba_app_comun ba_core mbedcrypto Qt6::Core Qt6::Gui Qt6::Svg
        FUENTES evento/APdf.cpp evento/Bitacora.cpp evento/CierreEvento.cpp evento/Condicion.cpp evento/Corrida.cpp
                evento/Documentos.cpp evento/Evento.cpp evento/EventoStore.cpp evento/EventoWeb.cpp
                evento/FotosConfig.cpp evento/Historico.cpp evento/Linea.cpp evento/LineaEdicion.cpp
                evento/LineaIntercambio.cpp evento/LineaPlantillas.cpp evento/LineaProductor.cpp
                evento/LineaVista.cpp evento/Padron.cpp evento/TemaEvento.cpp evento/TemaLogoPng.cpp
                evento/TiposDeCue.cpp)
    ba_biblioteca_completa(ba_app_evento evento)
    list(PREPEND BA_APP_BIBLIOTECAS ba_app_evento)

    # La sala remota (lo que se ve y se maneja de los que se conectan por la web). Usa
    # ui/ConferenceClient y lo de emision/ y arranque/ que sigue en ba-app, y evento/Padron:
    # lo resuelve quien enlaza (deuda de test_niveles).
    # mbedcrypto: core/crypto/Sha256.hpp (ClavesEvento, GuardadoEnEvento) incluye <mbedtls/sha256.h>.
    # ba_motor: LugaresConGente le pasa los lugares al motor (motor::Motor::ponerLugares).
    ba_biblioteca(ba_app_sala NIVEL 2
        DEPENDE ba_app_base ba_app_comun ba_core ba_motor mbedcrypto Qt6::Core Qt6::Network
        FUENTES sala/ArchivosPrueba.cpp sala/Atencion.cpp sala/CalidadPorCalor.cpp sala/CapacidadReglas.cpp
                sala/CapasNecesarias.cpp sala/CapasRemotos.cpp sala/ClavesEvento.cpp sala/ConfigSala.cpp
                sala/ControlSala.cpp sala/CorreccionSala.cpp sala/CupoSala.cpp sala/Direcciones.cpp
                sala/EnlaceNecesita.cpp sala/EnlacesSala.cpp sala/GeneradorCarga.cpp sala/GuardadoEnEvento.cpp
                sala/ListaPrompters.cpp sala/LugaresConGente.cpp sala/MedidaMaquina.cpp sala/OradoresSala.cpp
                sala/OradoresSalaReglas.cpp sala/PaginaPublico.cpp sala/PantallasSala.cpp
                sala/ParticipantesSala.cpp sala/Pedido.cpp sala/PreguntasSala.cpp sala/PruebaSalaReglas.cpp
                sala/QuienMira.cpp sala/ReglasSala.cpp sala/ResumenSala.cpp sala/SalidasSala.cpp
                sala/SeguridadSala.cpp sala/Teleprompter.cpp sala/VotacionSala.cpp)
    ba_biblioteca_completa(ba_app_sala sala)
    list(PREPEND BA_APP_BIBLIOTECAS ba_app_sala)

    # La asamblea: el cuerpo, las bancas, el orden del día, el debate, las votaciones, el
    # quórum y la sesión remota. Usa sala/ (Pedido, OradoresSala), control/BusEventos y lo de
    # ui/ que sigue en ba-app: lo resuelve quien enlaza, con ba_app_sala DESPUÉS de esta.
    ba_biblioteca(ba_app_asamblea NIVEL 2
        DEPENDE ba_app_base ba_app_comun ba_core Qt6::Core Qt6::Gui Qt6::Network
        FUENTES asamblea/Bancas.cpp asamblea/ConferenciaDcn.cpp asamblea/Cuerpo.cpp asamblea/CuerpoViewModel.cpp
                asamblea/Debate.cpp asamblea/InformesResultado.cpp asamblea/OrdenDelDia.cpp
                asamblea/Presidencia.cpp asamblea/PruebaAsambleaReglas.cpp asamblea/Quorum.cpp
                asamblea/RecintoModel.cpp asamblea/SeguirMicrofono.cpp asamblea/SesionRemota.cpp
                asamblea/Votaciones.cpp asamblea/VotacionUnificada.cpp)
    # El QR de los links personales (SesionRemota): qrcodegen.cpp lo compila quien enlaza.
    target_include_directories(ba_app_asamblea PRIVATE "${CMAKE_SOURCE_DIR}/third_party/qrcodegen")
    ba_biblioteca_completa(ba_app_asamblea asamblea)
    list(PREPEND BA_APP_BIBLIOTECAS ba_app_asamblea)

    # El audio: el rack (CLAP/LSP y GStreamer), la matriz, el mezclador, la calibración, las
    # mediciones, la voz y la escucha. Usa emision/, arranque/ y ui/ que siguen en ba-app,
    # agent/Vocabulario y dispositivos/Conocidos: lo resuelve quien enlaza (deuda de
    # test_niveles). Sin los headers de CLAP no hay audio (como antes en ba-app).
    if(BA_HAVE_CLAP)
        set(_ba_audio_gst "")
        if(BA_HAVE_GST)
            set(_ba_audio_gst ba::gstreamer)
        endif()
        ba_biblioteca(ba_app_audio NIVEL 2
            DEPENDE ba_app_base ba_app_comun ba_core ba_motor ${_ba_audio_gst} nlohmann_json::nlohmann_json
                    Qt6::Core Qt6::Gui Qt6::Multimedia
            FUENTES audio/Analyzer.cpp audio/AudioRack.cpp audio/AudioViewModel.cpp audio/Calibracion.cpp
                    audio/CalibracionAnalisis.cpp audio/CalibracionCaptura.cpp audio/ClapEditor.cpp
                    audio/ClapHost.cpp audio/EntradasAudio.cpp audio/EquipoAudio.cpp audio/EscuchaStreaming.cpp
                    audio/GrabadorWav.cpp audio/GstRack.cpp audio/GstVoz.cpp audio/LazoMedicion.cpp
                    audio/LoudnessPrograma.cpp audio/LspDsp.cpp audio/Matriz.cpp audio/Mezclador.cpp
                    audio/Modules.cpp audio/MonitorVoz.cpp audio/ObjetivoLoudness.cpp audio/PluginTerminos.cpp
                    audio/RackManager.cpp audio/SalaSimulada.cpp)
        # Lo que ba-app les daba a estas fuentes: los headers de CLAP, dónde están los
        # plugins LSP y ba-lspdsp en el árbol de desarrollo (RackManager, LspDsp), y lo que
        # piden para enlazar (CoreAudio en la Mac, X11 y dl en Linux).
        target_include_directories(ba_app_audio PRIVATE "${BA_CLAP_INC}")
        target_compile_definitions(ba_app_audio PRIVATE BA_DEV_LSP_CLAP="${BA_LSP_CLAP}" BA_DEV_LSPDSP="${BA_LSPDSP_DIR}")
        target_link_libraries(ba_app_audio PRIVATE ${CMAKE_DL_LIBS})
        if(APPLE)
            target_link_libraries(ba_app_audio PRIVATE "-framework CoreAudio" "-framework CoreFoundation")
        elseif(UNIX)
            find_package(X11 REQUIRED)
            target_link_libraries(ba_app_audio PRIVATE X11::X11)
        endif()
        # La voz (sherpa-onnx): solo si está.
        if(BA_HAVE_VOZ)
            target_sources(ba_app_audio PRIVATE audio/Escucha.cpp audio/EscuchaNemotron.cpp audio/Habla.cpp)
            target_include_directories(ba_app_audio PRIVATE "${BA_SHERPA_DIR}/include")
            # Las de sherpa después de la biblioteca: el enlazador de Linux resuelve una .a
            # solo con las que vienen después (en ba-app iban antes que ba_app_audio).
            if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
                target_link_libraries(ba_app_audio PRIVATE -Wl,--start-group ${BA_SHERPA_LIBS} -Wl,--end-group)
            else()
                target_link_libraries(ba_app_audio PRIVATE ${BA_SHERPA_LIBS})
            endif()
            set_property(GLOBAL APPEND PROPERTY BA_BIBLIOTECA_ba_app_audio_FUENTES
                         "${CMAKE_CURRENT_SOURCE_DIR}/audio/Escucha.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/audio/EscuchaNemotron.cpp"
                         "${CMAKE_CURRENT_SOURCE_DIR}/audio/Habla.cpp")
            ba_biblioteca_completa(ba_app_audio audio)
        else()
            ba_biblioteca_completa(ba_app_audio audio "/(Escucha|EscuchaNemotron|Habla)\\.cpp$")
        endif()
        list(PREPEND BA_APP_BIBLIOTECAS ba_app_audio)
    endif()

    # El asistente: los agentes y sus procesos, los modelos locales, la voz, la traducción,
    # los subtítulos, las alarmas, la mirada y el registro con tótems (registro/: su agente
    # y el chat en vivo se usan entre sí, por eso van juntos). Usa audio/, term/, emision/,
    # arranque/, ui/, sala/Teleprompter, asamblea/Cuerpo, carrusel/Conversion y
    # plugins/Hijo: lo resuelve quien enlaza (deuda de test_niveles).
    set(_ba_agente_gst "")
    if(BA_HAVE_GST)
        set(_ba_agente_gst ba::gstreamer)
    endif()
    ba_biblioteca(ba_app_agente NIVEL 2
        DEPENDE ba_app_net ba_app_base ba_app_comun ba_core ba_motor ${_ba_agente_gst} nlohmann_json::nlohmann_json
                Qt6::Core Qt6::Gui Qt6::Network Qt6::Multimedia
        FUENTES agent/Actualizacion.cpp agent/AgentViewModel.cpp agent/AlarmaSonora.cpp agent/AlertEngine.cpp
                agent/ArgumentosLocales.cpp agent/ChatEnVivo.cpp agent/ChatEnVivoFuentes.cpp agent/ControlSistema.cpp
                agent/Despachador.cpp agent/DiscoAgentes.cpp agent/EntradasAlarmas.cpp agent/EnvioOpencode.cpp agent/EstadoIA.cpp
                agent/FlujosTexto.cpp agent/HijosDeLaApp.cpp agent/Lanzamiento.hpp agent/Mirada.cpp
                agent/MiradaCuerpo.cpp agent/ModeloPropio.cpp agent/ModelosLocales.cpp agent/Notificador.cpp
                agent/Reconocedores.cpp agent/Roles.cpp agent/RotuloTraduccionConfig.cpp
                agent/ServidorModeloLocal.cpp agent/ServidorOpencode.cpp agent/SesionesEvento.cpp
                agent/Subtitulos.cpp agent/SubtitulosCC.cpp agent/Traductor.cpp agent/Vocabulario.cpp
                agent/VocesRack.cpp agent/Voz.cpp agent/VozIdioma.hpp agent/VozPrompter.cpp agent/VozPrompterPlan.cpp
                registro/AgenteRegistro.cpp registro/AgenteTotem.cpp registro/ConexionesTotem.cpp
                registro/Conversacion.cpp registro/RitmoTotem.cpp registro/Totems.cpp)
    ba_biblioteca_completa(ba_app_agente agent)
    ba_biblioteca_completa(ba_app_agente registro)
    list(PREPEND BA_APP_BIBLIOTECAS ba_app_agente)

    # Las pantallas: el catálogo de lo que se puede mostrar, los monitores y ventanas, las
    # distribuciones, los destinos y los televisores (Chromecast: buscarlos por mDNS y el
    # canal de cast por TLS). Usa arranque/ArchivoConfigQt, que sigue en ba-app: lo
    # resuelve quien enlaza (deuda de test_niveles).
    ba_biblioteca(ba_app_pantallas NIVEL 2
        DEPENDE ba_app_base ba_app_comun ba_core mbedtls mbedx509 mbedcrypto nlohmann_json::nlohmann_json
                Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml
        FUENTES pantallas/BuscadorCatalogo.cpp pantallas/Buscar.cpp pantallas/Cast.cpp pantallas/Catalogo.cpp
                pantallas/Destinos.cpp pantallas/Distribuciones.cpp pantallas/Monitores.cpp pantallas/Tarjetas.hpp
                pantallas/Ventanas.cpp)
    if(BA_MDNS_OK)
        target_link_libraries(ba_app_pantallas PRIVATE ba_mdns) # pantallas/Buscar.cpp
    endif()
    ba_biblioteca_completa(ba_app_pantallas pantallas)
    list(PREPEND BA_APP_BIBLIOTECAS ba_app_pantallas)

    # El control: las acciones, la botonera (y sus caras), el bus de eventos, el DSK, los
    # atajos globales, el control web y su réplica, y el servidor de Companion/Satellite. Usa
    # mcp/, sala/Pedido, escenas/RefEscena, emision/ y ui/ que siguen en ba-app: lo resuelve
    # quien enlaza (deuda de test_niveles). Las capturas de la botonera (:/botonera) siguen
    # en ba-app.
    set(_ba_control_gst "")
    if(BA_HAVE_GST)
        set(_ba_control_gst ba::gstreamer)
    endif()
    ba_biblioteca(ba_app_control NIVEL 2
        DEPENDE ba_app_base ba_app_comun ba_core ${_ba_control_gst} nlohmann_json::nlohmann_json
                Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml
        FUENTES control/Acciones.cpp control/AtajosGlobales.cpp control/Botonera.cpp control/BotoneraGateway.cpp
                control/BusEventos.cpp control/CarasBotonera.cpp control/ControlWeb.cpp
                control/ControlWebGateway.cpp control/CuadrosWeb.cpp control/Dsk.cpp control/MonitorBus.cpp
                control/ReplicaPermitidas.inc control/ReplicaPublicador.cpp control/SatelliteServidor.cpp
                control/TemasWeb.hpp)
    if(APPLE)
        target_link_libraries(ba_app_control PRIVATE "-framework Carbon") # control/AtajosGlobales
    endif()
    ba_biblioteca_completa(ba_app_control control)
    list(PREPEND BA_APP_BIBLIOTECAS ba_app_control)

    # Los equipos: el registro de dispositivos y sus sondas (drivers, ASIO, GStreamer), las
    # cámaras PTZ (VISCA, ONVIF, Panasonic, digital, por plugin), la red (cámaras IP, RTSP) y
    # la sincronía de labios de las cámaras. Se usan entre sí (red y dispositivos usan ptz),
    # por eso van juntos. Usa audio/, video/, plugins/, emision/, arranque/ y ui/: lo
    # resuelve quien enlaza (deuda de test_niveles).
    set(_ba_equipos_gst "")
    if(BA_HAVE_GST)
        set(_ba_equipos_gst ba::gstreamer)
    endif()
    ba_biblioteca(ba_app_equipos NIVEL 2
        DEPENDE ba_app_net ba_app_base ba_app_comun ba_core ${_ba_equipos_gst} nlohmann_json::nlohmann_json
                Qt6::Core Qt6::Gui Qt6::Network Qt6::Multimedia Qt6::Qml Qt6::Quick
        FUENTES camaras/SincroLabios.cpp
                dispositivos/Conocidos.cpp dispositivos/Dispositivos.cpp dispositivos/Drivers.cpp
                dispositivos/Onvif.cpp dispositivos/Registro.cpp dispositivos/SondaAsio.cpp
                dispositivos/SondaDrivers.cpp dispositivos/SondaGst.cpp dispositivos/SondaReal.hpp
                dispositivos/Sondeo.cpp
                ptz/ControlCamaras.cpp ptz/ControlCamarasCamara.hpp ptz/ControlCamarasFoto.cpp
                ptz/ControlCamarasPosiciones.cpp ptz/Interlock.hpp ptz/OnvifBusqueda.cpp ptz/Posiciones.cpp
                ptz/PtzBackend.cpp ptz/PtzDigital.cpp ptz/PtzOnvif.cpp ptz/PtzPanasonic.cpp ptz/PtzPlugin.cpp
                ptz/PtzProtocolos.cpp ptz/PtzVisca.cpp
                red/Biblioteca.cpp red/Plan.cpp red/Red.cpp red/Rtsp.cpp)
    if(WIN32)
        target_link_libraries(ba_app_equipos PRIVATE ole32) # los drivers ASIO, por COM (SondaAsio)
    endif()
    ba_biblioteca_completa(ba_app_equipos camaras)
    ba_biblioteca_completa(ba_app_equipos dispositivos)
    ba_biblioteca_completa(ba_app_equipos ptz)
    ba_biblioteca_completa(ba_app_equipos red)
    list(PREPEND BA_APP_BIBLIOTECAS ba_app_equipos)

    # Las cuentas de las plataformas (YouTube, Facebook, Twitch) y los vivos que las juntan:
    # solo usan ba_app_net. vivos/ usa los tres clientes, por eso van juntos.
    ba_biblioteca(ba_app_cuentas NIVEL 2
        DEPENDE ba_app_net ba_app_base ba_app_comun ba_core nlohmann_json::nlohmann_json
                Qt6::Core Qt6::Gui Qt6::Network
        FUENTES facebook/FacebookClient.cpp twitch/TwitchClient.cpp youtube/YouTubeClient.cpp
                vivos/PlataformasVivo.cpp vivos/Vivos.cpp)
    ba_biblioteca_completa(ba_app_cuentas facebook)
    ba_biblioteca_completa(ba_app_cuentas twitch)
    ba_biblioteca_completa(ba_app_cuentas youtube)
    ba_biblioteca_completa(ba_app_cuentas vivos)
    list(PREPEND BA_APP_BIBLIOTECAS ba_app_cuentas)

    # El carrusel de la escena (la tira y su guardado) y la conversión de lo que trae la
    # gente a láminas (PDF con Qt Pdf; Word/PowerPoint/planillas con los Craft: BA_CRAFT_DIR
    # se le agrega más abajo, donde se buscan). No usa ningún área.
    ba_biblioteca(ba_app_carrusel NIVEL 2
        DEPENDE ba_app_comun ba_core nlohmann_json::nlohmann_json Qt6::Core Qt6::Gui Qt6::Pdf
        FUENTES carrusel/Carrusel.cpp carrusel/CarruselStore.cpp carrusel/Conversion.cpp)
    ba_biblioteca_completa(ba_app_carrusel carrusel)
    list(PREPEND BA_APP_BIBLIOTECAS ba_app_carrusel)

    # La terminal de la pestaña Asistente (libvterm y un pty por sistema).
    ba_biblioteca(ba_app_term NIVEL 2
        DEPENDE ba_app_comun ba_core ba_vterm Qt6::Core Qt6::Gui Qt6::Quick
        FUENTES term/Pty.hpp term/TerminalSession.cpp term/TerminalView.cpp)
    if(WIN32)
        set(_ba_pty term/Pty_win.cpp)
    else()
        set(_ba_pty term/Pty_posix.cpp)
        if(NOT APPLE)
            target_link_libraries(ba_app_term PRIVATE util) # forkpty
        endif()
    endif()
    target_sources(ba_app_term PRIVATE ${_ba_pty})
    set_property(GLOBAL APPEND PROPERTY BA_BIBLIOTECA_ba_app_term_FUENTES "${CMAKE_CURRENT_SOURCE_DIR}/${_ba_pty}")
    ba_biblioteca_completa(ba_app_term term "/Pty_(win|posix)\\.cpp$")
    list(PREPEND BA_APP_BIBLIOTECAS ba_app_term)

    # --- Ola 4, parte B (W6): el motor, sus vecinos, el MCP y la interfaz ----------------------
    # Mismas definiciones e includes que tenían en ba-app (el preprocesado de cada fuente es el
    # mismo: docs/local/build-tiempos.md, «Ola 4 · W6»). Se usan entre sí y con las demás áreas
    # (deuda de test_niveles): lo resuelve quien enlaza.
    set(_ba_w6_gst "")
    if(BA_HAVE_GST)
        set(_ba_w6_gst ba::gstreamer)
    endif()

    # La emisión: BroadcastEngine y sus piezas, la API que usa la app (ApiMotor), los tipos de
    # fuente de la app, el documento y el latido del aire, la ventanilla y el turno del motor.
    # Los tipos de fuente se registran desde el arranque (AppRuntime: fuentes::registrar…).
    ba_biblioteca(ba_app_emision NIVEL 2
        DEPENDE ba_app_net ba_app_base ba_app_comun ba_core ba_motor ba_emisor_pedido ${_ba_w6_gst} nlohmann_json::nlohmann_json Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::Multimedia
        FUENTES emision/AvisoFueraDeUi.cpp emision/BroadcastEngine.cpp emision/CapasPrograma.cpp
                emision/ChatOverlaySource.cpp emision/DocumentoAire.cpp emision/EmisionSalidas.cpp
                emision/EmisorAparte.cpp emision/EngineSelfTest.cpp emision/EscenasYFuentes.cpp
                emision/FuentePdf.cpp emision/FuenteQr.cpp emision/FuentesApp.cpp emision/InputSpecs.cpp
                emision/LatidoAire.cpp emision/MediosApp.cpp emision/MezclaApp.cpp emision/NivelesMotor.cpp
                emision/OperatorFeed.cpp emision/PintorAire.cpp emision/RetornoGateway.cpp
                emision/RetornosInvitados.cpp emision/SalidasConfig.cpp emision/SeguridadAire.cpp
                emision/TiposBasicos.cpp emision/TurnoMotor.cpp emision/VentanillaMotor.cpp
                emision/VigiaDisco.cpp emision/VistasYMedicion.cpp emision/VozAlLobby.cpp)
    if(BA_HAVE_NDI)
        target_sources(ba_app_emision PRIVATE emision/NdiSource.cpp)
        target_link_libraries(ba_app_emision PRIVATE ba_noidea)
        set_property(GLOBAL APPEND PROPERTY BA_BIBLIOTECA_ba_app_emision_FUENTES
                     "${CMAKE_CURRENT_SOURCE_DIR}/emision/NdiSource.cpp")
        ba_biblioteca_completa(ba_app_emision emision)
    else()
        ba_biblioteca_completa(ba_app_emision emision "/NdiSource\\.cpp$")
    endif()
    list(PREPEND BA_APP_BIBLIOTECAS ba_app_emision)

    # El video: los grafos de cada entrada, la superresolución, las capturas, los scopes y el
    # catálogo de elementos de GStreamer.
    ba_biblioteca(ba_app_video NIVEL 2
        DEPENDE ba_app_base ba_app_comun ba_core ba_motor ba_emisor_pedido ${_ba_w6_gst} nlohmann_json::nlohmann_json Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::Multimedia
        FUENTES video/BancoSuperRes.cpp video/Camaras.cpp video/CatalogoGst.cpp video/Diagnostico.cpp
                video/ElementosGst.cpp video/FuentesCaptura.cpp video/GrafoVideo.cpp video/Scopes.cpp
                video/Sincronia.cpp video/SuperRes.cpp video/VideoGrafos.cpp)
    ba_biblioteca_completa(ba_app_video video)
    list(PREPEND BA_APP_BIBLIOTECAS ba_app_video)

    # Los gráficos (el proceso ba-grafico, su catálogo, sus vínculos y el video en un gráfico).
    ba_biblioteca(ba_app_graficos NIVEL 2
        DEPENDE ba_app_base ba_app_comun ba_core ba_motor ba_emisor_pedido ${_ba_w6_gst} nlohmann_json::nlohmann_json Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::Multimedia
        FUENTES graficos/Catalogo.cpp graficos/Emisor.cpp graficos/GraficoProceso.cpp
                graficos/GraficoSource.cpp graficos/Medios.cpp graficos/VideoEnGrafico.cpp
                graficos/Vinculos.cpp)
    ba_biblioteca_completa(ba_app_graficos graficos)
    list(PREPEND BA_APP_BIBLIOTECAS ba_app_graficos)

    # Las escenas: el conmutador de cada salida, el AFV, las grillas de personas, el respaldo
    # de entradas y sus ajustes.
    ba_biblioteca(ba_app_escenas NIVEL 2
        DEPENDE ba_app_base ba_app_comun ba_core ba_motor ba_emisor_pedido ${_ba_w6_gst} nlohmann_json::nlohmann_json Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::Multimedia
        FUENTES escenas/Afv.cpp escenas/AjustesEscenas.cpp escenas/Conmutador.cpp escenas/ControlGrillas.cpp
                escenas/FuenteGrilla.cpp escenas/GrillaPersonas.cpp escenas/RespaldoEntradas.cpp)
    ba_biblioteca_completa(ba_app_escenas escenas)
    list(PREPEND BA_APP_BIBLIOTECAS ba_app_escenas)

    # Los medios y la lista de reproducción.
    ba_biblioteca(ba_app_medios NIVEL 2
        DEPENDE ba_app_base ba_app_comun ba_core ba_motor ba_emisor_pedido ${_ba_w6_gst} nlohmann_json::nlohmann_json Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::Multimedia
        FUENTES medios/Medios.cpp medios/Playlist.cpp)
    ba_biblioteca_completa(ba_app_medios medios)
    list(PREPEND BA_APP_BIBLIOTECAS ba_app_medios)

    # Los tiempos del evento, la resolución de las salidas y quién es quién (rostros).
    ba_biblioteca(ba_app_tiempos NIVEL 2
        DEPENDE ba_app_base ba_app_comun ba_core ba_motor ba_emisor_pedido ${_ba_w6_gst} nlohmann_json::nlohmann_json Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::Multimedia
        FUENTES tiempos/FilasTiempo.cpp tiempos/Tiempos.cpp tiempos/TiemposModel.cpp)
    ba_biblioteca_completa(ba_app_tiempos tiempos)
    list(PREPEND BA_APP_BIBLIOTECAS ba_app_tiempos)
    ba_biblioteca(ba_app_resolucion NIVEL 2
        DEPENDE ba_app_base ba_app_comun ba_core ba_motor ba_emisor_pedido ${_ba_w6_gst} nlohmann_json::nlohmann_json Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::Multimedia
        FUENTES resolucion/Plan.cpp resolucion/ResolucionModel.cpp)
    ba_biblioteca_completa(ba_app_resolucion resolucion)
    list(PREPEND BA_APP_BIBLIOTECAS ba_app_resolucion)
    ba_biblioteca(ba_app_rostros NIVEL 2
        DEPENDE ba_app_base ba_app_comun ba_core ba_motor ba_emisor_pedido ${_ba_w6_gst} nlohmann_json::nlohmann_json Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::Multimedia
        FUENTES rostros/QuienEs.cpp)
    ba_biblioteca_completa(ba_app_rostros rostros)
    list(PREPEND BA_APP_BIBLIOTECAS ba_app_rostros)

    # El MCP: el servidor, el núcleo, el índice y las herramientas (un McpXTools.cpp por
    # familia). El registro es explícito: McpService::registerTools() llama a cada
    # registerXTools() (ningún inicializador estático); test_mcp_registro lo compara con
    # docs/mcp.json. El puente (mcp/bridge/, ba-mcp) es otro programa.
    ba_biblioteca(ba_app_mcp NIVEL 2
        DEPENDE ba_app_net ba_app_base ba_app_comun ba_core ba_motor ba_emisor_pedido ${_ba_w6_gst} nlohmann_json::nlohmann_json Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::Multimedia
        FUENTES mcp/McpAccountTools.cpp mcp/McpAgenteTools.cpp mcp/McpAire.cpp mcp/McpAlertTools.cpp
                mcp/McpAsambleaTools.cpp mcp/McpAssistantTools.cpp mcp/McpAtencionTools.cpp
                mcp/McpAudioTools.cpp mcp/McpBotoneraTools.cpp mcp/McpCalibracionTools.cpp
                mcp/McpCapacidadTools.cpp mcp/McpConfigTools.cpp mcp/McpConfiguracionesTools.cpp
                mcp/McpControlTools.cpp mcp/McpControlWebTools.cpp mcp/McpCore.cpp mcp/McpDcnTools.cpp
                mcp/McpDebateTools.cpp mcp/McpDocumentosTools.cpp mcp/McpEnlaceTools.cpp
                mcp/McpEquiposTools.cpp mcp/McpEscenasTools.cpp mcp/McpEtiquetas.cpp mcp/McpEventoTools.cpp
                mcp/McpFacebookTools.cpp mcp/McpFoto.cpp mcp/McpFuentesTools.cpp mcp/McpGrabacionTools.cpp
                mcp/McpGraficoTools.cpp mcp/McpGrillaTools.cpp mcp/McpHelp.cpp mcp/McpHttp.cpp
                mcp/McpImageTools.cpp mcp/McpIndice.cpp mcp/McpInformesTools.cpp mcp/McpInputTools.cpp
                mcp/McpLaminaTools.cpp mcp/McpLineaTools.cpp mcp/McpMando.cpp mcp/McpMatrizTools.cpp
                mcp/McpMediaTools.cpp mcp/McpMezclaTools.cpp mcp/McpModeloTools.cpp mcp/McpOperatorTools.cpp
                mcp/McpPantallaTools.cpp mcp/McpPlataformas.cpp mcp/McpPluginTools.cpp
                mcp/McpPreguntaTools.cpp mcp/McpProcedimientoTools.cpp mcp/McpPrompterTools.cpp
                mcp/McpPruebaAsambleaTools.cpp mcp/McpPruebaSalaTools.cpp mcp/McpPtzTools.cpp
                mcp/McpQrTools.cpp mcp/McpRecetaTools.cpp mcp/McpRecintoTools.cpp mcp/McpRedTools.cpp
                mcp/McpRegistroTools.cpp mcp/McpRenombres.cpp mcp/McpRoomTools.cpp mcp/McpRostroTools.cpp
                mcp/McpSalidasTools.cpp mcp/McpService.cpp mcp/McpSesionTools.cpp mcp/McpSystemTools.cpp
                mcp/McpTemaTools.cpp mcp/McpTiemposTools.cpp mcp/McpTotemTools.cpp mcp/McpTraduccionTools.cpp
                mcp/McpTransmisionTools.cpp mcp/McpTraza.cpp mcp/McpTwitchTools.cpp mcp/McpUiTools.cpp
                mcp/McpVentanaTools.cpp mcp/McpVideoTools.cpp mcp/McpVivosTools.cpp mcp/McpYouTubeTools.cpp)
    ba_biblioteca_completa(ba_app_mcp mcp)
    list(PREPEND BA_APP_BIBLIOTECAS ba_app_mcp)

    # La interfaz (nivel 3): Studio y los ViewModels (ui/), el arranque (arranque/). La QML y
    # los recursos siguen en ba-app (qt_add_resources), como main.cpp. Los tipos QML se
    # registran en GuiMode (llamadas, no inicializadores estáticos).
    ba_biblioteca(ba_app_ui NIVEL 3
        DEPENDE ba_app_net ba_app_base ba_app_comun ba_core ba_motor ba_emisor_pedido ${_ba_w6_gst} nlohmann_json::nlohmann_json Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::Multimedia Qt6::QuickControls2 Qt6::Svg Qt6::Pdf Qt6::GuiPrivate
        FUENTES ui/AlarmasViewModel.cpp ui/AppViewModel.cpp ui/AsambleaViewModel.cpp ui/AsistenteViewModel.cpp
                ui/AtencionViewModel.cpp ui/BitacoraEvento.cpp ui/CalidadCara.cpp ui/CapacidadViewModel.cpp
                ui/CarruselViewModel.cpp ui/ChatHilo.cpp ui/ChatOverlayRenderer.cpp ui/ConduccionLinea.cpp
                ui/ConferenceClient.cpp ui/ConferenceViewModel.cpp ui/ControlViewModel.cpp
                ui/CorridaViewModel.cpp ui/CuentasViewModel.cpp ui/DashboardModel.cpp
                ui/DashboardModelArchivo.cpp ui/Discursos.cpp ui/DocumentosViewModel.cpp ui/EjecucionLinea.cpp
                ui/EjecutorLinea.cpp ui/EmisionViewModel.cpp ui/EnlaceViewModel.cpp ui/EnlacesViewModel.cpp
                ui/EscenasViewModel.cpp ui/EscritorioViewModel.cpp ui/EstadoAireViewModel.cpp
                ui/EventoAbierto.cpp ui/EventoViewModel.cpp ui/EventosListaViewModel.cpp
                ui/ExtensionesModel.cpp ui/FacebookViewModel.cpp ui/FilaViva.cpp
                ui/Formulario.cpp ui/FotosPersonas.cpp ui/FrameService.cpp ui/FuenteQrPintor.cpp
                ui/GrabacionesModel.cpp ui/GraficoViewModel.cpp ui/GuionViewModel.cpp ui/HistoricoConsulta.cpp
                ui/HistoricoViewModel.cpp ui/ImagenViewModel.cpp ui/ImagenViva.cpp
                ui/InformeEventoViewModel.cpp ui/InputsViewModel.cpp ui/InterfazMcp.cpp
                ui/LectorDocumentos.cpp ui/LineaProductor.cpp ui/LineaViewModel.cpp ui/ListModels.cpp
                ui/ManualesViewModel.cpp ui/MediosViewModel.cpp ui/MezclaViewModel.cpp
                ui/MultiviewViewModel.cpp ui/OperatorMessages.cpp ui/PadronViewModel.cpp
                ui/PantallasViewModel.cpp ui/PdfEscena.cpp ui/PersonasViewModel.cpp ui/PlataformaViewModel.cpp
                ui/PrompterViewModel.cpp ui/PropertiesModel.cpp ui/ProveedorCuadros.cpp ui/ProveedorFotos.cpp
                ui/PruebaAsambleaViewModel.cpp ui/PruebaSalaViewModel.cpp ui/PublicoViewModel.cpp
                ui/PuestoCorreccion.cpp ui/QrViewModel.cpp ui/Reconocedor.cpp ui/RecursosViewModel.cpp
                ui/RegistroViewModel.cpp ui/Reloj.cpp ui/RelojNiveles.cpp ui/ResumenNodo.cpp ui/RielConfig.cpp
                ui/RotuladorViewModel.cpp ui/SalaViewModel.cpp ui/SalidasViewModel.cpp ui/ScopeVista.cpp
                ui/SeccionesConfig.cpp ui/Seguidor.cpp ui/SettingsModel.cpp ui/StudioCapacidad.cpp
                ui/StudioViewModel.cpp ui/SystemViewModel.cpp ui/TarjetasRostros.cpp
                ui/TemaEventoViewModel.cpp ui/TotemsViewModel.cpp ui/TwitchViewModel.cpp ui/VideoItem.cpp
                ui/VideoViewModel.cpp ui/VigiaInterfaz.cpp ui/VivosViewModel.cpp ui/Vumetro.cpp
                ui/WebEventoViewModel.cpp ui/YouTubeViewModel.cpp arranque/AireDurable.cpp
                arranque/AjustesMotor.cpp arranque/AppRuntime.cpp arranque/AppSettings.cpp
                arranque/Configuraciones.cpp arranque/EnergiaAire.cpp arranque/FotoMezcla.cpp
                arranque/GuiMode.cpp arranque/InicioConElSistema.cpp arranque/InterfazConfig.cpp
                arranque/Invitados.cpp arranque/ModoConsola.cpp arranque/ModoVivoConfig.cpp
                arranque/PlanoControl.cpp arranque/PublicadorFoto.cpp arranque/Registrar.cpp
                arranque/RepositorioEscenas.cpp arranque/RutasPortables.cpp arranque/SaludProceso.cpp
                arranque/ServicioEmision.cpp arranque/ServicioSalidas.cpp arranque/TraductorQt.cpp)
    ba_biblioteca_completa(ba_app_ui ui "/(FiltroCatalogo|JsonListModel|ListaObjetos)\\.cpp$")
    ba_biblioteca_completa(ba_app_ui arranque)
    # El lector de documentos (ui/LectorDocumentos: el DNI por su código de barras) mira
    # BA_HAVE_CODIGOS, que viene con ba_codigos (como en ba-app).
    if(TARGET ba_codigos)
        target_link_libraries(ba_app_ui PUBLIC ba_codigos)
    endif()
    list(PREPEND BA_APP_BIBLIOTECAS ba_app_ui)

    ba_biblioteca_fuentes(BA_APP_SOURCES ${BA_APP_BIBLIOTECAS})
endif()

if(MSVC)
    add_executable(ba-app WIN32 ${BA_APP_SOURCES})
else()
    add_executable(ba-app ${BA_APP_SOURCES})
endif()
# QR de los links personales de la banca virtual (Nayuki, MIT; el mismo que el gateway).
target_sources(ba-app PRIVATE "${CMAKE_SOURCE_DIR}/third_party/qrcodegen/qrcodegen.cpp")
target_include_directories(ba-app PRIVATE "${CMAKE_SOURCE_DIR}/third_party/qrcodegen")
if(BA_HAVE_VOZ)
    target_include_directories(ba-app PRIVATE "${BA_SHERPA_DIR}/include")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(ba-app PRIVATE -Wl,--start-group ${BA_SHERPA_LIBS} -Wl,--end-group)
    else()
        target_link_libraries(ba-app PRIVATE ${BA_SHERPA_LIBS})
    endif()
    install(FILES "${BA_SHERPA_DIR}/LICENSE" DESTINATION data/licencias RENAME sherpa-onnx-Apache-2.0.txt
            COMPONENT bundle OPTIONAL)
    message(STATUS "ba-app: con voz (sherpa-onnx)")
endif()
# Los modulos se incluyen como "evento/Evento.hpp": hace falta tambien sin Qt.
target_include_directories(ba-app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# Windows: el logo en el .exe (el mismo .ico que el launcher, ver tools/generar/icono.py).
if(WIN32)
    enable_language(RC)
    target_sources(ba-app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/ba-app.rc")
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/ba-app.rc"
        PROPERTIES COMPILE_OPTIONS "-I;${CMAKE_SOURCE_DIR}/assets/icons")
endif()
# macOS: permisos de camara/microfono (TCC) en un ejecutable que no es .app.
include(${CMAKE_SOURCE_DIR}/cmake/BaMacInfoPlist.cmake)
ba_mac_embed_info_plist(ba-app io.github.blairq.broadcastappliance.app "Broadcast Appliance")

# MinGW GUI: subsistema GUI (sin consola), igual que el launcher. Sin esto,
# MinGW enlaza como consola y el CRT espera `main` en vez de `WinMain`/
# `wWinMain` (ver el trampoline WinMain->wWinMain en main.cpp).
# GStreamer (si está vendoreado): el catálogo de módulos y sus propiedades.
if(BA_HAVE_GST)
    target_link_libraries(ba-app PRIVATE ba::gstreamer)
endif()
if(WIN32)
    # main.cpp trae su propio wWinMain: sin el de Qt (Qt6::EntryPointPrivate).
    set_target_properties(ba-app PROPERTIES WIN32_EXECUTABLE ON qt_no_entrypoint ON)
    target_link_libraries(ba-app PRIVATE dxgi) # placas de video (system_hardware)
    # La sonda de drivers crea cada driver ASIO por COM (dispositivos/SondaAsio.cpp).
    target_link_libraries(ba-app PRIVATE ole32)
endif()

target_link_libraries(ba-app PRIVATE ba_core mbedtls mbedx509 mbedcrypto) # HTTPS propio (net/HttpsClient)
# El lector de documentos (el DNI por su código de barras: ui/LectorDocumentos).
if(TARGET ba_codigos)
    target_link_libraries(ba-app PRIVATE ba_codigos)
endif()
if(APPLE)
    # Los canales físicos de cada dispositivo (audio/EquipoAudio: Qt informa de más en la Mac).
    target_link_libraries(ba-app PRIVATE "-framework CoreAudio" "-framework CoreFoundation" "-framework Carbon")
endif()
# Buscar Chromecast en la red (src/app/pantallas/Buscar.cpp).
if(BA_MDNS_OK)
    target_link_libraries(ba-app PRIVATE ba_mdns)
    message(STATUS "ba-app: busqueda de Chromecast por mDNS")
else()
    ba_falta("Búsqueda de televisores (mDNS)" "no se encontró libmicrodns (extern/libmicrodns, tools/deps/ndi-src.sh)")
endif()
# Raices de Mozilla para ese HTTPS (make deps-cacert): viajan en el bundle.
if(EXISTS "${CMAKE_SOURCE_DIR}/extern/cacert/cacert.pem")
    install(FILES "${CMAKE_SOURCE_DIR}/extern/cacert/cacert.pem" DESTINATION data)
endif()

# --- Motor de emision: GStreamer (src/motor) --------------------------------
if(BA_HAVE_MOTOR)
    target_link_libraries(ba-app PRIVATE ba_motor ba_emisor_pedido) # el pedido al emisor aparte (F3-01)
    if(BA_HAVE_NDI)
        # Solo libNoIDea: el SpeedHQ lo decodifica GStreamer (avdec_speedhq).
        target_link_libraries(ba-app PRIVATE ba_noidea)
    endif()
    # rpath: el ejecutable busca sus bibliotecas JUNTO A EL dentro del bundle
    # portable (bin/64bit/), nunca en el sistema.
    if(APPLE)
        set_target_properties(ba-app PROPERTIES
            BUILD_RPATH "${BA_QT_PREFIX}/lib"
            INSTALL_RPATH "@executable_path;@executable_path/qt/lib")
    elseif(NOT WIN32)
        set_target_properties(ba-app PROPERTIES
            INSTALL_RPATH "$ORIGIN;$ORIGIN/qt/lib")
    endif()
    # Despues de fijar los rpath de arriba (los pisarian): GStreamer, del bundle.
    if(TARGET ba::gstreamer)
        ba_gst_rpath(ba-app "")
    endif()
endif()

# -rdynamic: exporta los simbolos al ejecutable para que la pila de una caida
# salga con NOMBRES y no solo con direcciones (ver core/diag/Caidas.hpp). En
# Windows no aplica.
if(NOT WIN32)
    target_link_options(ba-app PRIVATE -rdynamic)
endif()

if(BA_APP_QT_OK)
    # Qt6::Svg: la vista previa y el PNG del generador de QR rasterizan el MISMO
    # SVG que se exporta (ver ui/QrViewModel.cpp).
    # Qt6::Pdf rinde las paginas de un PDF a imagenes (carrusel/Conversion.cpp).
# Es de Qt, no una dependencia nueva: ver make deps-qt-host (-m qtpdf).
# Qt6::Multimedia reproduce el video y el audio del visor de Medios (doble clic
# en un archivo). SU MOTOR ES FFMPEG y Qt lo trae enlazado ADENTRO del plugin:
# no hace falta un ffmpeg del sistema, ni VLC, ni levantar otro proceso. Se
# verifico con ldd: `libffmpegmediaplugin.so` no pide ningun libav*.
target_link_libraries(ba-app PRIVATE Qt6::Gui Qt6::Quick Qt6::Qml Qt6::QuickControls2 Qt6::Network Qt6::Svg Qt6::Pdf Qt6::Multimedia)
# QRhi (<rhi/qrhi.h>, semipúblico desde Qt 6.6): VideoItem sube cada cuadro a SU textura
# (sin crear y soltar una por cuadro). Solo los encabezados, la biblioteca es la misma.
target_link_libraries(ba-app PRIVATE Qt6::GuiPrivate)
# WordCraft, DeckCraft y GridCraft (sus herramientas de línea de comandos,
# tools/deps/craft.sh): pasan a PDF lo que trae la gente (el carrusel, la
# documentación del evento) y, por su MCP, el asistente arma documentos,
# presentaciones y gráficos. Reemplazan a LibreOffice. Se buscan acá para correr
# desde la compilación; en el bundle van en bin/craft.
if(WIN32)
    set(_ba_craft_dir "${CMAKE_SOURCE_DIR}/extern/craft/win")
    set(_ba_craft_exe ".exe")
elseif(APPLE)
    set(_ba_craft_dir "${CMAKE_SOURCE_DIR}/extern/craft/mac")
    set(_ba_craft_exe "")
else()
    set(_ba_craft_dir "${CMAKE_SOURCE_DIR}/extern/craft/linux")
    set(_ba_craft_exe "")
endif()
set(_ba_craft_ok TRUE)
foreach(_app wordcraft deckcraft gridcraft)
    if(NOT EXISTS "${_ba_craft_dir}/${_app}-cli${_ba_craft_exe}")
        set(_ba_craft_ok FALSE)
    endif()
endforeach()
if(_ba_craft_ok)
    target_compile_definitions(ba-app PRIVATE BA_CRAFT_DIR="${_ba_craft_dir}")
    if(TARGET ba_app_carrusel)
        target_compile_definitions(ba_app_carrusel PRIVATE BA_CRAFT_DIR="${_ba_craft_dir}") # carrusel/Conversion
    endif()
else()
    ba_falta("Documentos (WordCraft, DeckCraft, GridCraft)" "no están en ${_ba_craft_dir} ('make deps-craft')")
endif()
    # Version visible en Configuracion -> Sistema y datos (soporte).
    target_compile_definitions(ba-app PRIVATE BA_APP_VERSION="${PROJECT_VERSION}")
    if(TARGET ba_app_ui)
        # ui/SystemViewModel y arranque/GuiMode la muestran (#ifdef: sin ella no se ve).
        target_compile_definitions(ba_app_ui PRIVATE BA_APP_VERSION="${PROJECT_VERSION}")
    endif()
    target_include_directories(ba-app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    set_target_properties(ba-app PROPERTIES AUTOMOC ON)
    # Vista: los .qml viajan como recursos dentro del binario (qrc:/qml/...).
    file(GLOB_RECURSE BA_APP_QML RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.qml")
    qt_add_resources(ba-app "ba_qml" PREFIX "/" FILES ${BA_APP_QML})
    # Versiones fijadas de los agentes de IA (pestaña Asistente). Los prompts no: son archivos
    # de cada idioma (assets/idiomas/<código>/prompts/, ba::i18n::leer).
    qt_add_resources(ba-app "ba_agent" PREFIX "/" FILES agent/agentes.json agent/pi-guarda.mjs)
    # El catalogo de filtros LSP en castellano (Audio -> filtros).
    qt_add_resources(ba-app "ba_lsp_catalogo" PREFIX "/audio" BASE "${CMAKE_SOURCE_DIR}/datos"
                     FILES "${CMAKE_SOURCE_DIR}/datos/lsp-catalogo.json")
    # El escaner de red: las marcas de camaras con sus URLs tipicas y la base de
    # fabricantes por MAC (registro OUI de la IEEE). Van como archivos (data/red).
    install(FILES "${CMAKE_SOURCE_DIR}/datos/camaras-urls.json" "${CMAKE_SOURCE_DIR}/datos/oui.tsv"
            DESTINATION data/red COMPONENT bundle)
    # La documentacion en castellano de los modulos de GStreamer (Video -> agregar modulo).
    qt_add_resources(ba-app "ba_gst_modulos" PREFIX "/video" BASE "${CMAKE_SOURCE_DIR}/datos"
                     FILES "${CMAKE_SOURCE_DIR}/datos/gst-modulos.json" "${CMAKE_SOURCE_DIR}/datos/superres-catalogo.json")
    # Las reglas con las que el modelo local completa argumentos (agent/ArgumentosLocales) son
    # un archivo de cada idioma: assets/idiomas/<código>/voz-argumentos.txt.
    # Sus capturas (del codigo fuente de LSP, LGPL-3.0: tools/capturas/lsp-capturas.sh).
    file(GLOB _ba_lsp_capturas "${CMAKE_SOURCE_DIR}/assets/lsp-capturas/*.jpg")
    qt_add_resources(ba-app "ba_lsp_capturas" PREFIX "/lsp" BASE "${CMAKE_SOURCE_DIR}/assets/lsp-capturas"
                     FILES ${_ba_lsp_capturas})
    # Una captura chica de la página de cada categoría, para el selector de
    # herramientas de la Botonera (tools/capturas/capturas-botonera.py las saca de la app).
    file(GLOB _ba_bot_capturas "${CMAKE_SOURCE_DIR}/assets/botonera-capturas/*.jpg")
    if(_ba_bot_capturas)
        qt_add_resources(ba-app "ba_bot_capturas" PREFIX "/botonera" BASE "${CMAKE_SOURCE_DIR}/assets/botonera-capturas"
                         FILES ${_ba_bot_capturas})
    endif()
    install(FILES "${CMAKE_SOURCE_DIR}/assets/lsp-capturas/LEEME.txt" DESTINATION data/licencias
            RENAME lsp-capturas-LEEME.txt COMPONENT bundle)
    # Los modelos locales que se ofrecen bajar (los actualiza la fabrica al subir).
    qt_add_resources(ba-app "ba_modelos" PREFIX "/agent" BASE "${CMAKE_SOURCE_DIR}/datos"
                     FILES "${CMAKE_SOURCE_DIR}/datos/modelos-catalogo.json"
                           "${CMAKE_SOURCE_DIR}/datos/glosario-traduccion.json")
    # Las guías para preparar cada tipo de evento (MCP evento_escenario y «Evento nuevo» con
    # el asistente) son archivos del idioma: assets/idiomas/<c>/guias/escenarios, que
    # src/core/CMakeLists.txt instala con todo assets/idiomas (ba::i18n::leer).
    # El logo, para el icono de la ventana (tools/generar/icono.py los genera).
    set(_ba_iconos "")
    foreach(_lado 16 24 32 48 64 128 256 512)
        list(APPEND _ba_iconos "${CMAKE_SOURCE_DIR}/assets/icons/broadcast-appliance-${_lado}.png")
    endforeach()
    qt_add_resources(ba-app "ba_iconos" PREFIX "/icons" BASE "${CMAKE_SOURCE_DIR}/assets/icons" FILES ${_ba_iconos})
    target_link_libraries(ba-app PRIVATE ba_vterm)
    if(BA_HAVE_CLAP)
        target_include_directories(ba-app PRIVATE "${BA_CLAP_INC}")
        target_compile_definitions(ba-app PRIVATE BA_DEV_LSP_CLAP="${BA_LSP_CLAP}" BA_DEV_LSPDSP="${BA_LSPDSP_DIR}")
        target_include_directories(ba-app PRIVATE "${CMAKE_SOURCE_DIR}/src/lspdsp")
        # ClapEditor (las ventanas de los plugins) usa X11 en Linux; antes llegaba de rebote por libobs.
        if(UNIX AND NOT APPLE)
            find_package(X11 REQUIRED)
            target_link_libraries(ba-app PRIVATE X11::X11)
        endif()
        target_link_libraries(ba-app PRIVATE ${CMAKE_DL_LIBS})
    endif()
    if(NOT WIN32 AND NOT APPLE)
        target_link_libraries(ba-app PRIVATE util) # forkpty (en glibc >= 2.34 ya esta en libc)
    endif()
    # Fuente de la terminal (make deps-fonts, OFL).
    if(EXISTS "${CMAKE_SOURCE_DIR}/extern/fonts/JetBrainsMono-Regular.ttf")
        file(GLOB _ba_fonts "${CMAKE_SOURCE_DIR}/extern/fonts/*.ttf")
        install(FILES ${_ba_fonts} "${CMAKE_SOURCE_DIR}/extern/fonts/OFL.txt" DESTINATION data/fonts COMPONENT bundle)
    endif()
    # Las tipografías de texto (make deps-fuentes-texto): Liberation, Carlito,
    # Caladea, DejaVu, Noto, Alef, Rubik, Gentium y Linux Libertine. Son para los
    # gráficos y los rótulos: sin ellas todo dependería de las fuentes de la
    # máquina —lo que rompe la portabilidad: una escena diseñada acá se vería
    # distinta en el equipo de respaldo—.
    file(GLOB _ba_fuentes_texto "${CMAKE_SOURCE_DIR}/extern/fuentes-texto/*.ttf" "${CMAKE_SOURCE_DIR}/extern/fuentes-texto/*.otf")
    if(_ba_fuentes_texto)
        install(FILES ${_ba_fuentes_texto} DESTINATION data/fonts COMPONENT bundle)
        install(DIRECTORY "${CMAKE_SOURCE_DIR}/extern/fuentes-texto/licencias/" DESTINATION data/fonts/licencias
                COMPONENT bundle)
    else()
        ba_falta("Tipografías de texto" "no están en extern/fuentes-texto ('make deps-fuentes-texto')")
    endif()
    # WordCraft, DeckCraft y GridCraft (solo sus herramientas de línea de comandos) y sus licencias.
    if(_ba_craft_ok)
        foreach(_app wordcraft deckcraft gridcraft)
            install(PROGRAMS "${_ba_craft_dir}/${_app}-cli${_ba_craft_exe}" DESTINATION bin/craft COMPONENT bundle)
        endforeach()
        install(DIRECTORY "${_ba_craft_dir}/licencias/" DESTINATION bin/craft/licencias COMPONENT bundle)
        message(STATUS "ba-app: WordCraft, DeckCraft y GridCraft al bundle (${_ba_craft_dir})")
    endif()
    message(STATUS "ba-app: interfaz grafica con Qt ${Qt6_VERSION} vendoreado (${BA_QT_PREFIX})")
    # Test de la logica de disposicion del dashboard (ViewModel sin interfaz).
    add_executable(test_dashboard_model tests/test_dashboard_model.cpp)
    target_include_directories(test_dashboard_model PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_dashboard_model PRIVATE ba_app_ui ba_core Qt6::Core Qt6::Gui)
    set_target_properties(test_dashboard_model PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_dashboard_model COMMAND test_dashboard_model)
    # La lista que se actualiza en su lugar por clave (ui/ListaObjetos, sobre ui/ListaConClave): avisa lo justo.
    add_executable(test_lista_con_clave tests/test_lista_con_clave.cpp)
    target_include_directories(test_lista_con_clave PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_lista_con_clave PRIVATE ba_app_base Qt6::Core)
    set_target_properties(test_lista_con_clave PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_lista_con_clave COMMAND test_lista_con_clave)
    # La única búsqueda de los catálogos (ui/FiltroCatalogo).
    add_executable(test_filtro_catalogo tests/test_filtro_catalogo.cpp)
    target_include_directories(test_filtro_catalogo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_filtro_catalogo PRIVATE ba_app_base Qt6::Core)
    set_target_properties(test_filtro_catalogo PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_filtro_catalogo COMMAND test_filtro_catalogo)
    # El recinto: una fila por banca con lo de sus cinco fuentes ya cruzado.
    add_executable(test_recinto_model tests/test_recinto_model.cpp)
    target_include_directories(test_recinto_model PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_recinto_model PRIVATE ba_app_asamblea ba_app_base Qt6::Core)
    set_target_properties(test_recinto_model PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_recinto_model COMMAND test_recinto_model)
    # Los formularios por esquema: el tipo de cada campo y sus errores (ui/Formulario).
    add_executable(test_formulario tests/test_formulario.cpp)
    target_include_directories(test_formulario PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_formulario PRIVATE ba_app_ui Qt6::Core)
    set_target_properties(test_formulario PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_formulario COMMAND test_formulario)
    # La sección de Configuración que se pide (por su clave, un nombre viejo o su título).
    add_executable(test_secciones_config tests/test_secciones_config.cpp)
    target_include_directories(test_secciones_config PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_secciones_config PRIVATE ba_app_ui Qt6::Core)
    set_target_properties(test_secciones_config PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_secciones_config COMMAND test_secciones_config)
    # El registro de dispositivos de la máquina: nombres del operador, ausentes, persistencia.
    add_executable(test_dispositivos tests/test_dispositivos.cpp)
    target_include_directories(test_dispositivos PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_dispositivos PRIVATE ba_app_ui ba_app_equipos ba_core Qt6::Core)
    set_target_properties(test_dispositivos PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_dispositivos COMMAND test_dispositivos)
    # La cuarentena de drivers de punta a punta: la sonda en un proceso hijo (este mismo
    # test con --sonda-drivers) con drivers de mentira que andan, fallan, se cuelgan y se caen.
    add_executable(test_drivers tests/test_drivers.cpp)
    target_include_directories(test_drivers PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_drivers PRIVATE ba_app_equipos ba_core Qt6::Core)
    set_target_properties(test_drivers PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_drivers COMMAND test_drivers)
    # El catálogo de cosas que se pueden mostrar y los monitores (Salidas → Pantallas):
    # orden físico, ids estables, asignación, persistencia y espera, sin monitores reales.
    add_executable(test_pantallas_catalogo tests/test_pantallas_catalogo.cpp)
    target_include_directories(test_pantallas_catalogo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_pantallas_catalogo PRIVATE ba_app_pantallas ba_app_base ba_core Qt6::Core Qt6::Gui Qt6::Qml)
    set_target_properties(test_pantallas_catalogo PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_pantallas_catalogo COMMAND test_pantallas_catalogo)
    # Las distribuciones de pantallas (Pantallas locales → Distribuciones): relativas ↔
    # absolutas, monitor faltante, una ventana madre por monitor, aplicar idempotente,
    # capturar → aplicar igual, persistencia; con monitores de prueba.
    add_executable(test_pantallas_distribuciones tests/test_pantallas_distribuciones.cpp)
    target_include_directories(test_pantallas_distribuciones PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_pantallas_distribuciones PRIVATE ba_app_pantallas ba_app_base ba_core Qt6::Core Qt6::Gui Qt6::Qml)
    set_target_properties(test_pantallas_distribuciones PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_pantallas_distribuciones COMMAND test_pantallas_distribuciones)
    # Las pantallas de la red (Chromecast) y lo que muestran: lo guardado, la búsqueda, el
    # restaurado de lo fijado, los carruseles, las URLs y los canales de escena, sin red.
    add_executable(test_pantallas_destinos tests/test_pantallas_destinos.cpp)
    target_include_directories(test_pantallas_destinos PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_pantallas_destinos PRIVATE ba_app_pantallas nlohmann_json::nlohmann_json ba_core)
    add_test(NAME test_pantallas_destinos COMMAND test_pantallas_destinos)
    # Los instrumentos de video (forma de onda, vectorscopio, legalidad), sin GPU.
    add_executable(test_scopes tests/test_scopes.cpp)
    target_include_directories(test_scopes PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_scopes PRIVATE ba_app_video Qt6::Core Qt6::Gui)
    set_target_properties(test_scopes PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_scopes COMMAND test_scopes)

    # Servidor MCP: puente stdio (binario propio) y test del protocolo + HTTP.
    # Sin Qt y con el runtime de C++ estatico: solo necesita la libc (lo lanzan
    # clientes MCP externos, sin pasar por el launcher).
    add_executable(ba-mcp mcp/bridge/ba_mcp_main.cpp)
    target_link_libraries(ba-mcp PRIVATE ba_core)
    if(WIN32)
        target_link_libraries(ba-mcp PRIVATE ws2_32)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(ba-mcp PRIVATE -static-libstdc++ -static-libgcc)
    endif()
    install(TARGETS ba-mcp RUNTIME DESTINATION bin/64bit)
    # Control del escritorio de Windows (mouse, teclado, captura, programas…)
    # como un segundo servidor MCP para los asistentes. Sin Qt, como ba-mcp.
    add_executable(ba-control escritorio/ba_control_main.cpp escritorio/control_uia.cpp escritorio/control_chrome.cpp
                              escritorio/control_archivos.cpp)
    target_include_directories(ba-control PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(ba-control PRIVATE ba_core)
    if(WIN32)
        target_link_libraries(ba-control PRIVATE user32 gdi32 gdiplus shell32 shlwapi ole32 oleaut32 ws2_32 advapi32)
    elseif(APPLE)
        # El motor de macOS (las mismas herramientas system_*: PLAN-CONTROL-SISTEMA).
        target_sources(ba-control PRIVATE escritorio/control_mac.mm)
        set_source_files_properties(escritorio/control_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
        target_link_libraries(ba-control PRIVATE "-framework AppKit" "-framework ApplicationServices" "-framework Carbon"
                                                 "-framework CoreGraphics" "-framework ImageIO" "-framework IOKit"
                                                 "-framework UniformTypeIdentifiers" "-weak_framework ScreenCaptureKit")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # El motor de Linux: X11 se carga al usar (dlopen), no es una dependencia.
        target_sources(ba-control PRIVATE escritorio/control_linux.cpp)
        target_link_libraries(ba-control PRIVATE ${CMAKE_DL_LIBS})
        target_link_options(ba-control PRIVATE -static-libstdc++ -static-libgcc)
    endif()
    install(TARGETS ba-control RUNTIME DESTINATION bin/64bit)
    add_test(NAME test_ba_control_esquemas
             COMMAND ${CMAKE_COMMAND} -DBIN=$<TARGET_FILE:ba-control>
                     -DPEDIDO=${CMAKE_CURRENT_SOURCE_DIR}/tests/ba_control_tools_list.jsonl
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/ba_control_esquemas.cmake)
    add_executable(test_mcp tests/test_mcp.cpp)
    target_include_directories(test_mcp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/core/tests")
    target_link_libraries(test_mcp PRIVATE ba_app_mcp ba_core Qt6::Core Qt6::Network)
    # Dónde estaban al compilar el puente y el árbol fuente (el registro, docs/mcp.json, para la
    # prueba genérica de cada herramienta); BA_MCP_EXE y BA_FUENTES mandan (RutasPrueba.h).
    target_compile_definitions(test_mcp PRIVATE BA_MCP_PUENTE="$<TARGET_FILE:ba-mcp>"
                                                BA_PRUEBA_FUENTES="${CMAKE_SOURCE_DIR}")
    set_target_properties(test_mcp PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_dependencies(test_mcp ba-mcp)
    # El plano de control con la interfaz trabada (F3-03): MCP, alarmas y relojes, sin QML; y
    # las herramientas de la foto (McpFoto) contra un documento de aire de verdad.
    add_executable(test_plano_control tests/test_plano_control.cpp)
    # NivelesMotor lee motor/NivelAudio.hpp (sin el motor).
    target_include_directories(test_plano_control PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/motor")
    target_link_libraries(test_plano_control PRIVATE ba_app_ui ba_app_mcp ba_app_emision ba_app_control ba_app_agente ba_app_evento ba_core Qt6::Core Qt6::Network)
    set_target_properties(test_plano_control PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_plano_control COMMAND test_plano_control)
    # Las pastillas del aire (ui/EstadoAireViewModel): el tiempo de una grabación que ya
    # andaba y los avisos por grupo, con una ventanilla sintética.
    add_executable(test_estado_aire tests/test_estado_aire.cpp)
    target_include_directories(test_estado_aire PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_estado_aire PRIVATE ba_app_ui ba_app_emision ba_core Qt6::Core)
    set_target_properties(test_estado_aire PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_estado_aire COMMAND test_estado_aire)
    # Con 200 entradas/canales/invitados, una foto que cambia UNA cosa hace trabajo de una
    # (las trabas de 2-6 s del 2026-11-03): los tamaños de las entradas, el índice de canales,
    # el modelo de filas de la Matriz y los lugares de los rótulos.
    add_executable(test_trabajo_por_foto tests/test_trabajo_por_foto.cpp)
    target_include_directories(test_trabajo_por_foto PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_trabajo_por_foto PRIVATE ba_app_base ba_core Qt6::Core)
    set_target_properties(test_trabajo_por_foto PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_trabajo_por_foto COMMAND test_trabajo_por_foto)
    add_test(NAME test_mcp COMMAND test_mcp)
    # Los servidores que lanza la app (OpenCode, llama-server) mueren con ella.
    add_executable(test_hijos_app tests/test_hijos_app.cpp)
    target_include_directories(test_hijos_app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_hijos_app PRIVATE ba_app_agente ba_core Qt6::Core)
    set_target_properties(test_hijos_app PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_hijos_app COMMAND test_hijos_app)
    set_tests_properties(test_hijos_app PROPERTIES SKIP_RETURN_CODE 77)

    # El registro de herramientas (F3-02): docs/mcp.json y la tabla de MCP.md son lo que
    # sale del código, y toda herramienta declara su clase. Agregar una sin regenerar
    # (tools/docs/mcp-inventario.py --desde-codigo) rompe la suite.
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME test_mcp_registro COMMAND "${Python3_EXECUTABLE}" -B
                 "${CMAKE_SOURCE_DIR}/tools/docs/mcp-inventario.py" --verificar)
        ba_test_entradas(test_mcp_registro mcp agent/Roles.cpp "${CMAKE_SOURCE_DIR}/tools/docs"
                         "${CMAKE_SOURCE_DIR}/docs/mcp.json" "${CMAKE_SOURCE_DIR}/docs/funciones/MCP.md"
                         "${CMAKE_SOURCE_DIR}/assets/idiomas/es/ayuda")
    else()
        ba_falta("test_mcp_registro" "Python 3 (tools/docs/mcp-inventario.py --verificar)")
    endif()

    # El buscador de herramientas: que el indice encuentre lo que una persona
    # pediria con SUS palabras, y que ninguna herramienta quede sin etiquetar.
    add_executable(test_mcp_busqueda tests/test_mcp_busqueda.cpp)
    target_include_directories(test_mcp_busqueda PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_mcp_busqueda PRIVATE ba_app_mcp nlohmann_json::nlohmann_json)
    # El corpus de preguntas lo comparten este test y tools/entrenar/bateria-modelo.py.
    target_compile_definitions(test_mcp_busqueda PRIVATE
        BA_CORPUS_PREGUNTAS="${CMAKE_CURRENT_SOURCE_DIR}/tests/datos/preguntas.json"
        BA_STE_ES="${CMAKE_CURRENT_SOURCE_DIR}/../../datos/ste-es.json")
    add_test(NAME test_mcp_busqueda COMMAND test_mcp_busqueda)

    # YouTube: cliente OAuth + Data API contra un Google falso, y HTTPS real.
    add_executable(test_youtube tests/test_youtube.cpp)
    target_include_directories(test_youtube PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_youtube PRIVATE ba_app_cuentas ba_app_net ba_core Qt6::Core Qt6::Gui Qt6::Network mbedtls mbedx509 mbedcrypto)
    target_compile_definitions(test_youtube PRIVATE BA_TEST_CACERT="${CMAKE_SOURCE_DIR}/extern/cacert/cacert.pem")
    set_target_properties(test_youtube PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_youtube COMMAND test_youtube)

    # Lo común de YouTube, Twitch y Facebook: la fábrica de herramientas del MCP
    # (mcp/McpPlataformas) con tres plataformas falsas y un McpCore de verdad.
    add_executable(test_plataformas tests/test_plataformas.cpp)
    target_include_directories(test_plataformas PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_plataformas PRIVATE ba_app_ui ba_app_mcp ba_core Qt6::Core Qt6::Gui Qt6::Network)
    set_target_properties(test_plataformas PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_plataformas COMMAND test_plataformas)

    # El PTZ: protocolos byte a byte y cada backend contra una camara falsa
    # (VISCA UDP/TCP, Panasonic HTTP, ONVIF HTTP con WS-Security, WS-Discovery).
    add_executable(test_ptz tests/test_ptz.cpp)
    target_include_directories(test_ptz PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_ptz PRIVATE ba_app_equipos Qt6::Core Qt6::Network)
    set_target_properties(test_ptz PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_ptz COMMAND test_ptz)

    # La voz: Escucha con modelos de verdad (si estan; si no, se saltea).
    if(BA_HAVE_VOZ)
        add_executable(test_escucha tests/test_escucha.cpp)
        target_include_directories(test_escucha PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${BA_SHERPA_DIR}/include")
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_link_libraries(test_escucha PRIVATE ba_app_agente ba_app_audio ba_app_asamblea -Wl,--start-group ${BA_SHERPA_LIBS} -Wl,--end-group ba_core)
        else()
            target_link_libraries(test_escucha PRIVATE ba_app_agente ba_app_audio ba_app_asamblea ${BA_SHERPA_LIBS} ba_core)
        endif()
        add_test(NAME test_escucha COMMAND test_escucha)
        add_executable(test_habla tests/test_habla.cpp)
        target_include_directories(test_habla PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${BA_SHERPA_DIR}/include")
        target_compile_definitions(test_habla PRIVATE BA_TEST_TMP="${CMAKE_CURRENT_BINARY_DIR}")
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_link_libraries(test_habla PRIVATE ba_app_audio -Wl,--start-group ${BA_SHERPA_LIBS} -Wl,--end-group ba_core)
        else()
            target_link_libraries(test_habla PRIVATE ba_app_audio ${BA_SHERPA_LIBS} ba_core)
        endif()
        add_test(NAME test_habla COMMAND test_habla)
        set_tests_properties(test_habla PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
        set_tests_properties(test_escucha PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
        # Nemotron streaming con el modelo de verdad (BA_NEMOTRON_MODELO; si no, se saltea).
        add_executable(test_nemotron tests/test_nemotron.cpp)
        target_include_directories(test_nemotron PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${BA_SHERPA_DIR}/include")
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_link_libraries(test_nemotron PRIVATE ba_app_agente ba_app_audio ba_app_asamblea -Wl,--start-group ${BA_SHERPA_LIBS} -Wl,--end-group ba_core)
        else()
            target_link_libraries(test_nemotron PRIVATE ba_app_agente ba_app_audio ba_app_asamblea ${BA_SHERPA_LIBS} ba_core)
        endif()
        add_test(NAME test_nemotron COMMAND test_nemotron)
        set_tests_properties(test_nemotron PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)
    endif()
    # Los dos reconocedores (agent/Reconocedores) y el motor streaming con un modelo falso:
    # el lote, los márgenes del VAD, la carga por usos y la alerta. Sin sherpa-onnx.
    if(TARGET ba_app_audio) # sin los headers de CLAP no hay audio (ba_app_audio)
        add_executable(test_reconocedores tests/test_reconocedores.cpp)
        target_include_directories(test_reconocedores PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(test_reconocedores PRIVATE ba_app_agente ba_app_audio ba_app_asamblea ba_core)
        add_test(NAME test_reconocedores COMMAND test_reconocedores)
    endif()

    # Modelos locales: leer la API de Hugging Face y verificar lo bajado (sin red).
    # Las cuentas de la voz del teleprompter (agent/VozPrompterPlan).
    add_executable(test_voz_prompter tests/test_voz_prompter.cpp)
    target_include_directories(test_voz_prompter PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_voz_prompter PRIVATE ba_app_agente)
    add_test(NAME test_voz_prompter COMMAND test_voz_prompter)
    # Grabar lo que le llega a la tarjeta de salida (.wav, con partes).
    if(TARGET ba_app_audio) # sin los headers de CLAP no hay audio (ba_app_audio)
        add_executable(test_grabador_wav tests/test_grabador_wav.cpp)
        target_include_directories(test_grabador_wav PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(test_grabador_wav PRIVATE ba_app_audio)
        add_test(NAME test_grabador_wav COMMAND test_grabador_wav)
    endif()
    # Validar los argumentos que escribe el modelo local (agent/ArgumentosLocales).
    # El catálogo de módulos de GStreamer (con el GStreamer vendoreado).
    if(BA_HAVE_GST)
        add_executable(test_catalogo_gst tests/test_catalogo_gst.cpp ${CMAKE_SOURCE_DIR}/src/motor/Sondas.cpp)
        target_include_directories(test_catalogo_gst PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/motor")
        target_link_libraries(test_catalogo_gst PRIVATE ba_app_video ba_core ba::gstreamer)
        ba_gst_rpath(test_catalogo_gst "")
        target_compile_definitions(test_catalogo_gst PRIVATE BA_TEST_GST_RAIZ="${BA_GST_FW}/Versions/1.0"
            BA_TEST_GST_DOCS="${CMAKE_SOURCE_DIR}/datos/gst-modulos.json")
        add_test(NAME test_catalogo_gst COMMAND test_catalogo_gst)
        set_tests_properties(test_catalogo_gst PROPERTIES SKIP_RETURN_CODE 77)
        # F1-16: la placa de audio que se va y vuelve (entrada, escucha y Matriz), con un
        # proveedor de dispositivos falso: nada abre una placa real ni suena.
        add_executable(test_placa_que_se_va tests/test_placa_que_se_va.cpp)
        target_include_directories(test_placa_que_se_va PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(test_placa_que_se_va PRIVATE ba_app_agente ba_motor ba_core Qt6::Core)
        set_target_properties(test_placa_que_se_va PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
        ba_gst_rpath(test_placa_que_se_va "")
        target_compile_definitions(test_placa_que_se_va PRIVATE BA_TEST_GST_RAIZ="${BA_GST_FW}/Versions/1.0")
        add_test(NAME test_placa_que_se_va COMMAND test_placa_que_se_va)
        set_tests_properties(test_placa_que_se_va PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
        # El rack como elemento de GStreamer (barack).
        if(TARGET ba_app_audio) # sin los headers de CLAP no hay audio (ba_app_audio)
            add_executable(test_gst_rack tests/test_gst_rack.cpp ${CMAKE_SOURCE_DIR}/src/motor/Sondas.cpp)
            target_include_directories(test_gst_rack PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/motor")
            target_link_libraries(test_gst_rack PRIVATE ba_app_video ba_app_audio ba_core ba::gstreamer)
            ba_gst_rpath(test_gst_rack "")
            target_compile_definitions(test_gst_rack PRIVATE BA_TEST_GST_RAIZ="${BA_GST_FW}/Versions/1.0" BA_HAVE_GST=1)
            add_test(NAME test_gst_rack COMMAND test_gst_rack)
            set_tests_properties(test_gst_rack PROPERTIES SKIP_RETURN_CODE 77)
        endif()
        # Los medios de los gráficos animados (graficos/Medios): GIF e imágenes a
        # medios/, y videos (con alfa) a cuadros PNG con el GStreamer vendoreado.
        if(BA_APP_QT_OK)
            add_executable(test_graficos_medios tests/test_graficos_medios.cpp
 ${CMAKE_SOURCE_DIR}/src/motor/Sondas.cpp)
            set_target_properties(test_graficos_medios PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
            target_include_directories(test_graficos_medios PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/motor")
            target_link_libraries(test_graficos_medios PRIVATE ba_app_graficos ba_app_video ba_core ba::gstreamer Qt6::Core Qt6::Gui)
            ba_gst_rpath(test_graficos_medios "")
            target_compile_definitions(test_graficos_medios PRIVATE BA_HAVE_GST=1 BA_TEST_GST_RAIZ="${BA_GST_FW}/Versions/1.0")
            add_test(NAME test_graficos_medios COMMAND test_graficos_medios)
            set_tests_properties(test_graficos_medios PROPERTIES TIMEOUT 120)
        endif()
    endif()
    # El grafo de tarjetas de video (video/GrafoVideo), sin GPU.
    add_executable(test_grafo_video tests/test_grafo_video.cpp)
    target_include_directories(test_grafo_video PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_grafo_video PRIVATE ba_app_video ba_core)
    add_test(NAME test_grafo_video COMMAND test_grafo_video)
    # El resumen de la tarjeta de cada nodo (ui/ResumenNodo): nunca tira (a42d4a31).
    add_executable(test_resumen_nodo tests/test_resumen_nodo.cpp)
    target_include_directories(test_resumen_nodo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_resumen_nodo PRIVATE ba_app_ui ba_core Qt6::Core)
    add_test(NAME test_resumen_nodo COMMAND test_resumen_nodo)

    # F3-12: los tipos de antes al motor (inputs::alMotor): toda opción llega o se avisa.
    add_executable(test_al_motor tests/test_al_motor.cpp)
    target_include_directories(test_al_motor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_al_motor PRIVATE ba_app_emision ba_core ba_motor Qt6::Core) # InputSpecs → Camaras → ModosVideo (GStreamer)
    target_compile_definitions(test_al_motor PRIVATE BA_TEST_SRC="${CMAKE_SOURCE_DIR}")
    add_test(NAME test_al_motor COMMAND test_al_motor)

    # La medición de desfase entre dos entradas (datos sintéticos).
    add_executable(test_sincronia tests/test_sincronia.cpp)
    target_include_directories(test_sincronia PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_sincronia PRIVATE ba_app_video ba_core)
    add_test(NAME test_sincronia COMMAND test_sincronia)

    add_executable(test_diagnostico_video tests/test_diagnostico_video.cpp)
    target_include_directories(test_diagnostico_video PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_diagnostico_video PRIVATE ba_app_video ba_core)
    add_test(NAME test_diagnostico_video COMMAND test_diagnostico_video)

    # El escáner de red: la base OUI, la biblioteca de URLs y RTSP (sin red).
    add_executable(test_red tests/test_red.cpp)
    target_include_directories(test_red PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_red PRIVATE ba_app_equipos ba_core Qt6::Core)
    target_compile_definitions(test_red PRIVATE BA_TEST_DATOS="${CMAKE_SOURCE_DIR}/datos")
    add_test(NAME test_red COMMAND test_red)
    add_executable(test_argumentos_locales tests/test_argumentos_locales.cpp)
    target_include_directories(test_argumentos_locales PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_argumentos_locales PRIVATE ba_app_agente ba_core Qt6::Core)
    set_target_properties(test_argumentos_locales PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_argumentos_locales COMMAND test_argumentos_locales)
    add_executable(test_modelos_locales tests/test_modelos_locales.cpp)
    target_include_directories(test_modelos_locales PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_modelos_locales PRIVATE ba_app_agente ba_app_net ba_core Qt6::Core Qt6::Gui mbedtls mbedx509 mbedcrypto)
    target_compile_definitions(test_modelos_locales PRIVATE BA_TEST_TMP="${CMAKE_CURRENT_BINARY_DIR}"
        BA_TEST_CACERT="${CMAKE_SOURCE_DIR}/extern/cacert/cacert.pem")
    set_target_properties(test_modelos_locales PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    qt_add_resources(test_modelos_locales "ba_modelos_test" PREFIX "/agent" BASE "${CMAKE_SOURCE_DIR}/datos"
                     FILES "${CMAKE_SOURCE_DIR}/datos/modelos-catalogo.json")
    add_test(NAME test_modelos_locales COMMAND test_modelos_locales)

    # La cola unica de descargas: duplicados, por tipo, cancelar y seguir una
    # descarga cortada (contra un servidor local, sin red).
    add_executable(test_descargas tests/test_descargas.cpp)
    target_include_directories(test_descargas PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_descargas PRIVATE ba_app_net ba_core Qt6::Core Qt6::Network mbedtls mbedx509 mbedcrypto)
    set_target_properties(test_descargas PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_descargas COMMAND test_descargas)

    # Buscar la ultima version de los agentes en su fuente oficial (con red;
    # sin red se saltea).
    add_executable(test_agent_updates tests/test_agent_updates.cpp)
    target_include_directories(test_agent_updates PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_agent_updates PRIVATE ba_app_agente ba_app_net ba_core Qt6::Core mbedtls mbedx509 mbedcrypto)
    target_compile_definitions(test_agent_updates PRIVATE
        BA_TEST_CACERT="${CMAKE_SOURCE_DIR}/extern/cacert/cacert.pem"
        BA_AGENTES_JSON="${CMAKE_CURRENT_SOURCE_DIR}/agent/agentes.json"
        BA_TEST_TMP="${CMAKE_CURRENT_BINARY_DIR}")
    set_target_properties(test_agent_updates PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_agent_updates COMMAND test_agent_updates)
    set_tests_properties(test_agent_updates PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)

    # Twitch: login por codigo, Helix y chat IRC contra un Twitch falso.
    # Terminal de la pestaña Asistente (Pty real + libvterm). POSIX.
    if(NOT WIN32)
        add_executable(test_terminal tests/test_terminal.cpp)
        target_include_directories(test_terminal PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(test_terminal PRIVATE ba_app_term ba_vterm ba_core Qt6::Core Qt6::Gui)
        if(NOT APPLE)
            target_link_libraries(test_terminal PRIVATE util)
        endif()
        target_compile_definitions(test_terminal PRIVATE
            BA_TEST_TERM_DUMP="${CMAKE_CURRENT_SOURCE_DIR}/tests/datos/claude-code-122x35.bin")
        set_target_properties(test_terminal PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
        add_test(NAME test_terminal COMMAND test_terminal)


        set_tests_properties(test_terminal PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
    # Barra del asistente al operador.
    add_executable(test_operator_messages tests/test_operator_messages.cpp)
    target_include_directories(test_operator_messages PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_operator_messages PRIVATE ba_app_ui ba_core Qt6::Core)
    set_target_properties(test_operator_messages PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_operator_messages COMMAND test_operator_messages)
    # El guion del evento: tiempos y revision previa (correrlo es de la linea).
    # No depende de Qt ni del motor (dice QUE hacer, no lo hace), asi que
    # compila y corre en cualquier lado.
    add_executable(test_evento tests/test_evento.cpp)
    target_include_directories(test_evento PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_evento PRIVATE ba_app_evento nlohmann_json::nlohmann_json)
    add_test(NAME test_evento COMMAND test_evento)
    # La línea de tiempo v2: modelo, migración desde el v1, resolvedor (pronóstico,
    # hard starts, holds, estado en t, conflictos entre salidas), revisión, load to
    # time y retoma tras un corte; la fachada del guion de cues (proyectar/reconciliar),
    # editar en lote, y la corrida (el ejecutor: GO, HOLD, lo que entra solo, saltar,
    # adlibs, insertar, la retoma). Puro como el guion: sin Qt, sin motor, sin reloj.
    add_executable(test_linea tests/test_linea.cpp)
    target_include_directories(test_linea PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_linea PRIVATE ba_app_evento nlohmann_json::nlohmann_json)
    add_test(NAME test_linea COMMAND test_linea)
    # El asistente productor (L9): el solucionador del back-timing, las plantillas,
    # la línea desde una guía, el vigía del over/under y lo de las superficies web.
    add_executable(test_linea_productor tests/test_linea_productor.cpp)
    target_include_directories(test_linea_productor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_linea_productor PRIVATE ba_app_evento nlohmann_json::nlohmann_json)
    add_test(NAME test_linea_productor COMMAND test_linea_productor)
    # La línea lista para dibujar (escaleta, conducción, multipista, regla, estante y
    # el presupuesto de miniaturas): lo que ordena LineaViewModel, sin Qt ni motor.
    add_executable(test_linea_vista tests/test_linea_vista.cpp)
    target_include_directories(test_linea_vista PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_linea_vista PRIVATE ba_app_evento nlohmann_json::nlohmann_json)
    add_test(NAME test_linea_vista COMMAND test_linea_vista)

    # El puente con el renderizador de graficos: que un grafico colgado NO
    # cuelgue la app es la razon de ser del proceso aparte, asi que se prueba.
    if(BA_APP_QT_OK)
        add_executable(test_grafico_proceso tests/test_grafico_proceso.cpp)
        set_target_properties(test_grafico_proceso PROPERTIES AUTOMOC ON)
        target_include_directories(test_grafico_proceso PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(test_grafico_proceso PRIVATE ba_app_graficos Qt6::Core Qt6::Gui)
        add_test(NAME test_grafico_proceso COMMAND test_grafico_proceso)
        set_tests_properties(test_grafico_proceso PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
        # El catálogo de gráficos (graficos/Catalogo): lo mismo para el editor, el aire y el MCP.
        add_executable(test_graficos_catalogo tests/test_graficos_catalogo.cpp)
        target_include_directories(test_graficos_catalogo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(test_graficos_catalogo PRIVATE ba_app_graficos Qt6::Core)
        set_target_properties(test_graficos_catalogo PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
        add_test(NAME test_graficos_catalogo COMMAND test_graficos_catalogo)
        # El video en vivo dentro de un gráfico (graficos/VideoEnGrafico): la declaración,
        # la línea de tiempo y el paso al lienzo del motor, sin motor.
        add_executable(test_video_en_grafico tests/test_video_en_grafico.cpp)
        target_include_directories(test_video_en_grafico PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/motor")
        target_link_libraries(test_video_en_grafico PRIVATE ba_app_graficos Qt6::Core nlohmann_json::nlohmann_json)
        set_target_properties(test_video_en_grafico PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
        target_compile_definitions(test_video_en_grafico PRIVATE BA_TEST_GRAFICOS="${CMAKE_SOURCE_DIR}/assets/graficos")
        add_test(NAME test_video_en_grafico COMMAND test_video_en_grafico)
    endif()
    # Los gráficos de un plugin (@plugin.grafico): el probador del SDK corre el
    # ejemplo sin la app y verifica que cada gráfico describa o dibuje. Sobre una
    # copia, porque el plugin escribe en su carpeta datos/.
    if(BA_HAVE_PYTHON AND NOT WIN32)
        add_test(NAME preparar_plugin_grafico COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets/plugins/ejemplo-grafico" "${CMAKE_CURRENT_BINARY_DIR}/prueba-ejemplo-grafico")
        set_tests_properties(preparar_plugin_grafico PROPERTIES FIXTURES_SETUP plugin_grafico)
        add_test(NAME test_plugin_grafico COMMAND "${BA_PYTHON_DIR}/bin/python3" -I -B
            "${CMAKE_SOURCE_DIR}/src/plugins/sdk/ba_plugin/probar.py" "${CMAKE_CURRENT_BINARY_DIR}/prueba-ejemplo-grafico"
            --segundos 1)
        set_tests_properties(test_plugin_grafico PROPERTIES FIXTURES_REQUIRED plugin_grafico TIMEOUT 120)
    endif()

    # Medir el retardo entre labios y voz con un aplauso. C++ puro: recibe
    # mediciones ya hechas y devuelve el numero, asi se prueba con señales
    # inventadas sin pararse a aplaudir frente a una camara.
    add_executable(test_sincro_labios tests/test_sincro_labios.cpp)
    target_include_directories(test_sincro_labios PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_sincro_labios PRIVATE ba_app_equipos)
    add_test(NAME test_sincro_labios COMMAND test_sincro_labios)

    # La carpeta del evento: crear, guardar con respaldo, listar.
    add_executable(test_evento_store tests/test_evento_store.cpp)
    target_include_directories(test_evento_store PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_evento_store PRIVATE ba_app_evento nlohmann_json::nlohmann_json)
    add_test(NAME test_evento_store COMMAND test_evento_store)
    # La página web del evento en vivo: generar, no pisar, subdominio, estado público.
    add_executable(test_evento_web tests/test_evento_web.cpp)
    target_include_directories(test_evento_web PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_evento_web PRIVATE ba_app_evento nlohmann_json::nlohmann_json)
    target_compile_definitions(test_evento_web PRIVATE BA_PLANTILLAS_EVENTO="${CMAKE_SOURCE_DIR}/assets/web/evento/plantilla")
    add_test(NAME test_evento_web COMMAND test_evento_web)
    # El tema del evento (tema.json, Tema/ y la migración del sitio). La cuenta de colores
    # es del núcleo (tema/TemaWeb.cpp, en ba_core).
    add_executable(test_tema_evento tests/test_tema_evento.cpp)
    target_include_directories(test_tema_evento PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/core")
    target_link_libraries(test_tema_evento PRIVATE ba_app_evento nlohmann_json::nlohmann_json)
    add_test(NAME test_tema_evento COMMAND test_tema_evento)
    # El PNG de un logo SVG (QtSvg no pinta un SVG adentro del SVG del QR que dibuja la app).
    add_executable(test_tema_logo_png tests/test_tema_logo_png.cpp)
    target_include_directories(test_tema_logo_png PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/core")
    target_link_libraries(test_tema_logo_png PRIVATE ba_app_evento Qt6::Core Qt6::Gui Qt6::Svg nlohmann_json::nlohmann_json)
    set_target_properties(test_tema_logo_png PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_tema_logo_png COMMAND test_tema_logo_png)
    set_tests_properties(test_tema_logo_png PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    # Cada evento a su carpeta: la migración de las carpetas con varios eventos.
    add_executable(test_eventos_separar tests/test_eventos_separar.cpp)
    target_include_directories(test_eventos_separar PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_eventos_separar PRIVATE ba_app_ui ba_app_evento ba_core Qt6::Core nlohmann_json::nlohmann_json mbedtls mbedx509 mbedcrypto)
    set_target_properties(test_eventos_separar PROPERTIES AUTOMOC ON)
    add_test(NAME test_eventos_separar COMMAND test_eventos_separar)
    add_executable(test_bitacora tests/test_bitacora.cpp)
    target_include_directories(test_bitacora PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_bitacora PRIVATE ba_app_evento nlohmann_json::nlohmann_json)
    add_test(NAME test_bitacora COMMAND test_bitacora)
    add_executable(test_resolucion tests/test_resolucion.cpp ${CMAKE_SOURCE_DIR}/src/motor/ModosVideo.cpp)
    target_include_directories(test_resolucion PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/motor")
    target_link_libraries(test_resolucion PRIVATE ba_app_resolucion nlohmann_json::nlohmann_json ba::gstreamer)
    ba_gst_rpath(test_resolucion "")
    add_test(NAME test_resolucion COMMAND test_resolucion)
    add_executable(test_tiempos tests/test_tiempos.cpp)
    target_include_directories(test_tiempos PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_tiempos PRIVATE ba_app_tiempos nlohmann_json::nlohmann_json)
    add_test(NAME test_tiempos COMMAND test_tiempos)
    add_executable(test_fotos_config tests/test_fotos_config.cpp)
    target_include_directories(test_fotos_config PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_fotos_config PRIVATE ba_app_evento ba_core mbedcrypto nlohmann_json::nlohmann_json)
    add_test(NAME test_fotos_config COMMAND test_fotos_config)
    add_executable(test_cierre_evento tests/test_cierre_evento.cpp)
    target_include_directories(test_cierre_evento PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_cierre_evento PRIVATE ba_app_evento ba_core mbedcrypto nlohmann_json::nlohmann_json)
    add_test(NAME test_cierre_evento COMMAND test_cierre_evento)
    add_executable(test_historico tests/test_historico.cpp)
    target_include_directories(test_historico PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_historico PRIVATE ba_app_evento nlohmann_json::nlohmann_json)
    add_test(NAME test_historico COMMAND test_historico)
    # La API json del histórico (la página y el MCP), con eventos de otra configuración.
    add_executable(test_historico_consulta tests/test_historico_consulta.cpp)
    target_include_directories(test_historico_consulta PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_historico_consulta PRIVATE ba_app_ui ba_app_evento ba_core mbedcrypto Qt6::Core nlohmann_json::nlohmann_json)
    set_target_properties(test_historico_consulta PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_historico_consulta COMMAND test_historico_consulta)
    # La lista de eventos: una caché que avisa (leer no va al disco; lo nuevo aparece solo).
    add_executable(test_eventos_lista tests/test_eventos_lista.cpp)
    target_include_directories(test_eventos_lista PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_eventos_lista PRIVATE ba_app_ui ba_app_evento ba_core Qt6::Core nlohmann_json::nlohmann_json)
    set_target_properties(test_eventos_lista PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_eventos_lista COMMAND test_eventos_lista)
    # Los tipos de cue: una sola lista para el formulario y para guion_cue.
    add_executable(test_tipos_de_cue tests/test_tipos_de_cue.cpp)
    target_include_directories(test_tipos_de_cue PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_tipos_de_cue PRIVATE ba_app_evento nlohmann_json::nlohmann_json)
    add_test(NAME test_tipos_de_cue COMMAND test_tipos_de_cue)

    # El padron: quien viene, con que papel, por donde y si llego.
    add_executable(test_padron tests/test_padron.cpp)
    target_include_directories(test_padron PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_padron PRIVATE ba_app_evento nlohmann_json::nlohmann_json)
    add_test(NAME test_padron COMMAND test_padron)

    # La atención a quien se conecta: quién atiende, hablando de a uno, cuándo termina, el nombre que cuenta.
    add_executable(test_atencion tests/test_atencion.cpp)
    target_include_directories(test_atencion PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_atencion PRIVATE ba_app_sala nlohmann_json::nlohmann_json)
    add_test(NAME test_atencion COMMAND test_atencion)

    # Menos resolución a los invitados con la máquina caliente (F4-05), con una sala falsa.
    add_executable(test_calidad_calor tests/test_calidad_calor.cpp)
    target_include_directories(test_calidad_calor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_calidad_calor PRIVATE ba_app_sala)
    add_test(NAME test_calidad_calor COMMAND test_calidad_calor)

    # Quién mira el programa de cada salida: el retorno se le pide al motor solo al cambiar,
    # con la gracia vencida por un reloj de una vez (sin sondeo).
    add_executable(test_quien_mira tests/test_quien_mira.cpp)
    target_include_directories(test_quien_mira PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_quien_mira PRIVATE ba_app_sala Qt6::Core)
    set_target_properties(test_quien_mira PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_quien_mira COMMAND test_quien_mira)

    # Qué cámaras de remotos están al aire o armadas (la capa alta), de una foto del motor falsa.
    add_executable(test_capas_remotos tests/test_capas_remotos.cpp)
    target_include_directories(test_capas_remotos PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_capas_remotos PRIVATE ba_app_sala nlohmann_json::nlohmann_json Qt6::Core)
    add_test(NAME test_capas_remotos COMMAND test_capas_remotos)

    # El registro por tótem: la configuración (registro.json) y el guion hablado con sus normalizadores.
    add_executable(test_registro_totems tests/test_registro_totems.cpp)
    target_include_directories(test_registro_totems PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_registro_totems PRIVATE ba_app_agente nlohmann_json::nlohmann_json)
    add_test(NAME test_registro_totems COMMAND test_registro_totems)
    add_executable(test_registro_conversacion tests/test_registro_conversacion.cpp)
    target_include_directories(test_registro_conversacion PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    # ba_core: los tipos de documento (documentos/Documento, el DNI) del paso «documento».
    target_link_libraries(test_registro_conversacion PRIVATE ba_app_agente nlohmann_json::nlohmann_json ba_core)
    add_test(NAME test_registro_conversacion COMMAND test_registro_conversacion)
    # El ritmo de la conversación (fin del turno, esperas, silencio), con la hora a mano.
    add_executable(test_registro_ritmo tests/test_registro_ritmo.cpp)
    target_include_directories(test_registro_ritmo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_registro_ritmo PRIVATE ba_app_agente)
    add_test(NAME test_registro_ritmo COMMAND test_registro_ritmo)
    # Las conexiones en vivo (la conversación de cada una, su tic y la atención), con Puertos de mentira.
    add_executable(test_registro_conexiones tests/test_registro_conexiones.cpp)
    target_include_directories(test_registro_conexiones PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_registro_conexiones PRIVATE ba_app_agente nlohmann_json::nlohmann_json ba_core)
    add_test(NAME test_registro_conexiones COMMAND test_registro_conexiones)
    # Quién es cada cara (las reglas de Personas), con una libreta en memoria. La libreta es
    # de ba_core solo con el reconocimiento (BA_HAVE_ROSTROS).
    get_target_property(_ba_core_defs ba_core INTERFACE_COMPILE_DEFINITIONS)
    if(_ba_core_defs MATCHES "BA_HAVE_ROSTROS")
        add_executable(test_rostros_quien_es tests/test_rostros_quien_es.cpp)
        target_include_directories(test_rostros_quien_es PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(test_rostros_quien_es PRIVATE ba_app_rostros nlohmann_json::nlohmann_json ba_core)
        add_test(NAME test_rostros_quien_es COMMAND test_rostros_quien_es)
    endif()
    # La conversación atendida por una persona (la atención remota del registro).
    add_executable(test_registro_atencion tests/test_registro_atencion.cpp)
    target_include_directories(test_registro_atencion PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_registro_atencion PRIVATE ba_app_agente nlohmann_json::nlohmann_json ba_core) # ba_core: el paso «documento»
    add_test(NAME test_registro_atencion COMMAND test_registro_atencion)
    # El modelo que entiende en el tótem (iniciativa mixta): frases, streaming, herramientas y turnos, sin red.
    add_executable(test_registro_agente tests/test_registro_agente.cpp)
    target_include_directories(test_registro_agente PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_registro_agente PRIVATE ba_app_agente nlohmann_json::nlohmann_json ba_core)
    add_test(NAME test_registro_agente COMMAND test_registro_agente)
    # El ciclo entero contra un servidor de chat-completions falso (streaming, tiempos, la cola).
    add_executable(test_registro_agente_totem tests/test_registro_agente_totem.cpp)
    target_include_directories(test_registro_agente_totem PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_registro_agente_totem PRIVATE ba_app_agente ba_app_net ba_core Qt6::Core Qt6::Network mbedtls mbedx509 mbedcrypto
                          nlohmann_json::nlohmann_json)
    set_target_properties(test_registro_agente_totem PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_registro_agente_totem COMMAND test_registro_agente_totem)
    # El dorso del documento leído por el modelo propio: el pedido y la validación de lo que contesta.
    add_executable(test_documento_dorso tests/test_documento_dorso.cpp)
    target_include_directories(test_documento_dorso PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_documento_dorso PRIVATE ba_app_agente nlohmann_json::nlohmann_json ba_core)
    add_test(NAME test_documento_dorso COMMAND test_documento_dorso)

    # Las reglas de la sala sin red: errores de la API, la prueba técnica y el proxy.
    add_executable(test_reglas_sala tests/test_reglas_sala.cpp)
    target_include_directories(test_reglas_sala PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_reglas_sala PRIVATE ba_app_sala nlohmann_json::nlohmann_json)
    add_test(NAME test_reglas_sala COMMAND test_reglas_sala)

    # La grilla de personas: reparto, lugares estables y disposición (C++ puro, sin video).
    add_executable(test_grilla_personas tests/test_grilla_personas.cpp)
    target_include_directories(test_grilla_personas PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_grilla_personas PRIVATE ba_app_escenas nlohmann_json::nlohmann_json)
    add_test(NAME test_grilla_personas COMMAND test_grilla_personas)

    # El carrusel de la escena: la tira, el salto y las etiquetas.
    add_executable(test_carrusel tests/test_carrusel.cpp)
    target_include_directories(test_carrusel PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_carrusel PRIVATE ba_app_carrusel nlohmann_json::nlohmann_json ba_core)
    add_test(NAME test_carrusel COMMAND test_carrusel)

    # De un .pptx a láminas, corriendo el DeckCraft vendoreado de verdad.
    add_executable(test_conversion tests/test_conversion.cpp)
    target_include_directories(test_conversion PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_conversion PRIVATE ba_app_carrusel ba_core Qt6::Core Qt6::Gui Qt6::Pdf)
    target_compile_definitions(test_conversion PRIVATE
        BA_TEST_DATOS="${CMAKE_CURRENT_SOURCE_DIR}/tests/datos")
    if(_ba_craft_ok)
        target_compile_definitions(test_conversion PRIVATE BA_CRAFT_DIR="${_ba_craft_dir}")
    endif()
    set_target_properties(test_conversion PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_conversion COMMAND test_conversion)

    # Lo que se publica, a PDF (evento/APdf): texto en varios idiomas, Markdown, imagen,
    # y con WordCraft, DeckCraft y GridCraft, Word, HTML, .pptx y planillas. Cada PDF se vuelve a leer.
    add_executable(test_apdf tests/test_apdf.cpp)
    target_include_directories(test_apdf PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_apdf PRIVATE ba_app_carrusel ba_app_evento ba_core Qt6::Core Qt6::Gui Qt6::Pdf)
    target_compile_definitions(test_apdf PRIVATE BA_TEST_DATOS="${CMAKE_CURRENT_SOURCE_DIR}/tests/datos")
    if(_ba_craft_ok)
        target_compile_definitions(test_apdf PRIVATE BA_CRAFT_DIR="${_ba_craft_dir}")
    endif()
    set_target_properties(test_apdf PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_apdf COMMAND test_apdf)

    # Que el Qt vendoreado DECODIFIQUE de verdad. Ver el comentario del .cpp:
    # si el plugin de ffmpeg no viaja, no falla al compilar ni al arrancar,
    # falla cuando alguien hace doble clic en un video en vivo.
    add_executable(test_multimedia tests/test_multimedia.cpp)
    target_link_libraries(test_multimedia PRIVATE Qt6::Core Qt6::Multimedia)
    target_compile_definitions(test_multimedia PRIVATE
        BA_TEST_DATOS="${CMAKE_CURRENT_SOURCE_DIR}/tests/datos")
    set_target_properties(test_multimedia PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_multimedia COMMAND test_multimedia)
    set_tests_properties(test_multimedia PROPERTIES
        TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${BA_QT_PREFIX}/plugins")
    set_tests_properties(test_conversion PROPERTIES TIMEOUT 400)

    # Los archivos del operador: tipos, busqueda y no salirse de la carpeta.
    add_executable(test_medios tests/test_medios.cpp)
    target_include_directories(test_medios PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_medios PRIVATE ba_app_medios Qt6::Core)
    add_test(NAME test_medios COMMAND test_medios)

    # Analizador (LUFS, tercios, piso de ruido) y generador de prueba.
    if(BA_HAVE_CLAP)
        add_executable(test_audio_dsp tests/test_audio_dsp.cpp)
        target_include_directories(test_audio_dsp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(test_audio_dsp PRIVATE ba_app_audio nlohmann_json::nlohmann_json)
        add_test(NAME test_audio_dsp COMMAND test_audio_dsp)

        # ba-lspdsp (el DSP de LSP) contra señales conocidas, cargada como la carga la app.
        add_executable(test_lspdsp tests/test_lspdsp.cpp)
        target_include_directories(test_lspdsp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/lspdsp")
        target_compile_definitions(test_lspdsp PRIVATE BA_DEV_LSPDSP="${BA_LSPDSP_DIR}")
        target_link_libraries(test_lspdsp PRIVATE ba_app_audio ba_core ${CMAKE_DL_LIBS})
        add_test(NAME test_lspdsp COMMAND test_lspdsp)
        set_tests_properties(test_lspdsp PROPERTIES SKIP_RETURN_CODE 77)
        add_executable(test_calibracion tests/test_calibracion.cpp)
        target_include_directories(test_calibracion PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/lspdsp")
        target_compile_definitions(test_calibracion PRIVATE BA_DEV_LSPDSP="${BA_LSPDSP_DIR}")
        target_link_libraries(test_calibracion PRIVATE ba_app_audio ba_core Qt6::Core ${CMAKE_DL_LIBS})
        add_test(NAME test_calibracion COMMAND test_calibracion)
        set_tests_properties(test_calibracion PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    # Host CLAP contra los plugins LSP reales (sin interfaz grafica). Lo que
    # pide ClapEditor.cpp para enlazar: X11 (Linux) o Qt Gui (macOS, QWindow).
    set(_ba_clap_ui_libs "")
    if(APPLE)
        if(TARGET Qt6::Gui)
            set(_ba_clap_ui_libs Qt6::Gui)
        endif()
    elseif(NOT WIN32)
        find_package(X11 REQUIRED)
        set(_ba_clap_ui_libs X11::X11 ${CMAKE_DL_LIBS})
    endif()
    if(BA_HAVE_CLAP)
        add_executable(test_clap_host tests/test_clap_host.cpp)
        target_include_directories(test_clap_host PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${BA_CLAP_INC}")
        target_compile_definitions(test_clap_host PRIVATE BA_TEST_LSP_CLAP="${BA_LSP_CLAP}" _USE_MATH_DEFINES)
        # ba_core: el modo seguro (fault/ModoSeguro) decide si se carga un CLAP de terceros.
        target_link_libraries(test_clap_host PRIVATE ba_app_audio ba_core ${_ba_clap_ui_libs})
        if(APPLE)
            set_target_properties(test_clap_host PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
        elseif(NOT WIN32)
            set_target_properties(test_clap_host PROPERTIES BUILD_RPATH "${BA_TERCEROS}/lib")
        endif()
        add_test(NAME test_clap_host COMMAND test_clap_host)
        set_tests_properties(test_clap_host PROPERTIES SKIP_RETURN_CODE 77)
        # Herramienta (no test, solo Mac): captura la interfaz de un plugin CLAP
        # (las capturas y los iconos del Matrix Mixer: tools/docs/lsp-matrix-mixer-doc.py).
        if(APPLE)
            add_executable(capturar_lsp tools/capturar_lsp.cpp tools/capturar_vista_mac.mm)
            target_include_directories(capturar_lsp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${BA_CLAP_INC}")
            target_compile_definitions(capturar_lsp PRIVATE _USE_MATH_DEFINES)
            target_link_libraries(capturar_lsp PRIVATE ba_app_audio ba_core ${_ba_clap_ui_libs} Qt6::Gui "-framework AppKit")
            set_target_properties(capturar_lsp PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
        endif()
        # El Matrix Mixer (nuestro modulo LSP, third_party/lsp-plugins-matrix-mixer) con audio.
        add_executable(test_matrix_mixer tests/test_matrix_mixer.cpp)
        target_include_directories(test_matrix_mixer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${BA_CLAP_INC}")
        target_compile_definitions(test_matrix_mixer PRIVATE BA_TEST_LSP_CLAP="${BA_LSP_CLAP}")
        target_link_libraries(test_matrix_mixer PRIVATE ba_app_audio ba_core ${_ba_clap_ui_libs})
        if(APPLE)
            set_target_properties(test_matrix_mixer PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
        elseif(NOT WIN32)
            set_target_properties(test_matrix_mixer PROPERTIES BUILD_RPATH "${BA_TERCEROS}/lib")
        endif()
        add_test(NAME test_matrix_mixer COMMAND test_matrix_mixer)
        set_tests_properties(test_matrix_mixer PROPERTIES SKIP_RETURN_CODE 77)
        # El anillo estéreo sin candado de la Matriz y los racks (F3-08): dos hilos y deriva sintética.
        add_executable(test_ring_estereo tests/test_ring_estereo.cpp)
        target_include_directories(test_ring_estereo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME test_ring_estereo COMMAND test_ring_estereo)
        add_executable(test_audio_rack tests/test_audio_rack.cpp)
        target_include_directories(test_audio_rack PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${BA_CLAP_INC}")
        target_compile_definitions(test_audio_rack PRIVATE BA_TEST_LSP_CLAP="${BA_LSP_CLAP}" _USE_MATH_DEFINES)
        target_link_libraries(test_audio_rack PRIVATE ba_app_audio ba_core nlohmann_json::nlohmann_json)
        target_link_libraries(test_audio_rack PRIVATE ${_ba_clap_ui_libs})
        if(APPLE)
            set_target_properties(test_audio_rack PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
        endif()
        add_executable(test_audio_modules tests/test_audio_modules.cpp)
        target_include_directories(test_audio_modules PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${BA_CLAP_INC}")
        target_compile_definitions(test_audio_modules PRIVATE BA_TEST_LSP_CLAP="${BA_LSP_CLAP}")
        target_link_libraries(test_audio_modules PRIVATE ba_app_audio ba_core nlohmann_json::nlohmann_json)
        target_link_libraries(test_audio_modules PRIVATE ${_ba_clap_ui_libs})
        if(APPLE)
            set_target_properties(test_audio_modules PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
        endif()
        add_test(NAME test_audio_modules COMMAND test_audio_modules)
        set_tests_properties(test_audio_modules PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)
        add_test(NAME test_audio_rack COMMAND test_audio_rack)
        set_tests_properties(test_audio_rack PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)
        # La latencia del rack con un plugin CLAP sintético (atrasa 256 muestras y lo declara).
        add_library(ba_clap_retardo256 MODULE tests/clap_retardo256.cpp)
        target_include_directories(ba_clap_retardo256 PRIVATE "${BA_CLAP_INC}")
        set_target_properties(ba_clap_retardo256 PROPERTIES PREFIX "" SUFFIX ".clap")
        add_executable(test_rack_latencia tests/test_rack_latencia.cpp)
        target_include_directories(test_rack_latencia PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${BA_CLAP_INC}")
        target_compile_definitions(test_rack_latencia PRIVATE BA_TEST_CLAP_RETARDO="$<TARGET_FILE:ba_clap_retardo256>"
                                                              _USE_MATH_DEFINES)
        target_link_libraries(test_rack_latencia PRIVATE ba_app_audio ba_core nlohmann_json::nlohmann_json ${_ba_clap_ui_libs})
        add_dependencies(test_rack_latencia ba_clap_retardo256)
        if(APPLE)
            set_target_properties(test_rack_latencia PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
        endif()
        add_test(NAME test_rack_latencia COMMAND test_rack_latencia)
        set_tests_properties(test_rack_latencia PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    # Recepcion NDI abierta: descubrimiento + decodificador SpeedHQ del bundle.
    if(BA_HAVE_NDI)
        add_executable(test_ndi tests/test_ndi.cpp ${CMAKE_SOURCE_DIR}/src/motor/Sondas.cpp)
        target_include_directories(test_ndi PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/motor")
        target_link_libraries(test_ndi PRIVATE ba_app_video ba_app_emision ba_core ba_noidea ba::gstreamer)
        target_compile_definitions(test_ndi PRIVATE BA_HAVE_GST=1 BA_TEST_GST_RAIZ="${BA_GST_FW}/Versions/1.0")
        if(COMMAND ba_gst_rpath)
            ba_gst_rpath(test_ndi "")
        endif()
        add_test(NAME test_ndi COMMAND test_ndi)
        set_tests_properties(test_ndi PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    # Guias para conectar YouTube/Twitch/Facebook (misma fuente que el MCP).
    add_executable(test_account_guides tests/test_account_guides.cpp)
    target_include_directories(test_account_guides PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_account_guides PRIVATE ba_app_net ba_core Qt6::Core)
    set_target_properties(test_account_guides PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_account_guides COMMAND test_account_guides)
    # Disparadores (alertas) con entradas y reloj falsos.
    add_executable(test_cuerpo tests/test_cuerpo.cpp)
    target_include_directories(test_cuerpo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_cuerpo PRIVATE ba_app_asamblea ba_core)
    add_test(NAME test_cuerpo COMMAND test_cuerpo)
    # Bus de eventos y botonera (superficie de control) con ganchos falsos.
    add_executable(test_botonera tests/test_botonera.cpp)
    target_include_directories(test_botonera PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_botonera PRIVATE ba_app_control ba_core Qt6::Core)
    set_target_properties(test_botonera PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_botonera COMMAND test_botonera)
    # El contrato único de suscripción de las vistas (comun/UsoPorVistas): cuenta por vista.
    add_executable(test_uso_por_vistas tests/test_uso_por_vistas.cpp)
    target_include_directories(test_uso_por_vistas PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_uso_por_vistas PRIVATE ba_app_base Qt6::Core)
    set_target_properties(test_uso_por_vistas PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_uso_por_vistas COMMAND test_uso_por_vistas)
    # Los vúmetros a pedido (ui/RelojNiveles): solo con alguien mirando y algo que suene.
    add_executable(test_reloj_niveles tests/test_reloj_niveles.cpp)
    target_include_directories(test_reloj_niveles PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/motor")
    target_link_libraries(test_reloj_niveles PRIVATE ba_app_ui ba_app_emision Qt6::Core nlohmann_json::nlohmann_json)
    set_target_properties(test_reloj_niveles PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_reloj_niveles COMMAND test_reloj_niveles)
    # Los oradores: la regla del reloj, el texto, la lectura de tiempos y la fila (lo que
    # comparten la interfaz y el MCP; sala::OradoresSala, sin sala).
    add_executable(test_oradores_sala tests/test_oradores_sala.cpp)
    target_include_directories(test_oradores_sala PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_oradores_sala PRIVATE ba_app_sala ba_core Qt6::Core)
    set_target_properties(test_oradores_sala PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_oradores_sala COMMAND test_oradores_sala)
    # La prueba de sala: la guardia, quién es de prueba (fuera de lo que cuenta) y el informe.
    add_executable(test_prueba_sala_reglas tests/test_prueba_sala_reglas.cpp)
    target_include_directories(test_prueba_sala_reglas PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_prueba_sala_reglas PRIVATE ba_app_sala ba_core Qt6::Core)
    set_target_properties(test_prueba_sala_reglas PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_prueba_sala_reglas COMMAND test_prueba_sala_reglas)
    # El disco de las pruebas de sala y de asamblea (en el pool) y cortar el probador sin esperarlo.
    add_executable(test_archivos_prueba tests/test_archivos_prueba.cpp)
    target_include_directories(test_archivos_prueba PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_archivos_prueba PRIVATE ba_app_sala ba_core Qt6::Core)
    set_target_properties(test_archivos_prueba PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_archivos_prueba COMMAND test_archivos_prueba)
    # El enlace: cuánto necesita el evento (contra las mediciones de la prueba de sala) y el
    # veredicto con lo medido por el túnel (sala::enlace, sin la sala).
    add_executable(test_enlace_necesita tests/test_enlace_necesita.cpp)
    target_include_directories(test_enlace_necesita PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_enlace_necesita PRIVATE ba_app_sala ba_core Qt6::Core)
    set_target_properties(test_enlace_necesita PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_enlace_necesita COMMAND test_enlace_necesita)
    # La capacidad (docs/funciones/CAPACIDAD.md): los escalones, el juicio con datos
    # sintéticos, la guardia, el resultado y el código del que recibe (sala::capacidad).
    add_executable(test_capacidad_reglas tests/test_capacidad_reglas.cpp)
    target_include_directories(test_capacidad_reglas PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_capacidad_reglas PRIVATE ba_app_sala ba_core Qt6::Core)
    set_target_properties(test_capacidad_reglas PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_capacidad_reglas COMMAND test_capacidad_reglas)
    # El modelo de la pestaña Capacidad con una sala, un generador y una red de mentira.
    add_executable(test_capacidad_vm tests/test_capacidad_vm.cpp)
    target_include_directories(test_capacidad_vm PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_capacidad_vm PRIVATE ba_app_ui ba_app_sala ba_core Qt6::Core)
    set_target_properties(test_capacidad_vm PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_capacidad_vm COMMAND test_capacidad_vm)
    # El Reloj de las vistas (corre solo con lectores) y las fotos de los conectados.
    add_executable(test_reloj_vistas tests/test_reloj_vistas.cpp)
    target_include_directories(test_reloj_vistas PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_reloj_vistas PRIVATE ba_app_ui Qt6::Core)
    set_target_properties(test_reloj_vistas PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_reloj_vistas COMMAND test_reloj_vistas)
    # Un hilo del chat de la sala como modelo filtrado (sin copiar ni contador).
    add_executable(test_chat_hilo tests/test_chat_hilo.cpp)
    target_include_directories(test_chat_hilo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_chat_hilo PRIVATE ba_app_ui Qt6::Core Qt6::Gui)
    set_target_properties(test_chat_hilo PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_chat_hilo COMMAND test_chat_hilo)
    # El flanco de subida (el PIN del día al arrancar el vivo, T47).
    add_executable(test_flanco tests/test_flanco.cpp ui/Flanco.hpp)
    target_include_directories(test_flanco PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_flanco PRIVATE Qt6::Core)
    set_target_properties(test_flanco PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_flanco COMMAND test_flanco)
    # El nombre del evento abierto: el del vínculo {evento.nombre} de los gráficos.
    add_executable(test_evento_abierto tests/test_evento_abierto.cpp)
    target_include_directories(test_evento_abierto PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_evento_abierto PRIVATE ba_app_ui ba_app_evento ba_core Qt6::Core nlohmann_json::nlohmann_json)
    set_target_properties(test_evento_abierto PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_evento_abierto COMMAND test_evento_abierto)
    # Un solo contrato de errores: las acciones avisan; el MCP y las pruebas los capturan.
    add_executable(test_captura_error tests/test_captura_error.cpp)
    target_include_directories(test_captura_error PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_captura_error PRIVATE ba_app_base Qt6::Core)
    set_target_properties(test_captura_error PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_captura_error COMMAND test_captura_error)
    # El control web (dashboard /control/): emparejar, perfiles, llamar, confirmar y mirar
    # contra un McpCore de prueba (docs/planes/PLAN-WEB-CONTROL.md §2).
    add_executable(test_control_web tests/test_control_web.cpp)
    target_include_directories(test_control_web PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_control_web PRIVATE ba_app_mcp ba_app_control ba_core Qt6::Core)
    set_target_properties(test_control_web PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_control_web COMMAND test_control_web)
    # El tablero web (/control/app/): la réplica del modelo, la app y el navegador hablando
    # por JSON sin red (control/ReplicaPublicador + src/tablero-web/ClienteReplica).
    add_executable(test_replica tests/test_replica.cpp
 ui/ProveedorCuadros.hpp
                   ${CMAKE_SOURCE_DIR}/src/tablero-web/ObjetoReplica.cpp ${CMAKE_SOURCE_DIR}/src/tablero-web/ObjetoReplica.hpp
                   ${CMAKE_SOURCE_DIR}/src/tablero-web/ClienteReplica.cpp ${CMAKE_SOURCE_DIR}/src/tablero-web/ClienteReplica.hpp)
    target_include_directories(test_replica PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/tablero-web")
    target_link_libraries(test_replica PRIVATE ba_app_control ba_core Qt6::Core Qt6::Gui Qt6::Qml Qt6::QmlPrivate)
    set_target_properties(test_replica PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_replica COMMAND test_replica)
    add_executable(test_seguir_microfono tests/test_seguir_microfono.cpp)
    target_include_directories(test_seguir_microfono PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_seguir_microfono PRIVATE ba_app_control ba_app_asamblea ba_app_sala ba_core Qt6::Core)
    set_target_properties(test_seguir_microfono PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_seguir_microfono COMMAND test_seguir_microfono)
    add_executable(test_sesion_remota tests/test_sesion_remota.cpp
                   "${CMAKE_SOURCE_DIR}/third_party/qrcodegen/qrcodegen.cpp")
    target_include_directories(test_sesion_remota PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/third_party/qrcodegen")
    target_link_libraries(test_sesion_remota PRIVATE ba_app_ui ba_app_asamblea ba_core Qt6::Core)
    set_target_properties(test_sesion_remota PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_sesion_remota COMMAND test_sesion_remota)
    # La prueba de asamblea: no toca un padrón real, los de prueba marcados, su acceso al
    # gateway, el archivo del probador y la limpieza (asamblea/PruebaAsambleaReglas).
    add_executable(test_prueba_asamblea_reglas tests/test_prueba_asamblea_reglas.cpp
 "${CMAKE_SOURCE_DIR}/third_party/qrcodegen/qrcodegen.cpp")
    target_include_directories(test_prueba_asamblea_reglas PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/third_party/qrcodegen")
    target_link_libraries(test_prueba_asamblea_reglas PRIVATE ba_app_ui ba_app_asamblea ba_core Qt6::Core)
    set_target_properties(test_prueba_asamblea_reglas PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_prueba_asamblea_reglas COMMAND test_prueba_asamblea_reglas)
    add_executable(test_debate tests/test_debate.cpp)
    target_include_directories(test_debate PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_debate PRIVATE ba_app_asamblea ba_core Qt6::Core)
    set_target_properties(test_debate PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_debate COMMAND test_debate)
    add_executable(test_orden_del_dia tests/test_orden_del_dia.cpp)
    target_include_directories(test_orden_del_dia PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_orden_del_dia PRIVATE ba_app_control ba_app_asamblea ba_core Qt6::Core)
    set_target_properties(test_orden_del_dia PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_orden_del_dia COMMAND test_orden_del_dia)
    add_executable(test_informes_resultado tests/test_informes_resultado.cpp)
    target_include_directories(test_informes_resultado PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_informes_resultado PRIVATE ba_app_asamblea ba_core Qt6::Core)
    set_target_properties(test_informes_resultado PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_informes_resultado COMMAND test_informes_resultado)
    add_executable(test_quorum tests/test_quorum.cpp)
    target_include_directories(test_quorum PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_quorum PRIVATE ba_app_asamblea ba_core Qt6::Core)
    set_target_properties(test_quorum PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_quorum COMMAND test_quorum)
    add_executable(test_correccion tests/test_correccion.cpp)
    target_include_directories(test_correccion PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_correccion PRIVATE ba_app_emision ba_app_agente ba_core Qt6::Core)
    set_target_properties(test_correccion PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_correccion COMMAND test_correccion)
    # F3-03: lo que sale al aire se pinta en su hilo, con la interfaz trabada también.
    add_executable(test_pintor_aire tests/test_pintor_aire.cpp)
    target_include_directories(test_pintor_aire PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_pintor_aire PRIVATE ba_app_emision ba_app_agente ba_core Qt6::Core Qt6::Gui)
    set_target_properties(test_pintor_aire PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_pintor_aire COMMAND test_pintor_aire)
    add_executable(test_notificador tests/test_notificador.cpp)
    target_include_directories(test_notificador PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_notificador PRIVATE ba_app_agente ba_core Qt6::Core Qt6::Network)
    set_target_properties(test_notificador PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_notificador COMMAND test_notificador)
    # Lo que arrastra ConferenceClient: la foto de la sala se resume en su lector (sala/ResumenSala,
    # en ba_app_sala).
    # La foto de la sala fuera de la interfaz: el resumen y los cambios por fila en el hilo del
    # lector, la última gana (200 invitados a 10 Hz con la interfaz lenta), y la interfaz
    # aplicando solo lo que cambió.
    add_executable(test_resumen_sala tests/test_resumen_sala.cpp)
    target_include_directories(test_resumen_sala PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_resumen_sala PRIVATE ba_app_ui ba_app_sala ba_app_base ba_core Qt6::Core Qt6::Network)
    set_target_properties(test_resumen_sala PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_resumen_sala COMMAND test_resumen_sala)
    set_tests_properties(test_resumen_sala PROPERTIES TIMEOUT 60)
    # F1-13: la alarma «interfaz trabada con aire» sale del hilo del latido, sin la interfaz.
    add_executable(test_latido_aire tests/test_latido_aire.cpp)
    target_include_directories(test_latido_aire PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_latido_aire PRIVATE ba_app_ui ba_app_emision ba_app_agente ba_app_sala ba_app_base ba_core Qt6::Core Qt6::Network)
    set_target_properties(test_latido_aire PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_latido_aire COMMAND test_latido_aire)
    set_tests_properties(test_latido_aire PROPERTIES TIMEOUT 60)
    # A le vuelve a decir a B lo de estado cuando B no lo sabe (G-7): con una B de mentira.
    add_executable(test_republicadores tests/test_republicadores.cpp)
    target_include_directories(test_republicadores PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_republicadores PRIVATE ba_app_ui ba_app_sala ba_app_base ba_core Qt6::Core Qt6::Network)
    set_target_properties(test_republicadores PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_republicadores COMMAND test_republicadores)
    set_tests_properties(test_republicadores PROPERTIES TIMEOUT 60)
    # Los errores de la página del público (publico_pagina): la imagen o la sala, cada uno dicho.
    add_executable(test_pagina_publico tests/test_pagina_publico.cpp)
    target_include_directories(test_pagina_publico PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_pagina_publico PRIVATE ba_app_ui ba_app_sala ba_app_base ba_core Qt6::Core Qt6::Network)
    set_target_properties(test_pagina_publico PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_pagina_publico COMMAND test_pagina_publico)
    set_tests_properties(test_pagina_publico PROPERTIES TIMEOUT 60)
    add_executable(test_vocabulario tests/test_vocabulario.cpp)
    target_include_directories(test_vocabulario PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_vocabulario PRIVATE ba_app_agente ba_app_asamblea ba_core)
    add_test(NAME test_vocabulario COMMAND test_vocabulario)
    # La configuración chica que se guarda en Config/ (modo en vivo, escenas, rótulo, interfaz).
    add_executable(test_config_persistida tests/test_config_persistida.cpp)
    target_include_directories(test_config_persistida PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_config_persistida PRIVATE ba_app_ui ba_app_escenas ba_app_agente ba_core Qt6::Core)
    set_target_properties(test_config_persistida PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_config_persistida COMMAND test_config_persistida)
    # Las salidas guardadas (PLAN-SALIDAS, etapa 2): salidas.json, destinos con salida y al-aire.json por salida.
    add_executable(test_salidas_config tests/test_salidas_config.cpp)
    target_include_directories(test_salidas_config PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_salidas_config PRIVATE ba_app_emision ba_core nlohmann_json::nlohmann_json)
    add_test(NAME test_salidas_config COMMAND test_salidas_config)
    # Varias cuentas por plataforma y los vivos de cada salida (vivos/Vivos.hpp): la migración,
    # asignar con una plataforma de mentira, dos salidas al mismo vivo y el estado de cada destino.
    add_executable(test_vivos tests/test_vivos.cpp)
    target_include_directories(test_vivos PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_vivos PRIVATE ba_app_cuentas ba_core nlohmann_json::nlohmann_json)
    add_test(NAME test_vivos COMMAND test_vivos)
    # El documento único de aire (F3-04): corte en cada transición, daño y la versión 1.
    add_executable(test_documento_aire tests/test_documento_aire.cpp)
    target_include_directories(test_documento_aire PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_documento_aire PRIVATE ba_app_emision ba_core nlohmann_json::nlohmann_json)
    add_test(NAME test_documento_aire COMMAND test_documento_aire)
    # El AFV de cada salida (escenas::Afv): dos salidas con escenas distintas y la misma.
    add_executable(test_afv tests/test_afv.cpp)
    target_include_directories(test_afv PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_afv PRIVATE ba_app_escenas nlohmann_json::nlohmann_json)
    add_test(NAME test_afv COMMAND test_afv)
    if(TARGET ba_app_audio) # sin los headers de CLAP no hay audio (ba_app_audio)
        add_executable(test_loudness tests/test_loudness.cpp)
        target_include_directories(test_loudness PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(test_loudness PRIVATE ba_app_audio ba_core nlohmann_json::nlohmann_json)
        add_test(NAME test_loudness COMMAND test_loudness)
    endif()
    add_executable(test_alerts tests/test_alerts.cpp)
    target_include_directories(test_alerts PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_alerts PRIVATE ba_app_agente ba_core Qt6::Core)
    set_target_properties(test_alerts PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_alerts COMMAND test_alerts)
    # El corte de luz (F1-11): la política con estados de energía falsos.
    add_executable(test_energia_aire tests/test_energia_aire.cpp)
    target_include_directories(test_energia_aire PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_energia_aire PRIVATE ba_app_ui ba_core Qt6::Core)
    set_target_properties(test_energia_aire PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_energia_aire COMMAND test_energia_aire)
    # Lo pedido de cada salida (un solo dueño; el resultado de la orden manda).
    add_executable(test_pedidos_salida tests/test_pedidos_salida.cpp)
    target_include_directories(test_pedidos_salida PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME test_pedidos_salida COMMAND test_pedidos_salida)
    # Lo pedido al hilo de salidas del motor y su buzón (emision/BuzonSalidas): un «no arrancó»
    # viejo no baja lo pedido después; el buzón entrega en orden, desde varios hilos.
    add_executable(test_buzon_salidas tests/test_buzon_salidas.cpp)
    target_include_directories(test_buzon_salidas PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    find_package(Threads REQUIRED)
    target_link_libraries(test_buzon_salidas PRIVATE Threads::Threads)
    add_test(NAME test_buzon_salidas COMMAND test_buzon_salidas)
    # Qué avisa la foto del motor: lo que se rearma con otra parte se compara con la suya.
    add_executable(test_foto_partes tests/test_foto_partes.cpp)
    target_include_directories(test_foto_partes PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_foto_partes PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME test_foto_partes COMMAND test_foto_partes)
    # El id estable de escena (escenas/RefEscena.hpp): lo guardado de antes pasa al id, y
    # renombrar no deja nada apuntando al nombre viejo (la línea, transiciones, DSK, botones).
    add_executable(test_ref_escena tests/test_ref_escena.cpp)
    target_include_directories(test_ref_escena PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_ref_escena PRIVATE ba_app_evento nlohmann_json::nlohmann_json)
    add_test(NAME test_ref_escena COMMAND test_ref_escena)
    # Roles y despachador de agentes de un tiro, con un motor falso (script).
    add_executable(test_despachador tests/test_despachador.cpp)
    target_include_directories(test_despachador PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_despachador PRIVATE ba_app_agente ba_core Qt6::Core)
    set_target_properties(test_despachador PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_despachador COMMAND test_despachador)
    # Una conversación del asistente por evento y los avisos automáticos a ella (OpenCode,
    # con un servidor falso): agent/SesionesEvento y agent/EnvioOpencode.
    add_executable(test_sesion_evento tests/test_sesion_evento.cpp)
    target_include_directories(test_sesion_evento PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_sesion_evento PRIVATE ba_app_agente ba_core Qt6::Core Qt6::Network)
    set_target_properties(test_sesion_evento PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_sesion_evento COMMAND test_sesion_evento)
    # Los discursos del teleprompter: carpetas, nombres y que nada salga de la raiz.
    add_executable(test_captura_web tests/test_captura_web.cpp)
    target_include_directories(test_captura_web PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_captura_web PRIVATE ba_app_net ba_core Qt6::Core Qt6::Gui)
    if(APPLE)
        set(_ba_chs "${CMAKE_SOURCE_DIR}/extern/chrome-headless/mac-arm64/chrome-headless-shell")
    elseif(WIN32)
        set(_ba_chs "${CMAKE_SOURCE_DIR}/extern/chrome-headless/win64/chrome-headless-shell.exe")
    else()
        set(_ba_chs "${CMAKE_SOURCE_DIR}/extern/chrome-headless/linux64/chrome-headless-shell")
    endif()
    target_compile_definitions(test_captura_web PRIVATE BA_TEST_CHROME="${_ba_chs}")
    set_target_properties(test_captura_web PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_captura_web COMMAND test_captura_web)
    add_executable(test_manuales tests/test_manuales.cpp)
    target_include_directories(test_manuales PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_manuales PRIVATE ba_app_ui ba_app_net ba_core Qt6::Core Qt6::Gui)
    target_compile_definitions(test_manuales PRIVATE BA_TEST_IDIOMAS="${CMAKE_SOURCE_DIR}/assets/idiomas")
    set_target_properties(test_manuales PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_manuales COMMAND test_manuales)
    add_executable(test_lista_prompters tests/test_lista_prompters.cpp)
    target_include_directories(test_lista_prompters PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_lista_prompters PRIVATE ba_app_sala ba_core Qt6::Core)
    set_target_properties(test_lista_prompters PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_lista_prompters COMMAND test_lista_prompters)
    add_executable(test_discursos tests/test_discursos.cpp)
    target_include_directories(test_discursos PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_discursos PRIVATE ba_app_ui ba_app_medios Qt6::Core)
    set_target_properties(test_discursos PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_discursos COMMAND test_discursos)
    # El protocolo Cast: protobuf a mano y el troceo de mensajes.
    add_executable(test_cast tests/test_cast.cpp)
    target_include_directories(test_cast PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_cast PRIVATE ba_app_pantallas ba_core Qt6::Core mbedtls mbedx509 mbedcrypto)
    set_target_properties(test_cast PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_cast COMMAND test_cast)

    add_executable(test_twitch tests/test_twitch.cpp)
    target_include_directories(test_twitch PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_twitch PRIVATE ba_app_cuentas ba_app_net ba_core Qt6::Core Qt6::Gui Qt6::Network mbedtls mbedx509 mbedcrypto)
    set_target_properties(test_twitch PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_twitch COMMAND test_twitch)

    # Facebook: login, Paginas, vivos RTMPS, comentarios y subida por partes contra un Graph falso.
    add_executable(test_facebook tests/test_facebook.cpp)
    target_include_directories(test_facebook PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_facebook PRIVATE ba_app_cuentas ba_app_net ba_core Qt6::Core Qt6::Gui Qt6::Network mbedtls mbedx509 mbedcrypto)
    set_target_properties(test_facebook PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_facebook COMMAND test_facebook)
    # La interfaz COMPILA: cada .qml con el motor de QML (agarra lo que qmllint no:
    # propiedades asignadas dos veces, tipos o ids que no existen).
    add_executable(test_qml_carga tests/test_qml_carga.cpp)
    target_include_directories(test_qml_carga PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_qml_carga PRIVATE ba_app_ui ba_app_pantallas ba_app_base Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
    target_compile_definitions(test_qml_carga PRIVATE BA_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
    set_target_properties(test_qml_carga PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_qml_carga COMMAND test_qml_carga)
    # Banco de la sala con mucha gente (no es de ctest: se corre a mano, ver el .cpp). Con
    # QT_QML_DEBUG para poder medir los bindings con qmlprofiler.
    add_executable(bench_sala_conferencia tests/bench_sala_conferencia.cpp)
    target_include_directories(bench_sala_conferencia PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(bench_sala_conferencia PRIVATE ba_app_ui ba_app_base Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Qml Qt6::Quick)
    target_compile_definitions(bench_sala_conferencia PRIVATE BA_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml" QT_QML_DEBUG)
    set_target_properties(bench_sala_conferencia PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    # La grilla de la sala con 120 (SalaParticipantes de verdad, sin GPU): sin binding loop,
    # sin un VideoItem por persona, la reserva sin pedir cuadros.
    add_executable(test_sala_grilla tests/test_sala_grilla.cpp)
    target_include_directories(test_sala_grilla PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_sala_grilla PRIVATE ba_app_ui ba_app_base Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Qml Qt6::Quick)
    target_compile_definitions(test_sala_grilla PRIVATE BA_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
    set_target_properties(test_sala_grilla PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_sala_grilla COMMAND test_sala_grilla)
    # La lista genérica de la vista avisa solo las filas y los roles que cambiaron.
    add_executable(test_json_list_model tests/test_json_list_model.cpp)
    target_include_directories(test_json_list_model PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_json_list_model PRIVATE ba_app_ui ba_app_base Qt6::Core)
    set_target_properties(test_json_list_model PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_json_list_model COMMAND test_json_list_model)
    # El selector de herramientas de la Botonera: busca y filtra por sección.
    add_executable(test_selector_herramientas tests/test_selector_herramientas.cpp)
    target_include_directories(test_selector_herramientas PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_selector_herramientas PRIVATE ba_app_base Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
    target_compile_definitions(test_selector_herramientas PRIVATE BA_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
    set_target_properties(test_selector_herramientas PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_selector_herramientas COMMAND test_selector_herramientas)
    # La interfaz parsea: qmllint sobre cada QML, un test por archivo (ver cmake/tests): la
    # caché de tests corre solo el del .qml que cambió. Un error de sintaxis es del archivo
    # (lo que qmllint resuelve de los imports da advertencias, que no cuentan): su entrada es
    # el archivo solo. Un .qml nuevo reconfigura (CONFIGURE_DEPENDS) y suma su test.
    find_program(BA_QMLLINT qmllint PATHS "${BA_QT_PREFIX}/bin" NO_DEFAULT_PATH)
    if(BA_QMLLINT)
        file(GLOB_RECURSE _ba_qml_todos CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.qml")
        foreach(_ba_qml IN LISTS _ba_qml_todos)
            file(RELATIVE_PATH _ba_qml_rel "${CMAKE_CURRENT_SOURCE_DIR}/qml" "${_ba_qml}")
            string(REGEX REPLACE "\\.qml$" "" _ba_qml_n "${_ba_qml_rel}")
            string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _ba_qml_n "${_ba_qml_n}")
            add_test(NAME test_qml_syntax_${_ba_qml_n} COMMAND ${CMAKE_COMMAND} -DQMLLINT=${BA_QMLLINT}
                     -DQML_FILES=${_ba_qml} -P ${CMAKE_SOURCE_DIR}/cmake/tests/qml_syntax.cmake)
        endforeach()
    endif()
else()
    ba_falta("Interfaz (Qt)" "falta el Qt vendoreado ('make deps-qt-host')")
endif()
# Al final: sus raíces de include ya están (ba-app las tiene propias o por ba_motor y
# ba_emisor_pedido) y el orden de los -I de ba-app no cambia. Las bibliotecas, de la más
# alta a la más baja (el enlazador de Linux resuelve una .a con las que vienen después).
target_link_libraries(ba-app PRIVATE ${BA_APP_BIBLIOTECAS} ba_app_comun)

# --- Instalación ------------------------------------------------------------
# Proceso A va a bin/64bit/ (spec §5.1).
install(TARGETS ba-app
    RUNTIME DESTINATION bin/64bit
)

# Qt vendoreado dentro del bundle (Linux): bibliotecas que ba-app y los plugins
# usan + plugins de plataforma. Ver cmake/BaQtDeploy.cmake.
if(BA_APP_QT_OK AND UNIX AND NOT APPLE)
    set(_ba_qt_plugins "")
    foreach(_cat platforms wayland-shell-integration wayland-decoration-client
                 wayland-graphics-integration-client xcbglintegrations platforminputcontexts)
        file(GLOB _found RELATIVE "${BA_QT_PREFIX}/plugins" "${BA_QT_PREFIX}/plugins/${_cat}/*.so")
        list(APPEND _ba_qt_plugins ${_found})
    endforeach()
    # Plataformas: Wayland, X11 y offscreen. eglfs/linuxfb/vnc no hacen falta.
    list(FILTER _ba_qt_plugins EXCLUDE REGEX "^platforms/lib(qeglfs|qlinuxfb|qminimal|qminimalegl|qvnc|qvkkhrdisplay)\\.so$")
    list(APPEND _ba_qt_plugins "platformthemes/libqxdgdesktopportal.so")
    # JPEG: capturas del servidor MCP y fotos de los oradores. GIF: la vista
    # previa de Medios (un GIF del evento daba «Unsupported image format»).
    list(APPEND _ba_qt_plugins "imageformats/libqjpeg.so" "imageformats/libqgif.so")
    # El motor de reproduccion. `gstreamermediaplugin` NO va: pediria GStreamer
    # del sistema, que es justo lo que no puede pasar en un aparato portable.
    list(APPEND _ba_qt_plugins "multimedia/libffmpegmediaplugin.so")
    install(CODE "
        set(BA_QT_LIB_DIR \"${BA_QT_PREFIX}/lib\")
        set(BA_QT_PLUGIN_DIR \"${BA_QT_PREFIX}/plugins\")
        set(BA_QT_PLUGINS \"${_ba_qt_plugins}\")
        set(BA_APP_BUILD_EXE \"$<TARGET_FILE:ba-app>\")
        set(BA_QT_QML_DIR \"${BA_QT_PREFIX}/qml\")
        set(BA_QT_QML_MODULES \"QtQml;QtQuick;QtMultimedia\")
    ")
    install(SCRIPT "${CMAKE_SOURCE_DIR}/cmake/BaQtDeploy.cmake")
endif()

# macOS: frameworks de Qt en bin/64bit/qt. Ver cmake/BaQtDeployMac.cmake.
if(BA_APP_QT_OK AND APPLE)
    set(_ba_qt_plugins "platforms/libqcocoa.dylib;platforms/libqoffscreen.dylib;imageformats/libqjpeg.dylib;imageformats/libqgif.dylib;imageformats/libqsvg.dylib;imageformats/libqico.dylib;iconengines/libqsvgicon.dylib;styles/libqmacstyle.dylib;tls/libqsecuretransportbackend.dylib;tls/libqcertonlybackend.dylib")
    # Reproduccion del visor de Medios: el motor de FFmpeg que Qt trae adentro
    # (como en Linux) y el nativo de macOS (AVFoundation, sin nada extra).
    list(APPEND _ba_qt_plugins "multimedia/libffmpegmediaplugin.dylib" "multimedia/libdarwinmediaplugin.dylib")
    install(CODE "
        set(BA_QT_LIB_DIR \"${BA_QT_PREFIX}/lib\")
        set(BA_QT_PLUGIN_DIR \"${BA_QT_PREFIX}/plugins\")
        set(BA_QT_PLUGINS \"${_ba_qt_plugins}\")
        set(BA_APP_BUILD_EXE \"$<TARGET_FILE:ba-app>\")
        set(BA_QT_QML_DIR \"${BA_QT_PREFIX}/qml\")
        set(BA_QT_QML_MODULES \"QtQml;QtQuick;QtCore;QtMultimedia\")
    ")
    install(SCRIPT "${CMAKE_SOURCE_DIR}/cmake/BaQtDeployMac.cmake")
endif()

# Windows: Qt (DLLs junto a ba-app) + CRT de MSVC. Ver cmake/BaQtDeployWin.cmake.
if(BA_APP_QT_OK AND WIN32)
    find_program(BA_OBJDUMP_WIN NAMES llvm-objdump HINTS "${BA_WIN_LLVM}")
    install(CODE "
        set(BA_QT_BIN_DIR \"${BA_QT_PREFIX}/bin\")
        set(BA_QT_PLUGIN_DIR \"${BA_QT_PREFIX}/plugins\")
        set(BA_QT_PLUGINS \"platforms/qwindows.dll;platforms/qoffscreen.dll;imageformats/qjpeg.dll;imageformats/qgif.dll;imageformats/qsvg.dll;imageformats/qico.dll;tls/qschannelbackend.dll;tls/qcertonlybackend.dll;iconengines/qsvgicon.dll;multimedia/ffmpegmediaplugin.dll\")
        set(BA_APP_BUILD_EXE \"$<TARGET_FILE:ba-app>\")
        set(BA_QT_QML_DIR \"${BA_QT_PREFIX}/qml\")
        set(BA_QT_QML_MODULES \"QtQml;QtQuick;QtCore;QtMultimedia\")
        set(BA_OBJDUMP \"${BA_OBJDUMP_WIN}\")
        set(BA_CRT_REDIST_DIR \"${CMAKE_SOURCE_DIR}/extern/msvc/redist\")
    " COMPONENT bundle)
    install(SCRIPT "${CMAKE_SOURCE_DIR}/cmake/BaQtDeployWin.cmake" COMPONENT bundle)
endif()

# --- Demos ------------------------------------------------------------------
# Configuraciones de ejemplo completas, por idioma (assets/demo/<idioma>/<nombre>/ →
# Demos/<idioma>/<nombre>; las genera tools/generar/plantillas.py --idioma <c>): se crean
# como una configuración nueva desde Configuración → Configuraciones (o configuracion_crear
# {desde: "demo:<nombre>"}); la app lista las del idioma del operador.
# Ver src/app/arranque/Configuraciones.hpp.
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/demo")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/demo/"
        DESTINATION Demos COMPONENT bundle
    )
endif()

# --- Extensiones de ejemplo -------------------------------------------------
# Tarjetas del dashboard que viven en el DISCO, no adentro del binario (ver
# src/app/ui/ExtensionesModel.hpp). Las de fabrica se instalan como referencia;
# las que escriba el operador (o el asistente) conviven en la misma carpeta y
# NO las toca ni el manifest ni --update.
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/extensiones")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/extensiones/"
        DESTINATION Extensiones COMPONENT bundle
    )
endif()

# Los ajustes avanzados (src/core/ajustes) y los idiomas (src/core/i18n): muchos tests
# compilan fuentes de la app sueltas y esas fuentes los consultan. Todos los test_* (y
# las herramientas capturar_* y los bancos bench_*) enlazan ba_core: también por el
# detector de acoples (diag/Acoples.hpp, BA_ACOPLE en las fuentes de la interfaz).
get_property(_ba_app_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_t IN LISTS _ba_app_targets)
    if(_t MATCHES "^(test|capturar|bench)_")
        get_target_property(_tipo ${_t} TYPE)
        if(_tipo STREQUAL "EXECUTABLE")
            target_link_libraries(${_t} PRIVATE ba_core)
        endif()
    endif()
endforeach()

# En Linux, las bibliotecas de la app en un --start-group (en ba-app y en cada test que
# enlaza dos o más): las áreas de nivel 2 todavía se usan entre sí (cmake/BaBiblioteca.cmake).
ba_biblioteca_grupos_linux()
