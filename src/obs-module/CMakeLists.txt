# ============================================================================
# src/obs-module — `ba-sources` (módulo propio de fuentes/filtros libobs)
#
# Biblioteca compartida que libobs carga como plugin, desplegada en
# obs-plugins/64bit/ (spec §5.1). Registra seis cosas:
#   - FUENTE `ba_guest_audio`: lee el ring BA.Audio.Guest.<n> del Proceso B.
#   - FUENTE `ba_guest_video`: lee el canal compartido BA.Guest.<n>.
#   - FILTRO `ba_channel_strip`: el DSP nativo de ba_core (HPF->EQ4->de-esser->
#     gate->comp->lim), RT-safe, con presets podium/lavalier/handheld.
#   - FUENTE `ba_channel_preset`: tono de referencia para oir cada preset.
#   - FILTRO `ba_mejora_imagen`: ruido temporal + color guiado + nitidez, en
#     la GPU (data/mejora-imagen.effect, que libobs traduce a D3D11/OpenGL).
#   - FILTRO `ba_ptz_digital`: pan/tilt/zoom recortando en la GPU, el PTZ que
#     tiene cualquier entrada (data/ptz-digital.effect).
#   - FILTRO `ba_video_grafo` + FUENTE `ba_video_salida`: el grafo de tarjetas
#     de video de una entrada y sus salidas con nombre (data/video-grafo.effect).
#
# Se compila contra libobs DE VERDAD y en CUALQUIER plataforma: libobs es
# cross-platform, asi que aca no hay `#ifdef _WIN32`. Si libobs no esta
# disponible se compila solo `ba_placeholder.cpp`, para que el arbol siga
# compilando y el resto del proyecto se pueda validar igual.
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
# (mismo criterio que ba_core).
list(FILTER BA_SOURCES_SOURCES EXCLUDE REGEX ".*/tests/.*")

# Con libobs se compila el modulo real; sin ella, solo el esqueleto.
if(BA_HAVE_LIBOBS)
    list(FILTER BA_SOURCES_SOURCES EXCLUDE REGEX ".*/ba_placeholder\\.cpp$")
else()
    list(FILTER BA_SOURCES_SOURCES EXCLUDE REGEX ".*/ba_module\\.cpp$")
    list(FILTER BA_SOURCES_SOURCES EXCLUDE REGEX ".*/ba_ipc_sources\\.cpp$")
    list(FILTER BA_SOURCES_SOURCES EXCLUDE REGEX ".*/ba_dsp_sources\\.cpp$")
    list(FILTER BA_SOURCES_SOURCES EXCLUDE REGEX ".*/ba_mejora_imagen\\.cpp$")
    list(FILTER BA_SOURCES_SOURCES EXCLUDE REGEX ".*/ba_ptz_digital\\.cpp$")
    list(FILTER BA_SOURCES_SOURCES EXCLUDE REGEX ".*/ba_video_grafo\\.cpp$")
endif()

# Shared lib: OBS la carga como plugin. En Windows se llama ba-sources.dll.
add_library(ba-sources SHARED ${BA_SOURCES_SOURCES})

# `ba_core` aporta (PUBLIC) la ruta de src/core para los headers de dsp/ e ipc/;
# los de libobs los aporta el target importado `ba::libobs`.

target_link_libraries(ba-sources PRIVATE ba_core)

# --- libobs real ------------------------------------------------------------
# Con libobs presente el modulo se compila de verdad EN CUALQUIER PLATAFORMA
# (libobs es cross-platform), no solo en Windows. El nombre del archivo queda
# sin el prefijo `lib` porque asi lo espera el patron de carga de OBS
# (`%module%.so` / `%module%.dll`).
if(BA_HAVE_LIBOBS)
    target_link_libraries(ba-sources PRIVATE ba::libobs)
    target_compile_definitions(ba-sources PRIVATE BA_HAVE_LIBOBS=1)
    set_target_properties(ba-sources PROPERTIES PREFIX "")
    if(APPLE)
        # macOS: `.so` como en Linux (no `.dylib`), que es lo que busca el
        # motor junto a los bundles .plugin; rpath con @loader_path.
        set_target_properties(ba-sources PROPERTIES
            SUFFIX ".so"
            BUILD_RPATH "${BA_OBS_MAC_PREFIX}/lib"
            INSTALL_RPATH "@loader_path;@loader_path/../../bin/64bit")
    elseif(NOT WIN32)
        set_target_properties(ba-sources PROPERTIES
            BUILD_RPATH "${BA_OBS_LINUX_PREFIX}/lib"
            INSTALL_RPATH "$ORIGIN:$ORIGIN/../../bin/64bit")
    endif()
endif()

