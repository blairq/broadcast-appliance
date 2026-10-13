# ============================================================================
# cmake/BaWinStaticPart.cmake — Windows: launcher y gateway con CRT estatico
#
# Por que un sub-build aparte:
#   - libwebrtc precompilado para Windows (Shiguredo M123) esta compilado con
#     la CRT ESTATICA de MSVC (/MT: libcmt/libcpmt). Todo lo que se enlace con
#     el tiene que ser /MT, incluido ba_core y las libs de C.
#   - El launcher tambien va /MT: arranca en cualquier Windows sin depender de
#     las DLLs del CRT (esas viajan en bin/64bit, junto a A).
#   - A (Qt, GStreamer) va /MD, como Qt y GStreamer.
#   Un mismo arbol de CMake no puede tener ba_core en las dos variantes, asi
#   que la parte /MT se configura aparte (mismo CMakeLists raiz, mismo
#   toolchain, BA_WIN_STATIC_PART=ON) y sus ejecutables se instalan desde aca.
#   Es el mismo patron que el gateway de Linux (cmake/gateway-webrtc).
# ============================================================================
include(ExternalProject)

set(BA_WIN_MT_DIR "${CMAKE_BINARY_DIR}/win-mt")
if(NOT DEFINED BA_WEBRTC_ROOT)
    set(BA_WEBRTC_ROOT "${CMAKE_SOURCE_DIR}/extern/webrtc-win")
endif()
set(_ba_mt_args
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DBA_WIN_STATIC_PART=ON
    -DBA_PERFILAR=${BA_PERFILAR}
    -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
    -DBA_WEBRTC_ROOT=${BA_WEBRTC_ROOT})
if(CMAKE_TOOLCHAIN_FILE)
    list(APPEND _ba_mt_args -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
endif()
if(EXISTS "${BA_WEBRTC_ROOT}/lib/webrtc.lib")
    list(APPEND _ba_mt_args -DBA_GATEWAY_USE_WEBRTC=ON)
    message(STATUS "ba-win: gateway con libwebrtc REAL (${BA_WEBRTC_ROOT})")
else()
    message(WARNING "ba-win: falta ${BA_WEBRTC_ROOT}/lib/webrtc.lib (make deps-webrtc-win): "
                    "el gateway sale SIN WebRTC (backend NULL)")
endif()

ExternalProject_Add(ba_win_mt
    SOURCE_DIR "${CMAKE_SOURCE_DIR}"
    BINARY_DIR "${BA_WIN_MT_DIR}"
    CMAKE_GENERATOR "${CMAKE_GENERATOR}"
    CMAKE_ARGS ${_ba_mt_args}
    INSTALL_COMMAND ""
    BUILD_ALWAYS ON
    USES_TERMINAL_BUILD ON
    BUILD_BYPRODUCTS
        "${BA_WIN_MT_DIR}/src/launcher/BroadcastAppliance.exe"
        "${BA_WIN_MT_DIR}/src/gateway/ba-gateway.exe")

install(PROGRAMS "${BA_WIN_MT_DIR}/src/launcher/BroadcastAppliance.exe" DESTINATION . COMPONENT bundle)
install(PROGRAMS "${BA_WIN_MT_DIR}/src/gateway/ba-gateway.exe" DESTINATION bin/64bit COMPONENT bundle)
# Las vistas web: assets/web/ entero, como en src/gateway (ver ahí por qué no
# hay más una lista por vista: Windows se quedaba sin las nuevas).
install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/web/" DESTINATION Web COMPONENT bundle)

# Los tests de la parte /MT (ba_core, launcher, gateway) se corren desde su
# propio arbol (en Windows): ctest --test-dir build/win/win-mt
