# ============================================================================
# src/gateway — `ba-gateway` (Proceso B: Gateway WebRTC + conversación + túnel)
#
# Reemplaza al fake-gateway de la Fase 1 por el gateway real (spec §3.3, §8):
#   - servidor de señalización HTTP + WebSocket embebido en 127.0.0.1 (§8.3);
#   - una PeerConnection libwebrtc por invitado (offer/answer + ICE) (§8.1);
#   - motor de medios: consume el ring de retorno de audio (BA.Audio.
#     ContentForReturn, A→B) y el vídeo de programa (BA.Program, A→B) y los
#     enruta a los tracks de retorno de cada invitado; en sentido inverso
#     publica BA.Guest.<n> (SpoutSender) y BA.Audio.Guest.<n> (ring) (§8.5, §8.6);
#   - auth de invitado con ba_core::security::TokenService (room + PIN + TTL)
#     (§8.3);
#   - supervisión de cloudflared.exe (Job Object, token por env, backoff)
#     (§8.8);
#   - sirve la app web del invitado desde assets/web/guest/ (HTTP estático mínimo).
#
# Headless: sin Qt (spec §3.3). En Windows enlaza libwebrtc estáticamente;
# en Linux / build offline se compila con un backend NULL de WebRTC para que
# el árbol configure y compile sin el SDK.
#
# /MD (CRT dinámica) por consistencia con libwebrtc — hereda del root.
# ============================================================================

