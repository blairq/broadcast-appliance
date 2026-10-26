# ============================================================================
# cmake/BaTestsCache.cmake — lo que la caché de tests necesita saber de cada test
#
# tools/ci/tests-con-cache.py no vuelve a correr un test que ya pasó con
# exactamente las mismas entradas (ver su cabecera). El ejecutable y las
# bibliotecas que carga los encuentra solo; lo que el test LEE por su cuenta
# (datos, otros programas, carpetas de plugins) sale de acá:
#
#   - las definiciones de compilación del test que son rutas
#     (BA_TEST_DATOS="${CMAKE_SOURCE_DIR}/datos", "$<TARGET_FILE:ba-web>"...):
#     la manera de siempre de pasarle una ruta a un test ya alcanza;
#   - las entradas declaradas a mano con ba_test_entradas(), para lo que el
#     test encuentra por otro camino (al lado de su ejecutable, importando
#     módulos de Python, una variable de entorno).
#
# Y los tests lentos (más de ~10 s en la Mac) llevan la etiqueta `lento`:
# `make test-rapido` (o TESTS_SIN_LENTOS=1) los deja afuera; `make test` los
# corre solo si cambió lo que prueban; `make publicar` siempre.
#
# Todo queda en <build>/tests-entradas.json (tools/ci/tests-con-cache.py).
# ============================================================================

# Una sola lista: la usan las etiquetas de ctest y el corredor de Wine (que lee
# el json). Medido en la Mac (ctest -j1, Release) el 2026-10-26.
set(BA_TESTS_LENTOS
    test_retraso_propio test_composicion test_motor_app test_motor test_salidas
    test_grafico_proceso test_motor_retornos test_qml_syntax test_cloudflared
    test_superres_vivo test_sondas test_baanillo)

# ba_test_entradas(<test> <ruta>...): archivos o carpetas que el test lee y que
# la caché no puede adivinar. Rutas relativas: a la carpeta de fuentes actual.
# Se aceptan expresiones de generador ($<TARGET_FILE:ba-grafico>).
function(ba_test_entradas test)
    set(_rutas "")
    foreach(_r IN LISTS ARGN)
        if(NOT IS_ABSOLUTE "${_r}" AND NOT _r MATCHES "^\\$<")
            set(_r "${CMAKE_CURRENT_SOURCE_DIR}/${_r}")
        endif()
        list(APPEND _rutas "${_r}")
    endforeach()
    set_property(GLOBAL APPEND PROPERTY BA_TEST_ENTRADAS_${test} ${_rutas})
endfunction()

# Lo que estos tests leen sin que se lo pase una definición (rutas: a la raíz).
# test_grafico_proceso arranca ../grafico/ba-grafico, al lado de su ejecutable.
ba_test_entradas(test_grafico_proceso "$<TARGET_FILE:ba-grafico>")
# El modelo ONNX lo busca en los datos del desarrollo (si no está, se saltea).
ba_test_entradas(test_superres_vivo "dist/BroadcastAppliance Datos/Modelos/superres/span-x2.onnx"
                                    "Modelos/superres/span-x2.onnx")
# Los de Python importan el SDK entero y prueban los moldes de assets/plugins.
ba_test_entradas(test_plugins_ejemplos src/plugins/sdk assets/plugins)
ba_test_entradas(test_plugin_grafico src/plugins/sdk)

function(_ba_tests_carpetas dir salida)
    set(_todas "${dir}")
    get_property(_subs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_s IN LISTS _subs)
        _ba_tests_carpetas("${_s}" _mas)
        list(APPEND _todas ${_mas})
    endforeach()
    set(${salida} "${_todas}" PARENT_SCOPE)
endfunction()

