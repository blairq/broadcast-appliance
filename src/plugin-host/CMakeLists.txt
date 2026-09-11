# ============================================================================
# src/plugin-host — `ba_plugin_host` (host VST3, spec D6/§7.5 — LSP como CORE)
#
# Módulo que carga los bundles VST3 (los .vst3 de LSP que se despliegan en
# assets/audio-plugins/) y embebe su editor gráfico completo.
#
# Regla de plataforma (por qué el intento anterior falló):
#   - LINUX: el build es OFFLINE. No se trae ni se compila el VST3 SDK; el
#     módulo compila solo con std::filesystem (scanDirectory es puro C++ y
#     se testa en Linux). load/attachEditor son stubs que loguean.
#   - WINDOWS: implementación REAL con los headers del VST3 SDK (MIT), que
#     solo se declara bajo `if(WIN32)` en el CMakeLists raíz (FetchContent).
#
# Los sources los aporta el agente del plugin-host; aquí solo se descubren
# con file(GLOB_RECURSE ... CONFIGURE_DEPENDS) para no nombrar archivos.
#
# Los unit tests viven en src/plugin-host/tests/test_*.cpp: cada uno se
# compila como un ejecutable que enlaza `ba_plugin_host` y se registra como
# un test de CTest (mismo patrón que src/core).
# ============================================================================

# --- Sources del módulo -----------------------------------------------------
file(GLOB_RECURSE BA_PLUGIN_HOST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

# Excluir los unit tests del propio lib: son ejecutables, no parte de la lib.
list(FILTER BA_PLUGIN_HOST_SOURCES EXCLUDE REGEX ".*/tests/.*")

# Saneamiento: si no hay sources todavía creamos un placeholder mínimo para
# que el target configure y compile.
if(NOT BA_PLUGIN_HOST_SOURCES)
    message(STATUS "ba_plugin_host: sin sources propios todavía; usando placeholder para que el target configure")
    set(BA_PLUGIN_HOST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/placeholder.cpp")
endif()

add_library(ba_plugin_host STATIC ${BA_PLUGIN_HOST_SOURCES})

# Incluye públicos: el header Vst3Host.hpp se consume desde tests y desde el
# Proceso A (ba-app) cuando embeba los editores.
target_include_directories(ba_plugin_host
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

# --- nlohmann/json (header-only) --------------------------------------------
# PluginDeployer serializa el manifest.json con nlohmann::ordered_json. Se
# enlaza PUBLIC (header-only, sin binario) para que los tests y el Proceso A
# hereden el include al enlazar contra ba_plugin_host. El target
# `nlohmann_json::nlohmann_json` ya existe: se declara por FetchContent en el
# CMakeLists raíz (mismo target que usa ba_core).
target_link_libraries(ba_plugin_host PUBLIC nlohmann_json::nlohmann_json)

# --- VST3 SDK (MIT) — SOLO Windows ------------------------------------------
# El SDK se trajo por FetchContent bajo `if(WIN32)` en el CMakeLists raíz.
# Aquí solo se enlazan sus targets cuando existen (Windows). En Linux NO se
# enlaza nada del SDK: el módulo compila offline con std::filesystem.
if(WIN32)
    # Targets reales del VST3 SDK v3.7.9 (NO existen `vst_base_core`/
    # `vst_host_lib` en ese tag):
    #   `base`           : base del SDK (FUnknown, FUID, helpers) — sus
    #                      include dirs PUBLIC apuntan a la raíz del SDK, lo
    #                      que expone los headers de `pluginterfaces/...`.
    #   `pluginterfaces` : interfaces (IAudioProcessor, IEditControllerEx1,
    #                      IPlugView, ftypes).
    # Solo se usan como interfaces (métodos virtuales) y constantes. `base`
    # aporta FUnknown/FUID; `pluginterfaces` los headers de las interfaces.
    # `sdk_hosting` aporta la DEFINICIÓN de los `iid` estáticos
    # (IAudioProcessor::iid / IEditController::iid viven en vstinitiids.cpp,
    # que compila dentro de sdk_hosting): sin enlazarlo, el host queda con
    # referencia indefinida a esos símbolos en el link.
    #
    # Feature flag (mismo patrón que ba-app con BA_APP_HAVE_QT_OBS): solo se
    # enlaza el SDK y se define `BA_PLUGIN_HOST_HAVE_VST3` cuando los targets
    # del SDK realmente existen. Así un Windows donde FetchContent no trajo
    # el SDK cae al fallback HEADLESS (solo std::filesystem) y el módulo
    # sigue compilando offline.
    if(TARGET base AND TARGET pluginterfaces AND TARGET sdk_hosting)
        target_link_libraries(ba_plugin_host PRIVATE base pluginterfaces sdk_hosting)
        target_compile_definitions(ba_plugin_host PRIVATE BA_PLUGIN_HOST_HAVE_VST3)
        message(STATUS "ba_plugin_host: VST3 real (SDK presente) -> LoadLibraryW + QI + createView")
    else()
        message(STATUS "ba_plugin_host: modo headless (SDK ausente) -> solo std::filesystem")
    endif()
endif()

# --- Unit tests de ba_plugin_host (CTest) -----------------------------------
# Cada test_*.cpp en src/plugin-host/tests/ se convierte en un ejecutable que
# enlaza ba_plugin_host y se registra con add_test. Sin framework externo:
# cada test es un main() que devuelve 0 si pasa y != 0 si falla.
file(GLOB_RECURSE BA_PLUGIN_HOST_TESTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp"
)

foreach(test_source ${BA_PLUGIN_HOST_TESTS})
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} PRIVATE ba_plugin_host)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# --- Instalación ------------------------------------------------------------
# ba_plugin_host es estática y se enlaza dentro del Proceso A; no se instala
# como artefacto suelto. (Los headers tampoco: son internos.)
