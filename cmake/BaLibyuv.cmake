# ============================================================================
# cmake/BaLibyuv.cmake — libyuv para el gateway: `ba_libyuv`
#
# El núcleo del gateway (src/gateway: Grillas, MediaPump) escala y convierte imagen con
# libyuv, el mismo código en el producto y en los tests. De dónde sale la biblioteca:
#
#   ba_libyuv_del_sdk(<raíz del SDK> <biblioteca>)
#       Donde este build enlaza el SDK de libwebrtc (Mac; Windows en la parte /MT): la libyuv
#       que ya viene adentro (con su MJPEG sobre libjpeg-turbo), con los headers del SDK. Una
#       sola copia en el programa: la que usa libwebrtc por dentro es la misma. Del archivo
#       el enlazador trae solo los objetos de libyuv (y de jpeg) que se nombran.
#
#   ba_libyuv_propia()
#       Donde no hay SDK con la misma ABI (Linux: el libwebrtc de Linux usa la libc++ de
#       Chromium y el build principal es GCC + libstdc++): libyuv desde fuente, en el commit
#       que trae el SDK M123 (versión 1886), con libjpeg-turbo para el MJPEG. Las dos en
#       extern/ (`make deps-libyuv`, que forma parte de `make deps-linux`).
# ============================================================================

function(ba_libyuv_del_sdk raiz biblioteca)
    add_library(ba_libyuv INTERFACE)
    target_include_directories(ba_libyuv SYSTEM INTERFACE "${raiz}/include/third_party/libyuv/include")
    target_link_libraries(ba_libyuv INTERFACE "${biblioteca}")
    message(STATUS "libyuv: la del SDK de libwebrtc (${biblioteca})")
endfunction()

function(ba_libyuv_propia)
    set(_yuv "${CMAKE_SOURCE_DIR}/extern/libyuv")
    set(_jpeg "${CMAKE_SOURCE_DIR}/extern/libjpeg-turbo")
    if(NOT EXISTS "${_yuv}/include/libyuv.h" OR NOT EXISTS "${_jpeg}/CMakeLists.txt")
        ba_falta("libyuv (escalar y convertir imagen en el gateway)" "falta extern/libyuv o extern/libjpeg-turbo ('make deps-libyuv')")
        # Para que el resto configure igual (ba_falta frena al final, con la lista entera).
        add_library(ba_libyuv INTERFACE)
        return()
    endif()

    # libjpeg-turbo: aparte (ExternalProject), con su CMake, sin SIMD (no pide nasm) y solo la
    # estática; se instala adentro del build, nunca en el paquete.
    include(ExternalProject)
    set(_jpeg_dir "${CMAKE_BINARY_DIR}/libjpeg-turbo")
    set(_jpeg_lib "${_jpeg_dir}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}jpeg${CMAKE_STATIC_LIBRARY_SUFFIX}")
    ExternalProject_Add(ba_libjpeg_ext
        SOURCE_DIR "${_jpeg}"
        BINARY_DIR "${CMAKE_BINARY_DIR}/libjpeg-turbo-build"
        CMAKE_ARGS
            -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_INSTALL_PREFIX=${_jpeg_dir}
            -DCMAKE_INSTALL_LIBDIR=lib
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DENABLE_SHARED=OFF
            -DENABLE_STATIC=ON
            -DWITH_SIMD=OFF
            -DWITH_TURBOJPEG=OFF
            -DWITH_TOOLS=OFF
            -DWITH_TESTS=OFF
        BUILD_BYPRODUCTS "${_jpeg_lib}")

    file(GLOB _fuentes CONFIGURE_DEPENDS "${_yuv}/source/*.cc")
    add_library(ba_libyuv STATIC ${_fuentes})
    add_dependencies(ba_libyuv ba_libjpeg_ext)
    target_include_directories(ba_libyuv SYSTEM PUBLIC "${_yuv}/include")
    target_include_directories(ba_libyuv PRIVATE "${_jpeg_dir}/include")
    target_compile_definitions(ba_libyuv PRIVATE HAVE_JPEG)
    set_target_properties(ba_libyuv PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_options(ba_libyuv PRIVATE -w) # código de terceros: sin nuestros avisos
    target_link_libraries(ba_libyuv PUBLIC "${_jpeg_lib}")
    message(STATUS "libyuv: la de extern/libyuv, con libjpeg-turbo de extern/")
endfunction()
