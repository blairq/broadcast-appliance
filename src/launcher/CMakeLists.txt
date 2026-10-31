# ============================================================================
# src/launcher — `BroadcastAppliance` (Proceso 0: launcher/supervisor)
#
# Win32 puro, CRT estática (/MT), subsistema GUI (sin consola) — spec §3.1, §14.
# Es la pieza más simple y robusta del sistema: no lleva Qt ni DLLs propias.
#
# Se configura en Windows (MSVC) con /MT + WIN32; fuera de Windows se compila
# como stub (main no-Win) para poder validar la compilación del arbol en Linux.
# ============================================================================

# --- Sources ----------------------------------------------------------------
file(GLOB_RECURSE BA_LAUNCHER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

# Excluir los unit tests del target del launcher: son ejecutables propios, no
# parte de BroadcastAppliance (mismo criterio que ba_core).
list(FILTER BA_LAUNCHER_SOURCES EXCLUDE REGEX ".*/tests/.*")

if(NOT BA_LAUNCHER_SOURCES)
    message(STATUS "launcher: sin sources propios todavía; usando placeholder")
    set(BA_LAUNCHER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/placeholder.cpp")
endif()

add_executable(BroadcastAppliance ${BA_LAUNCHER_SOURCES})
include(${CMAKE_SOURCE_DIR}/cmake/BaMacInfoPlist.cmake)
ba_mac_embed_info_plist(BroadcastAppliance io.github.blairq.broadcastappliance "Broadcast Appliance")

# --- Ícono y datos del .exe (solo Windows) ----------------------------------
# El .rc referencia el .ico por nombre: se le agrega assets/icons al camino de
# inclusión del compilador de recursos en vez de meter una ruta absoluta en el
# archivo. Sin esto el programa sale con el ícono genérico.
if(WIN32)
    enable_language(RC)
    target_sources(BroadcastAppliance PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/BroadcastAppliance.rc")
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/BroadcastAppliance.rc"
        PROPERTIES COMPILE_OPTIONS "-I;${CMAKE_SOURCE_DIR}/assets/icons"
    )
endif()

if(MSVC)
    # CRT estática /MT solo en el launcher (spec §14: "/MT solo en el launcher").
    target_compile_options(BroadcastAppliance PRIVATE /MT)
endif()
# WIN32 => subsistema GUI (sin ventana de consola). Debe aplicarse a MSVC y
# MinGW por igual: sin esto, MinGW enlaza como consola y el CRT espera `main`
# en vez de `WinMain`/`wWinMain` (ver trampoline WinMain->wWinMain en main.cpp).
if(WIN32)
    set_target_properties(BroadcastAppliance PROPERTIES WIN32_EXECUTABLE ON)
endif()
target_link_libraries(BroadcastAppliance PRIVATE ba_core)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # El launcher arranca en CUALQUIER Linux (aunque su libstdc++ sea viejo):
    # lleva el runtime de C++ adentro y decide cual usan A y B (bin/64bit/cxx).
    target_link_options(BroadcastAppliance PRIVATE -static-libstdc++ -static-libgcc)
endif()

# --- Unit tests del launcher (CTest) ----------------------------------------
# La copia sombra (spec 3.1.5, decision D3) es portable (std::filesystem), por
# lo que su test compila y corre en Linux y Windows. Se enlaza contra ShadowCopy
# y Log (el logger estructurado) y contra ba_core por consistencia con el resto
# de tests del arbol.
add_executable(test_shadow_copy tests/test_shadow_copy.cpp ShadowCopy.cpp Log.cpp)
target_include_directories(test_shadow_copy PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_shadow_copy PRIVATE ba_core)
add_test(NAME test_shadow_copy COMMAND test_shadow_copy)
# 77 = saltado (ver src/core/CMakeLists.txt).
set_tests_properties(test_shadow_copy PROPERTIES SKIP_RETURN_CODE 77)

# El actualizador: reemplaza el programa sin tocar los datos.
add_executable(test_updater tests/test_updater.cpp Updater.cpp Politica.cpp Integrity.cpp Log.cpp)
target_include_directories(test_updater PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_updater PRIVATE ba_core)
add_test(NAME test_updater COMMAND test_updater)

# Lo que decide el supervisor sin procesos: backoff agendado que se olvida,
# readopción con versión y si hay aire (Politica.hpp).
add_executable(test_politica tests/test_politica.cpp Politica.cpp)
target_include_directories(test_politica PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_politica PRIVATE ba_core)
add_test(NAME test_politica COMMAND test_politica)

# La salida capturada de A y B (Logs/app.log): rotacion y encabezado por corrida.
# La carpeta de datos al lado de la .app (macOS) y la mudanza.
add_executable(test_datos tests/test_datos.cpp Datos.cpp)
target_include_directories(test_datos PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_datos PRIVATE ba_core)
add_test(NAME test_datos COMMAND test_datos)

add_executable(test_child_output tests/test_child_output.cpp ChildOutput.cpp)
target_include_directories(test_child_output PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_child_output PRIVATE ba_core)
add_test(NAME test_child_output COMMAND test_child_output)

# SHA-256 de la verificacion de integridad, contra los vectores de FIPS 180-4.
add_executable(test_sha256 tests/test_sha256.cpp Integrity.cpp Log.cpp)
target_include_directories(test_sha256 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME test_sha256 COMMAND test_sha256)

# --- Instalación ------------------------------------------------------------
# El launcher vive en la RAÍZ del disco (spec §5.1), no en bin/64bit/.
install(TARGETS BroadcastAppliance
    RUNTIME DESTINATION .
)
