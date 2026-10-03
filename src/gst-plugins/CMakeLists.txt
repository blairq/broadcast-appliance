# ============================================================================
# src/gst-plugins — plugins de GStreamer propios (libgst*.so/.dylib/.dll)
#
# Se instalan en `gst-plugins/` del bundle; el programa agrega esa carpeta a
# la ruta de plugins de GStreamer (platform/GstEntorno), así los ve el
# programa y también `gst-launch-1.0` con su entorno.
#
#   superres/  basuperres (+ bafsr, baonnxsr, bavtsuperres): superresolución x2.
# ============================================================================

set(BA_ORT_DIR "${CMAKE_SOURCE_DIR}/extern/rostros/onnxruntime")
if(WIN32)
    set(_ba_ort "${BA_ORT_DIR}/lib/onnxruntime.lib")
elseif(APPLE)
    set(_ba_ort "${BA_ORT_DIR}/lib/libonnxruntime.dylib")
else()
    set(_ba_ort "${BA_ORT_DIR}/lib/libonnxruntime.so")
endif()

set(_srs superres/plugin.cpp superres/fsr.cpp superres/onnxsr.cpp)
if(APPLE)
    list(APPEND _srs superres/vtsuperres_mac.mm)
    set_source_files_properties(superres/vtsuperres_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
endif()
add_library(gstbasuperres MODULE ${_srs})
target_link_libraries(gstbasuperres PRIVATE ba::gstreamer)
if(EXISTS "${BA_ORT_DIR}/include/onnxruntime_cxx_api.h" AND EXISTS "${_ba_ort}")
    target_include_directories(gstbasuperres PRIVATE "${BA_ORT_DIR}/include")
    target_link_libraries(gstbasuperres PRIVATE "${_ba_ort}")
    target_compile_definitions(gstbasuperres PRIVATE BA_HAVE_ORT=1)
endif()
if(APPLE)
    target_link_libraries(gstbasuperres PRIVATE "-framework VideoToolbox" "-framework CoreVideo" "-framework CoreMedia" "-framework Foundation")
    # Como lo busca GStreamer en macOS: libgst<nombre>.dylib.
    set_target_properties(gstbasuperres PROPERTIES PREFIX "lib" SUFFIX ".dylib"
        INSTALL_RPATH "@loader_path/../bin/64bit;@loader_path/../bin/64bit/GStreamer.framework/Versions/1.0/lib"
        BUILD_RPATH "${BA_ORT_DIR}/lib;${BA_GST_LIBDIR}")
elseif(NOT WIN32)
    set_target_properties(gstbasuperres PROPERTIES PREFIX "lib" INSTALL_RPATH "$ORIGIN/../bin/64bit")
endif()
install(TARGETS gstbasuperres LIBRARY DESTINATION gst-plugins RUNTIME DESTINATION gst-plugins COMPONENT bundle)
