# ============================================================================
# src/obs-module — `ba-sources` (módulo propio de fuentes/filtros libobs)
#
# Es una DLL de libobs (shared lib) que se despliega en obs-plugins/64bit/
# (spec §5.1). Registra (spec §7.2/§7.3) sobre el DSP nativo de ba_core::dsp:
#   - FILTRO de audio `ba_channel_strip` (ChannelStrip: HPF->EQ4->de-esser->
#     gate->comp->lim), RT-safe, con presets podium/lavalier/handheld.
#   - FUENTE async `ba_channel_preset` (expone un preset de canal como
#     referencia audible).
#
# Todo lo de libobs va tras #ifdef _WIN32 en los sources: en Windows enlaza
# contra obs.dll (los headers mínimos de la API de libobs se vendorean en
# obs/ para que el cross-build compile sin libobs instalada); en Linux se
# configura como esqueleto para que el árbol compile.
#
# /MD (CRT dinámica) por consistencia con libobs — hereda del root.
# ============================================================================

# --- Sources ----------------------------------------------------------------
file(GLOB_RECURSE BA_SOURCES_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

# Excluir los unit tests del propio lib: son ejecutables, no parte de la DLL
# (mismo criterio que ba_core y ba_plugin_host).
list(FILTER BA_SOURCES_SOURCES EXCLUDE REGEX ".*/tests/.*")

# Shared lib: OBS la carga como plugin. En Windows se llama ba-sources.dll.
add_library(ba-sources SHARED ${BA_SOURCES_SOURCES})

# Incluye privados:
#   - el propio directorio, para resolver los headers mínimos de la API de
#     libobs vendoreados en obs/ (obs/obs-module.h, obs/obs.h, obs/obs-properties.h).
# `ba_core` aporta (PUBLIC) la ruta de src/core para los headers de dsp/.
target_include_directories(ba-sources
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(ba-sources PRIVATE ba_core)

# --- Unit tests de ba-sources (CTest) ----------------------------------------
# Cada test_*.cpp en src/obs-module/tests/ se convierte en un ejecutable que
# enlaza `ba_core` (el DSP nativo que el módulo envuelve) y se registra con
# add_test. No hay framework externo: cada test es un main() que devuelve 0 si
# pasa y != 0 si falla (mismo patrón que src/core y src/plugin-host).
#
# El test de carga (`test_ba_sources_load`) va tras el feature-flag
# `BA_SOURCES_HAVE_OBS`: cuando libobs está presente se define el flag (y se
# enlaza obs) para ejercitar obs_startup + registro + procesado; cuando no
# (Linux nativo / cross MinGW) el test imprime `SKIP: no libobs` y devuelve 0,
# compilando sin headers de libobs. Aquí solo se enlaza `ba_core` en ambos
# casos; el enlace contra obs y la definición del flag se añaden cuando libobs
# esté disponible (mismo criterio que ba-app con BA_APP_HAVE_QT_OBS).
file(GLOB_RECURSE BA_SOURCES_TESTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp"
)

foreach(test_source ${BA_SOURCES_TESTS})
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} PRIVATE ba_core)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# --- Instalación ------------------------------------------------------------
# El módulo va a obs-plugins/64bit/ (spec §5.1).
install(TARGETS ba-sources
    RUNTIME DESTINATION obs-plugins/64bit
    LIBRARY DESTINATION obs-plugins/64bit
    ARCHIVE DESTINATION obs-plugins/64bit
)
