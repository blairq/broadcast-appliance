# ============================================================================
# src/pinggy — `ba-pinggy`: el túnel de pinggy.io en un proceso propio
#
# Lo supervisa el gateway (como a cloudflared). Aparte del gateway a propósito:
# libpinggy trae su OpenSSL y el gateway tiene el BoringSSL de libwebrtc; en un
# mismo proceso chocarían los símbolos. Sin ba_core ni nada propio: un main y la
# biblioteca de pinggy (extern/pinggy/<plataforma>, make deps-pinggy).
#
# Bundle: ba-pinggy va en bin/64bit (al lado de ba-gateway) y sus bibliotecas en
# bin/64bit/pinggy/ (en macOS y Linux: libpinggy + su libssl/libcrypto, que no se
# pueden mezclar con las de GStreamer, del mismo nombre). En Windows la DLL va al
# lado del .exe: es una sola (OpenSSL y la CRT adentro) y no choca con nada.
# ============================================================================

if(WIN32)
    set(_pg_plat windows-x86_64)
elseif(APPLE)
    set(_pg_plat macos)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(_pg_plat linux-aarch64)
else()
    set(_pg_plat linux-x86_64)
endif()
set(BA_PINGGY_DIR "${CMAKE_SOURCE_DIR}/extern/pinggy/${_pg_plat}")
if(NOT EXISTS "${BA_PINGGY_DIR}/pinggy.h")
    message(STATUS "ba-pinggy: falta extern/pinggy/${_pg_plat} ('make deps-pinggy'): sin túnel de pinggy")
    return()
endif()

if(MSVC)
    # Sin consola (como el gateway), pero con main(argc, argv).
    add_executable(ba-pinggy WIN32 main.cpp)
    target_link_options(ba-pinggy PRIVATE /ENTRY:mainCRTStartup)
else()
    add_executable(ba-pinggy main.cpp)
endif()
target_include_directories(ba-pinggy PRIVATE "${BA_PINGGY_DIR}")
target_compile_features(ba-pinggy PRIVATE cxx_std_20)

if(WIN32)
    target_link_libraries(ba-pinggy PRIVATE "${BA_PINGGY_DIR}/pinggy.lib" ws2_32)
    # Carga diferida: la DLL trae su propia CRT estática, que copia el entorno al
    # cargarse. Así ve el OPENSSL_CONF que main pone antes de la primera llamada.
    # Si el CRT del toolchain no trae delayimp.lib, se enlaza normal: OpenSSL
    # buscaría su openssl.cnf en la carpeta de fábrica (C:\Program Files\Common
    # Files\SSL, que solo puede escribir un administrador).
    set(_pg_delay ON)
    if(BA_WINSYSROOT)
        # Cross desde Linux (clang-cl + xwin): la CRT está en extern/msvc.
        file(GLOB_RECURSE _pg_delayimp "${BA_WINSYSROOT}/*elay*mp.lib")
        if(NOT _pg_delayimp)
            set(_pg_delay OFF)
        endif()
    endif()
    if(_pg_delay)
        target_link_options(ba-pinggy PRIVATE /DELAYLOAD:pinggy.dll)
        target_link_libraries(ba-pinggy PRIVATE delayimp)
    else()
        message(WARNING "ba-pinggy: sin delayimp.lib, pinggy.dll se carga al arrancar")
    endif()
    set(_pg_libs "${BA_PINGGY_DIR}/pinggy.dll")
    set(_pg_dest bin/64bit)
    # Para correrlo desde el build (pruebas).
    add_custom_command(TARGET ba-pinggy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BA_PINGGY_DIR}/pinggy.dll" "$<TARGET_FILE_DIR:ba-pinggy>")
elseif(APPLE)
    target_link_libraries(ba-pinggy PRIVATE "${BA_PINGGY_DIR}/libpinggy.dylib")
    set_target_properties(ba-pinggy PROPERTIES
        BUILD_RPATH "${BA_PINGGY_DIR}"
        INSTALL_RPATH "@executable_path/pinggy")
    set(_pg_libs "${BA_PINGGY_DIR}/libpinggy.dylib" "${BA_PINGGY_DIR}/libssl.3.dylib" "${BA_PINGGY_DIR}/libcrypto.3.dylib")
    set(_pg_dest bin/64bit/pinggy)
else()
    target_link_libraries(ba-pinggy PRIVATE "${BA_PINGGY_DIR}/libpinggy.so")
    set_target_properties(ba-pinggy PROPERTIES
        BUILD_RPATH "${BA_PINGGY_DIR}"
        INSTALL_RPATH "$ORIGIN/pinggy")
    set(_pg_libs "${BA_PINGGY_DIR}/libpinggy.so" "${BA_PINGGY_DIR}/libssl.so.3" "${BA_PINGGY_DIR}/libcrypto.so.3")
    set(_pg_dest bin/64bit/pinggy)
endif()

install(TARGETS ba-pinggy RUNTIME DESTINATION bin/64bit COMPONENT bundle)
install(PROGRAMS ${_pg_libs} DESTINATION ${_pg_dest} COMPONENT bundle)

# Linux: el RUNPATH no se hereda; cada biblioteca busca las suyas con el propio.
# libpinggy trae $ORIGIN/openssl/lib y libssl ninguno: todas a $ORIGIN (están juntas).
if(NOT WIN32 AND NOT APPLE)
    find_program(BA_PATCHELF_PINGGY patchelf)
    if(BA_PATCHELF_PINGGY)
        install(CODE "
            foreach(_l libpinggy.so libssl.so.3 libcrypto.so.3)
                execute_process(COMMAND \"${BA_PATCHELF_PINGGY}\" --set-rpath \"\\\$ORIGIN\"
                                \"\${CMAKE_INSTALL_PREFIX}/bin/64bit/pinggy/\${_l}\")
            endforeach()
        " COMPONENT bundle)
    else()
        message(STATUS "ba-pinggy: sin patchelf, las bibliotecas de pinggy quedan con su RUNPATH de fábrica")
    endif()
endif()
message(STATUS "ba-pinggy: con libpinggy de ${BA_PINGGY_DIR}")
