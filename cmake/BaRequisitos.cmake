# ============================================================================
# cmake/BaRequisitos.cmake — lo que el programa TIENE que traer
#
# Cada parte del programa que depende de algo de afuera (una biblioteca vendoreada,
# un binario bajado) se declara acá cuando falta, con ba_falta(parte motivo). Al
# final de la configuración, si falta algo, CMake FRENA con la lista entera: un
# paquete al que le falta una función (la búsqueda de televisores, el túnel, el
# texto de los rostros…) no se arma callado. Pasó: el de Windows salió meses sin
# mDNS y solo lo dijo una línea de STATUS que nadie lee.
#
# Para un build de desarrollo a medias, a propósito: -DBA_PERMITIR_INCOMPLETO=ON
# (lo que falta queda como WARNING). Los paquetes (make dist-*) no lo usan.
# ============================================================================

option(BA_PERMITIR_INCOMPLETO "Configurar aunque falte una parte del programa (solo desarrollo)" OFF)

function(ba_falta parte motivo)
    set_property(GLOBAL APPEND PROPERTY BA_FALTAN "${parte}: ${motivo}")
    message(STATUS "FALTA ${parte}: ${motivo}")
endfunction()

# Al final del CMakeLists principal.
function(ba_revisar_requisitos)
    get_property(_faltan GLOBAL PROPERTY BA_FALTAN)
    if(NOT _faltan)
        message(STATUS "Requisitos: el programa va completo")
        return()
    endif()
    list(JOIN _faltan "\n  - " _lista)
    if(BA_PERMITIR_INCOMPLETO)
        message(WARNING "Al programa le faltan partes (BA_PERMITIR_INCOMPLETO=ON):\n  - ${_lista}")
    else()
        message(FATAL_ERROR "Al programa le faltan partes; no se arma así:\n  - ${_lista}\n"
                            "Bajá lo que falta (make deps-…) o, solo para desarrollo, -DBA_PERMITIR_INCOMPLETO=ON.")
    endif()
endfunction()