# Al final del CMakeLists raíz, cuando ya están todos los tests.
function(ba_tests_registrar)
    if(CMAKE_VERSION VERSION_LESS 3.28)
        # set_property(TEST ... DIRECTORY) es de 3.28: sin eso no hay etiquetas
        # (el json igual se escribe y el corredor de Wine las lee de ahí).
        message(WARNING "CMake ${CMAKE_VERSION} < 3.28: los tests lentos quedan sin la etiqueta «lento» de ctest")
    endif()
    _ba_tests_carpetas("${CMAKE_SOURCE_DIR}" _dirs)
    set(_json "{}")
    foreach(_d IN LISTS _dirs)
        get_property(_tests DIRECTORY "${_d}" PROPERTY TESTS)
        foreach(_t IN LISTS _tests)
            set(_e "{}")
            if(_t IN_LIST BA_TESTS_LENTOS)
                string(JSON _e SET "${_e}" lento true)
                if(NOT CMAKE_VERSION VERSION_LESS 3.28)
                    set_property(TEST "${_t}" DIRECTORY "${_d}" APPEND PROPERTY LABELS lento)
                endif()
            endif()
            # Rutas: de las definiciones del ejecutable del test (las propias, no las
            # heredadas: las rutas de un test se las da su CMakeLists) y las declaradas.
            set(_rutas "")
            if(TARGET "${_t}")
                get_target_property(_defs "${_t}" COMPILE_DEFINITIONS)
                foreach(_def IN LISTS _defs)
                    if(NOT _def MATCHES "^[A-Za-z_][A-Za-z0-9_]*=(.*)$")
                        continue()
                    endif()
                    set(_v "${CMAKE_MATCH_1}")
                    # BA_TEST_TMP y compañía: donde el test ESCRIBE, no lo que lee.
                    if(_def MATCHES "^[A-Za-z0-9_]*_(TMP|SALIDA)=")
                        continue()
                    endif()
                    string(REGEX REPLACE "^\"(.*)\"$" "\\1" _v "${_v}")
                    # Solo lo que parece una ruta. Otras expresiones de generador
                    # ($<COMPILE_LANGUAGE:...>) no se pueden evaluar fuera de una compilación.
                    if(_v MATCHES "\\$<" AND NOT _v MATCHES "^\\$<TARGET_FILE(_DIR)?:[^>]+>[^$]*$")
                        continue()
                    endif()
                    if(_v MATCHES "^/" OR _v MATCHES "^[A-Za-z]:[/\\\\]" OR _v MATCHES "^\\$<TARGET_FILE")
                        list(APPEND _rutas "${_v}")
                    endif()
                endforeach()
            endif()
            get_property(_decl GLOBAL PROPERTY BA_TEST_ENTRADAS_${_t})
            foreach(_r IN LISTS _decl)
                # Un $<TARGET_FILE:x> de un target que este árbol no tiene (el
                # sub-build /MT de Windows) no se puede evaluar: no va.
                # (Dos if: ${CMAKE_MATCH_2} se expande antes de evaluar el MATCHES.)
                if(_r MATCHES "^\\$<TARGET_FILE(_DIR)?:([^>]+)>")
                    if(NOT TARGET "${CMAKE_MATCH_2}")
                        continue()
                    endif()
                endif()
                list(APPEND _rutas "${_r}")
            endforeach()
            if(_rutas)
                set(_arr "[]")
                set(_i 0)
                foreach(_r IN LISTS _rutas)
                    string(REPLACE "\\" "\\\\" _r "${_r}")
                    string(REPLACE "\"" "\\\"" _r "${_r}")
                    string(JSON _arr SET "${_arr}" ${_i} "\"${_r}\"")
                    math(EXPR _i "${_i} + 1")
                endforeach()
                string(JSON _e SET "${_e}" entradas "${_arr}")
            endif()
            string(JSON _json SET "${_json}" "${_t}" "${_e}")
        endforeach()
    endforeach()
    # file(GENERATE): evalúa los $<TARGET_FILE:...> y solo reescribe si cambió.
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/tests-entradas.json" CONTENT "${_json}\n")
endfunction()
