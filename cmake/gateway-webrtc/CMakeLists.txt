# ============================================================================
# cmake/gateway-webrtc/CMakeLists.txt — Gateway (B) con libwebrtc REAL (POSIX)
#
# Por que un subproyecto aparte:
#   libwebrtc precompilado (Shiguredo, M123) esta compilado contra la libc++ de
#   Chromium con su propio espacio de nombres ABI (std::__Cr) y trae ADENTRO del
#   .a la implementacion de libc++/libc++abi. Todo lo que intercambie tipos de
#   la STL con libwebrtc (std::string, std::unique_ptr...) tiene que compilarse
#   con ESOS headers y ESA configuracion (__config_site de Chromium: __Cr, ABI
#   v2). El resto del producto (A, launcher, core) usa la libstdc++ de GCC; por
#   eso solo el gateway se compila aca, con clang, en su propio arbol.
#
#   Resultado: un ba-gateway sin dependencias de C++ del sistema (la libc++ va
#   estatica dentro de libwebrtc.a); solo libc/libm/libpthread/libdl, que son
#   parte de cualquier Linux.
#
# Lo llama src/gateway/CMakeLists.txt via ExternalProject con:
#   BA_SRC            raiz del repo
#   BA_WEBRTC_ROOT    extern/webrtc (include/ + lib/libwebrtc.a)
#   BA_LIBCXX_CFG     directorio con __config_site/__assertion_handler de Chromium
#   BA_C_LIBS         libs de C del build principal (mbedtls, samplerate, argon2):
#                     C puro, sin ABI de C++, se reutilizan tal cual
#   BA_C_INCLUDES     sus includes (+ nlohmann_json)
# ============================================================================
cmake_minimum_required(VERSION 3.24)
project(ba_gateway_webrtc LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

foreach(v BA_SRC BA_WEBRTC_ROOT BA_LIBCXX_CFG)
    if(NOT ${v})
        message(FATAL_ERROR "gateway-webrtc: falta ${v}")
    endif()
endforeach()

set(WRTC "${BA_WEBRTC_ROOT}")
# Headers completos de libc++ (el SDK trae solo los .h): extern/webrtc/libcxx
set(LIBCXX_INC "${WRTC}/libcxx/include")
set(LIBCXXABI_INC "${WRTC}/include/third_party/libc++abi/src/include")

# --- libc++ de Chromium para TODO el C++ de este arbol -----------------------
add_compile_options(
    "$<$<COMPILE_LANGUAGE:CXX>:-nostdinc++>"
    "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-isystem ${BA_LIBCXX_CFG}>"
    "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-isystem ${LIBCXX_INC}>"
    "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-isystem ${LIBCXXABI_INC}>")
# Chromium fija el modo de "hardening" por flags de GN, no en __config_site.
# No cambia el ABI; NONE = sin chequeos extra en el camino de datos.
add_compile_definitions("$<$<COMPILE_LANGUAGE:CXX>:_LIBCPP_HARDENING_MODE_DEFAULT=_LIBCPP_HARDENING_MODE_NONE>")
# libc++ y libc++abi vienen dentro de libwebrtc.a: nada de libstdc++.
add_link_options(-nostdlib++ -static-libgcc)
find_package(Threads REQUIRED)

# --- ba_core (mismo codigo que el del producto, otra STL) --------------------
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS "${BA_SRC}/src/core/*.cpp")
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/tests/.*")
# El reconocimiento de rostros es de la interfaz (A), no del gateway: B no ve
# video de camaras, ve sesiones de WebRTC. Sacarlo de aca ademas evita que el
# gateway arrastre ONNX Runtime por nada.
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/rostros/.*")
if(APPLE)
    list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/platform/.*_(win|linux)\\.cpp$")
    file(GLOB CORE_MM CONFIGURE_DEPENDS "${BA_SRC}/src/core/platform/*_mac.mm")
    list(APPEND CORE_SOURCES ${CORE_MM})
else()
    list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/platform/.*_(win|mac)\\.cpp$")
endif()
# qrcodegen: el nucleo lo usa para generar QR (src/core/qr/Qr.cpp), asi que
# esta copia del nucleo tambien lo necesita, con su header a la vista.
list(APPEND CORE_SOURCES "${BA_SRC}/third_party/qrcodegen/qrcodegen.cpp")

add_library(ba_core_cr STATIC ${CORE_SOURCES})
target_include_directories(ba_core_cr PUBLIC "${BA_SRC}/src/core"
                           "${BA_SRC}/third_party/qrcodegen" ${BA_C_INCLUDES})
target_link_libraries(ba_core_cr PUBLIC ${BA_C_LIBS} Threads::Threads)

# --- Gateway ------------------------------------------------------------------
file(GLOB GW_SOURCES CONFIGURE_DEPENDS "${BA_SRC}/src/gateway/*.cpp" "${BA_SRC}/src/gateway/net/*.cpp"
     "${BA_SRC}/src/gateway/proxy/*.cpp")
