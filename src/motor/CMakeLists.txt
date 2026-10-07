# ============================================================================
# src/motor — el motor de GStreamer (sin libobs)
#
# Biblioteca estática `ba_motor`: reloj del estudio, entradas (lo que era
# ba_gst_fuente, sin OBS), buses de audio (audiomixer) y, después, composición
# y salidas. Ver docs/PLAN-TODO-EN-UNO.md, bloque M.
# ============================================================================

add_library(ba_motor STATIC Piezas.cpp Reloj.cpp OrigenRed.cpp Entrada.cpp Mezcla.cpp Escucha.cpp Composicion.cpp Salidas.cpp Vistas.cpp Motor.cpp Mapa.cpp)
target_include_directories(ba_motor PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ba_motor PUBLIC ba_core ba::gstreamer)

if(BUILD_TESTING OR TRUE)
    add_executable(test_motor tests/test_motor.cpp)
    target_link_libraries(test_motor PRIVATE ba_motor)
    ba_gst_rpath(test_motor "")
    target_compile_definitions(test_motor PRIVATE BA_TEST_GST_RAIZ="${BA_GST_FW}/Versions/1.0"
        BA_TEST_GST_PLUGINS="${CMAKE_BINARY_DIR}/src/gst-plugins")
    add_dependencies(test_motor gstbaaudio gstbaipc) # balimitador; baanillosink
    add_test(NAME test_motor COMMAND test_motor)
    set_tests_properties(test_motor PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)

    add_executable(test_motor_retornos tests/test_motor_retornos.cpp)
    target_link_libraries(test_motor_retornos PRIVATE ba_motor)
    ba_gst_rpath(test_motor_retornos "")
    target_compile_definitions(test_motor_retornos PRIVATE BA_TEST_GST_RAIZ="${BA_GST_FW}/Versions/1.0"
        BA_TEST_GST_PLUGINS="${CMAKE_BINARY_DIR}/src/gst-plugins")
    add_dependencies(test_motor_retornos gstbaaudio gstbaipc)
    add_test(NAME test_motor_retornos COMMAND test_motor_retornos)
    set_tests_properties(test_motor_retornos PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)

    add_executable(test_composicion tests/test_composicion.cpp)
    target_link_libraries(test_composicion PRIVATE ba_motor)
    ba_gst_rpath(test_composicion "")
    target_compile_definitions(test_composicion PRIVATE BA_TEST_GST_RAIZ="${BA_GST_FW}/Versions/1.0")
    add_test(NAME test_composicion COMMAND test_composicion)
    set_tests_properties(test_composicion PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)

    add_executable(test_salidas tests/test_salidas.cpp)
    target_link_libraries(test_salidas PRIVATE ba_motor)
    ba_gst_rpath(test_salidas "")
    target_compile_definitions(test_salidas PRIVATE BA_TEST_GST_RAIZ="${BA_GST_FW}/Versions/1.0"
        BA_TEST_GST_PLUGINS="${CMAKE_BINARY_DIR}/src/gst-plugins")
    add_dependencies(test_salidas gstbaaudio gstbaipc)
    add_test(NAME test_salidas COMMAND test_salidas)
    set_tests_properties(test_salidas PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)

    add_executable(test_motor_app tests/test_motor_app.cpp)
    target_link_libraries(test_motor_app PRIVATE ba_motor)
    ba_gst_rpath(test_motor_app "")
    target_compile_definitions(test_motor_app PRIVATE BA_TEST_GST_RAIZ="${BA_GST_FW}/Versions/1.0"
        BA_TEST_GST_PLUGINS="${CMAKE_BINARY_DIR}/src/gst-plugins")
    add_dependencies(test_motor_app gstbaaudio gstbaipc)
    add_test(NAME test_motor_app COMMAND test_motor_app)
    set_tests_properties(test_motor_app PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()
