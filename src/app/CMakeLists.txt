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
        find_package(Qt6 COMPONENTS Gui Quick Qml QuickControls2 Network CONFIG PATHS "${BA_QT_PREFIX}" NO_DEFAULT_PATH QUIET)
        set(BA_APP_QT_OK ${Qt6_FOUND})
    endif()
endif()
if(NOT BA_APP_QT_OK)
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/(GuiMode)\\.cpp$")
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/ui/.*")
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
    set_target_properties(ba-app PROPERTIES WIN32_EXECUTABLE ON)
endif()

target_link_libraries(ba-app PRIVATE ba_core)

# --- Motor de emision sobre libobs (spec §6) --------------------------------
# libobs es cross-platform y el motor tambien: cuando `ba::libobs` existe se
# compila `BroadcastEngine` en CUALQUIER plataforma, no solo en Windows. Ese
# acoplamiento (todo lo de obs detras de `_WIN32`) era lo que mantenia el motor
# sin compilar ni ejecutar en ningun lado.
if(BA_HAVE_LIBOBS)
    target_link_libraries(ba-app PRIVATE ba::libobs)
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

if(BA_APP_QT_OK)
    target_link_libraries(ba-app PRIVATE Qt6::Gui Qt6::Quick Qt6::Qml Qt6::QuickControls2 Qt6::Network)
    target_compile_definitions(ba-app PRIVATE BA_APP_HAVE_QT_OBS=1)
    target_include_directories(ba-app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    set_target_properties(ba-app PROPERTIES AUTOMOC ON)
    # Vista: los .qml viajan como recursos dentro del binario (qrc:/qml/...).
    file(GLOB_RECURSE BA_APP_QML RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.qml")
    qt_add_resources(ba-app "ba_qml" PREFIX "/" FILES ${BA_APP_QML})
    message(STATUS "ba-app: interfaz grafica con Qt ${Qt6_VERSION} vendoreado (${BA_QT_PREFIX})")
    # Test de la logica de disposicion del dashboard (ViewModel sin interfaz).
    add_executable(test_dashboard_model tests/test_dashboard_model.cpp ui/DashboardModel.cpp)
    target_include_directories(test_dashboard_model PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_dashboard_model PRIVATE Qt6::Core Qt6::Gui)
    set_target_properties(test_dashboard_model PROPERTIES AUTOMOC ON BUILD_RPATH "${BA_QT_PREFIX}/lib")
    add_test(NAME test_dashboard_model COMMAND test_dashboard_model)
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
