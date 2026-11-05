# ============================================================================
# cmake/estaticas/huella.cmake — de qué salen las estáticas de terceros
#
# ba_estaticas_huella(<var> <plataforma> <raíz del repo>): 16 hex del sha256 de lo que
# decide cómo quedan compiladas: las fuentes fijadas (URL y sha256 de mk/versiones.mk),
# la configuración de mbedtls del proyecto, la receta (compilar.cmake y el CMakeLists de
# acá) y la plataforma (macos, linux, win, win-mt). Van a extern/estaticas/<plataforma>-<huella>:
# por CONTENIDO, así dos worktrees con distinta configuración de mbedtls no se pisan y
# volver a una configuración vieja no recompila nada.
#
# También como script, para tools/deps (imprime la carpeta, relativa a la raíz):
#   cmake -DPLATAFORMA=<p> -DRAIZ=<repo> -P cmake/estaticas/huella.cmake
# ============================================================================
function(ba_estaticas_huella var plataforma raiz)
    file(STRINGS "${raiz}/mk/versiones.mk" _pins
         REGEX "^(MBEDTLS|MBEDTLS_FW|SAMPLERATE|LIBDEFLATE|ZXING|ARGON2)_(URL|SHA) ")
    set(_todo "plataforma ${plataforma}\n")
    foreach(_l IN LISTS _pins)
        string(REGEX REPLACE "[ \t]+" " " _l "${_l}")
        string(APPEND _todo "${_l}\n")
    endforeach()
    foreach(_a cmake/mbedtls_user_config.h cmake/estaticas/compilar.cmake cmake/estaticas/ba-estaticas.cmake.in
               cmake/estaticas/CMakeLists.txt cmake/estaticas/huella.cmake)
        file(SHA256 "${raiz}/${_a}" _h)
        string(APPEND _todo "${_a} ${_h}\n")
    endforeach()
    string(SHA256 _h "${_todo}")
    string(SUBSTRING "${_h}" 0 16 _h)
    set(${var} "${_h}" PARENT_SCOPE)
endfunction()

# Las entradas de la huella (para CMAKE_CONFIGURE_DEPENDS: si cambian, se reconfigura).
set(BA_ESTATICAS_ENTRADAS mk/versiones.mk cmake/mbedtls_user_config.h cmake/estaticas/compilar.cmake cmake/estaticas/ba-estaticas.cmake.in
    cmake/estaticas/CMakeLists.txt cmake/estaticas/huella.cmake)

if(CMAKE_SCRIPT_MODE_FILE AND CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
    if(NOT PLATAFORMA OR NOT RAIZ)
        message(FATAL_ERROR "uso: cmake -DPLATAFORMA=<macos|linux|win|win-mt> -DRAIZ=<repo> -P huella.cmake")
    endif()
    ba_estaticas_huella(_h "${PLATAFORMA}" "${RAIZ}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E echo "extern/estaticas/${PLATAFORMA}-${_h}")
endif()
