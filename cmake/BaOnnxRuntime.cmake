# ============================================================================
# cmake/BaOnnxRuntime.cmake — el ONNX Runtime de ESTA plataforma, en un lugar
#
# Lo usan los rostros (ba_core), la voz (sherpa-onnx, ba-app) y la
# superresolución ONNX (gst-plugins). Cada plataforma tiene el suyo en una
# carpeta propia: el volumen de compilar en Docker es el mismo para Linux y
# Windows, y uno no puede pisar al otro.
#   macOS / Linux  extern/rostros/onnxruntime   (make deps-rostros)
#   Windows        extern/onnxruntime-win       (make deps-onnxruntime-win:
#                                                el oficial con DirectML)
#
# Define BA_ORT_DIR (con include/), BA_ORT_LIB (para enlazar), BA_ORT_RUNTIME
# (lo que viaja en bin/64bit) y BA_HAVE_ORT.
# ============================================================================
if(WIN32)
    set(BA_ORT_DIR "${CMAKE_SOURCE_DIR}/extern/onnxruntime-win")
    set(BA_ORT_LIB "${BA_ORT_DIR}/lib/onnxruntime.lib")
    file(GLOB BA_ORT_RUNTIME "${BA_ORT_DIR}/lib/*.dll")
elseif(APPLE)
    set(BA_ORT_DIR "${CMAKE_SOURCE_DIR}/extern/rostros/onnxruntime")
    set(BA_ORT_LIB "${BA_ORT_DIR}/lib/libonnxruntime.dylib")
    file(GLOB BA_ORT_RUNTIME "${BA_ORT_DIR}/lib/libonnxruntime*.dylib")
else()
    set(BA_ORT_DIR "${CMAKE_SOURCE_DIR}/extern/rostros/onnxruntime")
    set(BA_ORT_LIB "${BA_ORT_DIR}/lib/libonnxruntime.so")
    file(GLOB BA_ORT_RUNTIME "${BA_ORT_DIR}/lib/libonnxruntime.so*")
endif()
if(EXISTS "${BA_ORT_DIR}/include/onnxruntime_cxx_api.h" AND EXISTS "${BA_ORT_LIB}")
    set(BA_HAVE_ORT ON)
    message(STATUS "ONNX Runtime: ${BA_ORT_DIR}")
else()
    set(BA_HAVE_ORT OFF)
    message(STATUS "ONNX Runtime: no está (${BA_ORT_DIR})")
endif()
