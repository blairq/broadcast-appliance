# ============================================================================
# src/gst-plugins — plugins de GStreamer propios (libgst*.so/.dylib/.dll)
#
# Se instalan en `gst-plugins/` del bundle; el programa agrega esa carpeta a
# la ruta de plugins de GStreamer (platform/GstEntorno), así los ve el
# programa y también `gst-launch-1.0` con su entorno.
#
#   superres/  basuperres (+ bafsr, baonnxsr, bavtsuperres, bacoremlsr): superresolución x2.
#   filtros/   bafiltros: bacolor, bacroma, barecorte, balegal, baptz, bamejora, ba360, basombra (tarjetas de imagen, legalizador, PTZ digital, mejorar imagen, vista 360, sombra).
#   audio/     baaudio: baautomixer (mesa redonda por grupo), balimitador (limitador true peak del máster).
#   ipc/       baipc: baanillo y bavideoanillo (el audio y el video de los invitados que deja el gateway en memoria compartida)
#              y baanillosink (el programa y los mix-minus que el motor le escribe al gateway).
#   plugin/    baplugins: baplugin (tarjeta de un plugin de Python) y bapluginsrc (su entrada).
#   rostros/   barostros (+ barostrosgl, barostrosd3d12): caras en la cadena de una entrada.
#   captura/   bacaptura (Mac): bacapturaventana, la ventana de una aplicación (ScreenCaptureKit).
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