# --- Sources ----------------------------------------------------------------
file(GLOB_RECURSE BA_GATEWAY_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

# Excluir los tests del target ba-gateway: son ejecutables propios (con su
# propio main), no parte del gateway (mismo criterio que ba_core/launcher).
list(FILTER BA_GATEWAY_SOURCES EXCLUDE REGEX ".*/tests/.*")

# Saneamiento: si no hay sources propios todavía, placeholder para que el
# target configure y compile.
if(NOT BA_GATEWAY_SOURCES)
    message(STATUS "ba-gateway: sin sources propios todavía; usando placeholder")
    set(BA_GATEWAY_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/placeholder.cpp")
endif()

# WIN32 en MSVC: subsistema GUI (sin consola). En el resto (MinGW cross y
# Linux) es un ejecutable de consola: el launcher (Proceso 0) lo lanza en
# segundo plano, así que la consola no se muestra.
if(MSVC)
    add_executable(ba-gateway WIN32 ${BA_GATEWAY_SOURCES})
    # Sin consola pero con `main(argc, argv)`: el CRT entra por main.
    target_link_options(ba-gateway PRIVATE /ENTRY:mainCRTStartup)
else()
    add_executable(ba-gateway ${BA_GATEWAY_SOURCES})
endif()

target_link_libraries(ba-gateway PRIVATE ba_core mbedcrypto)
# El cliente HTTPS propio (mbedtls, sin Qt) de la app: lo usa el cliente ACME del proxy (T61).
set(BA_GW_HTTPS "${CMAKE_SOURCE_DIR}/src/app/net/HttpsClient.cpp")
target_sources(ba-gateway PRIVATE "${BA_GW_HTTPS}")
set_source_files_properties("${BA_GW_HTTPS}" PROPERTIES INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/src/app")
target_include_directories(ba-gateway PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# QR de ingreso (Nayuki, MIT) en third_party/: codigo ajeno, fuera del lint.
set(BA_QRCODEGEN_DIR "${CMAKE_SOURCE_DIR}/third_party/qrcodegen")
target_sources(ba-gateway PRIVATE "${BA_QRCODEGEN_DIR}/qrcodegen.cpp")
target_include_directories(ba-gateway PRIVATE "${BA_QRCODEGEN_DIR}")

# --- lwIP (pila IP en espacio de usuario para el tunel WireGuard) ------------
# extern/lwip (make deps-lwip). Sin lwIP el gateway compila igual, sin tunel.
set(LWIP_DIR "${CMAKE_SOURCE_DIR}/extern/lwip")
set(BA_HAVE_LWIP OFF)
if(EXISTS "${LWIP_DIR}/src/Filelists.cmake")
    include("${LWIP_DIR}/src/Filelists.cmake")
    add_library(ba_lwip STATIC ${lwipcore_SRCS} ${lwipcore4_SRCS})
    target_include_directories(ba_lwip PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/net/lwip-port" "${LWIP_DIR}/src/include")
    set_target_properties(ba_lwip PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 99)
    target_compile_options(ba_lwip PRIVATE -w) # codigo de terceros: sin nuestros warnings
    target_link_libraries(ba-gateway PRIVATE ba_lwip)
    target_compile_definitions(ba-gateway PRIVATE BA_HAVE_LWIP=1)
    set(BA_HAVE_LWIP ON)
else()
    message(STATUS "ba-gateway: sin lwIP (extern/lwip); el tunel WireGuard queda deshabilitado")
    get_target_property(_gw_srcs ba-gateway SOURCES)
    list(FILTER _gw_srcs EXCLUDE REGEX ".*/net/IpStack\\.cpp$")
    set_target_properties(ba-gateway PROPERTIES SOURCES "${_gw_srcs}")
    target_compile_definitions(ba-gateway PRIVATE BA_HAVE_LWIP=0)
endif()

# --- Dependencias de red / plataforma ---------------------------------------
# El servidor de señalización usa sockets. En Windows es winsock2 (ws2_32);
# en POSIX el stack ya está en libc.
if(WIN32)
    target_link_libraries(ba-gateway PRIVATE ws2_32)
endif()

# --- libwebrtc (solo Windows, solo si el SDK está presente) ------------------
# El stack real de WebRTC (spec §8.1, decisión D4) es libwebrtc precompilado
# (M123, versions.cmake). El SDK vive en `extern/webrtc` (include/ + lib/) y NO
# se trae por FetchContent porque es un asset de despliegue grande. Si no está
# presente (build offline / Linux) se compila con BA_GATEWAY_HAVE_WEBRTC=0 y el
# backend NULL hace las veces de PeerConnection para que el árbol compile y la
# señalización sea testeable sin el SDK.
#
# BA_WEBRTC_ROOT: ruta al SDK (include/ + lib/). Se puede forzar con
#   -D BA_WEBRTC_ROOT=<ruta>   (p. ej. extern/webrtc o un webrtc-build de
#   Shiguredo). Por defecto se mira en <fuente>/extern/webrtc.
#
# El resultado queda en BA_GATEWAY_WEBRTC (0/1) para que el target ba-gateway
# y los tests de abajo (bloque inferior) hereden el MISMO flag: sin el SDK el
# test imprime SKIP y pasa.
set(BA_GATEWAY_WEBRTC 0)
# El backend REAL de libwebrtc se enciende SOLO con opt-in explícito
# (-D BA_GATEWAY_USE_WEBRTC=1). La mera presencia del SDK en extern/webrtc
# (lo deja `make deps`) NO basta: Webrtc.cpp aún se adapta a los headers M123
# reales (absl/, api/, p2p/...), así que que el SDK esté descargado no debe
# romper el verde del cross-build MinGW (WIN32). Sin el opt-in, el gateway y
# sus tests siguen con el backend NULL (BA_GATEWAY_HAVE_WEBRTC=0).
#
# La puerta es MSVC (no WIN32): extern/webrtc/lib/webrtc.lib es un archivo COFF
# con símbolos C++ mangling-MSVC y NO hay webrtc.dll, así que MinGW (mangling
# Itanium) NO puede enlazarlo y el backend real solo ENLAZA bajo MSVC. El
# cross-build MinGW entrega el backend NULL (BA_GATEWAY_HAVE_WEBRTC=0) y
# valida el path real vía el target de compilación realcheck (bloque inferior).
if(NOT DEFINED BA_GATEWAY_USE_WEBRTC)
    set(BA_GATEWAY_USE_WEBRTC OFF)
endif()
# Ruta al SDK: se fija SIEMPRE (es solo una búsqueda de ruta), para que el
# target realcheck de abajo la vea en cualquier toolchain. Solo el ENLACE del
# backend real queda restringido a MSVC (bloque if(MSVC) de abajo).
if(NOT DEFINED BA_WEBRTC_ROOT)
    set(BA_WEBRTC_ROOT "${CMAKE_SOURCE_DIR}/extern/webrtc")
endif()
if(MSVC)
    if(BA_GATEWAY_USE_WEBRTC AND BA_WEBRTC_ROOT AND EXISTS "${BA_WEBRTC_ROOT}/include")
        set(BA_GATEWAY_WEBRTC 1)
    endif()
endif()

# macOS: el libwebrtc de Shiguredo (macos_arm64) esta compilado contra la
# libc++ del sistema (std::__1), la misma de Apple clang: se enlaza directo en
# este build, sin el sub-build de Linux. Lo baja `make deps-webrtc` en macOS.
set(BA_GATEWAY_WEBRTC_MAC 0)
if(APPLE AND EXISTS "${BA_WEBRTC_ROOT}/lib/libwebrtc.a" AND EXISTS "${BA_WEBRTC_ROOT}/include/api/peer_connection_interface.h")
    set(BA_GATEWAY_WEBRTC_MAC 1)
    message(STATUS "ba-gateway: libwebrtc REAL (macOS arm64) en ${BA_WEBRTC_ROOT}")
    # Igual que en Windows (ver abajo): el SDK se compilo con un clang mas
    # viejo y el de Xcode 26 rechaza [[clang::lifetimebound]] en parametros de
    # funciones void (api/candidate.h). Copia de absl/base/attributes.h sin
    # ese atributo delante del SDK (solo avisos de analisis; mismo ABI).
    set(_ba_absl_shadow "${CMAKE_BINARY_DIR}/webrtc-mac-shadow")
    file(READ "${BA_WEBRTC_ROOT}/include/third_party/abseil-cpp/absl/base/attributes.h" _ba_attr)
    string(REPLACE "#define ABSL_ATTRIBUTE_LIFETIME_BOUND [[clang::lifetimebound]]"
           "#define ABSL_ATTRIBUTE_LIFETIME_BOUND" _ba_attr "${_ba_attr}")
    file(WRITE "${_ba_absl_shadow}/absl/base/attributes.h" "${_ba_attr}")
    add_library(ba_webrtc_mac INTERFACE)
    target_include_directories(ba_webrtc_mac SYSTEM BEFORE INTERFACE
        "${_ba_absl_shadow}"
        "${BA_WEBRTC_ROOT}/include"
        "${BA_WEBRTC_ROOT}/include/third_party/abseil-cpp"
        "${BA_WEBRTC_ROOT}/include/third_party/libyuv/include")
    target_compile_definitions(ba_webrtc_mac INTERFACE BA_GATEWAY_HAVE_WEBRTC=1 WEBRTC_POSIX WEBRTC_MAC)
    target_link_libraries(ba_webrtc_mac INTERFACE "${BA_WEBRTC_ROOT}/lib/libwebrtc.a"
        "-framework Foundation" "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox"
        "-framework AudioUnit" "-framework CoreVideo" "-framework CoreMedia" "-framework VideoToolbox"
        "-framework AVFoundation" "-framework AppKit" "-framework Metal" "-framework MetalKit"
        "-framework IOSurface" "-framework ApplicationServices" "-framework CoreGraphics"
        "-framework QuartzCore" "-framework CoreServices" "-framework Security"
        "-framework SystemConfiguration")
    # libwebrtc esta compilado sin RTTI.
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/Webrtc.cpp" PROPERTIES COMPILE_OPTIONS "-fno-rtti")
    target_link_libraries(ba-gateway PRIVATE ba_webrtc_mac)
endif()

if(BA_GATEWAY_WEBRTC_MAC)
    # (configurado arriba)
elseif(BA_GATEWAY_WEBRTC)
    message(STATUS "ba-gateway: libwebrtc en ${BA_WEBRTC_ROOT}")
    target_compile_definitions(ba-gateway PRIVATE BA_GATEWAY_HAVE_WEBRTC=1)
    # Binarios precompilados de Shiguredo: webrtc.lib en COFF con símbolos
    # C++ mangling-MSVC, CRT estatica (/MT) y sin webrtc.dll: solo enlazable
    # bajo MSVC/clang-cl y desde el sub-build /MT (cmake/BaWinStaticPart.cmake).
    # El SDK se compilo con un clang mas viejo: el clang actual rechaza
    # [[clang::lifetimebound]] en parametros de funciones void (api/candidate.h).
    # Se usa una copia de absl/base/attributes.h sin ese atributo (solo son
    # avisos de analisis; no cambia el codigo ni el ABI), delante del SDK.
    set(_ba_absl_shadow "${CMAKE_BINARY_DIR}/webrtc-win-shadow")
    file(READ "${BA_WEBRTC_ROOT}/include/third_party/abseil-cpp/absl/base/attributes.h" _ba_attr)
    string(REPLACE "#define ABSL_ATTRIBUTE_LIFETIME_BOUND [[clang::lifetimebound]]"
           "#define ABSL_ATTRIBUTE_LIFETIME_BOUND" _ba_attr "${_ba_attr}")
    file(WRITE "${_ba_absl_shadow}/absl/base/attributes.h" "${_ba_attr}")
    add_library(ba_webrtc_win INTERFACE)
    target_include_directories(ba_webrtc_win SYSTEM BEFORE INTERFACE
        "${_ba_absl_shadow}"
        "${BA_WEBRTC_ROOT}/include"
        "${BA_WEBRTC_ROOT}/include/third_party/abseil-cpp"
        "${BA_WEBRTC_ROOT}/include/third_party/libyuv/include")
    target_compile_definitions(ba_webrtc_win INTERFACE BA_GATEWAY_HAVE_WEBRTC=1 WEBRTC_WIN)
    target_link_libraries(ba_webrtc_win INTERFACE "${BA_WEBRTC_ROOT}/lib/webrtc.lib"
        winmm secur32 ws2_32 iphlpapi crypt32 dmoguids wmcodecdspuuid msdmo strmiids
        ole32 oleaut32 d3d11 dxgi advapi32 user32 gdi32 shell32)
    # libwebrtc esta compilado sin RTTI: las clases que heredan de sus
    # interfaces no pueden pedir typeinfo de sus bases.
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/Webrtc.cpp" PROPERTIES COMPILE_OPTIONS "/GR-")
    target_link_libraries(ba-gateway PRIVATE ba_webrtc_win)
else()
    message(STATUS "ba-gateway (GCC): backend NULL para los tests de logica (BA_GATEWAY_HAVE_WEBRTC=0)")
    target_compile_definitions(ba-gateway PRIVATE BA_GATEWAY_HAVE_WEBRTC=0)
endif()

# --- Validación por compilación del backend real (no-MSVC) ------------------
# webrtc.lib es COFF con nombres C++ mangling-MSVC y no hay webrtc.dll: MinGW
# (mangling Itanium) NO puede enlazarlo, así que el backend real solo ENLAZA bajo
# MSVC. Pero el cierre de headers M123 SÍ compila bajo MinGW si se neutraliza el
# static_assert de sanity RTC_EXPORT_TEMPLATE_TEST(MSVC_HACK, __declspec(dllexport))
# de rtc_base/system/rtc_export_template.h (GCC reescribe __declspec -> __attribute__).
# Este target OBJECT compila SOLO Webrtc.cpp con BA_GATEWAY_HAVE_WEBRTC=1 (sin
# enlazar webrtc.lib) como prueba de compilación del path real en toolchains no-MSVC.
if(NOT MSVC AND BA_GATEWAY_USE_WEBRTC AND DEFINED BA_WEBRTC_ROOT AND EXISTS "${BA_WEBRTC_ROOT}/include")
    # Solo UNA de las cuatro llamadas del auto-test falla bajo GCC: la de
    # __declspec(dllexport) (GCC la reescribe a __attribute__ y rompe el
    # token-paste que la mapea a MSVC_HACK). Las otras tres (vacía, __attribute__,
    # dllimport) son válidas bajo GCC. Por eso se usa REPLACE LITERAL sobre esa
    # única línea (no un regex ancho, que casaría también la #define del macro).
    file(READ "${BA_WEBRTC_ROOT}/include/rtc_base/system/rtc_export_template.h" _rtc_export_src)
    string(REPLACE "RTC_EXPORT_TEMPLATE_TEST(MSVC_HACK, __declspec(dllexport));"
           "#if defined(_MSC_VER)\nRTC_EXPORT_TEMPLATE_TEST(MSVC_HACK, __declspec(dllexport));\n#endif"
           _rtc_export_shaded "${_rtc_export_src}")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/webrtc-shadow/rtc_base/system")
    file(WRITE "${CMAKE_BINARY_DIR}/webrtc-shadow/rtc_base/system/rtc_export_template.h"
         "${_rtc_export_shaded}")
    add_library(ba-gateway-webrtc-realcheck OBJECT "${CMAKE_CURRENT_SOURCE_DIR}/Webrtc.cpp")
    target_compile_definitions(ba-gateway-webrtc-realcheck PRIVATE
        BA_GATEWAY_HAVE_WEBRTC=1 WEBRTC_WIN OS_WIN UNICODE _UNICODE)
    target_include_directories(ba-gateway-webrtc-realcheck BEFORE PRIVATE
        "${CMAKE_BINARY_DIR}/webrtc-shadow"
        "${BA_WEBRTC_ROOT}/include"
        "${BA_WEBRTC_ROOT}/include/third_party/abseil-cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}")
else()
    message(STATUS "ba-gateway: realcheck desactivado (sin SDK o sin BA_GATEWAY_USE_WEBRTC)")
endif()

# --- Tests del gateway (CTest) ----------------------------------------------
# Mismo patrón que src/core: cada tests/test_*.cpp es un ejecutable con su
# propio main() que devuelve 0 si pasa y != 0 si falla, y se registra con
# add_test. El test compila los sources del gateway SIN main.cpp (aporta su
# propio main) y hereda BA_GATEWAY_HAVE_WEBRTC: sin el SDK imprime
# "SKIP: no libwebrtc" y pasa (path OFF que compila solo con std + ba_core).
file(GLOB_RECURSE BA_GATEWAY_TESTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp"
)
# test_webrtc_*: necesitan libwebrtc de verdad; los compila el subproyecto
# cmake/gateway-webrtc (clang + libc++ de Chromium), no este build.
list(FILTER BA_GATEWAY_TESTS EXCLUDE REGEX ".*/test_webrtc_[^/]*\\.cpp$")

# Sources del gateway para los tests: los del propio directorio (no
# recursivo, para no meter los tests) sin main.cpp.
file(GLOB BA_GATEWAY_LIB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/net/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/proxy/*.cpp"
)
list(FILTER BA_GATEWAY_LIB_SOURCES EXCLUDE REGEX ".*/tests/.*")
list(FILTER BA_GATEWAY_LIB_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")
list(APPEND BA_GATEWAY_LIB_SOURCES "${BA_GW_HTTPS}")
if(NOT BA_HAVE_LWIP)
    list(FILTER BA_GATEWAY_LIB_SOURCES EXCLUDE REGEX ".*/net/IpStack\\.cpp$")
    list(FILTER BA_GATEWAY_TESTS EXCLUDE REGEX ".*/test_ipstack\\.cpp$")
endif()

foreach(test_source ${BA_GATEWAY_TESTS})
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source} ${BA_GATEWAY_LIB_SOURCES} "${BA_QRCODEGEN_DIR}/qrcodegen.cpp")
    target_include_directories(${test_name} PRIVATE "${BA_QRCODEGEN_DIR}")
    target_include_directories(${test_name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    # Tests de LOGICA: siempre con el backend NULL (el SDP de prueba no es un
    # SDP real). Los medios reales los prueba test_webrtc_loopback.
    target_compile_definitions(${test_name} PRIVATE
        BA_GATEWAY_HAVE_WEBRTC=0
        # Webroot real (assets/web/guest) para el humo HTTP: sirve el index del
        # invitado de verdad en vez de un directorio inventado.
        BA_TEST_WEBROOT="${CMAKE_SOURCE_DIR}/assets/web/guest"
        # Par WireGuard de referencia (Python) para la interoperabilidad.
        BA_TEST_WG_PEER="${CMAKE_CURRENT_SOURCE_DIR}/tests/wg_reference_peer.py")
    target_link_libraries(${test_name} PRIVATE ba_core mbedcrypto)
    if(BA_HAVE_LWIP)
        target_link_libraries(${test_name} PRIVATE ba_lwip)
        target_compile_definitions(${test_name} PRIVATE BA_HAVE_LWIP=1)
    endif()
    if(WIN32)
        target_link_libraries(${test_name} PRIVATE ws2_32)
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
    # 77 = saltado (ver src/core/CMakeLists.txt).
    set_tests_properties(${test_name} PROPERTIES SKIP_RETURN_CODE 77)
endforeach()

# Windows: el test de medios reales (RTP/SRTP sobre UDP local) tambien.
if(BA_GATEWAY_WEBRTC AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_webrtc_loopback.cpp")
    add_executable(test_webrtc_loopback tests/test_webrtc_loopback.cpp ${BA_GATEWAY_LIB_SOURCES}
                   "${BA_QRCODEGEN_DIR}/qrcodegen.cpp")
    target_include_directories(test_webrtc_loopback PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${BA_QRCODEGEN_DIR}")
    target_link_libraries(test_webrtc_loopback PRIVATE ba_core mbedcrypto ba_webrtc_win ws2_32)
    if(BA_HAVE_LWIP)
        target_link_libraries(test_webrtc_loopback PRIVATE ba_lwip)
        target_compile_definitions(test_webrtc_loopback PRIVATE BA_HAVE_LWIP=1)
    endif()
    set_source_files_properties(tests/test_webrtc_loopback.cpp PROPERTIES COMPILE_OPTIONS "/GR-")
    add_test(NAME test_webrtc_loopback COMMAND test_webrtc_loopback)
    set_tests_properties(test_webrtc_loopback PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()
if(BA_GATEWAY_WEBRTC_MAC AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_webrtc_loopback.cpp")
    add_executable(test_webrtc_loopback tests/test_webrtc_loopback.cpp ${BA_GATEWAY_LIB_SOURCES}
                   "${BA_QRCODEGEN_DIR}/qrcodegen.cpp")
    target_include_directories(test_webrtc_loopback PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${BA_QRCODEGEN_DIR}")
    target_link_libraries(test_webrtc_loopback PRIVATE ba_core mbedcrypto ba_webrtc_mac)
    if(BA_HAVE_LWIP)
        target_link_libraries(test_webrtc_loopback PRIVATE ba_lwip)
        target_compile_definitions(test_webrtc_loopback PRIVATE BA_HAVE_LWIP=1)
    endif()
    set_source_files_properties(tests/test_webrtc_loopback.cpp PROPERTIES COMPILE_OPTIONS "-fno-rtti")
    add_test(NAME test_webrtc_loopback COMMAND test_webrtc_loopback)
    set_tests_properties(test_webrtc_loopback PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()

# --- Instalación ------------------------------------------------------------
# Proceso B va a bin/64bit/ (spec §5.1). La app web del invitado se instala en
# Web/guest/ (la sirve B, spec §5.1 layout).
# --- Gateway con libwebrtc REAL en Linux --------------------------------------
# libwebrtc (Shiguredo M123) esta compilado contra la libc++ de Chromium
# (std::__Cr) y la trae adentro. El gateway real se compila aparte, con clang y
# esos headers (cmake/gateway-webrtc); reutiliza las libs de C de este build.
# El ba-gateway de arriba (GCC, backend NULL) sigue existiendo para los tests
# de logica. `make deps-webrtc BA_WEBRTC_TARGET=linux` baja el SDK.
set(BA_GATEWAY_REAL_POSIX OFF)
set(BA_GW_RTC_DIR "${CMAKE_BINARY_DIR}/gateway-webrtc")
if(UNIX AND NOT APPLE AND BA_WEBRTC_ROOT
   AND EXISTS "${BA_WEBRTC_ROOT}/lib/libwebrtc.a"
   AND EXISTS "${BA_WEBRTC_ROOT}/libcxx/include/vector"
   AND EXISTS "${BA_WEBRTC_ROOT}/buildtools-libcxx/__config_site")
    find_program(BA_CLANG clang)
    find_program(BA_CLANGXX clang++)
    if(BA_CLANG AND BA_CLANGXX)
        set(BA_GATEWAY_REAL_POSIX ON)
    else()
        message(WARNING "ba-gateway: hay SDK de libwebrtc pero falta clang; queda el backend NULL")
    endif()
endif()

if(BA_GATEWAY_REAL_POSIX)
    message(STATUS "ba-gateway: libwebrtc REAL (clang + libc++ de Chromium) en ${BA_GW_RTC_DIR}")
    include(ExternalProject)
    ExternalProject_Add(ba_gateway_webrtc
        SOURCE_DIR "${CMAKE_SOURCE_DIR}/cmake/gateway-webrtc"
        BINARY_DIR "${BA_GW_RTC_DIR}"
        LIST_SEPARATOR |
        CMAKE_ARGS
            -DCMAKE_C_COMPILER=${BA_CLANG}
            -DCMAKE_CXX_COMPILER=${BA_CLANGXX}
            -DCMAKE_BUILD_TYPE=Release
            -DBA_SRC=${CMAKE_SOURCE_DIR}
            -DBA_WEBRTC_ROOT=${BA_WEBRTC_ROOT}
            -DBA_LIBCXX_CFG=${BA_WEBRTC_ROOT}/buildtools-libcxx
            "-DBA_C_LIBS=$<TARGET_FILE:mbedtls>|$<TARGET_FILE:mbedx509>|$<TARGET_FILE:mbedcrypto>|$<TARGET_FILE:samplerate>|$<TARGET_FILE:argon2>"
            "-DBA_C_INCLUDES=${mbedtls_SOURCE_DIR}/include|${libsamplerate_SOURCE_DIR}/include|${argon2_SOURCE_DIR}/include|${nlohmann_json_SOURCE_DIR}/include"
        INSTALL_COMMAND ""
        BUILD_ALWAYS ON
        DEPENDS mbedtls mbedx509 mbedcrypto samplerate argon2
        BUILD_BYPRODUCTS "${BA_GW_RTC_DIR}/ba-gateway" "${BA_GW_RTC_DIR}/test_webrtc_loopback")
    # Medios reales de punta a punta (RTP/SRTP sobre UDP local).
    add_test(NAME test_webrtc_loopback COMMAND "${BA_GW_RTC_DIR}/test_webrtc_loopback")
    set_tests_properties(test_webrtc_loopback PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
    install(PROGRAMS "${BA_GW_RTC_DIR}/ba-gateway" DESTINATION bin/64bit)
else()
    install(TARGETS ba-gateway
        RUNTIME DESTINATION bin/64bit
    )
endif()
# App web del invitado (spec §8.3). Si el directorio existe en el árbol de
# fuente se copia; si no, se deja el directorio vacío (lo crea el root).
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/web/guest")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/web/guest/"
        DESTINATION Web/guest
    )
endif()
# Consola web del operador (la sirve B en 127.0.0.1:adminPort/admin/).
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/web/admin")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/web/admin/"
        DESTINATION Web/admin
    )
endif()
# Tablero de oradores (solo lectura, para una tablet; lo sirve B en /board/).
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/web/board")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/web/board/"
        DESTINATION Web/board
    )
endif()
# El canal de control, compartido por TODAS las superficies web (canal.js).
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/web/comun")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/web/comun/"
        DESTINATION Web/comun
    )
endif()
# Nota: assets/web/pantalla/ incluye carrusel/ (se copia el directorio entero).
# El programa en vivo para un televisor (/cast/): la pagina del invitado sin
# las partes de invitado, por WebRTC (100-300 ms, contra los 1-5 s del
# reproductor de fabrica del Chromecast).
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/web/cast")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/web/cast/"
        DESTINATION Web/cast
    )
endif()
# Lo que se manda a un televisor (/pantalla/): mensajes, QR y reloj. Una sola
# pagina para todo lo que se proyecta (ver assets/web/pantalla/index.html).
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/web/pantalla")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/web/pantalla/"
        DESTINATION Web/pantalla
    )
endif()
# Puesto del corrector de subtítulos (/correccion/#k=…, T44).
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/web/correccion")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/web/correccion/"
        DESTINATION Web/correccion
    )
endif()

# Teleprompter: la tablet del orador (/prompter) y el control del celular
# (/prompter/#control). Lo sirve B, como el resto de las vistas web.
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/web/prompter")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/web/prompter/"
        DESTINATION Web/prompter
    )
endif()

# Botonera: la superficie de control de la tablet (/botonera/), emparejada por QR.
# La consola web de la presidencia (/presidencia/, docs/funciones/SESION-REMOTA.md §8).
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/web/presidencia")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/web/presidencia/"
        DESTINATION Web/presidencia
    )
endif()
# La sesión remota de los integrantes (/sesion/, docs/funciones/SESION-REMOTA.md).
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/web/sesion")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/web/sesion/"
        DESTINATION Web/sesion
    )
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/web/botonera")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/web/botonera/"
        DESTINATION Web/botonera
    )
endif()

# Publico: el que esta en la butaca, con su telefono (/publico/). Pregunta y
# vota; no entra a la sala ni pide camara.
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/web/publico")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/web/publico/"
        DESTINATION Web/publico
    )
endif()

# Rotulos: escribir el nombre del que habla desde el celular (/rotulos/).
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/web/rotulos")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/web/rotulos/"
        DESTINATION Web/rotulos
    )
endif()
