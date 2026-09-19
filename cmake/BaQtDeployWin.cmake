# ============================================================================
# cmake/BaQtDeployWin.cmake — Windows: Qt vendoreado + CRT dentro del bundle
#
# Se ejecuta como paso de `install` (install(SCRIPT)), DESPUES de instalar
# ba-app. Variables que fija src/app/CMakeLists.txt antes de incluirlo:
#   BA_QT_BIN_DIR      bin/ del Qt msvc2019_64 (sus DLLs)
#   BA_QT_PLUGIN_DIR   plugins/
#   BA_QT_PLUGINS      plugins que viajan (relativos a plugins/)
#   BA_QT_QML_DIR      qml/
#   BA_QT_QML_MODULES  modulos QML que viajan
#   BA_APP_BUILD_EXE   ba-app.exe del arbol de build
#   BA_OBJDUMP         llvm-objdump (lee la tabla de importaciones de un PE)
#   BA_CRT_REDIST_DIR  DLLs redistribuibles del CRT de MSVC (extern/msvc/redist)
#
# Layout (Windows busca las DLLs junto al .exe):
#   bin/64bit/*.dll          Qt6*.dll que hacen falta + CRT (vcruntime140...)
#   bin/64bit/qt/plugins/    plataforma (qwindows), formatos de imagen, TLS
#   bin/64bit/qt/qml/        modulos QML de la interfaz
#   bin/64bit/qt.conf        le dice a Qt donde estan plugins y modulos QML
#
# Que DLLs de Qt copiar se CALCULA siguiendo las importaciones (llvm-objdump)
# de ba-app.exe, los plugins y los modulos QML: solo viaja lo que se usa, y
# nunca las variantes de depuracion (*d.dll) que trae el SDK.
# ============================================================================

set(_bin "${CMAKE_INSTALL_PREFIX}/bin/64bit")
set(_qtdst "${_bin}/qt")

# DLL de depuracion del SDK de Qt: "Qt6Cored.dll", "qwindowsd.dll"... (existe
# la misma sin la "d" final).
function(_ba_is_debug_dll path out)
    get_filename_component(_d "${path}" DIRECTORY)
    get_filename_component(_n "${path}" NAME_WE)
    set(${out} FALSE PARENT_SCOPE)
    if(_n MATCHES "d$")
        string(REGEX REPLACE "d$" "" _r "${_n}")
        if(EXISTS "${_d}/${_r}.dll")
            set(${out} TRUE PARENT_SCOPE)
        endif()
    endif()
endfunction()

set(_scan "${BA_APP_BUILD_EXE}")

# Plugins de Qt.
foreach(_p IN LISTS BA_QT_PLUGINS)
    if(EXISTS "${BA_QT_PLUGIN_DIR}/${_p}")
        get_filename_component(_cat "${_p}" DIRECTORY)
        file(INSTALL "${BA_QT_PLUGIN_DIR}/${_p}" DESTINATION "${_qtdst}/plugins/${_cat}")
        list(APPEND _scan "${BA_QT_PLUGIN_DIR}/${_p}")
    endif()
endforeach()

# Modulos QML, sin DLLs de depuracion, .pdb ni archivos de diseñador.
foreach(_m IN LISTS BA_QT_QML_MODULES)
    if(NOT EXISTS "${BA_QT_QML_DIR}/${_m}")
        continue()
    endif()
    file(GLOB_RECURSE _files RELATIVE "${BA_QT_QML_DIR}" "${BA_QT_QML_DIR}/${_m}/*")
    foreach(_f IN LISTS _files)
        if(_f MATCHES "\\.pdb$" OR _f MATCHES "/designer/")
            continue()
        endif()
        if(_f MATCHES "\\.dll$")
            _ba_is_debug_dll("${BA_QT_QML_DIR}/${_f}" _dbg)
            if(_dbg)
                continue()
            endif()
            list(APPEND _scan "${BA_QT_QML_DIR}/${_f}")
        endif()
        get_filename_component(_sub "${_f}" DIRECTORY)
        file(INSTALL "${BA_QT_QML_DIR}/${_f}" DESTINATION "${_qtdst}/qml/${_sub}")
    endforeach()
endforeach()

# Cierre transitivo de importaciones sobre bin/ de Qt.
set(_seen "")
set(_copied 0)
while(_scan)
    list(POP_FRONT _scan _pe)
    execute_process(COMMAND "${BA_OBJDUMP}" -p "${_pe}" OUTPUT_VARIABLE _dump ERROR_QUIET)
    string(REGEX MATCHALL "DLL Name: [^\r\n]+" _imports "${_dump}")
    foreach(_i IN LISTS _imports)
        string(REGEX REPLACE "^DLL Name: " "" _dll "${_i}")
        string(TOLOWER "${_dll}" _key)
        list(FIND _seen "${_key}" _idx)
        if(NOT _idx EQUAL -1)
            continue()
        endif()
        list(APPEND _seen "${_key}")
        if(EXISTS "${BA_QT_BIN_DIR}/${_dll}")
            file(INSTALL "${BA_QT_BIN_DIR}/${_dll}" DESTINATION "${_bin}")
            list(APPEND _scan "${BA_QT_BIN_DIR}/${_dll}")
            math(EXPR _copied "${_copied} + 1")
        endif()
    endforeach()
endwhile()

# Render por software si la maquina no tiene GPU/driver usable (Mesa llvmpipe).
if(EXISTS "${BA_QT_BIN_DIR}/opengl32sw.dll")
    file(INSTALL "${BA_QT_BIN_DIR}/opengl32sw.dll" DESTINATION "${_bin}")
endif()

# CRT de MSVC junto a los .exe (despliegue app-local): el sistema no necesita
# el instalador "VC++ Redistributable".
file(GLOB _crt "${BA_CRT_REDIST_DIR}/*.dll")
if(_crt)
    file(INSTALL ${_crt} DESTINATION "${_bin}")
endif()

file(WRITE "${_bin}/qt.conf" "[Paths]\nPrefix=qt\nPlugins=plugins\nQmlImports=qml\n")
message(STATUS "ba-bundle: Qt vendoreado (${_copied} DLLs + plugins + QML) y CRT de MSVC en bin/64bit")
