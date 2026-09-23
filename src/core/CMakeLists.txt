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

# --- Capa de plataforma (`platform/`) ----------------------------------------
# Cada interfaz de `ba::platform` tiene una implementacion por sistema, elegida
# por SUFIJO de archivo y no por #ifdef en la logica:
#   *_win.cpp   -> Windows
#   *_posix.cpp -> Linux y macOS (lo comun a POSIX)
#   *_linux.cpp -> solo Linux
#   *_mac.cpp / *_mac.mm -> solo macOS
# Los archivos sin sufijo (p. ej. Paths.cpp) son comunes a todas.
file(GLOB BA_CORE_PLATFORM_MM CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/platform/*_mac.mm")
if(WIN32)
    list(FILTER BA_CORE_SOURCES EXCLUDE REGEX ".*/platform/.*_(posix|linux|mac)\\.cpp$")
elseif(APPLE)
    list(FILTER BA_CORE_SOURCES EXCLUDE REGEX ".*/platform/.*_(win|linux)\\.cpp$")
    list(APPEND BA_CORE_SOURCES ${BA_CORE_PLATFORM_MM})
else()
    list(FILTER BA_CORE_SOURCES EXCLUDE REGEX ".*/platform/.*_(win|mac)\\.cpp$")
endif()

# Saneamiento: si no hay sources todavía (otros agentes aún no los escribieron)
# creamos un placeholder mínimo para que el target configure y compile.
if(NOT BA_CORE_SOURCES)
    message(STATUS "ba_core: sin sources propios todavía; usando placeholder para que el target configure")
    set(BA_CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/placeholder.cpp")
endif()

# Generador de QR con estilo (qr/Qr.cpp): qrcodegen vive en third_party y lo
# comparten el nucleo y el gateway. Un solo generador para todo el sistema,
# porque el mismo QR sale por la pantalla, por un televisor y por un archivo.
set(BA_QRCODEGEN "${CMAKE_SOURCE_DIR}/third_party/qrcodegen")
list(APPEND BA_CORE_SOURCES "${BA_QRCODEGEN}/qrcodegen.cpp")

# --- Reconocimiento de rostros (opcional) ------------------------------------
# Solo si estan los modelos y el runtime vendoreados (`make deps-rostros`).
# Sin eso, el nucleo compila igual y el resto del sistema no se entera: esto
# agrega una capacidad, no es un cimiento.
set(BA_ROSTROS_DIR "${CMAKE_SOURCE_DIR}/extern/rostros")
set(BA_HAVE_ROSTROS OFF)
if(EXISTS "${BA_ROSTROS_DIR}/onnxruntime/include/onnxruntime_cxx_api.h"
   AND EXISTS "${BA_ROSTROS_DIR}/modelos/yunet.onnx")
    set(BA_HAVE_ROSTROS ON)
else()
    list(FILTER BA_CORE_SOURCES EXCLUDE REGEX ".*/rostros/.*")
endif()

add_library(ba_core STATIC ${BA_CORE_SOURCES})
if(BA_HAVE_ROSTROS)
    file(GLOB _ba_ort_lib "${BA_ROSTROS_DIR}/onnxruntime/lib/libonnxruntime.so"
                          "${BA_ROSTROS_DIR}/onnxruntime/lib/onnxruntime.lib"
                          "${BA_ROSTROS_DIR}/onnxruntime/lib/libonnxruntime.dylib")
    list(GET _ba_ort_lib 0 _ba_ort_lib)
    target_include_directories(ba_core PUBLIC "${BA_ROSTROS_DIR}/onnxruntime/include")
    target_link_libraries(ba_core PUBLIC "${_ba_ort_lib}")
    target_compile_definitions(ba_core PUBLIC BA_HAVE_ROSTROS=1)
    # La biblioteca viaja en el bundle, como todo lo demas.
    file(GLOB _ba_ort_runtime "${BA_ROSTROS_DIR}/onnxruntime/lib/libonnxruntime.so*"
                              "${BA_ROSTROS_DIR}/onnxruntime/lib/onnxruntime.dll")
    install(FILES ${_ba_ort_runtime} DESTINATION bin/64bit)
    install(DIRECTORY "${BA_ROSTROS_DIR}/modelos/" DESTINATION data/rostros FILES_MATCHING PATTERN "*.onnx")
    install(FILES "${BA_ROSTROS_DIR}/onnxruntime/LICENSE" DESTINATION data/licencias
            RENAME onnxruntime-MIT.txt OPTIONAL)
endif()
target_include_directories(ba_core PUBLIC "${BA_QRCODEGEN}")

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
# -fPIC: `ba_core` es estatica, pero el modulo de libobs (`ba-sources`) es una
# biblioteca COMPARTIDA que la enlaza. Sin codigo independiente de posicion el
# enlace de ese modulo falla ("can not be used when making a shared object").
set_target_properties(ba_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(ba_core PRIVATE mbedcrypto mbedx509 mbedtls)
target_link_libraries(ba_core PUBLIC nlohmann_json::nlohmann_json)
target_link_libraries(ba_core PRIVATE samplerate)
target_link_libraries(ba_core PRIVATE argon2)

# Bibliotecas del sistema que usa `ba::platform`.
#   Windows: advapi32 (tokens, SIDs y descriptores de seguridad de los pipes).
#   Linux: pthread (hilos de std::thread, explicito por glibc < 2.34).
if(WIN32)
    target_link_libraries(ba_core PUBLIC advapi32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(ba_core PUBLIC Threads::Threads)
endif()

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
    # Codigo 77 = "saltado", no "pasado". Sin esto, un test que imprime SKIP y
    # devuelve 0 lo contaba CTest como PASS, y el verde tapaba que caminos
    # enteros (libwebrtc, libobs) no se ejercitaban nunca.
    set_tests_properties(${test_name} PROPERTIES SKIP_RETURN_CODE 77)
endforeach()

# --- Instalación ------------------------------------------------------------
# ba_core es estática y se enlaza dentro de los ejecutables/DLLs propias; no se
# instala como artefacto suelto. (Los headers tampoco: son internos.)
