# ============================================================================
# cmake/BaPython.cmake — el Python de los plugins (docs/funciones/PLUGINS.md)
#
# Si está extern/python (make deps-python, tools/deps/python.sh), el intérprete
# portable se instala en el bundle (python/) y se define BA_HAVE_PYTHON. El SDK
# de los plugins (src/plugins/sdk) y los ejemplos (assets/plugins) van siempre
# en plugins-python/: son Python puro.
# ============================================================================
set(BA_HAVE_PYTHON OFF)
if(WIN32)
    set(BA_PYTHON_DIR "${CMAKE_SOURCE_DIR}/extern/python-win")
    set(_ba_py "${BA_PYTHON_DIR}/python.exe")
else()
    set(BA_PYTHON_DIR "${CMAKE_SOURCE_DIR}/extern/python")
    set(_ba_py "${BA_PYTHON_DIR}/bin/python3")
endif()
if(EXISTS "${_ba_py}")
    set(BA_HAVE_PYTHON ON)
    # El bytecode SIN FECHA (unchecked-hash): Python no compara el .pyc con la
    # fecha del .py. Con el de fábrica (por fecha), copiar el programa a un disco
    # que guarda las fechas con otra precisión (exFAT, FAT) los deja «viejos», y
    # cualquier Python que corra sin -B (un agente con consola) los reescribe
    # adentro del programa: la integridad del arranque da «hash distinto» (pasó
    # en la PC de pruebas con un SSD exFAT: 79 .pyc de numpy). Una sola vez por
    # extern (la marca); para Windows (compilado cruzado) con un Python 3.12 de acá.
    if(NOT EXISTS "${BA_PYTHON_DIR}/.pyc-sin-fecha")
        if(WIN32)
            find_program(_ba_py_host NAMES python3.12 python3 python)
            set(_ba_py_lib "${BA_PYTHON_DIR}/Lib")
            if(_ba_py_host)
                execute_process(COMMAND "${_ba_py_host}" -c "import sys; print('%d.%d' % sys.version_info[:2])"
                                OUTPUT_VARIABLE _ba_py_host_ver OUTPUT_STRIP_TRAILING_WHITESPACE)
                if(NOT _ba_py_host_ver STREQUAL "3.12")
                    unset(_ba_py_host)
                endif()
            endif()
        else()
            set(_ba_py_host "${_ba_py}")
            file(GLOB _ba_py_lib "${BA_PYTHON_DIR}/lib/python3.*")
        endif()
        if(_ba_py_host AND _ba_py_lib)
            message(STATUS "Python de los plugins: bytecode sin fecha (una vez; tarda un poco)")
            execute_process(COMMAND "${_ba_py_host}" -I -m compileall -q -f -j 0 --invalidation-mode unchecked-hash
                                    ${_ba_py_lib}
                            RESULT_VARIABLE _ba_py_r OUTPUT_QUIET ERROR_QUIET)
            # 1 = algún archivo no compiló (tests con sintaxis vieja): el resto quedó.
            if(_ba_py_r EQUAL 0 OR _ba_py_r EQUAL 1)
                file(TOUCH "${BA_PYTHON_DIR}/.pyc-sin-fecha")
            endif()
        else()
            message(WARNING "Python de los plugins: no hay un Python 3.12 para el bytecode sin fecha")
        endif()
    endif()
    # Con su bytecode (precompilado en tools/deps/python.sh): corre con -B. Las
    # cabeceras y los .lib son solo para compilar extensiones.
    install(DIRECTORY "${BA_PYTHON_DIR}/" DESTINATION python USE_SOURCE_PERMISSIONS COMPONENT bundle
            PATTERN "include" EXCLUDE PATTERN "Include" EXCLUDE PATTERN "libs" EXCLUDE PATTERN ".version" EXCLUDE)
    message(STATUS "Python de los plugins: ${BA_PYTHON_DIR}")
else()
    message(STATUS "Python de los plugins: no está (make deps-python); los plugins no van a poder correr")
endif()
install(DIRECTORY "${CMAKE_SOURCE_DIR}/src/plugins/sdk/" DESTINATION plugins-python/sdk COMPONENT bundle
        PATTERN "__pycache__" EXCLUDE PATTERN "tests" EXCLUDE)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/plugins/" DESTINATION plugins-python/ejemplos COMPONENT bundle
        PATTERN "__pycache__" EXCLUDE PATTERN "_libs" EXCLUDE)
# El traductor en vivo (agent/Traductor): corre con este mismo Python.
install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/traductor/" DESTINATION traductor COMPONENT bundle
        PATTERN "__pycache__" EXCLUDE)
# El laboratorio de superresolución (video/SuperRes): entrena y convierte modelos.
install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/superres/" DESTINATION superres COMPONENT bundle
        PATTERN "__pycache__" EXCLUDE)
