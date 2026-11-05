# ============================================================================
# cmake/BaTestsPy.cmake — los chequeos de Python de tests/, cada uno con lo que lee
#
# Antes eran un solo test_codigo_py que leía el árbol entero (y, registrado después de
# ba_tests_registrar(), sin entradas: la caché no lo volvía a correr). Ahora, con el
# pytest del venv de desarrollo (sin venv no se registran):
#
#   test_py_area_<área>     las pruebas marcadas `area` (tests/areas.py) con BA_AREA=<área>:
#                           escritor único, temporales, json de literales, ramas por idioma,
#                           llamadas que lupdate no lee, el C++ de la interfaz y los ganchos
#                           del detector de acoples. Un área es un componente de
#                           tools/diag/niveles.txt o «resto». Entradas: los archivos del área.
#   test_py_i18n            el resto de tests/test_i18n.py (catálogos, perfiles, ayuda)
#   test_py_acoples_qml     los bindings de la QML a Q_INVOKABLE (tests/test_acoples.py)
#   test_py_<archivo>       los demás, uno por archivo, con sus entradas
#
# La unión es exactamente lo de antes: los mismos archivos de prueba, las mismas tablas de
# deuda (cada área mira sus filas). Las listas de archivos salen de tools/ci/entradas_py.py al
# configurar; si aparece o se va un archivo de src/ se reconfigura (CONFIGURE_DEPENDS).
# ============================================================================

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/.venv/bin/python")
    message(STATUS "Chequeos de Python (test_py_*): sin .venv (make docs / make pytest lo crean), no se registran")
    return()
endif()
find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_Interpreter_FOUND)
    ba_falta("test_py_*" "Python 3 (tools/ci/entradas_py.py)")
    return()
endif()

set(_ba_py_gen "${CMAKE_BINARY_DIR}/tests-py/tests-py.cmake")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/ci/entradas_py.py" --cmake "${_ba_py_gen}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE _ba_py_rc ERROR_VARIABLE _ba_py_err)
if(NOT _ba_py_rc EQUAL 0)
    message(FATAL_ERROR "tools/ci/entradas_py.py --cmake falló:\n${_ba_py_err}")
endif()
include("${_ba_py_gen}")
# Un archivo nuevo (o borrado) en src/ o assets/web/ cambia las listas: se vuelve a configurar.
file(GLOB_RECURSE _ba_py_vigilar CONFIGURE_DEPENDS ${BA_PY_FUENTES_GLOB})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/tools/ci/entradas_py.py" "${CMAKE_SOURCE_DIR}/tools/diag/niveles.txt"
    "${CMAKE_SOURCE_DIR}/tests/areas.py" "${CMAKE_SOURCE_DIR}/tests/test_republicadores.py")

set(_ba_py "${CMAKE_SOURCE_DIR}/.venv/bin/python")
set(_ba_t "${CMAKE_SOURCE_DIR}/tests")
# Lo de pytest que lee toda prueba: la configuración y el conftest.
set(_ba_py_comunes "${CMAKE_SOURCE_DIR}/pyproject.toml" "${_ba_t}/conftest.py")

