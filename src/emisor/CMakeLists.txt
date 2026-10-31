# ============================================================================
# src/emisor — `ba-emisor`: la salida de una salida en un proceso aparte (F3-01)
#
# `ba_emisor_pedido`: el pedido de la app al emisor (Pedido.hpp; lo usan la app y
# el emisor). `ba-emisor`: el proceso, con las clases de salida del motor
# (motor/Salidas.*: no copia pipelines). Lo lanza el launcher con `emisor.aparte`.
# ============================================================================

add_library(ba_emisor_pedido STATIC Pedido.cpp)
target_include_directories(ba_emisor_pedido PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ba_emisor_pedido PUBLIC ba_motor ba_core)

add_executable(ba-emisor main.cpp Emisor.cpp)
target_link_libraries(ba-emisor PRIVATE ba_emisor_pedido)
# Lee los canales con bavideoanillo/baanillo (gstbaipc): que estén armados con él.
add_dependencies(ba-emisor gstbaipc)
if(APPLE)
    set_target_properties(ba-emisor PROPERTIES INSTALL_RPATH "@executable_path")
elseif(NOT WIN32)
    set_target_properties(ba-emisor PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()
ba_gst_rpath(ba-emisor "")
# Va con A y B (spec §5.1).
install(TARGETS ba-emisor RUNTIME DESTINATION bin/64bit COMPONENT bundle)

if(BUILD_TESTING OR TRUE)
    # Un «A» de mentira publica el canal y el anillo, se lo mata con SIGKILL y vuelve:
    # la grabación sigue en el mismo archivo, con la placa en el hueco.
    add_executable(test_emisor tests/test_emisor.cpp Emisor.cpp)
    target_link_libraries(test_emisor PRIVATE ba_emisor_pedido)
    ba_gst_rpath(test_emisor "")
    target_compile_definitions(test_emisor PRIVATE BA_TEST_GST_RAIZ="${BA_GST_FW}/Versions/1.0"
        BA_TEST_GST_PLUGINS="${CMAKE_BINARY_DIR}/src/gst-plugins")
    add_dependencies(test_emisor gstbaipc)
    add_test(NAME test_emisor COMMAND test_emisor)
    set_tests_properties(test_emisor PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()
