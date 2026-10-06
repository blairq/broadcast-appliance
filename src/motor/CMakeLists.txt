# ============================================================================
# src/motor — el motor de GStreamer (sin libobs)
#
# Biblioteca estática `ba_motor`: reloj del estudio, entradas (lo que era
# ba_gst_fuente, sin OBS), buses de audio (audiomixer) y, después, composición
# y salidas. Ver docs/PLAN-TODO-EN-UNO.md, bloque M.
# ============================================================================

add_library(ba_motor STATIC Piezas.cpp Reloj.cpp Entrada.cpp Mezcla.cpp)
target_include_directories(ba_motor PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ba_motor PUBLIC ba_core ba::gstreamer)

if(BUILD_TESTING OR TRUE)
    add_executable(test_motor tests/test_motor.cpp)
    target_link_libraries(test_motor PRIVATE ba_motor)
    ba_gst_rpath(test_motor "")
    target_compile_definitions(test_motor PRIVATE BA_TEST_GST_RAIZ="${BA_GST_FW}/Versions/1.0")
    add_test(NAME test_motor COMMAND test_motor)
    set_tests_properties(test_motor PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()
