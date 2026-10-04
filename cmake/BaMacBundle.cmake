# ============================================================================
# cmake/BaMacBundle.cmake — ultimo paso del bundle portable en macOS
#
# `cmake --install` reescribe rpaths/install names con install_name_tool, y eso
# invalida la firma de codigo. En Apple Silicon un binario sin firma valida no
# arranca (el kernel lo mata), asi que todo lo instalado se re-firma: con el
# certificado propio «BA Desarrollo local» si esta (tools/firma-local.sh: la
# identidad no cambia entre compilaciones y macOS recuerda los permisos de
# camara y microfono), o ad-hoc ("-"). Alcanza para correr en esta Mac. Para distribuir a otras Macs hace
# falta firma Developer ID + notarizacion (docs/MACOS.md).
# Tambien se quita la cuarentena de Gatekeeper por si el arbol vino de un zip.
# ============================================================================
install(CODE [=[
    set(_root "${CMAKE_INSTALL_PREFIX}")
    execute_process(COMMAND xattr -dr com.apple.quarantine "${_root}" ERROR_QUIET)
    file(GLOB_RECURSE _all LIST_DIRECTORIES false "${_root}/*")
    # Con el certificado propio (tools/firma-local.sh) la identidad de la app
    # no cambia entre compilaciones y macOS recuerda los permisos (cámara,
    # micrófono). Solo si firma SIN preguntar (si no, la compilación quedaría
    # esperando un cartel): si no, ad-hoc, como siempre.
    set(_firma "-")
    execute_process(COMMAND security find-identity -p codesigning OUTPUT_VARIABLE _ids ERROR_QUIET)
    if(_ids MATCHES "BA Desarrollo local")
        set(_prueba "${_root}/.ba-firma-prueba")
        file(COPY_FILE /usr/bin/true "${_prueba}")
        execute_process(COMMAND codesign -f -s "BA Desarrollo local" "${_prueba}" RESULT_VARIABLE _rc
                        TIMEOUT 15 OUTPUT_QUIET ERROR_QUIET)
        if(_rc EQUAL 0)
            set(_firma "BA Desarrollo local")
        endif()
        file(REMOVE "${_prueba}")
    endif()
    message(STATUS "ba-bundle: firma ${_firma}")
    set(_n 0)
    foreach(_f IN LISTS _all)
        if(IS_SYMLINK "${_f}")
            continue()
        endif()
        # Lo que esta adentro de una .app (LibreOffice) se firma con ella, abajo:
        # firmar sus archivos sueltos rompe el sello de la .app.
        if(_f MATCHES "\\.app/")
            continue()
        endif()
        # Solo Mach-O: dylibs, .so, ejecutables y binarios de frameworks/plugins.
        execute_process(COMMAND file -b "${_f}" OUTPUT_VARIABLE _kind ERROR_QUIET)
        if(_kind MATCHES "Mach-O")
            execute_process(COMMAND codesign -f -s "${_firma}" "${_f}" RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
            if(_rc EQUAL 0)
                math(EXPR _n "${_n} + 1")
            else()
                message(WARNING "ba-bundle: no se pudo firmar ${_f}")
            endif()
        endif()
    endforeach()
    message(STATUS "ba-bundle: ${_n} binarios Mach-O firmados (${_firma})")
    # Cada .app entera (con --deep: sus frameworks y dylibs de adentro).
    file(GLOB_RECURSE _apps LIST_DIRECTORIES true "${_root}/*.app")
    foreach(_a IN LISTS _apps)
        if(IS_DIRECTORY "${_a}" AND _a MATCHES "\\.app$" AND NOT _a MATCHES "\\.app/.*\\.app$")
            execute_process(COMMAND codesign --force --deep -s "${_firma}" "${_a}" RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
            if(NOT _rc EQUAL 0)
                message(WARNING "ba-bundle: no se pudo firmar ${_a}")
            else()
                message(STATUS "ba-bundle: firmada ${_a}")
            endif()
        endif()
    endforeach()
]=] COMPONENT bundle)
