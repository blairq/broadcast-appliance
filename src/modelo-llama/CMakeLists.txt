# ============================================================================
# src/modelo-llama — ba-modelo con llama.cpp: el modelo local en Windows y Linux
#
# Habla el mismo protocolo que el de la Mac (src/modelo-mac, MLX). Se enlaza con
# la biblioteca de llama.cpp que deja tools/deps/llamacpp-lib.sh en
# extern/llamacpp-<plataforma> (los binarios oficiales, Vulkan); si no está, no
# se arma y ModelosLocales dice que no hay motor.
#
# En la Mac se arma como «ba-modelo-llama» solo para desarrollar y probar (la
# app usa el de MLX): no se instala.
# ============================================================================
if(WIN32)
    set(_ba_llama "${CMAKE_SOURCE_DIR}/extern/llamacpp-win")
elseif(APPLE)
    set(_ba_llama "${CMAKE_SOURCE_DIR}/extern/llamacpp-mac")
else()
    set(_ba_llama "${CMAKE_SOURCE_DIR}/extern/llamacpp-linux")
endif()
if(NOT EXISTS "${_ba_llama}/include/llama.h")
    ba_falta("Modelo local (llama.cpp)" "falta ${_ba_llama} (tools/deps/llamacpp-lib.sh)")
    return()
endif()

set(_ba_nombre ba-modelo)
if(APPLE)
    set(_ba_nombre ba-modelo-llama)
endif()
add_executable(${_ba_nombre} main.cpp)
target_include_directories(${_ba_nombre} PRIVATE "${_ba_llama}/include")
target_link_libraries(${_ba_nombre} PRIVATE nlohmann_json::nlohmann_json)
if(WIN32)
    target_link_libraries(${_ba_nombre} PRIVATE "${_ba_llama}/lib/llama.lib" "${_ba_llama}/lib/ggml.lib"
                                                "${_ba_llama}/lib/ggml-base.lib" "${_ba_llama}/lib/mtmd.lib")
    # El ejecutable y TODAS las DLL de la release juntas en bin/64bit: los backends
    # (ggml-cpu-*, ggml-vulkan) se cargan al arrancar desde la carpeta del ejecutable.
    install(TARGETS ${_ba_nombre} RUNTIME DESTINATION bin/64bit COMPONENT bundle)
    # Solo lo que carga el modelo: las de las herramientas de línea de comandos de la
    # release (llama-*-impl, llama-common, ggml-rpc) no viajan.
    file(GLOB _ba_dlls "${_ba_llama}/bin/llama.dll" "${_ba_llama}/bin/mtmd.dll" "${_ba_llama}/bin/ggml.dll"
         "${_ba_llama}/bin/ggml-base.dll" "${_ba_llama}/bin/ggml-vulkan.dll" "${_ba_llama}/bin/ggml-cpu-*.dll"
         "${_ba_llama}/bin/libomp.dll")
    install(FILES ${_ba_dlls} DESTINATION bin/64bit COMPONENT bundle)
else()
    find_library(_ba_libllama llama PATHS "${_ba_llama}/lib" NO_DEFAULT_PATH REQUIRED)
    find_library(_ba_libggml ggml PATHS "${_ba_llama}/lib" NO_DEFAULT_PATH REQUIRED)
    find_library(_ba_libggmlbase ggml-base PATHS "${_ba_llama}/lib" NO_DEFAULT_PATH REQUIRED)
    find_library(_ba_libmtmd mtmd PATHS "${_ba_llama}/lib" NO_DEFAULT_PATH REQUIRED)
    target_link_libraries(${_ba_nombre} PRIVATE ${_ba_libllama} ${_ba_libggml} ${_ba_libggmlbase} ${_ba_libmtmd})
    set_target_properties(${_ba_nombre} PROPERTIES BUILD_RPATH "${_ba_llama}/lib")
    if(NOT APPLE)
        # Linux: en bin/64bit/llama, con su rpath (portable: nada del sistema).
        set_target_properties(${_ba_nombre} PROPERTIES INSTALL_RPATH "$ORIGIN/llama")
        install(TARGETS ${_ba_nombre} RUNTIME DESTINATION bin/64bit COMPONENT bundle)
        # Solo lo que carga el modelo, como en Windows: las de las herramientas de línea
        # de comandos de la release (llama-common, llama-server-impl, ggml-rpc) piden
        # libssl/libcrypto del sistema y no viajan.
        file(GLOB _ba_sos "${_ba_llama}/lib/libllama.so*" "${_ba_llama}/lib/libmtmd.so*" "${_ba_llama}/lib/libggml.so*"
             "${_ba_llama}/lib/libggml-base.so*" "${_ba_llama}/lib/libggml-vulkan.so*" "${_ba_llama}/lib/libggml-cpu-*.so*")
        install(FILES ${_ba_sos} DESTINATION bin/64bit/llama COMPONENT bundle)
        # Los backends de la CPU usan OpenMP: libgomp del compilador, al lado (portable).
        execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libgomp.so.1 OUTPUT_VARIABLE _ba_gomp
                        OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(NOT IS_ABSOLUTE "${_ba_gomp}" OR NOT EXISTS "${_ba_gomp}")
            find_file(_ba_gomp libgomp.so.1 PATHS /usr/lib/x86_64-linux-gnu /usr/lib64 /usr/lib NO_DEFAULT_PATH)
        endif()
        if(_ba_gomp AND EXISTS "${_ba_gomp}")
            get_filename_component(_ba_gomp_real "${_ba_gomp}" REALPATH)
            install(FILES "${_ba_gomp_real}" DESTINATION bin/64bit/llama RENAME libgomp.so.1 COMPONENT bundle)
        else()
            message(WARNING "ba-modelo: no encontré libgomp.so.1; los backends de la CPU de llama.cpp no van a cargar")
        endif()
    endif()
