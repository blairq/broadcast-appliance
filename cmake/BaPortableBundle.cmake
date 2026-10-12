# ============================================================================
# cmake/BaPortableBundle.cmake — hace PORTABLE el bundle instalado (Linux)
#
# Proposito:
#   Un bundle portable tiene que resolver TODAS sus bibliotecas dentro de si
#   mismo. Los ejecutables propios ya se enlazan con rpath `$ORIGIN`, pero las
#   bibliotecas de terceros que se copian al bundle traen el rpath del arbol
#   donde se compilaron, o ninguno: sin corregirlo, el bundle falla en una
#   maquina de destino con "no se encontro libX" aunque el archivo este ahi al
#   lado.
#
#   1) Copia a bin/64bit las bibliotecas de terceros compiladas desde fuente
#      (tools/deps/terceros-linux.sh, en extern/terceros-linux): FFmpeg, x264,
#      libva y libv4l2 (las piden los plugins de GStreamer, que buscan en
#      bin/64bit con su RUNPATH) y cairo, pixman, libpng y libsndfile (las
#      piden los plugins LSP y el plugin cairo de GStreamer).
#   2) Reescribe el rpath de lo instalado:
#        - bin/64bit/*.so*        -> $ORIGIN
#        - plugins/lsp/*.clap     -> $ORIGIN:$ORIGIN/../../bin/64bit
#
#   Se ejecuta como paso de `install`, sobre los archivos ya copiados; no toca
#   nada en `extern/`.
# ============================================================================

set(BA_TERCEROS_LINUX "${CMAKE_SOURCE_DIR}/extern/terceros-linux")
if(EXISTS "${BA_TERCEROS_LINUX}/lib")
    set(_ba_terceros "")
    foreach(_pat avcodec avdevice avfilter avformat avutil swresample swscale x264 va va-drm v4l2 v4lconvert
                 cairo cairo-gobject pixman-1 png16 sndfile)
        file(GLOB _dep "${BA_TERCEROS_LINUX}/lib/lib${_pat}.so.*")
        list(APPEND _ba_terceros ${_dep})
    endforeach()
    if(_ba_terceros)
        install(FILES ${_ba_terceros} DESTINATION bin/64bit)
    endif()
    message(STATUS "ba-bundle: bibliotecas de terceros de ${BA_TERCEROS_LINUX}")
else()
    message(STATUS "ba-bundle: falta extern/terceros-linux ('make deps-terceros-linux'): el bundle no va a ser portable")
endif()

find_program(BA_PATCHELF patchelf)

if(NOT BA_PATCHELF)
    message(STATUS "ba-bundle: patchelf ausente; el bundle necesitara LD_LIBRARY_PATH")
    return()
endif()

# `install(CODE ...)` corre en tiempo de instalacion, cuando los archivos ya
# estan en su destino final.
install(CODE "
    set(_bin \"\${CMAKE_INSTALL_PREFIX}/bin/64bit\")

    file(GLOB _libs \"\${_bin}/*.so\" \"\${_bin}/*.so.*\")
    foreach(_lib IN LISTS _libs)
        if(IS_SYMLINK \"\${_lib}\")
            continue()
        endif()
        execute_process(COMMAND \"${BA_PATCHELF}\" --set-rpath \"\\\$ORIGIN\" \"\${_lib}\"
                        RESULT_VARIABLE _rc ERROR_QUIET)
    endforeach()

    # Plugins de audio LSP (CLAP): sus bibliotecas (cairo, libsndfile...) en bin/64bit.
    file(GLOB _claps \"\${CMAKE_INSTALL_PREFIX}/plugins/lsp/*.clap\")
    foreach(_c IN LISTS _claps)
        execute_process(COMMAND \"${BA_PATCHELF}\"
                        --set-rpath \"\\\$ORIGIN:\\\$ORIGIN/../../bin/64bit\" \"\${_c}\"
                        RESULT_VARIABLE _rc ERROR_QUIET)
    endforeach()

    message(STATUS \"ba-bundle: rpath del bundle fijado a \\\$ORIGIN (portable)\")
")
