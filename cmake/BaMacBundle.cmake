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
# Las rutas del árbol de fuentes y del build, para el bloque de abajo (que va
# entre [=[ ]=] y no expande variables de la configuración).
install(CODE "set(_ba_src \"${CMAKE_SOURCE_DIR}\")\nset(_ba_bin \"${CMAKE_BINARY_DIR}\")" COMPONENT bundle)
install(CODE [=[
    set(_root "${CMAKE_INSTALL_PREFIX}")
    execute_process(COMMAND xattr -dr com.apple.quarantine "${_root}" ERROR_QUIET)
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
    # Solo lo nuevo o cambiado, en paralelo, conservando la fecha de cada archivo
    # (si no, cmake los vuelve a copiar en la instalación siguiente): ver
    # tools/deps/firmar-macos.sh. La cache va en el build (una por destino), no en
    # el bundle: si no, entraría en el manifest y en la .app.
    string(MD5 _clave "${_root}")
    execute_process(COMMAND bash "${_ba_src}/tools/deps/firmar-macos.sh" "${_root}" "${_firma}"
                            "${_ba_bin}/firmas-${_clave}.txt" "${_ba_bin}/firmados-${_clave}.txt"
                    RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "ba-bundle: falló la firma de los binarios")
    endif()
]=] COMPONENT bundle)