# --- Datos del modulo (locale) ----------------------------------------------
# libobs busca los textos en <data>/%module%/locale/<idioma>.ini. Se copian
# junto al binario para que el modulo tenga sus nombres tanto al correr desde
# el arbol de build como desde el bundle instalado.
# Es un target aparte (y no un POST_BUILD) para que cambiar SOLO un dato (el
# shader de Mejorar imagen) tambien se copie: el POST_BUILD corre solo cuando
# se vuelve a enlazar la biblioteca.
if(BA_HAVE_LIBOBS)
    file(GLOB_RECURSE _ba_sources_datos CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/data/*")
    set(_ba_sources_sello "${CMAKE_CURRENT_BINARY_DIR}/ba-sources-datos.sello")
    add_custom_command(OUTPUT "${_ba_sources_sello}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/data"
                "${CMAKE_CURRENT_BINARY_DIR}/ba-sources"
        COMMAND ${CMAKE_COMMAND} -E touch "${_ba_sources_sello}"
        DEPENDS ${_ba_sources_datos}
        COMMENT "ba-sources: copiando datos del modulo (locale, shaders)")
    add_custom_target(ba-sources-datos ALL DEPENDS "${_ba_sources_sello}")
    add_dependencies(ba-sources ba-sources-datos)
endif()

# --- Unit tests de ba-sources (CTest) ----------------------------------------
# Cada test_*.cpp en src/obs-module/tests/ se convierte en un ejecutable que
# enlaza `ba_core` (el DSP nativo que el módulo envuelve) y se registra con
# add_test. No hay framework externo: cada test es un main() que devuelve 0 si
# pasa y != 0 si falla (mismo patrón que src/core).
#
# `test_ba_sources_real` carga el módulo en una libobs real y comprueba que el
# ABI y el registro de fuentes funcionan; solo se compila si libobs está.
file(GLOB_RECURSE BA_SOURCES_TESTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp"
)

# `test_ba_sources_real` necesita libobs de verdad: se excluye si no esta.
if(NOT BA_HAVE_LIBOBS)
    list(FILTER BA_SOURCES_TESTS EXCLUDE REGEX ".*/test_ba_sources_real\\.cpp$")
    list(FILTER BA_SOURCES_TESTS EXCLUDE REGEX ".*/test_mejora_imagen\\.cpp$")
    list(FILTER BA_SOURCES_TESTS EXCLUDE REGEX ".*/test_video_grafo\\.cpp$")
endif()

# El modulo grafico con el que `test_mejora_imagen` corre el filtro en la GPU.
if(WIN32)
    set(_ba_test_gfx "libobs-d3d11")
elseif(APPLE)
    set(_ba_test_gfx "${BA_OBS_MAC_PREFIX}/lib/libobs-opengl.so")
else()
    set(_ba_test_gfx "libobs-opengl")
endif()

foreach(test_source ${BA_SOURCES_TESTS})
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} PRIVATE ba_core)

    if(test_name STREQUAL "test_ba_sources_real" OR test_name STREQUAL "test_mejora_imagen"
       OR test_name STREQUAL "test_video_grafo")
        target_link_libraries(${test_name} PRIVATE ba::libobs ${CMAKE_DL_LIBS})
        # Directorio donde el build deja ba-sources: el test carga desde ahi.
        target_compile_definitions(${test_name} PRIVATE
            BA_SOURCES_MODULE_DIR="$<TARGET_FILE_DIR:ba-sources>"
            BA_TEST_GRAPHICS_MODULE="${_ba_test_gfx}"
            BA_TEST_OBS_DATA_DIR="${BA_LIBOBS_DATA_DIR}")
        add_dependencies(${test_name} ba-sources)
        if(NOT WIN32)
            set_target_properties(${test_name} PROPERTIES
                BUILD_RPATH "${BA_OBS_LINUX_PREFIX}/lib")
        endif()
    endif()

    add_test(NAME ${test_name} COMMAND ${test_name})
    # 77 = saltado (ver src/core/CMakeLists.txt).
    set_tests_properties(${test_name} PROPERTIES SKIP_RETURN_CODE 77)
endforeach()

# --- Instalación ------------------------------------------------------------
# El módulo va a obs-plugins/64bit/ (spec §5.1).
install(TARGETS ba-sources
    RUNTIME DESTINATION obs-plugins/64bit
    LIBRARY DESTINATION obs-plugins/64bit
    # La lib de importacion (.lib en Windows) no sirve en el bundle.
    ARCHIVE DESTINATION lib COMPONENT thirdparty
)

# Datos del modulo en el layout que libobs espera dentro del bundle.
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/"
    DESTINATION data/obs-plugins/ba-sources)
