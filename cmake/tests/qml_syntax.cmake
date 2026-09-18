# ============================================================================
# cmake/tests/qml_syntax.cmake — la interfaz QML parsea (CTest)
#
# Un error de sintaxis en un .qml no rompe el build (los QML viajan como
# recursos) sino el ARRANQUE de la interfaz. Este test pasa qmllint (del Qt
# vendoreado) por todos los QML y falla ante errores de SINTAXIS; las
# advertencias de estilo de qmllint no cuentan.
#   -DQMLLINT=<ruta a qmllint> -DQML_DIR=<src/app/qml>
# ============================================================================
file(GLOB_RECURSE files "${QML_DIR}/*.qml")
set(bad 0)
foreach(f ${files})
    execute_process(COMMAND "${QMLLINT}" "${f}" OUTPUT_VARIABLE out ERROR_VARIABLE err)
    string(REGEX MATCHALL "[^\n]*(Unexpected token|Expected token|Syntax error|Expected a qualified name)[^\n]*" hits "${out}${err}")
    if(hits)
        message("${f}:")
        foreach(h ${hits})
            message("  ${h}")
        endforeach()
        math(EXPR bad "${bad} + 1")
    endif()
endforeach()
list(LENGTH files n)
if(bad GREATER 0)
    message(FATAL_ERROR "${bad} de ${n} archivos QML con errores de sintaxis")
endif()
message("qml_syntax: ${n} archivos QML sin errores de sintaxis")
