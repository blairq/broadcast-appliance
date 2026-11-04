# ============================================================================
# cmake/BaNiveles.cmake — los tests del diseño físico, por componente
#
# tools/diag/niveles.txt reparte src/ en componentes con nivel (0 ba_core → 1
# ba_motor, ba_gateway_* → 2 áreas de la app → 3 ba_app_ui → 4 programas).
# Al configurar, tools/diag/niveles.py --cmake escribe <build>/niveles/niveles.cmake
# con los archivos de cada componente, sus headers y los headers del proyecto que
# estos alcanzan; con eso se registra un test por componente con sus entradas
# EXACTAS (cmake/BaTestsCache.cmake): tocar un área corre solo los de esa área.
#
#   test_niveles_<comp>        sus #include no suben ni cruzan entre iguales
#                              (tests/test_niveles.py; entradas: sus archivos)
#   test_niveles_enlaces       target_link_libraries / ba_biblioteca (los CMake)
#   test_niveles_mapa          el mapa cubre todos los archivos de src/
#   test_headers_solos_<comp>[_<k>]  cada header de core y de la app compila solo (por partes; solo
#                              macOS; entradas: sus headers, los que alcanzan y
#                              compile_commands.json)
#
# Lo que la huella no ve: un header NUEVO sin .cpp nuevo no reconfigura (las
# listas son de la última configuración); el test del área lo lee igual cuando
# corre, y la corrida sin caché (make publicar) lo ve siempre.
# ============================================================================

find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_Interpreter_FOUND)
    ba_falta("test_niveles / test_headers_solos" "Python 3 (tools/diag/niveles.py)")
    return()
endif()

set(_ba_niv_gen "${CMAKE_BINARY_DIR}/niveles/niveles.cmake")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/diag/niveles.py" --cmake "${_ba_niv_gen}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE _ba_niv_rc ERROR_VARIABLE _ba_niv_err)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/tools/diag/niveles.txt" "${CMAKE_SOURCE_DIR}/tools/diag/niveles.py")
if(NOT _ba_niv_rc EQUAL 0)
    message(FATAL_ERROR "tools/diag/niveles.py --cmake falló (¿tools/diag/niveles.txt mal escrito?):\n${_ba_niv_err}")
endif()
include("${_ba_niv_gen}")

set(_ba_niv_comunes "${CMAKE_SOURCE_DIR}/tools/diag/niveles.py" "${CMAKE_SOURCE_DIR}/tools/diag/niveles.txt")

foreach(_id IN LISTS BA_NIVELES_COMPONENTES)
    add_test(NAME test_niveles_${_id}
             COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/test_niveles.py"
                     --componente "${BA_NIVELES_${_id}_NOMBRE}")
    ba_test_entradas(test_niveles_${_id} ${_ba_niv_comunes} ${BA_NIVELES_${_id}_ARCHIVOS})
endforeach()

add_test(NAME test_niveles_enlaces
         COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/test_niveles.py" --enlaces)
ba_test_entradas(test_niveles_enlaces ${_ba_niv_comunes} ${BA_NIVELES_CMAKE})

# El generado cambia cuando aparece o se va un archivo de C/C++ (se reconfigura con
# los GLOB CONFIGURE_DEPENDS de core y app): ahí se vuelve a revisar el mapa.
add_test(NAME test_niveles_mapa
         COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/test_niveles.py" --mapa)
ba_test_entradas(test_niveles_mapa ${_ba_niv_comunes} "${_ba_niv_gen}")

# Los headers solos: con las flags de compile_commands.json de este build y el clang
# de la Mac (la deuda se midió ahí). Una compilación a la vez por test: ctest ya
# corre varios tests juntos (-j del turno).
if(APPLE AND CMAKE_EXPORT_COMPILE_COMMANDS)
    # Un test por PARTE de cada componente (unos 8 headers: tools/diag/niveles.py,
    # partes_headers_solos): en frío se reparten entre los núcleos y la caché de tests vuelve a
    # correr solo las partes cuyos headers (o lo que incluyen) cambiaron. De a una compilación
    # por test: el -j de ctest las reparte (antes, de a dos por test sin decírselo a ctest:
    # dieciséis clang en ocho núcleos). Con una sola parte, el nombre de siempre.
    foreach(_id IN LISTS BA_NIVELES_HEADERS_SOLOS)
        math(EXPR _ultima "${BA_NIVELES_${_id}_PARTES} - 1")
        foreach(_k RANGE ${_ultima})
            if(BA_NIVELES_${_id}_PARTES EQUAL 1)
                set(_t test_headers_solos_${_id})
            else()
                set(_t test_headers_solos_${_id}_${_k})
            endif()
            add_test(NAME ${_t}
                     COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/test_headers_solos.py"
                             --build "${CMAKE_BINARY_DIR}" --componente "${BA_NIVELES_${_id}_NOMBRE}"
                             --parte ${_k} --partes ${BA_NIVELES_${_id}_PARTES} --jobs 1)
            set_tests_properties(${_t} PROPERTIES SKIP_RETURN_CODE 77)
            ba_test_entradas(${_t} ${_ba_niv_comunes}
                             "${CMAKE_BINARY_DIR}/compile_commands.json"
                             ${BA_NIVELES_${_id}_P${_k}_HEADERS} ${BA_NIVELES_${_id}_P${_k}_CIERRE})
        endforeach()
    endforeach()
elseif(NOT APPLE)
    message(STATUS "test_headers_solos: solo en macOS (la deuda se midió con el clang y las flags de la Mac)")
else()
    message(STATUS "test_headers_solos: sin registrar (hace falta -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)")
endif()
