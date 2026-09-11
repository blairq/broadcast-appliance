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
else()
    add_executable(ba-gateway ${BA_GATEWAY_SOURCES})
endif()

target_link_libraries(ba-gateway PRIVATE ba_core)

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
if(WIN32)
    if(NOT DEFINED BA_WEBRTC_ROOT)
        set(BA_WEBRTC_ROOT "${CMAKE_SOURCE_DIR}/extern/webrtc")
    endif()
    if(BA_WEBRTC_ROOT AND EXISTS "${BA_WEBRTC_ROOT}/include")
        set(BA_GATEWAY_WEBRTC 1)
    endif()
endif()

if(BA_GATEWAY_WEBRTC)
    message(STATUS "ba-gateway: libwebrtc en ${BA_WEBRTC_ROOT}")
    target_compile_definitions(ba-gateway PRIVATE BA_GATEWAY_HAVE_WEBRTC=1)
    target_include_directories(ba-gateway PRIVATE "${BA_WEBRTC_ROOT}/include")
    target_link_directories(ba-gateway PRIVATE "${BA_WEBRTC_ROOT}/lib")
    # Binarios precompilados de Shiguredo: libwebrtc.a (+ deps). El nombre
    # exacto del artefacto puede variar entre builds; se declara el link
    # genérico y se ajusta en el deploy si el tag lo requiere.
    # VERIFY: nombre exacto del .a en el webrtc-build fijado (M123).
    target_link_libraries(ba-gateway PRIVATE webrtc)
else()
    message(STATUS "ba-gateway: SDK libwebrtc ausente; backend NULL (BA_GATEWAY_HAVE_WEBRTC=0)")
    target_compile_definitions(ba-gateway PRIVATE BA_GATEWAY_HAVE_WEBRTC=0)
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

# Sources del gateway para los tests: los del propio directorio (no
# recursivo, para no meter los tests) sin main.cpp.
file(GLOB BA_GATEWAY_LIB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
list(FILTER BA_GATEWAY_LIB_SOURCES EXCLUDE REGEX ".*/tests/.*")
list(FILTER BA_GATEWAY_LIB_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

foreach(test_source ${BA_GATEWAY_TESTS})
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source} ${BA_GATEWAY_LIB_SOURCES})
    target_include_directories(${test_name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(${test_name} PRIVATE
        BA_GATEWAY_HAVE_WEBRTC=${BA_GATEWAY_WEBRTC})
    target_link_libraries(${test_name} PRIVATE ba_core)
    if(WIN32)
        target_link_libraries(${test_name} PRIVATE ws2_32)
    endif()
    if(BA_GATEWAY_WEBRTC)
        target_include_directories(${test_name} PRIVATE "${BA_WEBRTC_ROOT}/include")
        target_link_directories(${test_name} PRIVATE "${BA_WEBRTC_ROOT}/lib")
        target_link_libraries(${test_name} PRIVATE webrtc)
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# --- Instalación ------------------------------------------------------------
# Proceso B va a bin/64bit/ (spec §5.1). La app web del invitado se instala en
# Web/guest/ (la sirve B, spec §5.1 layout).
install(TARGETS ba-gateway
    RUNTIME DESTINATION bin/64bit
)
# App web del invitado (spec §8.3). Si el directorio existe en el árbol de
# fuente se copia; si no, se deja el directorio vacío (lo crea el root).
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/web/guest")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/web/guest/"
        DESTINATION Web/guest
    )
endif()