# bacaptura (solo la Mac): bacapturaventana, la ventana de una aplicación con ScreenCaptureKit
# (en Windows y Linux lo hacen d3d12screencapturesrc y ximagesrc: ver src/motor/Capturas).
if(APPLE)
    add_library(gstbacaptura MODULE captura/ventana_mac.mm)
    set_source_files_properties(captura/ventana_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    ba_plugin_gst(gstbacaptura)
    target_link_libraries(gstbacaptura PRIVATE "-framework ScreenCaptureKit" "-framework CoreMedia" "-framework CoreVideo"
                                               "-framework Foundation")
endif()

add_library(gstbafiltros MODULE filtros/plugin.cpp filtros/color.cpp filtros/croma.cpp filtros/recorte.cpp filtros/legal.cpp filtros/ptz.cpp filtros/mejora.cpp filtros/gl.cpp filtros/salida.cpp filtros/vista360.cpp filtros/sombra.cpp)
ba_plugin_gst(gstbafiltros)
if(WIN32)
    # ba360d3d12: la biblioteca D3D12 de GStreamer (de plugins-bad: API «inestable»).
    target_link_libraries(gstbafiltros PRIVATE "${CMAKE_SOURCE_DIR}/extern/gstreamer-win/lib/gstd3d12-1.0.lib")
    target_compile_definitions(gstbafiltros PRIVATE GST_USE_UNSTABLE_API)
endif()

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
    # El video del canal (como el gateway: publica BA.Guest.<n>): el tamaño del
    # canal en las caps, también después de que abajo pidan renegociar.
    add_executable(test_bavideoanillo tests/test_bavideoanillo.cpp "${CMAKE_SOURCE_DIR}/src/core/ipc/SpoutTransport.cpp"
        "${CMAKE_SOURCE_DIR}/src/core/ipc/SharedMemoryRegion.cpp")
    target_include_directories(test_bavideoanillo PRIVATE "${CMAKE_SOURCE_DIR}/src/core" "${CMAKE_SOURCE_DIR}/src/core/ipc")
    target_link_libraries(test_bavideoanillo PRIVATE ba::gstreamer)
    if(DEFINED BA_GST_LIBDIR)
        set_target_properties(test_bavideoanillo PROPERTIES BUILD_RPATH "${BA_GST_LIBDIR}")
    endif()
    target_compile_definitions(test_bavideoanillo PRIVATE BA_TEST_GST_PLUGINS="${CMAKE_CURRENT_BINARY_DIR}")
    add_dependencies(test_bavideoanillo gstbaipc)
    add_test(NAME test_bavideoanillo COMMAND test_bavideoanillo)
    set_tests_properties(test_bavideoanillo PROPERTIES SKIP_RETURN_CODE 77)
endif()

add_library(gstbaplugins MODULE plugin/baplugin.cpp)
target_link_libraries(gstbaplugins PRIVATE nlohmann_json::nlohmann_json)
if(WIN32)
    target_link_libraries(gstbaplugins PRIVATE ws2_32) # el socket con el plugin
endif()
ba_plugin_gst(gstbaplugins)

# El ONNX Runtime de esta plataforma (cmake/BaOnnxRuntime.cmake).
set(_ba_ort "${BA_ORT_LIB}")

# diag/Utf8.cpp: los mensajes de ONNX Runtime en Windows vienen en cp1252.
set(_srs superres/plugin.cpp superres/fsr.cpp superres/onnxsr.cpp superres/glsl.cpp "${CMAKE_SOURCE_DIR}/src/core/diag/Utf8.cpp")
if(APPLE)
    list(APPEND _srs superres/vtsuperres_mac.mm superres/coremlsr_mac.mm superres/cvmemoria_mac.mm)
    set_source_files_properties(superres/vtsuperres_mac.mm superres/coremlsr_mac.mm superres/cvmemoria_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
endif()
add_library(gstbasuperres MODULE ${_srs})
ba_plugin_gst(gstbasuperres)
target_include_directories(gstbasuperres PRIVATE "${CMAKE_SOURCE_DIR}/src/core")
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
# Vulkan (proveedor «vulkan» de baonnxsr): ncnn, su cargador de Vulkan y pnnx
# (pasa la red de ONNX a ncnn), todo en bin/64bit (tools/deps/ncnn-win.sh). Del
# sistema solo el driver de la placa.
if(WIN32)
    set(_ba_ncnn "${CMAKE_SOURCE_DIR}/extern/ncnn-win")
    if(EXISTS "${_ba_ncnn}/include/ncnn/net.h" AND EXISTS "${_ba_ncnn}/lib/ncnn.lib")
        target_include_directories(gstbasuperres PRIVATE "${_ba_ncnn}/include")
        target_link_libraries(gstbasuperres PRIVATE "${_ba_ncnn}/lib/ncnn.lib")
        target_compile_definitions(gstbasuperres PRIVATE BA_HAVE_NCNN=1)
        install(FILES "${_ba_ncnn}/bin/ncnn.dll" "${_ba_ncnn}/bin/vulkan-1.dll" "${_ba_ncnn}/bin/vulkaninfo.exe"
                      "${_ba_ncnn}/herramientas/pnnx.exe" DESTINATION bin/64bit COMPONENT bundle)
        message(STATUS "ncnn + Vulkan: baonnxsr acepta proveedor=vulkan")
    endif()
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

# Rostros: YuNet + SFace (src/core/rostros, el mismo de Personas) con el ONNX
# Runtime de la plataforma; en la Mac, además el detector de Apple (Vision).
if(EXISTS "${BA_ORT_DIR}/include/onnxruntime_cxx_api.h" AND EXISTS "${_ba_ort}")
    set(_ros rostros/plugin.cpp rostros/barostros.cpp rostros/seguir.cpp "${CMAKE_SOURCE_DIR}/src/core/rostros/Rostros.cpp" "${CMAKE_SOURCE_DIR}/src/core/ajustes/Ajustes.cpp"
             "${CMAKE_SOURCE_DIR}/src/core/diag/Utf8.cpp") # los mensajes de ONNX Runtime (cp1252 en Windows)
    if(APPLE)
        list(APPEND _ros rostros/vision_mac.mm)
        set_source_files_properties(rostros/vision_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    else()
        list(APPEND _ros rostros/vision_otros.cpp)
    endif()
    add_library(gstbarostros MODULE ${_ros})
    ba_plugin_gst(gstbarostros)
    target_include_directories(gstbarostros PRIVATE "${BA_ORT_DIR}/include" "${CMAKE_SOURCE_DIR}/src/core")
    target_link_libraries(gstbarostros PRIVATE "${_ba_ort}" nlohmann_json::nlohmann_json)
    if(APPLE)
        target_link_libraries(gstbarostros PRIVATE "-framework Vision" "-framework CoreVideo" "-framework Foundation")
    endif()
    # El texto del overlay: Pango + Cairo, los mismos que usa textoverlay (vienen
    # con GStreamer). Sin ellos, el overlay anda sin texto.
    get_filename_component(_ba_gst_inc "${BA_GST_LIBDIR}/../include" ABSOLUTE)
    find_library(_ba_pangocairo NAMES pangocairo-1.0 PATHS "${BA_GST_LIBDIR}" NO_DEFAULT_PATH)
    find_library(_ba_pango NAMES pango-1.0 PATHS "${BA_GST_LIBDIR}" NO_DEFAULT_PATH)
    find_library(_ba_cairo NAMES cairo PATHS "${BA_GST_LIBDIR}" NO_DEFAULT_PATH)
    find_library(_ba_gobject NAMES gobject-2.0 PATHS "${BA_GST_LIBDIR}" NO_DEFAULT_PATH)
    if(_ba_pangocairo AND _ba_pango AND _ba_cairo AND EXISTS "${_ba_gst_inc}/pango-1.0/pango/pangocairo.h")
        target_include_directories(gstbarostros PRIVATE "${_ba_gst_inc}/pango-1.0" "${_ba_gst_inc}/cairo"
                                   "${_ba_gst_inc}/harfbuzz" "${_ba_gst_inc}/freetype2" "${_ba_gst_inc}/fribidi")
        target_link_libraries(gstbarostros PRIVATE "${_ba_pangocairo}" "${_ba_pango}" "${_ba_cairo}" "${_ba_gobject}")
        target_compile_definitions(gstbarostros PRIVATE BA_ROSTROS_TEXTO=1)
        message(STATUS "barostros: con texto en el overlay (Pango + Cairo)")
    else()
        message(STATUS "barostros: sin Pango/Cairo, el overlay va sin texto")
    endif()
endif()

# Que todos nuestros elementos dejen pasar los eventos (serializados, fuera de
# banda y hacia arriba): la cadena de una entrada se comunica así.
add_executable(test_eventos tests/test_eventos.cpp)
target_link_libraries(test_eventos PRIVATE ba::gstreamer)
if(DEFINED BA_GST_LIBDIR)
    set_target_properties(test_eventos PROPERTIES BUILD_RPATH "${BA_GST_LIBDIR};${BA_ORT_DIR}/lib")
endif()
target_compile_definitions(test_eventos PRIVATE BA_TEST_GST_PLUGINS="${CMAKE_CURRENT_BINARY_DIR}")
add_dependencies(test_eventos gstbafiltros gstbaaudio gstbaplugins gstbasuperres)
if(TARGET gstbarostros)
    add_dependencies(test_eventos gstbarostros)
endif()
add_test(NAME test_eventos COMMAND test_eventos)

# baptzgl con el video andando: de quieto (pasante) a moverse y vuelta, sin caerse.
add_executable(test_ptz_gl tests/test_ptz_gl.cpp)
target_link_libraries(test_ptz_gl PRIVATE ba::gstreamer)
if(DEFINED BA_GST_LIBDIR)
    set_target_properties(test_ptz_gl PROPERTIES BUILD_RPATH "${BA_GST_LIBDIR}")
endif()
target_compile_definitions(test_ptz_gl PRIVATE BA_TEST_GST_PLUGINS="${CMAKE_CURRENT_BINARY_DIR}")
add_dependencies(test_ptz_gl gstbafiltros)
add_test(NAME test_ptz_gl COMMAND test_ptz_gl)

# La superresolución cambiada ANDANDO desde otro hilo (las caídas del 2026-10-10).
add_executable(test_superres_vivo tests/test_superres_vivo.cpp)
target_link_libraries(test_superres_vivo PRIVATE ba::gstreamer)
if(DEFINED BA_GST_LIBDIR)
    set_target_properties(test_superres_vivo PROPERTIES BUILD_RPATH "${BA_GST_LIBDIR};${BA_ORT_DIR}/lib")
endif()
target_compile_definitions(test_superres_vivo PRIVATE BA_TEST_GST_PLUGINS="${CMAKE_CURRENT_BINARY_DIR}"
                                                      BA_TEST_REPO="${CMAKE_SOURCE_DIR}")
add_dependencies(test_superres_vivo gstbasuperres)
add_test(NAME test_superres_vivo COMMAND test_superres_vivo)

# basombra: dónde cae la sombra, el borde difuso, el margen, y CPU = GPU.
add_executable(test_sombra tests/test_sombra.cpp)
target_link_libraries(test_sombra PRIVATE ba::gstreamer)
if(DEFINED BA_GST_LIBDIR)
    set_target_properties(test_sombra PROPERTIES BUILD_RPATH "${BA_GST_LIBDIR}")
endif()
target_compile_definitions(test_sombra PRIVATE BA_TEST_GST_PLUGINS="${CMAKE_CURRENT_BINARY_DIR}")
add_dependencies(test_sombra gstbafiltros)
add_test(NAME test_sombra COMMAND test_sombra)
