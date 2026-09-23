# ============================================================================
# src/app — `ba-app` (Proceso A: motor de emisión + UI de operación)
#
# El Proceso A es un ejecutable GUI (sin consola en Windows) que integra
# `ba_core` (dsp + ipc + persist) con la UI de operación (spec §6).
#
# Arquitectura (docs/prompts/primer_prompt.md §6, §14):
#   - `ba_core` es el núcleo: SIN Qt, SIN libobs, testeable por unidad.
#   - `ba-app` (A) usa /MD (CRT dinámica) por consistencia con libobs y Qt.
#   - Todo lo Qt/obs va tras `#ifdef _WIN32` (y, en la app completa, tras
#     `BA_APP_HAVE_QT_OBS`, definido solo cuando Qt 6 + libobs están presentes).
#
# Dos rutas de compilación, elegidas en el configure:
#   1. Windows + Qt 6 + libobs presentes  -> app COMPLETA (QApplication +
#      obs_startup + MainWindow). Se enlaza Qt 6 Widgets + libobs y se define
#      `BA_APP_HAVE_QT_OBS`.
#   2. Cross-compile MinGW de validación (o Linux) sin Qt/libobs -> modo
#      HEADLESS: solo `ba_core`, que ejercita la misma integración dsp/ipc/
#      persist (ver main.cpp, runHeadlessSelfTest). Así el target compila y se
#      valida en cualquier plataforma, y la app real se construye en el
#      entorno con Qt/libobs.
#
# /MD (CRT dinámica) por consistencia con libobs y Qt — hereda del root.
# ============================================================================

