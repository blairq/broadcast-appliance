# ============================================================================
# src/gateway — `ba-gateway` (Proceso B: Gateway WebRTC + conversación + túnel)
# [SKELETON]
#
# ⚠️  SKELETON: solo el andamiaje de build. La lógica (libwebrtc, motor de
#     conversación, señalización, supervisión de cloudflared — spec §8) la
#     aporta el agente del Proceso B.
#
# Headless: ícono en bandeja, sin Qt (spec §3.3). En Windows enlaza libwebrtc
# estáticamente; en Linux se configura como esqueleto para que el árbol
# compile.
#
# /MD (CRT dinámica) por consistencia con libwebrtc — hereda del root.
# ============================================================================

# --- Sources ----------------------------------------------------------------
file(GLOB_RECURSE BA_GATEWAY_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

# SKELETON: placeholder para que el target configure y compile hasta que el
# agente de B aporte los sources reales.
if(NOT BA_GATEWAY_SOURCES)
    message(STATUS "ba-gateway: SKELETON — sin sources propios todavía; usando placeholder")
    set(BA_GATEWAY_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/placeholder.cpp")
endif()

# WIN32 en MSVC: subsistema GUI (sin consola). En Linux es un ejecutable normal.
if(MSVC)
    add_executable(ba-gateway WIN32 ${BA_GATEWAY_SOURCES})
else()
    add_executable(ba-gateway ${BA_GATEWAY_SOURCES})
endif()

target_link_libraries(ba-gateway PRIVATE ba_core)

# --- Instalación ------------------------------------------------------------
# Proceso B va a bin/64bit/ (spec §5.1).
install(TARGETS ba-gateway
    RUNTIME DESTINATION bin/64bit
)
