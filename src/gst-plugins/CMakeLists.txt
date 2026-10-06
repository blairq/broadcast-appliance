# ============================================================================
# src/gst-plugins — plugins de GStreamer propios (libgst*.so/.dylib/.dll)
#
# Se instalan en `gst-plugins/` del bundle; el programa agrega esa carpeta a
# la ruta de plugins de GStreamer (platform/GstEntorno), así los ve el
# programa y también `gst-launch-1.0` con su entorno.
#
#   superres/  basuperres (+ bafsr, baonnxsr, bavtsuperres, bacoremlsr): superresolución x2.
#   filtros/   bafiltros: bacolor, bacroma, barecorte, balegal, baptz, bamejora (tarjetas de imagen, legalizador, PTZ digital, mejorar imagen).
#   audio/     baaudio: baautomixer (mesa redonda por grupo), balimitador (limitador true peak del máster).
#   ipc/       baipc: baanillo y bavideoanillo (el audio y el video de los invitados que deja el gateway en memoria compartida)
#              y baanillosink (el programa y los mix-minus que el motor le escribe al gateway).
#   plugin/    baplugins: baplugin (tarjeta de un plugin de Python) y bapluginsrc (su entrada).
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

add_library(gstbafiltros MODULE filtros/plugin.cpp filtros/color.cpp filtros/croma.cpp filtros/recorte.cpp filtros/legal.cpp filtros/ptz.cpp filtros/mejora.cpp filtros/gl.cpp)
ba_plugin_gst(gstbafiltros)

# El DSP del núcleo va compilado adentro (sin enlazar todo ba_core).
add_library(gstbaaudio MODULE audio/plugin.cpp audio/automixer.cpp audio/limitador.cpp "${CMAKE_SOURCE_DIR}/src/core/dsp/Automixer.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/dsp/LimitadorTP.cpp")
target_include_directories(gstbaaudio PRIVATE "${CMAKE_SOURCE_DIR}/src/core")
ba_plugin_gst(gstbaaudio)

add_library(gstbaipc MODULE ipc/plugin.cpp ipc/anillo.cpp ipc/anillosink.cpp ipc/video.cpp "${CMAKE_SOURCE_DIR}/src/core/ipc/SharedAudioRing.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/ipc/SharedMemoryRegion.cpp" "${CMAKE_SOURCE_DIR}/src/core/ipc/SpoutTransport.cpp")
target_include_directories(gstbaipc PRIVATE "${CMAKE_SOURCE_DIR}/src/core" "${CMAKE_SOURCE_DIR}/src/core/ipc")
ba_plugin_gst(gstbaipc)
if(TRUE) # el test de baanillo (como el gateway: escribe el anillo)
    add_executable(test_baanillo tests/test_baanillo.cpp "${CMAKE_SOURCE_DIR}/src/core/ipc/SharedAudioRing.cpp"
        "${CMAKE_SOURCE_DIR}/src/core/ipc/SharedMemoryRegion.cpp")
    target_include_directories(test_baanillo PRIVATE "${CMAKE_SOURCE_DIR}/src/core" "${CMAKE_SOURCE_DIR}/src/core/ipc")
    target_link_libraries(test_baanillo PRIVATE ba::gstreamer)
    if(DEFINED BA_GST_LIBDIR)
        set_target_properties(test_baanillo PROPERTIES BUILD_RPATH "${BA_GST_LIBDIR}")
    endif()
    target_compile_definitions(test_baanillo PRIVATE BA_TEST_GST_PLUGINS="${CMAKE_CURRENT_BINARY_DIR}")
    add_dependencies(test_baanillo gstbaipc)
    add_test(NAME test_baanillo COMMAND test_baanillo)
    set_tests_properties(test_baanillo PROPERTIES SKIP_RETURN_CODE 77)
endif()

add_library(gstbaplugins MODULE plugin/baplugin.cpp)
target_link_libraries(gstbaplugins PRIVATE nlohmann_json::nlohmann_json)
ba_plugin_gst(gstbaplugins)

# El ONNX Runtime de esta plataforma (cmake/BaOnnxRuntime.cmake).
set(_ba_ort "${BA_ORT_LIB}")

set(_srs superres/plugin.cpp superres/fsr.cpp superres/onnxsr.cpp superres/glsl.cpp)
if(APPLE)
    list(APPEND _srs superres/vtsuperres_mac.mm superres/coremlsr_mac.mm superres/cvmemoria_mac.mm)
    set_source_files_properties(superres/vtsuperres_mac.mm superres/coremlsr_mac.mm superres/cvmemoria_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
endif()
add_library(gstbasuperres MODULE ${_srs})
ba_plugin_gst(gstbasuperres)
if(WIN32)
    # GL/glext.h y KHR/khrplatform.h (Khronos, MIT): gstglfuncs.h los pide y el
    # SDK de Windows no los trae (baglsl).
    target_include_directories(gstbasuperres PRIVATE "${CMAKE_SOURCE_DIR}/third_party/khronos")
endif()
if(EXISTS "${BA_ORT_DIR}/include/onnxruntime_cxx_api.h" AND EXISTS "${_ba_ort}")
    target_include_directories(gstbasuperres PRIVATE "${BA_ORT_DIR}/include")
    target_link_libraries(gstbasuperres PRIVATE "${_ba_ort}")
    target_compile_definitions(gstbasuperres PRIVATE BA_HAVE_ORT=1)
endif()
# NVIDIA sin bajar a la CPU (baonnxsr con memory:CUDAMemory): gst-cuda de
# GStreamer y los encabezados stub de CUDA que trae GStreamer (no hace falta el
# SDK de CUDA: gst-cuda carga el controlador al correr).
if(NOT APPLE AND DEFINED BA_GST_LIBDIR)
    get_filename_component(_ba_gst_raiz "${BA_GST_LIBDIR}" DIRECTORY)
    if(WIN32)
        set(_ba_gstcuda "${_ba_gst_raiz}/lib/gstcuda-1.0.lib")
    else()
        set(_ba_gstcuda "${BA_GST_LIBDIR}/libgstcuda-1.0.so")
    endif()
    if(EXISTS "${_ba_gstcuda}" AND EXISTS "${_ba_gst_raiz}/include/gstreamer-1.0/gst/cuda/gstcuda.h")
        target_include_directories(gstbasuperres PRIVATE "${CMAKE_SOURCE_DIR}/third_party/gst-cuda-stub")
        target_link_libraries(gstbasuperres PRIVATE "${_ba_gstcuda}")
        target_compile_definitions(gstbasuperres PRIVATE BA_HAVE_GST_CUDA=1 GST_USE_UNSTABLE_API=1)
        message(STATUS "gst-cuda: baonnxsr acepta memory:CUDAMemory")
    endif()
endif()
if(APPLE)
    target_link_libraries(gstbasuperres PRIVATE "-framework VideoToolbox" "-framework CoreVideo" "-framework CoreMedia" "-framework Foundation"
        "-framework CoreML" "-framework Accelerate")
endif()
