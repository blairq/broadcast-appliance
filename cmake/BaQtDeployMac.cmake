# ============================================================================
# cmake/BaQtDeployMac.cmake — Qt vendoreado dentro del bundle portable (macOS)
#
# Mismo papel y mismo layout que cmake/BaQtDeploy.cmake (Linux), con los
# frameworks de Qt para macOS (extern/qt6-host/<ver>/macos):
#   bin/64bit/qt/lib/      Qt*.framework que ba-app y los plugins usan
#   bin/64bit/qt/plugins/  cocoa (plataforma), imagenes, TLS, estilo macOS
#   bin/64bit/qt/qml/      modulos QML de la interfaz
#   bin/64bit/qt.conf      donde estan plugins y modulos QML
# ba-app lleva LC_RPATH @executable_path/qt/lib; los frameworks se refieren
# entre si por @rpath/QtX.framework/..., asi que resuelven ahi sin reescribirse.
#
# Variables (las fija src/app/CMakeLists.txt): BA_QT_LIB_DIR, BA_QT_PLUGIN_DIR,
# BA_QT_PLUGINS, BA_APP_BUILD_EXE, BA_QT_QML_DIR, BA_QT_QML_MODULES.
# ============================================================================

set(_bin "${CMAKE_INSTALL_PREFIX}/bin/64bit")
set(_qtdst "${_bin}/qt")
set(_modules "")

foreach(_p IN LISTS BA_QT_PLUGINS)
    if(EXISTS "${BA_QT_PLUGIN_DIR}/${_p}")
        get_filename_component(_cat "${_p}" DIRECTORY)
        file(INSTALL "${BA_QT_PLUGIN_DIR}/${_p}" DESTINATION "${_qtdst}/plugins/${_cat}")
        list(APPEND _modules "${BA_QT_PLUGIN_DIR}/${_p}")
    endif()
endforeach()

foreach(_m IN LISTS BA_QT_QML_MODULES)
    if(EXISTS "${BA_QT_QML_DIR}/${_m}")
        file(COPY "${BA_QT_QML_DIR}/${_m}" DESTINATION "${_qtdst}/qml" PATTERN "designer" EXCLUDE
             PATTERN "*.dSYM" EXCLUDE)
        file(GLOB_RECURSE _qmlplugins "${BA_QT_QML_DIR}/${_m}/*.dylib")
        list(APPEND _modules ${_qmlplugins})
    endif()
endforeach()

# Dependencias (Mach-O): solo las que viven en el Qt vendoreado.
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${BA_APP_BUILD_EXE}"
    MODULES ${_modules}
    RESOLVED_DEPENDENCIES_VAR _deps
    UNRESOLVED_DEPENDENCIES_VAR _unresolved
    CONFLICTING_DEPENDENCIES_PREFIX _conflict
    DIRECTORIES "${BA_QT_LIB_DIR}"
    PRE_INCLUDE_REGEXES ".*Qt[A-Za-z0-9]*\\.framework.*"
    PRE_EXCLUDE_REGEXES ".*")
foreach(_name IN LISTS _conflict_FILENAMES)
    list(GET _conflict_${_name} 0 _first)
    list(APPEND _deps "${_first}")
endforeach()

# Cada dependencia es .../QtX.framework/Versions/A/QtX: se copia el framework
# entero (sin headers), con sus enlaces simbolicos.
get_filename_component(BA_QT_LIB_DIR "${BA_QT_LIB_DIR}" REALPATH)
set(_fws "")
foreach(_d IN LISTS _deps)
    get_filename_component(_d "${_d}" REALPATH)
    if(_d MATCHES "^(.*/Qt[A-Za-z0-9]*\\.framework)/")
        list(APPEND _fws "${CMAKE_MATCH_1}")
    endif()
endforeach()
list(REMOVE_DUPLICATES _fws)
set(_count 0)
foreach(_fw IN LISTS _fws)
    string(FIND "${_fw}" "${BA_QT_LIB_DIR}" _pos)
    if(_pos EQUAL 0)
        file(COPY "${_fw}" DESTINATION "${_qtdst}/lib" PATTERN "Headers" EXCLUDE PATTERN "*.prl" EXCLUDE)
        math(EXPR _count "${_count} + 1")
    endif()
endforeach()

file(WRITE "${_bin}/qt.conf" "[Paths]\nPrefix=qt\nPlugins=plugins\nQmlImports=qml\n")
message(STATUS "ba-bundle: Qt vendoreado copiado (${_count} frameworks + plugins + QML en bin/64bit/qt)")
