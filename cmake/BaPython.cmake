# ============================================================================
# cmake/BaPython.cmake — el Python de los plugins (docs/PLUGINS.md)
#
# Si está extern/python (make deps-python, tools/deps/python.sh), el intérprete
# portable se instala en el bundle (python/) y se define BA_HAVE_PYTHON. El SDK
# de los plugins (src/plugins/sdk) y los ejemplos (assets/plugins) van siempre
# en plugins-python/: son Python puro.
# ============================================================================
set(BA_HAVE_PYTHON OFF)
set(BA_PYTHON_DIR "${CMAKE_SOURCE_DIR}/extern/python")
if(EXISTS "${BA_PYTHON_DIR}/bin/python3")
    set(BA_HAVE_PYTHON ON)
    # Con su bytecode (precompilado en tools/deps/python.sh): corre con -B.
    install(DIRECTORY "${BA_PYTHON_DIR}/" DESTINATION python USE_SOURCE_PERMISSIONS COMPONENT bundle)
    message(STATUS "Python de los plugins: ${BA_PYTHON_DIR}")
else()
    message(STATUS "Python de los plugins: no está (make deps-python); los plugins no van a poder correr")
endif()
install(DIRECTORY "${CMAKE_SOURCE_DIR}/src/plugins/sdk/" DESTINATION plugins-python/sdk COMPONENT bundle
        PATTERN "__pycache__" EXCLUDE PATTERN "tests" EXCLUDE)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/plugins/" DESTINATION plugins-python/ejemplos COMPONENT bundle
        PATTERN "__pycache__" EXCLUDE PATTERN "_libs" EXCLUDE)