# ba_test_py(<nombre> ARCHIVOS <tests/x.py>... [MARCA <expr>] [AREA <área>] ENTRADAS <ruta>...)
# Las rutas de los .py van absolutas en el comando: entran solas en la clave de la caché.
function(ba_test_py nombre)
    cmake_parse_arguments(A "" "MARCA;AREA" "ARCHIVOS;ENTRADAS" ${ARGN})
    set(_args "")
    foreach(_a IN LISTS A_ARCHIVOS)
        list(APPEND _args "${_ba_t}/${_a}")
    endforeach()
    if(A_MARCA)
        list(APPEND _args -m "${A_MARCA}")
    endif()
    add_test(NAME ${nombre}
             COMMAND "${_ba_py}" -B -m pytest -q -p no:cacheprovider ${_args}
             WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    if(A_AREA)
        set_tests_properties(${nombre} PROPERTIES ENVIRONMENT "BA_AREA=${A_AREA}")
    endif()
    ba_test_entradas(${nombre} ${_ba_py_comunes} ${A_ENTRADAS})
endfunction()

# --- por área --------------------------------------------------------------------------
set(_ba_py_area_archivos test_escritor_unico.py test_temporales.py test_json_literales.py
                         test_i18n.py test_acoples.py)
set(_ba_py_area_comunes "${_ba_t}/areas.py" "${CMAKE_SOURCE_DIR}/tools/diag/niveles.py"
    "${CMAKE_SOURCE_DIR}/tools/diag/niveles.txt" "${CMAKE_SOURCE_DIR}/tools/diag/acoples.py"
    "${CMAKE_SOURCE_DIR}/tools/i18n/llamadas.py")
foreach(_id IN LISTS BA_PY_AREAS)
    ba_test_py(test_py_area_${_id} ARCHIVOS ${_ba_py_area_archivos} MARCA area
               AREA "${BA_PY_AREA_${_id}_NOMBRE}"
               ENTRADAS ${_ba_py_area_comunes} ${BA_PY_AREA_${_id}_ARCHIVOS})
endforeach()

# --- lo que no es por área -------------------------------------------------------------
# Los catálogos, los perfiles, los nombres de las herramientas, los temas de ayuda, los
# convertidores de tools/i18n (y el extractor de la web, que lee assets/web).
ba_test_py(test_py_i18n ARCHIVOS test_i18n.py MARCA "not area"
           ENTRADAS "${_ba_t}/areas.py" "${CMAKE_SOURCE_DIR}/tools/diag/niveles.py"
                    "${CMAKE_SOURCE_DIR}/i18n" "${CMAKE_SOURCE_DIR}/assets/idiomas"
                    "${CMAKE_SOURCE_DIR}/docs/mcp.json" "${CMAKE_SOURCE_DIR}/tools/i18n"
                    "${CMAKE_SOURCE_DIR}/src/app/mcp/McpHelp.cpp" "${CMAKE_SOURCE_DIR}/assets/web")
ba_test_py(test_py_acoples_qml ARCHIVOS test_acoples.py MARCA "not area"
           ENTRADAS "${_ba_t}/areas.py" "${CMAKE_SOURCE_DIR}/tools/diag/niveles.py"
                    "${CMAKE_SOURCE_DIR}/tools/diag/niveles.txt" "${CMAKE_SOURCE_DIR}/tools/diag/acoples.py"
                    ${BA_PY_ACOPLES_QML})
# Lee todos los .cpp de la app buscando quién registra un republicador; entran los que
# registran (y las rutas que clasifica): para que pase a fallar hay que tocar uno de esos.
ba_test_py(test_py_republicadores ARCHIVOS test_republicadores.py ENTRADAS ${BA_PY_REPUBLICADORES})
ba_test_py(test_py_replica_permitidas ARCHIVOS test_replica_permitidas.py
           ENTRADAS "${CMAKE_SOURCE_DIR}/tools/generar/replica-permitidas.py"
                    "${CMAKE_SOURCE_DIR}/src/app/qml/pages/DashboardPage.qml"
                    "${CMAKE_SOURCE_DIR}/src/app/qml/cards" "${CMAKE_SOURCE_DIR}/src/app/qml/controls"
                    "${CMAKE_SOURCE_DIR}/src/tablero-web/qml" "${CMAKE_SOURCE_DIR}/docs/mcp.json"
                    "${CMAKE_SOURCE_DIR}/src/app/control/ReplicaPermitidas.inc"
                    # La QML del tablero no lee por funciones: las Q_INVOKABLE de la app (sus .hpp,
                    # con el lector de tools/diag) y qué es acción (ReplicaPublicador::esAccion).
                    "${CMAKE_SOURCE_DIR}/src/app/control/ReplicaPublicador.cpp"
                    "${CMAKE_SOURCE_DIR}/tools/diag/acoples.py" ${BA_PY_ACOPLES_QML})
ba_test_py(test_py_caos_comparar ARCHIVOS test_caos_comparar.py
           ENTRADAS "${CMAKE_SOURCE_DIR}/tools/caos" "${CMAKE_SOURCE_DIR}/tools/test/asamblea-mcp.py")
ba_test_py(test_py_enlace_eco ARCHIVOS test_enlace_eco.py
           ENTRADAS "${CMAKE_SOURCE_DIR}/tools/vps/ba-enlace-eco.py")
ba_test_py(test_py_lotes_formato ARCHIVOS test_lotes_formato.py
           ENTRADAS "${CMAKE_SOURCE_DIR}/tools/i18n/lotes.py" "${CMAKE_SOURCE_DIR}/i18n/ba_fr.ts"
                    "${CMAKE_SOURCE_DIR}/assets/idiomas/fr.json")
