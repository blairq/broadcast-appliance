# ============================================================================
# cmake/BaMacBundle.cmake — ultimo paso del bundle portable en macOS
#
# `cmake --install` reescribe rpaths/install names con install_name_tool, y eso
# invalida la firma de codigo. En Apple Silicon un binario sin firma valida no
# arranca (el kernel lo mata), asi que todo lo instalado se re-firma ad-hoc
# ("-"): alcanza para correr en esta Mac. Para distribuir a otras Macs hace
# falta firma Developer ID + notarizacion (docs/MACOS.md).
# Tambien se quita la cuarentena de Gatekeeper por si el arbol vino de un zip.
# ============================================================================
install(CODE [=[
    set(_root "${CMAKE_INSTALL_PREFIX}")
    execute_process(COMMAND xattr -dr com.apple.quarantine "${_root}" ERROR_QUIET)
    file(GLOB_RECURSE _all LIST_DIRECTORIES false "${_root}/*")
    set(_n 0)
    foreach(_f IN LISTS _all)
        if(IS_SYMLINK "${_f}")
            continue()
        endif()
        # Solo Mach-O: dylibs, .so, ejecutables y binarios de frameworks/plugins.
        execute_process(COMMAND file -b "${_f}" OUTPUT_VARIABLE _kind ERROR_QUIET)
        if(_kind MATCHES "Mach-O")
            execute_process(COMMAND codesign -f -s - "${_f}" RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
            if(_rc EQUAL 0)
                math(EXPR _n "${_n} + 1")
            else()
                message(WARNING "ba-bundle: no se pudo firmar ${_f}")
            endif()
        endif()
    endforeach()
    message(STATUS "ba-bundle: ${_n} binarios Mach-O firmados ad-hoc")
]=] COMPONENT bundle)
