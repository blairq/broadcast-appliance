# ============================================================================
# cmake/estaticas/CMakeLists.txt — las estáticas de terceros, compiladas UNA vez
#
# mbedtls, libsamplerate, argon2, libdeflate y zxing-cpp no cambian nunca: antes entraban
# por FetchContent y se configuraban en cada cmake y se compilaban en cada build y en cada
# worktree (~200 .o, ZXing solo ~1 min). Ahora `make deps-estaticas` (parte de `make deps`)
# las compila con esto y las deja en extern/estaticas/<plataforma>-<huella>/:
#   include/   los headers (zxing en include/ZXing)
#   lib/       las .a / .lib
#   licencias/ sus licencias
#   ba-estaticas.cmake   los objetivos IMPORTED con los nombres de siempre (mbedcrypto,
#                        mbedx509, mbedtls, samplerate, argon2, libdeflate::libdeflate_static,
#                        ZXing::ZXing); lo incluye cmake/BaEstaticas.cmake.
# Cómo se compila cada una: compilar.cmake (el mismo que usa el modo desde fuente).
#
# Uso (lo hace tools/deps/estaticas.sh):
#   cmake -S cmake/estaticas -B <obra> -DBA_RAIZ=<repo> -DBA_PLATAFORMA=<macos|linux|win|win-mt>
#         [-DCMAKE_TOOLCHAIN_FILE=… -DCMAKE_MSVC_RUNTIME_LIBRARY=…]
#   cmake --build <obra> --target ba_estaticas && cmake --install <obra> --component ba
# ============================================================================
cmake_minimum_required(VERSION 3.21)
project(ba_estaticas LANGUAGES C CXX)

foreach(_v BA_RAIZ BA_PLATAFORMA)
    if(NOT ${_v})
        message(FATAL_ERROR "estáticas: falta -D${_v}")
    endif()
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/huella.cmake")
ba_estaticas_huella(_huella "${BA_PLATAFORMA}" "${BA_RAIZ}")
set(CMAKE_INSTALL_PREFIX "${BA_RAIZ}/extern/estaticas/${BA_PLATAFORMA}-${_huella}" CACHE PATH "" FORCE)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Las mismas banderas que tenían adentro del build: el FetchContent iba ANTES de los
# add_compile_options del CMakeLists raíz (-g, /Zi, /utf-8, _WIN32_WINNT…), así que solo
# les llegaban las de Release, el toolchain (/winsysroot), la CRT (/MD o /MT) y en la Mac
# la arquitectura y el macOS mínimo. Más -fPIC en todas (antes, solo en algunas): las
# enlaza también un plugin (biblioteca compartida).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(BA_ESTATICAS_FUENTES "${BA_RAIZ}/extern")
set(BA_MBEDTLS_USER_CONFIG "${BA_RAIZ}/cmake/mbedtls_user_config.h")
# El sub-build /MT de Windows no lee códigos de barras (cmake/BaZxing.cmake).
if(BA_PLATAFORMA STREQUAL "win-mt")
    set(BA_ESTATICAS_ZXING OFF)
else()
    set(BA_ESTATICAS_ZXING ON)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/compilar.cmake")

set(_libs mbedcrypto mbedx509 mbedtls everest p256m samplerate argon2 libdeflate_static)
if(BA_ESTATICAS_ZXING)
    list(APPEND _libs ZXing)
endif()
add_custom_target(ba_estaticas ALL DEPENDS ${_libs})

# --- Lo que se instala (componente «ba»; lo de cada proyecto no) ----------------
foreach(_t IN LISTS _libs)
    install(FILES "$<TARGET_FILE:${_t}>" DESTINATION lib COMPONENT ba)
endforeach()
set(_f "${BA_ESTATICAS_FUENTES}")
install(DIRECTORY "${_f}/mbedtls/include/mbedtls" "${_f}/mbedtls/include/psa" DESTINATION include COMPONENT ba)
install(FILES "${BA_MBEDTLS_USER_CONFIG}" DESTINATION include RENAME ba_mbedtls_user_config.h COMPONENT ba)
install(FILES "${_f}/libsamplerate/include/samplerate.h" "${_f}/argon2/include/argon2.h"
              "${_f}/libdeflate/libdeflate.h" DESTINATION include COMPONENT ba)
install(FILES "${_f}/mbedtls/LICENSE" DESTINATION licencias RENAME mbedtls-Apache-2.0.txt COMPONENT ba)
install(FILES "${_f}/libsamplerate/COPYING" DESTINATION licencias RENAME libsamplerate-BSD-2.txt COMPONENT ba)
install(FILES "${_f}/argon2/LICENSE" DESTINATION licencias RENAME argon2-CC0-Apache-2.0.txt COMPONENT ba)
install(FILES "${_f}/libdeflate/COPYING" DESTINATION licencias RENAME libdeflate-MIT.txt COMPONENT ba)
if(BA_ESTATICAS_ZXING)
    # Los headers públicos de zxing (los de la raíz de core/src; los nuestros los
    # incluyen sin prefijo: "ReadBarcode.h") y el Version.h que arma su configuración.
    file(GLOB _zx_h "${_f}/zxing-cpp/core/src/*.h")
    install(FILES ${_zx_h} "${CMAKE_BINARY_DIR}/_estaticas/zxing-cpp/core/Version.h"
            DESTINATION include/ZXing COMPONENT ba)
    install(FILES "${_f}/zxing-cpp/LICENSE" DESTINATION licencias RENAME zxing-cpp-Apache-2.0.txt COMPONENT ba)
endif()

# --- ba-estaticas.cmake: los IMPORTED, con rutas relativas a su carpeta ---------
# Lo que cada una necesita al enlazar (lo que su CMake les ponía como INTERFACE).
set(BA_EST_SISTEMA_MBEDCRYPTO "")
set(BA_EST_SISTEMA_SAMPLERATE "")
set(BA_EST_HILOS "")
if(WIN32)
    set(BA_EST_SISTEMA_MBEDCRYPTO "ws2_32;bcrypt")
else()
    set(BA_EST_HILOS "Threads::Threads")
    if(NOT APPLE)
        set(BA_EST_SISTEMA_SAMPLERATE "m")
    endif()
endif()
set(BA_EST_ZXING_BLOQUE "")
if(BA_ESTATICAS_ZXING)
    set(BA_EST_ZXING_BLOQUE [=[
_ba_est_importar(ZXing "$<TARGET_FILE_NAME:ZXing>")
set_target_properties(ZXing PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_ba_est}/include/ZXing"
    INTERFACE_COMPILE_FEATURES "cxx_std_17"
    INTERFACE_LINK_LIBRARIES "@BA_EST_HILOS@")
add_library(ZXing::ZXing ALIAS ZXing)]=])
    string(REPLACE "@BA_EST_HILOS@" "${BA_EST_HILOS}" BA_EST_ZXING_BLOQUE "${BA_EST_ZXING_BLOQUE}")
endif()
set(BA_EST_HUELLA "${_huella}")
configure_file("${CMAKE_CURRENT_LIST_DIR}/ba-estaticas.cmake.in" "${CMAKE_BINARY_DIR}/ba-estaticas.cmake.gen" @ONLY)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/ba-estaticas.cmake" INPUT "${CMAKE_BINARY_DIR}/ba-estaticas.cmake.gen")
install(FILES "${CMAKE_BINARY_DIR}/ba-estaticas.cmake" DESTINATION . COMPONENT ba)
