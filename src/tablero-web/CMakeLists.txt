# ============================================================================
# src/tablero-web — el dashboard de la app en el navegador (Qt 6.6.3 para WebAssembly)
#
# Proyecto APARTE del de la app: se configura con el qt-cmake de extern/qt6-wasm (Qt para
# WebAssembly, estático) y el emscripten de extern/emsdk. Lo arma `make tablero-web`
# (tools/deps/tablero-web.sh); nada de esto se instala en el sistema.
#
# La QML NO se copia: entran como recursos los mismos archivos de src/app/qml (las
# tarjetas, los controles, DashboardPage y Theme). Tocar una tarjeta cambia la app y la web.
# ============================================================================
cmake_minimum_required(VERSION 3.21)
project(ba_tablero LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Svg)

get_filename_component(BA_RAIZ "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(BA_APP "${BA_RAIZ}/src/app")

qt_add_executable(ba-tablero
    main.cpp
    ClienteReplica.cpp ClienteReplica.hpp
    ObjetoReplica.cpp ObjetoReplica.hpp
    VideoItemWeb.cpp VideoItemWeb.hpp
    Falsos.hpp
    # Los tipos de la app que andan igual en el navegador (sin motor ni red).
    ${BA_APP}/ui/Reloj.cpp ${BA_APP}/ui/Reloj.hpp
    ${BA_APP}/ui/FotosPersonas.cpp ${BA_APP}/ui/FotosPersonas.hpp
    ${BA_APP}/ui/Vumetro.cpp ${BA_APP}/ui/Vumetro.hpp
    ${BA_APP}/ui/ImagenViva.cpp ${BA_APP}/ui/ImagenViva.hpp
    ${BA_APP}/ui/ListaObjetos.cpp ${BA_APP}/ui/ListaObjetos.hpp ${BA_APP}/ui/ListaConClave.hpp
    ${BA_APP}/ui/FiltroCatalogo.cpp ${BA_APP}/ui/FiltroCatalogo.hpp
    ${BA_APP}/pantallas/BuscadorCatalogo.cpp ${BA_APP}/pantallas/BuscadorCatalogo.hpp
    ${BA_APP}/ui/ChatHilo.cpp ${BA_APP}/ui/ChatHilo.hpp
)
target_include_directories(ba-tablero PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${BA_APP}" "${BA_RAIZ}/src/core")
target_link_libraries(ba-tablero PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::QmlPrivate Qt6::Quick Qt6::QuickControls2 Qt6::Svg)

# --- La QML de la app, tal cual ------------------------------------------------------------
file(GLOB _ba_qml RELATIVE "${BA_APP}" CONFIGURE_DEPENDS
     "${BA_APP}/qml/cards/*.qml" "${BA_APP}/qml/controls/*.qml" "${BA_APP}/qml/controls/qmldir"
     "${BA_APP}/qml/Theme.qml" "${BA_APP}/qml/pages/DashboardPage.qml")
set(_ba_qml_abs "")
foreach(f ${_ba_qml})
    list(APPEND _ba_qml_abs "${BA_APP}/${f}")
endforeach()
qt_add_resources(ba-tablero "ba_qml" PREFIX "/" BASE "${BA_APP}" FILES ${_ba_qml_abs})
qt_add_resources(ba-tablero "ba_web" PREFIX "/web" BASE "${CMAKE_CURRENT_SOURCE_DIR}/qml" FILES
                 "${CMAKE_CURRENT_SOURCE_DIR}/qml/TableroWeb.qml")
# Los íconos de la app (qrc:/icons, como en ba-app).
file(GLOB _ba_iconos CONFIGURE_DEPENDS "${BA_RAIZ}/assets/icons/*.svg" "${BA_RAIZ}/assets/icons/*.png")
if(_ba_iconos)
    qt_add_resources(ba-tablero "ba_iconos" PREFIX "/icons" BASE "${BA_RAIZ}/assets/icons" FILES ${_ba_iconos})
endif()
# Las letras de Theme (Noto Sans y JetBrains Mono), adentro del .wasm.
set(_ba_fuentes "")
foreach(f "${BA_RAIZ}/extern/fuentes-texto/NotoSans-Regular.ttf" "${BA_RAIZ}/extern/fuentes-texto/NotoSans-Bold.ttf"
          "${BA_RAIZ}/extern/fonts/JetBrainsMono-Regular.ttf")
    if(EXISTS "${f}")
        list(APPEND _ba_fuentes "${f}")
    else()
        message(WARNING "tablero web: falta ${f} (make deps-fonts deps-fuentes-texto)")
    endif()
endforeach()
if(_ba_fuentes)
    qt_add_resources(ba-tablero "ba_fuentes" PREFIX "/fuentes" BASE "${BA_RAIZ}/extern" FILES ${_ba_fuentes})
endif()

# Los módulos QML que usa la QML (QtQuick.Controls, Layouts, Effects, Shapes, Dialogs…):
# el escáner mira los .qml de arriba.
qt_import_qml_plugins(ba-tablero PATH_TO_SCAN "${BA_APP}/qml" "${CMAKE_CURRENT_SOURCE_DIR}/qml")

# --- WebAssembly -----------------------------------------------------------------------------
# Sin hilos (wasm_singlethread): no hace falta COOP/COEP en el servidor y anda en cualquier
# navegador. Memoria inicial generosa (la QML del dashboard entera) y que crezca si hace falta.
target_link_options(ba-tablero PRIVATE "SHELL:-s INITIAL_MEMORY=134217728" "SHELL:-s ALLOW_MEMORY_GROWTH=1"
                    "SHELL:-s ASSERTIONS=0" "-O2" "--bind")
target_compile_options(ba-tablero PRIVATE "-O2")
