# ============================================================================
# cmake/BaQtDeploy.cmake — copia el Qt vendoreado dentro del bundle portable
#
# Se ejecuta como paso de `install` (install(SCRIPT)), DESPUES de instalar
# ba-app. Variables que fija el CMakeLists antes de incluirlo:
#   BA_QT_LIB_DIR      lib/ del Qt vendoreado (extern/qt6-host/<ver>/gcc_64/lib)
#   BA_QT_PLUGIN_DIR   plugins/ del Qt vendoreado
#   BA_QT_PLUGINS      lista de plugins (relativos a plugins/) que viajan
#   BA_APP_BUILD_EXE   ba-app del arbol de build
#   BA_QT_QML_DIR      qml/ del Qt vendoreado
#   BA_QT_QML_MODULES  modulos QML que viajan (QtQml, QtQuick)
#
# Layout dentro del bundle (junto a ba-app, en bin/64bit/):
#   qt/lib/        bibliotecas de Qt (+ ICU) que ba-app y los plugins usan
#   qt/plugins/    plugins de plataforma (Wayland, X11...) y afines
#   qt/qml/        modulos QML de la interfaz
#   qt.conf        le dice a Qt donde estan plugins y modulos QML
# Los plugins oficiales traen RUNPATH relativo a su lib/ ($ORIGIN/../../lib,
# $ORIGIN/../../../lib): con este layout resuelven qt/lib sin reescribirlos.
# ba-app lleva RUNPATH $ORIGIN/qt/lib.
#
# Que bibliotecas copiar se CALCULA con GET_RUNTIME_DEPENDENCIES sobre ba-app
# y todos los plugins que viajan: solo las que viven en el Qt vendoreado. Las
# del sistema de escritorio (libc, GL, xcb, wayland, fontconfig) no viajan:
# son la base del sistema grafico, como para libobs.
# ============================================================================

set(_bin "${CMAKE_INSTALL_PREFIX}/bin/64bit")
set(_qtdst "${_bin}/qt")
set(_modules "")

# Plugins de plataforma y afines.
foreach(_p IN LISTS BA_QT_PLUGINS)
    if(EXISTS "${BA_QT_PLUGIN_DIR}/${_p}")
        get_filename_component(_cat "${_p}" DIRECTORY)
        file(INSTALL "${BA_QT_PLUGIN_DIR}/${_p}" DESTINATION "${_qtdst}/plugins/${_cat}")
        list(APPEND _modules "${BA_QT_PLUGIN_DIR}/${_p}")
    endif()
endforeach()

# Modulos QML (QtQuick, Controls, Layouts, Effects, Shapes...).
foreach(_m IN LISTS BA_QT_QML_MODULES)
    if(EXISTS "${BA_QT_QML_DIR}/${_m}")
        file(COPY "${BA_QT_QML_DIR}/${_m}" DESTINATION "${_qtdst}/qml" PATTERN "designer" EXCLUDE)
        file(GLOB_RECURSE _qmlplugins "${BA_QT_QML_DIR}/${_m}/*.so")
        list(APPEND _modules ${_qmlplugins})
    endif()
endforeach()

# El binario del BUILD (su RUNPATH apunta al Qt vendoreado): el instalado
# resolveria contra el sistema, porque qt/lib todavia esta vacio.
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${BA_APP_BUILD_EXE}"
    MODULES ${_modules}
    RESOLVED_DEPENDENCIES_VAR _deps
    UNRESOLVED_DEPENDENCIES_VAR _unresolved
    CONFLICTING_DEPENDENCIES_PREFIX _conflict
    DIRECTORIES "${BA_QT_LIB_DIR}"
    PRE_INCLUDE_REGEXES "libQt6.*" "libicu.*"
    PRE_EXCLUDE_REGEXES ".*")

# Cada plugin resuelve Qt por su RUNPATH, asi que la misma biblioteca aparece
# con rutas distintas ("conflicto" para CMake): es el mismo archivo. Se
# normaliza a la ruta absoluta y se deduplica.
foreach(_name IN LISTS _conflict_FILENAMES)
    list(GET _conflict_${_name} 0 _first)
    list(APPEND _deps "${_first}")
endforeach()
set(_norm "")
foreach(_d IN LISTS _deps)
    get_filename_component(_d "${_d}" ABSOLUTE)
    list(APPEND _norm "${_d}")
endforeach()
list(REMOVE_DUPLICATES _norm)
get_filename_component(BA_QT_LIB_DIR "${BA_QT_LIB_DIR}" ABSOLUTE)

set(_count 0)
foreach(_d IN LISTS _norm)
    string(FIND "${_d}" "${BA_QT_LIB_DIR}" _pos)
    if(_pos EQUAL 0)
        file(INSTALL "${_d}" DESTINATION "${_qtdst}/lib" TYPE SHARED_LIBRARY FOLLOW_SYMLINK_CHAIN)
        math(EXPR _count "${_count} + 1")
    endif()
endforeach()

file(WRITE "${_bin}/qt.conf" "[Paths]\nPrefix=qt\nPlugins=plugins\nQmlImports=qml\n")
message(STATUS "ba-bundle: Qt vendoreado copiado (${_count} bibliotecas + plugins + QML en bin/64bit/qt)")
