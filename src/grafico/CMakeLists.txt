# ============================================================================
# src/grafico — `ba-grafico`: el renderizador de gráficos animados, aparte
#
# Es otro proceso a propósito (ver main.cpp): un gráfico es código que se está
# escribiendo, o sea código roto todo el tiempo. Si corriera adentro de la app,
# un bucle infinito colgaría el programa que está al aire.
#
# Necesita Qt Quick, el mismo vendoreado que usa ba-app. Sin Qt no se compila:
# el resto del árbol sigue andando igual (los gráficos no van a existir, y la
# app lo va a decir).
# ============================================================================

# El Qt vendoreado, SOLO de extern/ (nunca el del sistema): el bundle es
# portable. Se busca igual que en src/app, pero aparte, porque las variables
# de un subdirectorio no las ve un hermano.
if(WIN32)
    set(_g_qt_root "${CMAKE_SOURCE_DIR}/extern/qt6/${QT6_VERSION}")
else()
    set(_g_qt_root "${CMAKE_SOURCE_DIR}/extern/qt6-host/${QT6_VERSION}")
endif()
file(GLOB _g_qt_cfg "${_g_qt_root}/*/lib/cmake/Qt6/Qt6Config.cmake")
if(NOT _g_qt_cfg)
    message(STATUS "graficos: sin Qt vendoreado, no se compila ba-grafico")
    return()
endif()
list(GET _g_qt_cfg 0 _g_qt_cfg)
get_filename_component(_g_qt_dir "${_g_qt_cfg}" DIRECTORY)
get_filename_component(_g_qt_prefix "${_g_qt_dir}/../../.." ABSOLUTE)
list(PREPEND CMAKE_PREFIX_PATH "${_g_qt_prefix}")
find_package(Qt6 COMPONENTS Gui Qml Quick CONFIG PATHS "${_g_qt_prefix}" NO_DEFAULT_PATH QUIET)
if(NOT Qt6_FOUND)
    message(STATUS "graficos: Qt6 incompleto, no se compila ba-grafico")
    return()
endif()

add_executable(ba-grafico
    main.cpp
    Declarativo.cpp
    Renderizador.cpp
    # El header va en la lista a proposito: tiene un Q_OBJECT y no tiene .cpp
    # propio, asi que AUTOMOC no lo encontraria solo.
    RelojAnimaciones.hpp
)
set_target_properties(ba-grafico PROPERTIES AUTOMOC ON)
target_include_directories(ba-grafico PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
find_package(Qt6 COMPONENTS OpenGL CONFIG PATHS "${_g_qt_prefix}" NO_DEFAULT_PATH QUIET)
# ba_core: el registro comun (diag/Registro). Un grafico roto tiene que dejar
# rastro en Logs/ como cualquier otra cosa, no solo en un stderr que nadie lee.
target_link_libraries(ba-grafico PRIVATE Qt6::Gui Qt6::OpenGL Qt6::Qml Qt6::Quick ba_core)

# rpath $ORIGIN, igual que ba-app: el bundle es portable y NUNCA usa el Qt del
# sistema. Sin esto, ba-grafico enlazaba contra el Qt de la maquina y en
# cualquier equipo con un Qt distinto ni arrancaba ("version Qt_6.6 not
# found"), que es exactamente lo que el vendoreado viene a evitar.
if(APPLE)
    # macOS no entiende $ORIGIN: el equivalente es @executable_path.
    set_target_properties(ba-grafico PROPERTIES
        BUILD_RPATH "${_g_qt_prefix}/lib"
        INSTALL_RPATH "@executable_path;@executable_path/qt/lib")
elseif(NOT WIN32)
    set_target_properties(ba-grafico PROPERTIES
        BUILD_RPATH "${_g_qt_prefix}/lib"
        INSTALL_RPATH "$ORIGIN;$ORIGIN/qt/lib")
endif()

install(TARGETS ba-grafico RUNTIME DESTINATION bin/64bit COMPONENT bundle)

# Los gráficos que vienen de fábrica (zócalos, placas) viajan con el bundle.
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/graficos")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/graficos/"
        DESTINATION Graficos COMPONENT bundle
    )
endif()
