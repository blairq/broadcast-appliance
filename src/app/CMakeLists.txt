# ============================================================================
# src/app — `ba-app` (Proceso A: motor de emisión + UI de operación)  [SKELETON]
#
# ⚠️  SKELETON: solo el andamiaje de build. La lógica (Qt 6 sobre libobs,
#     escenas, encoding, UI del operador) la aporta el agente del Proceso A.
#
# En Windows enlaza Qt 6 + libobs (spec §6). En Linux se configura como
# esqueleto para que el árbol compile; el agente de A decide cómo proveer
# libobs/Qt en cada plataforma (o restringir el target a MSVC).
#
# /MD (CRT dinámica) por consistencia con libobs y Qt — hereda del root.
# ============================================================================

# --- Sources ----------------------------------------------------------------
file(GLOB_RECURSE BA_APP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

# SKELETON: placeholder para que el target configure y compile hasta que el
# agente de A aporte los sources reales.
if(NOT BA_APP_SOURCES)
    message(STATUS "ba-app: SKELETON — sin sources propios todavía; usando placeholder")
    set(BA_APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/placeholder.cpp")
endif()

# WIN32 en MSVC: subsistema GUI (sin consola). En Linux es un ejecutable normal.
if(MSVC)
    add_executable(ba-app WIN32 ${BA_APP_SOURCES})
else()
    add_executable(ba-app ${BA_APP_SOURCES})
endif()

target_link_libraries(ba-app PRIVATE ba_core)

# --- Instalación ------------------------------------------------------------
# Proceso A va a bin/64bit/ (spec §5.1).
install(TARGETS ba-app
    RUNTIME DESTINATION bin/64bit
)
