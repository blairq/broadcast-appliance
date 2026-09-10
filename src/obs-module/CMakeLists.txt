# ============================================================================
# src/obs-module — `ba-sources` (módulo propio de fuentes libobs)  [SKELETON]
#
# ⚠️  SKELETON: solo el andamiaje de build. El contenido real (filtros de
#     channel strip, fuentes IPC, automixer — spec §7, §4) lo aporta el agente
#     del módulo.
#
# Es una DLL de libobs (shared lib) que se despliega en obs-plugins/64bit/
# (spec §5.1). En Windows enlaza libobs; en Linux se configura como esqueleto
# para que el árbol compile.
#
# /MD (CRT dinámica) por consistencia con libobs — hereda del root.
# ============================================================================

# --- Sources ----------------------------------------------------------------
file(GLOB_RECURSE BA_SOURCES_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

# SKELETON: placeholder para que el target configure y compile hasta que el
# agente del módulo aporte los sources reales.
if(NOT BA_SOURCES_SOURCES)
    message(STATUS "ba-sources: SKELETON — sin sources propios todavía; usando placeholder")
    set(BA_SOURCES_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/placeholder.cpp")
endif()

# Shared lib: OBS la carga como plugin. En Windows se llama ba-sources.dll.
add_library(ba-sources SHARED ${BA_SOURCES_SOURCES})

target_link_libraries(ba-sources PRIVATE ba_core)

# --- Instalación ------------------------------------------------------------
# El módulo va a obs-plugins/64bit/ (spec §5.1).
install(TARGETS ba-sources
    RUNTIME DESTINATION obs-plugins/64bit
    LIBRARY DESTINATION obs-plugins/64bit
    ARCHIVE DESTINATION obs-plugins/64bit
)
