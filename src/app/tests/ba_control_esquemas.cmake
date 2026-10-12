# src/app/tests/ba_control_esquemas.cmake — los esquemas de ba-control son válidos
#
# Cada herramienta de tools/list tiene que tener inputSchema con type "object" y
# `properties` OBJETO (aunque esté vacío): con `properties: null` Claude no
# cargaba el servidor entero (pasó con windows_monitores y windows_ventanas).
# Uso: cmake -DBIN=<ba-control> -DPEDIDO=<archivo con el tools/list> -P este.cmake
execute_process(COMMAND "${BIN}" INPUT_FILE "${PEDIDO}" OUTPUT_VARIABLE salida RESULT_VARIABLE rc)
string(JSON n LENGTH "${salida}" result tools)
if(n LESS 1)
    message(FATAL_ERROR "ba-control no listó herramientas: ${salida}")
endif()
math(EXPR ultimo "${n} - 1")
foreach(i RANGE ${ultimo})
    string(JSON nombre GET "${salida}" result tools ${i} name)
    string(JSON tipo GET "${salida}" result tools ${i} inputSchema type)
    string(JSON tprops TYPE "${salida}" result tools ${i} inputSchema properties)
    if(NOT tipo STREQUAL "object" OR NOT tprops STREQUAL "OBJECT")
        message(FATAL_ERROR "${nombre}: inputSchema.type=${tipo}, properties es ${tprops} (tiene que ser OBJECT)")
    endif()
endforeach()
message(STATUS "ba-control: ${n} herramientas, todos los esquemas bien")
