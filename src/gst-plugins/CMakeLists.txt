# ============================================================================
# src/gst-plugins — plugins de GStreamer propios (libgst*.so/.dylib/.dll)
#
# Se instalan en `gst-plugins/` del bundle; el programa agrega esa carpeta a
# la ruta de plugins de GStreamer (platform/GstEntorno), así los ve el
# programa y también `gst-launch-1.0` con su entorno.
#
#   superres/  basuperres (+ bafsr, baonnxsr, bavtsuperres, bacoremlsr): superresolución x2.
#   filtros/   bafiltros: bacolor, bacroma, barecorte (las tarjetas de imagen).
# ============================================================================

# Lo que comparten todos: nombre libgst<x> y rutas para hallar GStreamer en el bundle.
function(ba_plugin_gst target)
    target_link_libraries(${target} PRIVATE ba::gstreamer)
    if(APPLE)
        set_target_properties(${target} PROPERTIES PREFIX "lib" SUFFIX ".dylib"
            INSTALL_RPATH "@loader_path/../bin/64bit;@loader_path/../bin/64bit/GStreamer.framework/Versions/1.0/lib"
            BUILD_RPATH "${BA_ORT_DIR}/lib;${BA_GST_LIBDIR}")
    elseif(NOT WIN32)
        set_target_properties(${target} PROPERTIES PREFIX "lib" INSTALL_RPATH "$ORIGIN/../bin/64bit;$ORIGIN/../bin/64bit/gstreamer/lib"
            BUILD_RPATH "${BA_ORT_DIR}/lib;${BA_GST_LIBDIR}")
    endif()
    install(TARGETS ${target} LIBRARY DESTINATION gst-plugins RUNTIME DESTINATION gst-plugins COMPONENT bundle)
endfunction()

add_library(gstbafiltros MODULE filtros/plugin.cpp filtros/color.cpp filtros/croma.cpp filtros/recorte.cpp)
ba_plugin_gst(gstbafiltros)

# El ONNX Runtime de esta plataforma (cmake/BaOnnxRuntime.cmake).
set(_ba_ort "${BA_ORT_LIB}")

set(_srs superres/plugin.cpp superres/fsr.cpp superres/onnxsr.cpp superres/glsl.cpp)
if(APPLE)
    list(APPEND _srs superres/vtsuperres_mac.mm superres/coremlsr_mac.mm superres/cvmemoria_mac.mm)
    set_source_files_properties(superres/vtsuperres_mac.mm superres/coremlsr_mac.mm superres/cvmemoria_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
endif()
add_library(gstbasuperres MODULE ${_srs})
ba_plugin_gst(gstbasuperres)
if(EXISTS "${BA_ORT_DIR}/include/onnxruntime_cxx_api.h" AND EXISTS "${_ba_ort}")
    target_include_directories(gstbasuperres PRIVATE "${BA_ORT_DIR}/include")
    target_link_libraries(gstbasuperres PRIVATE "${_ba_ort}")
    target_compile_definitions(gstbasuperres PRIVATE BA_HAVE_ORT=1)
endif()
if(APPLE)
    target_link_libraries(gstbasuperres PRIVATE "-framework VideoToolbox" "-framework CoreVideo" "-framework CoreMedia" "-framework Foundation"
        "-framework CoreML" "-framework Accelerate")
endif()