list(FILTER GW_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")
# El cliente HTTPS propio de la app (mbedtls, sin Qt): lo usa el cliente ACME
# del proxy (T61), igual que en src/gateway/CMakeLists.txt.
set(BA_GW_HTTPS "${BA_SRC}/src/app/net/HttpsClient.cpp")
list(APPEND GW_SOURCES "${BA_GW_HTTPS}")
set_source_files_properties("${BA_GW_HTTPS}" PROPERTIES INCLUDE_DIRECTORIES "${BA_SRC}/src/app")
# lwIP (C, sin ABI de C++): se compila aca con clang.
set(LWIP_DIR "${BA_SRC}/extern/lwip")
if(EXISTS "${LWIP_DIR}/src/Filelists.cmake")
    include("${LWIP_DIR}/src/Filelists.cmake")
    add_library(ba_lwip_cr STATIC ${lwipcore_SRCS} ${lwipcore4_SRCS})
    target_include_directories(ba_lwip_cr PUBLIC "${BA_SRC}/src/gateway/net/lwip-port" "${LWIP_DIR}/src/include")
    set_target_properties(ba_lwip_cr PROPERTIES C_STANDARD 99)
    target_compile_options(ba_lwip_cr PRIVATE -w)
    set(BA_LWIP_LIB ba_lwip_cr)
    set(BA_LWIP_DEF BA_HAVE_LWIP=1)
else()
    list(FILTER GW_SOURCES EXCLUDE REGEX ".*/net/IpStack\\.cpp$")
    set(BA_LWIP_LIB "")
    set(BA_LWIP_DEF BA_HAVE_LWIP=0)
endif()
list(APPEND GW_SOURCES "${BA_SRC}/third_party/qrcodegen/qrcodegen.cpp")
add_library(ba_gateway_rtc STATIC ${GW_SOURCES})
target_compile_definitions(ba_gateway_rtc PUBLIC ${BA_LWIP_DEF})
target_include_directories(ba_gateway_rtc PUBLIC
    "${BA_SRC}/src/gateway"
    "${BA_SRC}/third_party/qrcodegen"
    "${WRTC}/include"
    "${WRTC}/include/third_party/abseil-cpp"
    "${WRTC}/include/third_party/libyuv/include")
target_compile_definitions(ba_gateway_rtc PUBLIC BA_GATEWAY_HAVE_WEBRTC=1 WEBRTC_POSIX)
if(APPLE)
    target_compile_definitions(ba_gateway_rtc PUBLIC WEBRTC_MAC)
else()
    target_compile_definitions(ba_gateway_rtc PUBLIC WEBRTC_LINUX)
endif()
# libwebrtc esta compilado sin RTTI: las clases que heredan de sus interfaces
# (observadores, fuentes, sinks) no pueden pedir typeinfo de sus bases.
set_source_files_properties("${BA_SRC}/src/gateway/Webrtc.cpp" PROPERTIES COMPILE_OPTIONS "-fno-rtti")
# libwebrtc.a al FINAL: aporta la libc++ que usan todos los de arriba.
target_link_libraries(ba_gateway_rtc PUBLIC ba_core_cr ${BA_LWIP_LIB} "${WRTC}/lib/libwebrtc.a" Threads::Threads ${CMAKE_DL_LIBS})
if(NOT APPLE)
    target_link_libraries(ba_gateway_rtc PUBLIC rt m)
else()
    target_link_libraries(ba_gateway_rtc PUBLIC
        "-framework Foundation" "-framework CoreAudio" "-framework AudioToolbox"
        "-framework CoreVideo" "-framework CoreMedia" "-framework VideoToolbox"
        "-framework AVFoundation" "-framework AppKit" "-framework Metal"
        "-framework IOSurface" "-framework ApplicationServices" "-framework CoreGraphics")
endif()

# Xlib inerte (ver x11_stubs.cpp): va como objeto DEL EJECUTABLE, no dentro de
# un .a, porque solo lo pide libwebrtc.a al final de la linea de link.
set(BA_X11_STUBS "")
if(NOT APPLE)
    set(BA_X11_STUBS "${CMAKE_CURRENT_SOURCE_DIR}/x11_stubs.cpp")
endif()

add_executable(ba-gateway "${BA_SRC}/src/gateway/main.cpp" ${BA_X11_STUBS})
target_link_libraries(ba-gateway PRIVATE ba_gateway_rtc)
# El binario se instala en bin/64bit junto a las libs del bundle.
set_target_properties(ba-gateway PROPERTIES INSTALL_RPATH "$ORIGIN")

# --- Tests con medios reales --------------------------------------------------
# Los tests de logica del gateway corren en el build principal (backend NULL).
# Aca van los que necesitan libwebrtc de verdad.
foreach(t test_webrtc_loopback)
    if(EXISTS "${BA_SRC}/src/gateway/tests/${t}.cpp")
        add_executable(${t} "${BA_SRC}/src/gateway/tests/${t}.cpp" ${BA_X11_STUBS})
        target_link_libraries(${t} PRIVATE ba_gateway_rtc)
        set_source_files_properties("${BA_SRC}/src/gateway/tests/${t}.cpp" PROPERTIES COMPILE_OPTIONS "-fno-rtti")
    endif()
endforeach()
