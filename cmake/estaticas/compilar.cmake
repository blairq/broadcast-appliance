# ============================================================================
# cmake/estaticas/compilar.cmake — CÓMO se compilan las bibliotecas estáticas de terceros
#
# mbedtls (mbedcrypto/mbedx509/mbedtls + everest y p256m), libsamplerate, argon2,
# libdeflate y zxing-cpp, desde las fuentes de extern/ (make deps-src), con los objetivos
# que usa el proyecto: mbedcrypto mbedx509 mbedtls samplerate argon2
# libdeflate::libdeflate_static ZXing::ZXing. Un solo lugar, dos usos:
#
#   - cmake/estaticas/CMakeLists.txt (make deps-estaticas): las compila UNA vez por
#     máquina y plataforma y las deja en extern/estaticas/<plataforma>-<huella>; el
#     proyecto las usa ya hechas (cmake/BaEstaticas.cmake). Es lo de siempre.
#   - El CMakeLists raíz con -DBA_ESTATICAS_DESDE_FUENTE=ON: adentro del build, con las
#     banderas del build (los sanitizers del CI: TSan quiere instrumentado también lo de
#     terceros).
#
# Entradas: BA_ESTATICAS_FUENTES (la carpeta extern/ con las fuentes),
#           BA_ESTATICAS_ZXING (ON/OFF: el sub-build /MT de Windows no lee códigos),
#           BA_MBEDTLS_USER_CONFIG (cmake/mbedtls_user_config.h).
# Lo que cambie acá cambia la huella (cmake/estaticas/huella.cmake): se recompilan solas.
# ============================================================================
set(_f "${BA_ESTATICAS_FUENTES}")
foreach(_s mbedtls/CMakeLists.txt mbedtls/framework/CMakeLists.txt libsamplerate/CMakeLists.txt
           argon2/Makefile libdeflate/CMakeLists.txt)
    if(NOT EXISTS "${_f}/${_s}")
        message(FATAL_ERROR "estáticas de terceros: falta ${_f}/${_s} (make deps-src)")
    endif()
endforeach()
if(BA_ESTATICAS_ZXING AND NOT EXISTS "${_f}/zxing-cpp/CMakeLists.txt")
    message(FATAL_ERROR "estáticas de terceros: falta ${_f}/zxing-cpp (make deps-zxing)")
endif()

# CMake 4 ya no acepta cmake_minimum_required < 3.5 (libsamplerate 0.2.2 y otras).
if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()
# Sus reglas de instalación, a un componente que no se instala: lo que se instala lo
# dicen cmake/estaticas/CMakeLists.txt (a extern/) y nadie en el modo desde fuente.
set(_ba_comp_antes "${CMAKE_INSTALL_DEFAULT_COMPONENT_NAME}")
set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME thirdparty)

# --- mbedtls: estática, sin tests/programs, con la configuración del proyecto ----
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
# Sin autotests de mbedTLS (ver el archivo: evita una carrera en ecp.c).
set(MBEDTLS_USER_CONFIG_FILE "${BA_MBEDTLS_USER_CONFIG}" CACHE FILEPATH "" FORCE)
# MinGW/GCC 13+ y Apple clang 21 avisan -Wunterminated-string-initialization en
# ssl_tls13_keys.h; mbedtls lo vuelve error solo con MBEDTLS_FATAL_WARNINGS.
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
add_subdirectory("${_f}/mbedtls" "${CMAKE_BINARY_DIR}/_estaticas/mbedtls" EXCLUDE_FROM_ALL)

# --- libsamplerate (target `samplerate`) -------------------------------------
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(LIBSAMPLERATE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LIBSAMPLERATE_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${_f}/libsamplerate" "${CMAKE_BINARY_DIR}/_estaticas/libsamplerate" EXCLUDE_FROM_ALL)

# --- libdeflate: solo la estática --------------------------------------------
set(LIBDEFLATE_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(LIBDEFLATE_BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
set(LIBDEFLATE_BUILD_GZIP OFF CACHE BOOL "" FORCE)
set(LIBDEFLATE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LIBDEFLATE_ZLIB_SUPPORT OFF CACHE BOOL "" FORCE)
add_subdirectory("${_f}/libdeflate" "${CMAKE_BINARY_DIR}/_estaticas/libdeflate" EXCLUDE_FROM_ALL)

# --- argon2: el repo no trae CMakeLists; los sources del Makefile oficial ------
# `ref.c` = la implementación de referencia (sin SIMD), válida en cualquier plataforma.
add_library(argon2 STATIC
    "${_f}/argon2/src/argon2.c"
    "${_f}/argon2/src/core.c"
    "${_f}/argon2/src/ref.c"
    "${_f}/argon2/src/thread.c"
    "${_f}/argon2/src/encoding.c"
    "${_f}/argon2/src/blake2/blake2b.c")
target_include_directories(argon2 PUBLIC "${_f}/argon2/include")
set_target_properties(argon2 PROPERTIES EXCLUDE_FROM_ALL ON)

# --- zxing-cpp: lectores + el escritor viejo (test_codigos), estática --------------
# En una función: BUILD_SHARED_LIBS (option() de zxing, ON fuera de Windows) queda
# estática SOLO para zxing.
function(_ba_estaticas_zxing f)
    set(BUILD_SHARED_LIBS OFF)
    set(ZXING_READERS ON CACHE BOOL "" FORCE)
    set(ZXING_WRITERS "OLD" CACHE STRING "" FORCE)
    set(ZXING_USE_BUNDLED_ZINT OFF CACHE BOOL "" FORCE)
    set(ZXING_C_API OFF CACHE BOOL "" FORCE)
    set(ZXING_EXPERIMENTAL_API OFF CACHE BOOL "" FORCE)
    set(ZXING_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ZXING_BLACKBOX_TESTS OFF CACHE BOOL "" FORCE)
    set(ZXING_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(ZXING_PYTHON_MODULE OFF CACHE BOOL "" FORCE)
    set(ZXING_DEPENDENCIES "LOCAL" CACHE STRING "" FORCE)
    add_subdirectory("${f}/zxing-cpp" "${CMAKE_BINARY_DIR}/_estaticas/zxing-cpp" EXCLUDE_FROM_ALL)
endfunction()
if(BA_ESTATICAS_ZXING)
    _ba_estaticas_zxing("${_f}")
endif()

# Todas con -fPIC: ba_core y gstbaaudio (un plugin, biblioteca compartida) las enlazan.
foreach(_t mbedcrypto mbedx509 mbedtls everest p256m samplerate argon2 libdeflate_static ZXing)
    if(TARGET ${_t})
        set_target_properties(${_t} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
endforeach()
set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME "${_ba_comp_antes}")
