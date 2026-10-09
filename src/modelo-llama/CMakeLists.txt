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
    message(STATUS "ba-modelo (llama.cpp): sin ${_ba_llama} (tools/deps/llamacpp-lib.sh): sin modelo local en esta plataforma")
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
        install(DIRECTORY "${_ba_llama}/lib/" DESTINATION bin/64bit/llama COMPONENT bundle USE_SOURCE_PERMISSIONS)
    endif()
endif()
message(STATUS "ba-modelo (llama.cpp): ${_ba_llama}")
