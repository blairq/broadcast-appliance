# ============================================================================
# cmake/BaPortableBundle.cmake — hace PORTABLE el bundle instalado (Linux)
#
# Proposito:
#   Un bundle portable tiene que resolver TODAS sus bibliotecas dentro de si
#   mismo. Los ejecutables propios ya se enlazan con rpath `$ORIGIN`, pero las
#   bibliotecas de terceros que se copian al bundle (libobs, su modulo grafico,
#   los plugins de OBS) traen el rpath del arbol donde se compilaron, o
#   ninguno. Cuando libobs hace `dlopen("libobs-opengl")`, el enlazador usa el
#   rpath de LIBOBS, no el del ejecutable: sin corregirlo, el bundle falla en
#   una maquina de destino con "no se encontro libobs-opengl" aunque el archivo
#   este ahi al lado.
#
#   Este script reescribe el rpath de lo instalado:
#     - bin/64bit/*.so*        -> $ORIGIN
#     - obs-plugins/64bit/*.so -> $ORIGIN:$ORIGIN/../../bin/64bit
#
#   Se ejecuta como paso de `install`, sobre los archivos ya copiados; no toca
#   nada en `extern/`.
# ============================================================================

find_program(BA_PATCHELF patchelf)

if(NOT BA_PATCHELF)
    message(STATUS "ba-bundle: patchelf ausente; el bundle necesitara LD_LIBRARY_PATH")
    return()
endif()

# `install(CODE ...)` corre en tiempo de instalacion, cuando los archivos ya
# estan en su destino final.
install(CODE "
    set(_bin \"\${CMAKE_INSTALL_PREFIX}/bin/64bit\")
    set(_plug \"\${CMAKE_INSTALL_PREFIX}/obs-plugins/64bit\")

    # Bibliotecas y el helper de grabacion (obs-ffmpeg-mux usa FFmpeg del bundle).
    file(GLOB _libs \"\${_bin}/*.so\" \"\${_bin}/*.so.*\" \"\${_bin}/obs-ffmpeg-mux\")
    foreach(_lib IN LISTS _libs)
        execute_process(COMMAND \"${BA_PATCHELF}\" --set-rpath \"\\\$ORIGIN\" \"\${_lib}\"
                        RESULT_VARIABLE _rc ERROR_QUIET)
    endforeach()

    # (y GStreamer del bundle: ba-sources lo usa para las tarjetas gst)
    file(GLOB _plugins \"\${_plug}/*.so\")
    foreach(_p IN LISTS _plugins)
        execute_process(COMMAND \"${BA_PATCHELF}\"
                        --set-rpath \"\\\$ORIGIN:\\\$ORIGIN/../../bin/64bit:\\\$ORIGIN/../../bin/64bit/gstreamer/lib\" \"\${_p}\"
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