endif()
# llama-server: el chat del modelo local (pi, la pestaña Asistente) le habla por
# HTTP con el protocolo de OpenAI (agent/ServidorLocal). Va con TODAS las
# bibliotecas de la release (además de las de ba-modelo usa llama-server-impl y
# llama-common), en Windows al lado de las otras DLL y en Linux/macOS en
# bin/64bit/llama (su rpath es la carpeta donde está).
if(WIN32 AND EXISTS "${_ba_llama}/bin/llama-server.exe")
    file(GLOB _ba_dlls_todas "${_ba_llama}/bin/*.dll")
    install(FILES "${_ba_llama}/bin/llama-server.exe" ${_ba_dlls_todas} DESTINATION bin/64bit COMPONENT bundle)
elseif(NOT WIN32 AND EXISTS "${_ba_llama}/bin/llama-server")
    install(PROGRAMS "${_ba_llama}/bin/llama-server" DESTINATION bin/64bit/llama COMPONENT bundle)
    if(APPLE)
        file(GLOB _ba_libs_todas "${_ba_llama}/lib/*.dylib")
    else()
        file(GLOB _ba_libs_todas "${_ba_llama}/lib/*.so*")
    endif()
    install(FILES ${_ba_libs_todas} DESTINATION bin/64bit/llama COMPONENT bundle)
    # Linux: llama-common y llama-server-impl piden libssl/libcrypto 3 (la release
    # de Ubuntu las toma del sistema). Van al lado (su RUNPATH es $ORIGIN): las de
    # extern/pinggy, ya vendoreadas y con versión fija (OpenSSL 3.x trae OPENSSL_3.0.0).
    if(NOT APPLE)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
            set(_ba_ossl "${CMAKE_SOURCE_DIR}/extern/pinggy/linux-aarch64")
        else()
            set(_ba_ossl "${CMAKE_SOURCE_DIR}/extern/pinggy/linux-x86_64")
        endif()
        if(EXISTS "${_ba_ossl}/libssl.so.3" AND EXISTS "${_ba_ossl}/libcrypto.so.3")
            install(PROGRAMS "${_ba_ossl}/libssl.so.3" "${_ba_ossl}/libcrypto.so.3" DESTINATION bin/64bit/llama
                    COMPONENT bundle)
            find_program(BA_PATCHELF_LLAMA patchelf)
            if(BA_PATCHELF_LLAMA)
                install(CODE "
                    foreach(_l libssl.so.3 libcrypto.so.3)
                        execute_process(COMMAND \"${BA_PATCHELF_LLAMA}\" --set-rpath \"\\\$ORIGIN\"
                                        \"\${CMAKE_INSTALL_PREFIX}/bin/64bit/llama/\${_l}\")
                    endforeach()
                " COMPONENT bundle)
            endif()
        else()
            message(WARNING "llama-server: falta libssl/libcrypto en ${_ba_ossl} ('make deps-pinggy'): no va a arrancar")
        endif()
    endif()
endif()
message(STATUS "ba-modelo (llama.cpp): ${_ba_llama}")