# --- Sources ----------------------------------------------------------------
# CONFIGURE_DEPENDS detecta los archivos nuevos sin reconfigurar a mano.
file(GLOB_RECURSE BA_APP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/tests/.*")
list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/mcp/bridge/.*") # ba-mcp es otro binario

# WIN32 en MSVC: subsistema GUI (sin consola). En Linux es un ejecutable normal.
# BroadcastEngine.cpp incluye <obs.h>: sin libobs no puede compilarse, asi que
# se saca de la lista (el resto del Proceso A sigue compilando headless).
if(NOT BA_HAVE_LIBOBS)
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/BroadcastEngine(Scenes)?\\.cpp$")
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/AppSettings\\.cpp$")
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/EngineSelfTest\\.cpp$")
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/OperateMode\\.cpp$")
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/GuiMode\\.cpp$")
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/WindowingPlatform\\.cpp$")
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/OperatorFeed\\.cpp$")
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/ChatOverlaySource\\.cpp$")
endif()

# --- Qt 6 (interfaz grafica) — VENDOREADO, nunca el del sistema -------------
# Igual que libobs: el bundle es portable y se lleva su propio Qt. Se busca
# SOLO en extern/ (NO_DEFAULT_PATH):
#   - Linux/macOS: extern/qt6-host/<QT6_VERSION>/<arch>  (`make deps-qt-host`)
#   - Windows:     extern/qt6/<QT6_VERSION>/<arch>       (`make deps-qt`)
# Sin Qt vendoreado, ba-app se compila sin interfaz (modo consola).
set(BA_APP_QT_OK FALSE)
if(BA_HAVE_LIBOBS)
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
        find_package(Qt6 COMPONENTS Gui Quick Qml QuickControls2 Network Svg CONFIG PATHS "${BA_QT_PREFIX}" NO_DEFAULT_PATH QUIET)
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
endif()
# Pseudo-terminal: una implementacion por plataforma.
if(WIN32)
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/term/Pty_posix\\.cpp$")
else()
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/term/Pty_win\\.cpp$")
endif()

# --- NDI(R) sin SDK propietario: libNoIDea (VideoLAN, LGPL) + libmicrodns -----
# Fuente fijada por commit (tools/deps/obs-deps-linux.sh). Se compila adentro de
# ba-app; el video SpeedHQ lo decodifica el FFmpeg del bundle.
set(BA_NOIDEA_DIR "${CMAKE_SOURCE_DIR}/extern/libnoidea")
set(BA_OBS_DEPS "${CMAKE_SOURCE_DIR}/extern/obs-deps-linux")
set(BA_HAVE_NDI OFF)
if(BA_HAVE_LIBOBS AND NOT WIN32 AND EXISTS "${BA_NOIDEA_DIR}/src/recv.c" AND EXISTS "${BA_OBS_DEPS}/lib/libmicrodns.a")
    add_library(ba_noidea STATIC "${BA_NOIDEA_DIR}/src/recv.c" "${BA_NOIDEA_DIR}/src/scramble.c"
                                 "${BA_NOIDEA_DIR}/extensions/discovery.c")
    target_include_directories(ba_noidea PUBLIC "${BA_NOIDEA_DIR}/include" "${BA_OBS_DEPS}/include")
    target_compile_definitions(ba_noidea PUBLIC NOIDEA_EXT_DISCOVERY NOIDEA_STATIC PRIVATE HAVE_POLL=1 _GNU_SOURCE)
    set_target_properties(ba_noidea PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
    target_compile_options(ba_noidea PRIVATE -w)
    find_package(Threads REQUIRED)
    target_link_libraries(ba_noidea PUBLIC "${BA_OBS_DEPS}/lib/libmicrodns.a" Threads::Threads)
    set(BA_HAVE_NDI ON)
    # Licencias (LGPL) junto al programa.
    install(FILES "${BA_NOIDEA_DIR}/COPYING" DESTINATION data/licencias RENAME libnoidea-LGPL.txt)
    if(EXISTS "${CMAKE_SOURCE_DIR}/extern/.build-obs-deps/libmicrodns/COPYING")
        install(FILES "${CMAKE_SOURCE_DIR}/extern/.build-obs-deps/libmicrodns/COPYING" DESTINATION data/licencias
                RENAME libmicrodns-LGPL.txt)
    endif()
endif()
# Windows: libmicrodns y libNoIDea se compilan aca (tools/deps/ndi-src.sh) y
# libavcodec/libavutil son las DLL del runtime de OBS (FFmpeg n7.0.2), con los
# headers y .lib del obs-deps oficial que fija OBS (make deps-obs-deps-win).
set(BA_MDNS_DIR "${CMAKE_SOURCE_DIR}/extern/libmicrodns")

# --- mDNS para buscar Chromecast (src/app/pantallas) --------------------------
# La misma libmicrodns que usa NDI, pero como objetivo propio: NDI solo se
# compila con libobs y en Linux, y buscar televisores tiene que andar igual en
# las tres plataformas. En Linux se usa el .a de obs-deps si esta; si no (y en
# macOS), se compila del fuente vendoreado, que es el mismo.
set(BA_MDNS_OK OFF)
if(NOT WIN32 AND EXISTS "${BA_OBS_DEPS}/lib/libmicrodns.a")
    add_library(ba_mdns INTERFACE)
    target_include_directories(ba_mdns INTERFACE "${BA_OBS_DEPS}/include")
    target_link_libraries(ba_mdns INTERFACE "${BA_OBS_DEPS}/lib/libmicrodns.a")
    set(BA_MDNS_OK ON)
elseif(NOT WIN32 AND EXISTS "${BA_MDNS_DIR}/src/mdns.c")
    add_library(ba_mdns STATIC "${BA_MDNS_DIR}/src/mdns.c" "${BA_MDNS_DIR}/src/rr.c")
    target_include_directories(ba_mdns PUBLIC "${BA_MDNS_DIR}/include" PRIVATE "${BA_MDNS_DIR}")
    target_compile_options(ba_mdns PRIVATE -w)
    set_target_properties(ba_mdns PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
    set(BA_MDNS_OK ON)
endif()
# El enlace con ba-app va mas abajo, cuando el objetivo ya existe.
set(BA_OBS_DEPS_WIN "${CMAKE_SOURCE_DIR}/extern/obs-deps-win")
if(BA_HAVE_LIBOBS AND WIN32 AND EXISTS "${BA_NOIDEA_DIR}/src/recv.c" AND EXISTS "${BA_MDNS_DIR}/src/mdns.c"
   AND EXISTS "${BA_OBS_DEPS_WIN}/lib/avcodec.lib")
    add_library(ba_microdns STATIC "${BA_MDNS_DIR}/src/mdns.c" "${BA_MDNS_DIR}/src/rr.c"
                                   "${BA_MDNS_DIR}/compat/compat.c" "${BA_MDNS_DIR}/compat/inet.c"
                                   "${BA_MDNS_DIR}/compat/poll.c")
    target_include_directories(ba_microdns PUBLIC "${BA_MDNS_DIR}/include"
                               PRIVATE "${CMAKE_SOURCE_DIR}/cmake/ndi-win" "${BA_MDNS_DIR}" "${BA_MDNS_DIR}/compat")
    target_compile_definitions(ba_microdns PRIVATE HAVE_CONFIG_H _UNICODE=1 UNICODE=1 _CRT_NONSTDC_NO_DEPRECATE
                                                   _CRT_SECURE_NO_DEPRECATE)
    target_compile_options(ba_microdns PRIVATE -w /FIstdlib.h) # compat/ usa malloc sin incluirlo
    target_link_libraries(ba_microdns PUBLIC ws2_32 iphlpapi)

    # Sin poll() en Windows: el compat/poll.c de la propia libNoIDea (el de VLC).
    add_library(ba_noidea STATIC "${BA_NOIDEA_DIR}/src/recv.c" "${BA_NOIDEA_DIR}/src/scramble.c"
                                 "${BA_NOIDEA_DIR}/extensions/discovery.c" "${BA_NOIDEA_DIR}/compat/poll.c")
    # cmake/ndi-win: pthread.h minimo sobre Win32 y un unistd.h vacio, lo que
    # libNoIDea espera de MinGW.
    target_include_directories(ba_noidea PUBLIC "${BA_NOIDEA_DIR}/include" "${CMAKE_SOURCE_DIR}/cmake/ndi-win"
                               PRIVATE "${BA_NOIDEA_DIR}/src")
    target_compile_definitions(ba_noidea PUBLIC NOIDEA_EXT_DISCOVERY NOIDEA_STATIC)
    target_compile_options(ba_noidea PRIVATE -w)
    target_link_libraries(ba_noidea PUBLIC ba_microdns ws2_32)

    add_library(ba_ffmpeg_win INTERFACE)
    target_include_directories(ba_ffmpeg_win SYSTEM INTERFACE "${BA_OBS_DEPS_WIN}/include")
    target_link_libraries(ba_ffmpeg_win INTERFACE "${BA_OBS_DEPS_WIN}/lib/avcodec.lib" "${BA_OBS_DEPS_WIN}/lib/avutil.lib")
    set(BA_HAVE_NDI ON)
    install(FILES "${BA_NOIDEA_DIR}/COPYING" DESTINATION data/licencias RENAME libnoidea-LGPL.txt)
    install(FILES "${BA_MDNS_DIR}/COPYING" DESTINATION data/licencias RENAME libmicrodns-LGPL.txt)
endif()
if(NOT BA_HAVE_NDI)
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/NdiSource\\.cpp$")
endif()

# --- Plugins de audio CLAP (LSP) ---------------------------------------------
# Headers de CLAP (MIT, `make deps-clap`). Los plugins LSP se cargan en tiempo
# de ejecucion desde el bundle (plugins/lsp/). Sin los headers o sin libobs, el
# rack de audio no se compila.
set(BA_CLAP_INC "${CMAKE_SOURCE_DIR}/extern/clap/include")
set(BA_HAVE_CLAP OFF)
if(BA_HAVE_LIBOBS AND EXISTS "${BA_CLAP_INC}/clap/clap.h")
    set(BA_HAVE_CLAP ON)
else()
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/audio/.*")
endif()
# Plugins LSP en el bundle: plugins/lsp/ (+ sus licencias).
if(BA_HAVE_CLAP)
    if(WIN32)
        set(_ba_lsp_dir "${CMAKE_SOURCE_DIR}/extern/lsp/windows-x86_64")
    else()
        set(_ba_lsp_dir "${CMAKE_SOURCE_DIR}/extern/lsp/linux-x86_64")
    endif()
    file(GLOB _ba_lsp_files "${_ba_lsp_dir}/*.clap" "${_ba_lsp_dir}/*.dll" "${_ba_lsp_dir}/*.so*")
    if(_ba_lsp_files)
        install(FILES ${_ba_lsp_files} DESTINATION plugins/lsp)
        file(GLOB _ba_lsp_lic "${CMAKE_SOURCE_DIR}/extern/lsp/licencias/*")
        install(FILES ${_ba_lsp_lic} DESTINATION data/licencias/lsp-plugins)
    endif()
endif()
# LSP para los tests (en el bundle: plugins/lsp/).
if(WIN32)
    set(BA_LSP_CLAP "${CMAKE_SOURCE_DIR}/extern/lsp/windows-x86_64/lsp-plugins.clap")
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

if(MSVC)
    add_executable(ba-app WIN32 ${BA_APP_SOURCES})
else()
    add_executable(ba-app ${BA_APP_SOURCES})
endif()

# MinGW GUI: subsistema GUI (sin consola), igual que el launcher. Sin esto,
# MinGW enlaza como consola y el CRT espera `main` en vez de `WinMain`/
# `wWinMain` (ver el trampoline WinMain->wWinMain en main.cpp).
if(WIN32)
    # main.cpp trae su propio wWinMain: sin el de Qt (Qt6::EntryPointPrivate).
    set_target_properties(ba-app PROPERTIES WIN32_EXECUTABLE ON qt_no_entrypoint ON)
    target_link_libraries(ba-app PRIVATE dxgi) # placas de video (system_hardware)
endif()

target_link_libraries(ba-app PRIVATE ba_core mbedtls mbedx509 mbedcrypto) # HTTPS propio (net/HttpsClient)
# Buscar Chromecast en la red (src/app/pantallas/Buscar.cpp).
if(BA_MDNS_OK)
    target_link_libraries(ba-app PRIVATE ba_mdns)
    target_compile_definitions(ba-app PRIVATE BA_HAVE_MDNS=1)
    message(STATUS "ba-app: busqueda de Chromecast por mDNS")
else()
    message(STATUS "ba-app: sin mDNS (los televisores se agregan a mano por IP)")
endif()
# Raices de Mozilla para ese HTTPS (make deps-cacert): viajan en el bundle.
if(EXISTS "${CMAKE_SOURCE_DIR}/extern/cacert/cacert.pem")
    install(FILES "${CMAKE_SOURCE_DIR}/extern/cacert/cacert.pem" DESTINATION data)
endif()

# --- Motor de emision sobre libobs (spec §6) --------------------------------
# libobs es cross-platform y el motor tambien: cuando `ba::libobs` existe se
# compila `BroadcastEngine` en CUALQUIER plataforma, no solo en Windows. Ese
# acoplamiento (todo lo de obs detras de `_WIN32`) era lo que mantenia el motor
# sin compilar ni ejecutar en ningun lado.
if(BA_HAVE_LIBOBS)
    target_link_libraries(ba-app PRIVATE ba::libobs)
    if(BA_HAVE_NDI)
        if(WIN32)
            target_link_libraries(ba-app PRIVATE ba_noidea ba_ffmpeg_win)
        else()
            target_link_libraries(ba-app PRIVATE ba_noidea "${BA_OBS_DEPS}/lib/libavcodec.so" "${BA_OBS_DEPS}/lib/libavutil.so")
        endif()
        target_compile_definitions(ba-app PRIVATE BA_HAVE_NDI=1)
    endif()
    target_compile_definitions(ba-app PRIVATE BA_HAVE_LIBOBS=1)
    # Linux: el motor elige Wayland o X11 para libobs antes de obs_startup
    # (BroadcastEngine::selectWindowingPlatform). Son bibliotecas del sistema
    # de escritorio, las mismas que ya carga libobs.
    if(UNIX AND NOT APPLE)
        find_package(X11 REQUIRED)
        find_library(BA_WAYLAND_CLIENT wayland-client REQUIRED)
        target_link_libraries(ba-app PRIVATE X11::X11 ${BA_WAYLAND_CLIENT})
    endif()
    # rpath $ORIGIN: el ejecutable busca sus bibliotecas JUNTO A EL dentro del
    # bundle portable (bin/64bit/), no en el sistema. Es lo que hace que el
    # bundle corra en una maquina sin nada instalado.
    if(NOT WIN32)
        set_target_properties(ba-app PROPERTIES
            BUILD_RPATH "${BA_OBS_LINUX_PREFIX}/lib"
            # qt/lib: el Qt vendoreado que viaja en el bundle (BaQtDeploy.cmake).
            INSTALL_RPATH "$ORIGIN;$ORIGIN/qt/lib")
    endif()
else()
    target_compile_definitions(ba-app PRIVATE BA_HAVE_LIBOBS=0)
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
    target_link_libraries(ba-app PRIVATE Qt6::Gui Qt6::Quick Qt6::Qml Qt6::QuickControls2 Qt6::Network Qt6::Svg)
    target_compile_definitions(ba-app PRIVATE BA_APP_HAVE_QT_OBS=1)
    # Version visible en Configuracion -> Sistema y datos (soporte).
    target_compile_definitions(ba-app PRIVATE BA_APP_VERSION="${PROJECT_VERSION}")
    target_include_directories(ba-app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    set_target_properties(ba-app PROPERTIES AUTOMOC ON)
    # Vista: los .qml viajan como recursos dentro del binario (qrc:/qml/...).
    file(GLOB_RECURSE BA_APP_QML RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.qml")
    qt_add_resources(ba-app "ba_qml" PREFIX "/" FILES ${BA_APP_QML})
    # Versiones fijadas de los agentes de IA (pestaña Asistente).
    qt_add_resources(ba-app "ba_agent" PREFIX "/" FILES agent/agentes.json)
    target_link_libraries(ba-app PRIVATE ba_vterm)
    if(BA_HAVE_CLAP)
        target_include_directories(ba-app PRIVATE "${BA_CLAP_INC}")
        target_compile_definitions(ba-app PRIVATE BA_HAVE_CLAP=1 BA_DEV_LSP_CLAP="${BA_LSP_CLAP}")
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
    message(STATUS "ba-app: interfaz grafica con Qt ${Qt6_VERSION} vendoreado (${BA_QT_PREFIX})")
    # Test de la logica de disposicion del dashboard (ViewModel sin interfaz).
    add_executable(test_dashboard_model tests/test_dashboard_model.cpp ui/DashboardModel.cpp)
    target_include_directories(test_dashboard_model PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_dashboard_model PRIVATE Qt6::Core Qt6::Gui)
    set_target_properties(test_dashboard_model PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_dashboard_model COMMAND test_dashboard_model)

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
    add_executable(test_mcp tests/test_mcp.cpp mcp/McpCore.cpp mcp/McpHttp.cpp)
    target_include_directories(test_mcp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_mcp PRIVATE ba_core Qt6::Core Qt6::Network)
    target_compile_definitions(test_mcp PRIVATE BA_MCP_BRIDGE="$<TARGET_FILE:ba-mcp>")
    set_target_properties(test_mcp PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_dependencies(test_mcp ba-mcp)
    add_test(NAME test_mcp COMMAND test_mcp)

    # YouTube: cliente OAuth + Data API contra un Google falso, y HTTPS real.
    add_executable(test_youtube tests/test_youtube.cpp youtube/YouTubeClient.cpp net/HttpsClient.cpp)
    target_include_directories(test_youtube PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_youtube PRIVATE ba_core Qt6::Core Qt6::Gui Qt6::Network mbedtls mbedx509 mbedcrypto)
    target_compile_definitions(test_youtube PRIVATE BA_TEST_CACERT="${CMAKE_SOURCE_DIR}/extern/cacert/cacert.pem")
    set_target_properties(test_youtube PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_youtube COMMAND test_youtube)

    # Twitch: login por codigo, Helix y chat IRC contra un Twitch falso.
    # Terminal de la pestaña Asistente (Pty real + libvterm). POSIX.
    if(NOT WIN32)
        add_executable(test_terminal tests/test_terminal.cpp term/TerminalSession.cpp term/Pty_posix.cpp term/Pty.hpp
                                     term/TerminalSession.hpp)
        target_include_directories(test_terminal PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(test_terminal PRIVATE ba_vterm Qt6::Core Qt6::Gui)
        if(NOT APPLE)
            target_link_libraries(test_terminal PRIVATE util)
        endif()
        set_target_properties(test_terminal PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
        add_test(NAME test_terminal COMMAND test_terminal)
        set_tests_properties(test_terminal PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
    # Barra del asistente al operador.
    add_executable(test_operator_messages tests/test_operator_messages.cpp ui/OperatorMessages.cpp ui/OperatorMessages.hpp)
    target_include_directories(test_operator_messages PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_operator_messages PRIVATE ba_core Qt6::Core)
    set_target_properties(test_operator_messages PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_operator_messages COMMAND test_operator_messages)
    # El guion del evento: tiempos, revision previa y el recorrido en vivo.
    # No depende de Qt ni de libobs (dice QUE hacer, no lo hace), asi que
    # compila y corre en cualquier lado.
    add_executable(test_evento tests/test_evento.cpp evento/Evento.cpp)
    target_include_directories(test_evento PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_evento PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME test_evento COMMAND test_evento)

    # El puente con el renderizador de graficos: que un grafico colgado NO
    # cuelgue la app es la razon de ser del proceso aparte, asi que se prueba.
    if(BA_APP_QT_OK)
        add_executable(test_grafico_proceso tests/test_grafico_proceso.cpp graficos/GraficoProceso.cpp)
        set_target_properties(test_grafico_proceso PROPERTIES AUTOMOC ON)
        target_include_directories(test_grafico_proceso PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(test_grafico_proceso PRIVATE Qt6::Core Qt6::Gui)
        add_test(NAME test_grafico_proceso COMMAND test_grafico_proceso)
        set_tests_properties(test_grafico_proceso PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
    endif()

    # Medir el retardo entre labios y voz con un aplauso. C++ puro: recibe
    # mediciones ya hechas y devuelve el numero, asi se prueba con señales
    # inventadas sin pararse a aplaudir frente a una camara.
    add_executable(test_sincro_labios tests/test_sincro_labios.cpp camaras/SincroLabios.cpp)
    target_include_directories(test_sincro_labios PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME test_sincro_labios COMMAND test_sincro_labios)

    # La carpeta del evento: crear, guardar con respaldo, listar.
    add_executable(test_evento_store tests/test_evento_store.cpp evento/EventoStore.cpp evento/Evento.cpp)
    target_include_directories(test_evento_store PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_evento_store PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME test_evento_store COMMAND test_evento_store)

    # Analizador (LUFS, tercios, piso de ruido) y generador de prueba.
    if(BA_HAVE_CLAP)
        add_executable(test_audio_dsp tests/test_audio_dsp.cpp audio/Analyzer.cpp)
        target_include_directories(test_audio_dsp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(test_audio_dsp PRIVATE nlohmann_json::nlohmann_json)
        add_test(NAME test_audio_dsp COMMAND test_audio_dsp)
    endif()
    # Host CLAP contra los plugins LSP reales (sin interfaz grafica).
    if(BA_HAVE_CLAP)
        add_executable(test_clap_host tests/test_clap_host.cpp audio/ClapHost.cpp audio/ClapEditor.cpp)
        target_include_directories(test_clap_host PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${BA_CLAP_INC}")
        target_compile_definitions(test_clap_host PRIVATE BA_TEST_LSP_CLAP="${BA_LSP_CLAP}" _USE_MATH_DEFINES)
        if(NOT WIN32)
            target_link_libraries(test_clap_host PRIVATE X11::X11 ${CMAKE_DL_LIBS})
            set_target_properties(test_clap_host PROPERTIES BUILD_RPATH "${BA_OBS_DEPS}/lib")
        endif()
        add_test(NAME test_clap_host COMMAND test_clap_host)
        set_tests_properties(test_clap_host PROPERTIES SKIP_RETURN_CODE 77)
        add_executable(test_audio_rack tests/test_audio_rack.cpp audio/AudioRack.cpp audio/Analyzer.cpp
                                       audio/ClapHost.cpp audio/ClapEditor.cpp)
        target_include_directories(test_audio_rack PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${BA_CLAP_INC}")
        target_compile_definitions(test_audio_rack PRIVATE BA_TEST_LSP_CLAP="${BA_LSP_CLAP}" _USE_MATH_DEFINES)
        target_link_libraries(test_audio_rack PRIVATE nlohmann_json::nlohmann_json)
        if(NOT WIN32)
            target_link_libraries(test_audio_rack PRIVATE X11::X11 ${CMAKE_DL_LIBS})
        endif()
        add_executable(test_audio_modules tests/test_audio_modules.cpp audio/Modules.cpp audio/AudioRack.cpp
                                          audio/Analyzer.cpp audio/ClapHost.cpp audio/ClapEditor.cpp)
        target_include_directories(test_audio_modules PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${BA_CLAP_INC}")
        target_compile_definitions(test_audio_modules PRIVATE BA_TEST_LSP_CLAP="${BA_LSP_CLAP}")
        target_link_libraries(test_audio_modules PRIVATE nlohmann_json::nlohmann_json)
        if(NOT WIN32)
            target_link_libraries(test_audio_modules PRIVATE X11::X11 ${CMAKE_DL_LIBS})
        endif()
        add_test(NAME test_audio_modules COMMAND test_audio_modules)
        set_tests_properties(test_audio_modules PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)
        add_test(NAME test_audio_rack COMMAND test_audio_rack)
        set_tests_properties(test_audio_rack PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)
    endif()
    # Recepcion NDI abierta: descubrimiento + decodificador SpeedHQ del bundle.
    if(BA_HAVE_NDI)
        add_executable(test_ndi tests/test_ndi.cpp NdiSource.cpp)
        target_include_directories(test_ndi PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(test_ndi PRIVATE ba::libobs ba_noidea)
        if(WIN32)
            target_link_libraries(test_ndi PRIVATE ba_ffmpeg_win)
        else()
            target_link_libraries(test_ndi PRIVATE "${BA_OBS_DEPS}/lib/libavcodec.so" "${BA_OBS_DEPS}/lib/libavutil.so")
            set_target_properties(test_ndi PROPERTIES BUILD_RPATH "${BA_OBS_LINUX_PREFIX}/lib;${BA_OBS_DEPS}/lib")
        endif()
        add_test(NAME test_ndi COMMAND test_ndi)
    endif()
    # Guias para conectar YouTube/Twitch/Facebook (misma fuente que el MCP).
    add_executable(test_account_guides tests/test_account_guides.cpp AccountGuides.cpp)
    target_include_directories(test_account_guides PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_account_guides PRIVATE ba_core Qt6::Core)
    set_target_properties(test_account_guides PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_account_guides COMMAND test_account_guides)
    # Vigilancias (alertas) con entradas y reloj falsos.
    add_executable(test_alerts tests/test_alerts.cpp agent/AlertEngine.cpp agent/AlertEngine.hpp)
    target_include_directories(test_alerts PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_alerts PRIVATE ba_core Qt6::Core)
    set_target_properties(test_alerts PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_alerts COMMAND test_alerts)
    # Roles y despachador de agentes de un tiro, con un motor falso (script).
    add_executable(test_despachador tests/test_despachador.cpp agent/Despachador.cpp agent/Despachador.hpp
                   agent/Roles.cpp)
    target_include_directories(test_despachador PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_despachador PRIVATE ba_core Qt6::Core)
    set_target_properties(test_despachador PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_despachador COMMAND test_despachador)
    # Los discursos del teleprompter: carpetas, nombres y que nada salga de la raiz.
    add_executable(test_discursos tests/test_discursos.cpp ui/Discursos.cpp)
    target_include_directories(test_discursos PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_discursos PRIVATE Qt6::Core)
    set_target_properties(test_discursos PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_discursos COMMAND test_discursos)
    # El protocolo Cast: protobuf a mano y el troceo de mensajes.
    add_executable(test_cast tests/test_cast.cpp pantallas/Cast.cpp)
    target_include_directories(test_cast PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_cast PRIVATE ba_core Qt6::Core mbedtls mbedx509 mbedcrypto)
    set_target_properties(test_cast PROPERTIES BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_cast COMMAND test_cast)

    add_executable(test_twitch tests/test_twitch.cpp twitch/TwitchClient.cpp net/HttpsClient.cpp)
    target_include_directories(test_twitch PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_twitch PRIVATE ba_core Qt6::Core Qt6::Gui Qt6::Network mbedtls mbedx509 mbedcrypto)
    set_target_properties(test_twitch PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_twitch COMMAND test_twitch)

    # Facebook: login, Paginas, vivos RTMPS, comentarios y subida por partes contra un Graph falso.
    add_executable(test_facebook tests/test_facebook.cpp facebook/FacebookClient.cpp net/HttpsClient.cpp)
    target_include_directories(test_facebook PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_facebook PRIVATE ba_core Qt6::Core Qt6::Gui Qt6::Network mbedtls mbedx509 mbedcrypto)
    set_target_properties(test_facebook PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_facebook COMMAND test_facebook)
    # La interfaz parsea: qmllint sobre todos los QML (ver cmake/tests).
    find_program(BA_QMLLINT qmllint PATHS "${BA_QT_PREFIX}/bin" NO_DEFAULT_PATH)
    if(BA_QMLLINT)
        add_test(NAME test_qml_syntax COMMAND ${CMAKE_COMMAND} -DQMLLINT=${BA_QMLLINT}
                 -DQML_DIR=${CMAKE_CURRENT_SOURCE_DIR}/qml -P ${CMAKE_SOURCE_DIR}/cmake/tests/qml_syntax.cmake)
    endif()
else()
    target_compile_definitions(ba-app PRIVATE BA_APP_HAVE_QT_OBS=0)
    message(STATUS "ba-app: sin Qt vendoreado -> solo modo consola ('make deps-qt-host' lo baja)")
endif()

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
    # JPEG: capturas del servidor MCP y fotos de los oradores.
    list(APPEND _ba_qt_plugins "imageformats/libqjpeg.so")
    install(CODE "
        set(BA_QT_LIB_DIR \"${BA_QT_PREFIX}/lib\")
        set(BA_QT_PLUGIN_DIR \"${BA_QT_PREFIX}/plugins\")
        set(BA_QT_PLUGINS \"${_ba_qt_plugins}\")
        set(BA_APP_BUILD_EXE \"$<TARGET_FILE:ba-app>\")
        set(BA_QT_QML_DIR \"${BA_QT_PREFIX}/qml\")
        set(BA_QT_QML_MODULES \"QtQml;QtQuick\")
    ")
    install(SCRIPT "${CMAKE_SOURCE_DIR}/cmake/BaQtDeploy.cmake")
endif()

# Windows: Qt (DLLs junto a ba-app) + CRT de MSVC. Ver cmake/BaQtDeployWin.cmake.
if(BA_APP_QT_OK AND WIN32)
    find_program(BA_OBJDUMP_WIN NAMES llvm-objdump HINTS "${BA_WIN_LLVM}")
    install(CODE "
        set(BA_QT_BIN_DIR \"${BA_QT_PREFIX}/bin\")
        set(BA_QT_PLUGIN_DIR \"${BA_QT_PREFIX}/plugins\")
        set(BA_QT_PLUGINS \"platforms/qwindows.dll;platforms/qoffscreen.dll;imageformats/qjpeg.dll;imageformats/qsvg.dll;imageformats/qico.dll;tls/qschannelbackend.dll;tls/qcertonlybackend.dll;iconengines/qsvgicon.dll\")
        set(BA_APP_BUILD_EXE \"$<TARGET_FILE:ba-app>\")
        set(BA_QT_QML_DIR \"${BA_QT_PREFIX}/qml\")
        set(BA_QT_QML_MODULES \"QtQml;QtQuick;QtCore\")
        set(BA_OBJDUMP \"${BA_OBJDUMP_WIN}\")
        set(BA_CRT_REDIST_DIR \"${CMAKE_SOURCE_DIR}/extern/msvc/redist\")
    " COMPONENT bundle)
    install(SCRIPT "${CMAKE_SOURCE_DIR}/cmake/BaQtDeployWin.cmake" COMPONENT bundle)
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
