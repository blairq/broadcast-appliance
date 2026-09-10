# ============================================================================
# src/core — `ba_core` (núcleo, SIN Qt, SIN libobs)
#
# Es la pieza testeable por unidad (spec §9, §14). No depende de Qt ni de
# libobs, por lo que debe compilar en Windows/MSVC Y en Linux (gcc/clang) para
# poder correr sus unit tests en el desarrollo local.
#
# Los sources los aportan los agentes del núcleo; aquí solo se descubren con
# file(GLOB_RECURSE ... CONFIGURE_DEPENDS) para no nombrar archivos.
#
# Los unit tests viven en src/core/tests/test_*.cpp: cada uno se compila como
# un ejecutable que enlaza `ba_core` y se registra como un test de CTest.
# ============================================================================

# --- Sources del núcleo -----------------------------------------------------
# CONFIGURE_DEPENDS hace que CMake re-ejecute el glob al generar, de modo que
# los archivos que vayan agregando los agentes se detecten sin reconfigurar a
# mano.
file(GLOB_RECURSE BA_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

# Excluir los unit tests del propio lib: son ejecutables, no parte de la lib.
list(FILTER BA_CORE_SOURCES EXCLUDE REGEX ".*/tests/.*")

# Saneamiento: si no hay sources todavía (otros agentes aún no los escribieron)
# creamos un placeholder mínimo para que el target configure y compile.
if(NOT BA_CORE_SOURCES)
    message(STATUS "ba_core: sin sources propios todavía; usando placeholder para que el target configure")
    set(BA_CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/placeholder.cpp")
endif()

add_library(ba_core STATIC ${BA_CORE_SOURCES})

# Incluye públicos: los headers de ba_core se consumen desde tests y desde
# launcher/app/gateway.
target_include_directories(ba_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

# El núcleo no enlaza Qt ni libobs (spec §1.7). Solo std + las deps de
# terceros que se declaran en el root (FetchContent). En MSVC hereda los flags
# /MD del root; el launcher se pasa a /MT solo para él.
#   - mbedtls: PRIVATE porque ningún header de ba_core expone tipos de mbedtls.
#   - nlohmann_json: PUBLIC por si un header expone nlohmann::json (header-only,
#     barato propagarlo).
#   - samplerate (libsamplerate): PRIVATE porque Resampler.hpp declara
#     `SRC_STATE*` como opaca (struct SRC_STATE;), de modo que ningún header de
#     ba_core expone tipos de libsamplerate. El include de <samplerate.h> vive
#     solo en Resampler.cpp.
#   - argon2 (referencia P-H-C): PRIVATE porque SecretsStore.hpp NO expone
#     tipos de argon2 (solo usa std::string/std::size_t); el include de
#     <argon2.h> vive solo en SecretsStore.cpp. KDF Argon2id de secrets.json
#     (spec §12, D7).
target_link_libraries(ba_core PRIVATE mbedcrypto mbedx509 mbedtls)
target_link_libraries(ba_core PUBLIC nlohmann_json::nlohmann_json)
target_link_libraries(ba_core PRIVATE samplerate)
target_link_libraries(ba_core PRIVATE argon2)

# --- Unit tests de ba_core (CTest) ------------------------------------------
# Cada test_*.cpp en src/core/tests/ se convierte en un ejecutable que enlaza
# ba_core y se registra con add_test. No hay framework externo: cada test es
# un main() que devuelve 0 si pasa y != 0 si falla (lo definen los agentes).
file(GLOB_RECURSE BA_CORE_TESTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp"
)

foreach(test_source ${BA_CORE_TESTS})
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} PRIVATE ba_core)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# --- Instalación ------------------------------------------------------------
# ba_core es estática y se enlaza dentro de los ejecutables/DLLs propias; no se
# instala como artefacto suelto. (Los headers tampoco: son internos.)
