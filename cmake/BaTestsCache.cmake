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
    test_superres_vivo test_sondas test_baanillo test_motor_salidas)

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

# ba_biblioteca_lee(<biblioteca> <ruta>...): lo que una BIBLIOTECA lee del árbol al
# correr (no de una definición del test: de las suyas). Entra en la clave de todo test
# que la enlace, directa o indirectamente (se sigue LINK_LIBRARIES). Rutas como en
# ba_test_entradas().
function(ba_biblioteca_lee lib)
    set(_rutas "")
    foreach(_r IN LISTS ARGN)
        if(NOT IS_ABSOLUTE "${_r}" AND NOT _r MATCHES "^\\$<")
            set(_r "${CMAKE_CURRENT_SOURCE_DIR}/${_r}")
        endif()
        list(APPEND _rutas "${_r}")
    endforeach()
    set_property(GLOBAL APPEND PROPERTY BA_BIBLIOTECA_LEE_${lib} ${_rutas})
endfunction()

# ba_test_corre(<test> <target>...): el test corre esos programas (un script de CMake o de
# Python que los lanza): entra lo que leen sus bibliotecas (ba_biblioteca_lee), como si el
# test los enlazara. Sus ejecutables ya entran por el comando ($<TARGET_FILE:x>).
function(ba_test_corre test)
    set_property(GLOBAL APPEND PROPERTY BA_TEST_CORRE_${test} ${ARGN})
endfunction()

# Lo que estos tests leen sin que se lo pase una definición (rutas: a la raíz).
# test_grafico_proceso arranca ../grafico/ba-grafico, al lado de su ejecutable.
ba_test_entradas(test_grafico_proceso "$<TARGET_FILE:ba-grafico>")
# El modelo ONNX lo busca en los datos del desarrollo (si no está, se saltea).
ba_test_entradas(test_superres_vivo "dist/BroadcastAppliance Datos/Modelos/superres/span-x2.onnx"
                                    "Modelos/superres/span-x2.onnx")
# Los de Python importan el SDK entero y prueban los moldes de assets/plugins (y el de
# los ejemplos revisa que la ayuda de los plugins los nombre).
ba_test_entradas(test_plugins_ejemplos src/plugins/sdk assets/plugins assets/idiomas/es/ayuda/plugins.md)
ba_test_entradas(test_plugin_grafico src/plugins/sdk assets/plugins/ejemplo-grafico)
# Busca los modelos de rostros subiendo desde su carpeta (extern/rostros/modelos).
ba_test_entradas(test_rostros extern/rostros/modelos)

# --- Lo que lee ba_core al correr: los idiomas (ba::i18n) ---------------------------
# Cualquier test que enlace ba_core lee al arrancar los perfiles (assets/idiomas/*.json) y
# el catálogo del idioma con que corre (BA_IDIOMA=es, abajo: build/i18n/es.json). Los
# archivos de la carpeta de cada idioma (prompts, plantillas, ayuda: ba::i18n::archivo) los
# declara el test que los lee (medido con la traza de lecturas, Ola 5): así tocar una ayuda
# no vuelve a correr todo.
file(GLOB _ba_perfiles CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/idiomas/*.json")
ba_biblioteca_lee(ba_core ${_ba_perfiles} "${CMAKE_BINARY_DIR}/i18n/es.json")
ba_test_entradas(test_despachador assets/idiomas/es/prompts/roles)
ba_test_entradas(test_mrz assets/idiomas/es/prompts/vision-mrz.md)
ba_test_entradas(test_documento_dorso assets/idiomas/es/prompts/vision-mrz.md)
ba_test_entradas(test_informes_resultado assets/idiomas/es/plantillas/informe-resultado.md)
foreach(_t test_registro_agente test_registro_agente_totem)
    ba_test_entradas(${_t} assets/idiomas/es/prompts/totem assets/idiomas/es/totem.json)
endforeach()
foreach(_t test_registro_atencion test_registro_conexiones)
    ba_test_entradas(${_t} assets/idiomas/es/totem.json)
endforeach()
# Cambia de idioma: el tótem en inglés y en francés, con sus catálogos.
ba_test_entradas(test_registro_conversacion assets/idiomas/es/totem.json assets/idiomas/en/totem.json
                 assets/idiomas/fr/totem.json "${CMAKE_BINARY_DIR}/i18n/en.json" "${CMAKE_BINARY_DIR}/i18n/fr.json")
ba_test_entradas(test_manuales "${CMAKE_BINARY_DIR}/i18n/en.json")
ba_test_corre(test_ba_control_esquemas ba-control)

# Las bibliotecas que enlaza un target, todas (LINK_LIBRARIES de cada una, y las de
# interfaz). Sin repetir; lo que no es un target (Qt::, -l, rutas) se sigue si es target.
function(_ba_cierre_enlace target salida)
    set(_vistos "")
    set(_pila "${target}")
    while(_pila)
        list(POP_BACK _pila _x)
        if(_x IN_LIST _vistos OR NOT TARGET "${_x}")
            continue()
        endif()
        list(APPEND _vistos "${_x}")
        get_target_property(_tipo "${_x}" TYPE)
        set(_deps "")
        if(NOT _tipo STREQUAL "INTERFACE_LIBRARY")
            get_target_property(_l "${_x}" LINK_LIBRARIES)
            if(_l)
                list(APPEND _deps ${_l})
            endif()
        endif()
        get_target_property(_li "${_x}" INTERFACE_LINK_LIBRARIES)
        if(_li)
            list(APPEND _deps ${_li})
        endif()
        foreach(_d IN LISTS _deps)
            # $<LINK_ONLY:x>, $<BUILD_INTERFACE:x>: lo de adentro.
            if(_d MATCHES "^\\$<(LINK_ONLY|BUILD_INTERFACE):([^>]+)>$")
                set(_d "${CMAKE_MATCH_2}")
            endif()
            if(TARGET "${_d}")
                list(APPEND _pila "${_d}")
            endif()
        endforeach()
    endwhile()
    set(${salida} "${_vistos}" PARENT_SCOPE)
endfunction()

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
            # Los tests esperan los textos en el original: el idioma del sistema (o el
            # guardado en un launcher.json) no los cambia (ba::i18n::idiomaDeLaMaquina).
            if(NOT CMAKE_VERSION VERSION_LESS 3.28)
                set_property(TEST "${_t}" DIRECTORY "${_d}" APPEND PROPERTY ENVIRONMENT "BA_IDIOMA=es")
            endif()
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
            # Lo que leen las bibliotecas que enlaza (ba_biblioteca_lee).
            get_property(_corre GLOBAL PROPERTY BA_TEST_CORRE_${_t})
            if(TARGET "${_t}")
                list(APPEND _corre "${_t}")
            endif()
            foreach(_x IN LISTS _corre)
                _ba_cierre_enlace("${_x}" _libs)
                foreach(_lib IN LISTS _libs)
                    get_property(_lee GLOBAL PROPERTY BA_BIBLIOTECA_LEE_${_lib})
                    list(APPEND _decl ${_lee})
                endforeach()
            endforeach()
            list(REMOVE_DUPLICATES _decl)
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
