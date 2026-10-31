# ============================================================================
# src/motor — el motor de GStreamer (sin libobs)
#
# Biblioteca estática `ba_motor`: reloj del estudio, entradas (lo que era
# ba_gst_fuente, sin OBS), buses de audio (audiomixer) y, después, composición
# y salidas. Ver docs/planes/PLAN-TODO-EN-UNO.md, bloque M.
# ============================================================================

add_library(ba_motor STATIC Piezas.cpp BancoSr.cpp Reloj.cpp OrigenRed.cpp Entrada.cpp Mezcla.cpp Escucha.cpp Composicion.cpp Salidas.cpp Vistas.cpp Motor.cpp Mapa.cpp Candado.cpp Capturas.cpp Sondas.cpp ModosVideo.cpp Edad.cpp Prioridad.cpp Fallas.cpp)
target_include_directories(ba_motor PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ba_motor PUBLIC ba_core ba::gstreamer)
# Capturas: las ventanas de cada sistema (ver Capturas.hpp).
if(APPLE)
    target_sources(ba_motor PRIVATE Capturas_mac.mm)
    set_source_files_properties(Capturas_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(ba_motor PUBLIC "-framework AVFoundation" "-framework CoreGraphics" "-framework Foundation")
elseif(WIN32)
    target_link_libraries(ba_motor PUBLIC dwmapi)
endif()

if(BUILD_TESTING OR TRUE)
    add_executable(test_motor tests/test_motor.cpp)
    target_link_libraries(test_motor PRIVATE ba_motor)
    ba_gst_rpath(test_motor "")
    target_compile_definitions(test_motor PRIVATE BA_TEST_GST_RAIZ="${BA_GST_FW}/Versions/1.0"
        BA_TEST_GST_PLUGINS="${CMAKE_BINARY_DIR}/src/gst-plugins")
    add_dependencies(test_motor gstbaaudio gstbaipc) # balimitador; baanillosink
    add_test(NAME test_motor COMMAND test_motor)
    set_tests_properties(test_motor PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)

    # F1-14: las fallas que inyecta el banco de caos (diag_falla, solo --test).
    add_executable(test_fallas tests/test_fallas.cpp)
    target_link_libraries(test_fallas PRIVATE ba_motor)
    ba_gst_rpath(test_fallas "")
    target_compile_definitions(test_fallas PRIVATE BA_TEST_GST_RAIZ="${BA_GST_FW}/Versions/1.0")
    add_test(NAME test_fallas COMMAND test_fallas)
    set_tests_properties(test_fallas PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)

    add_executable(test_latencia_estudio tests/test_latencia_estudio.cpp)
    target_link_libraries(test_latencia_estudio PRIVATE ba_motor)
    ba_gst_rpath(test_latencia_estudio "")
    add_test(NAME test_latencia_estudio COMMAND test_latencia_estudio)
    set_tests_properties(test_latencia_estudio PROPERTIES TIMEOUT 30)
    add_executable(test_modos tests/test_modos.cpp)
    target_link_libraries(test_modos PRIVATE ba_motor)
    ba_gst_rpath(test_modos "")
    target_compile_definitions(test_modos PRIVATE BA_TEST_GST_RAIZ="${BA_GST_FW}/Versions/1.0"
        BA_TEST_DATOS="${CMAKE_CURRENT_SOURCE_DIR}/tests/datos")
    add_test(NAME test_modos COMMAND test_modos)
    set_tests_properties(test_modos PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_executable(test_sondas tests/test_sondas.cpp)
    target_link_libraries(test_sondas PRIVATE ba_motor)
    ba_gst_rpath(test_sondas "")
    target_compile_definitions(test_sondas PRIVATE BA_TEST_GST_RAIZ="${BA_GST_FW}/Versions/1.0")
    add_test(NAME test_sondas COMMAND test_sondas)
    set_tests_properties(test_sondas PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)

    add_executable(test_motor_retornos tests/test_motor_retornos.cpp)
    target_link_libraries(test_motor_retornos PRIVATE ba_motor)
    ba_gst_rpath(test_motor_retornos "")
    target_compile_definitions(test_motor_retornos PRIVATE BA_TEST_GST_RAIZ="${BA_GST_FW}/Versions/1.0"
        BA_TEST_GST_PLUGINS="${CMAKE_BINARY_DIR}/src/gst-plugins")
    add_dependencies(test_motor_retornos gstbaaudio gstbaipc)
    add_test(NAME test_motor_retornos COMMAND test_motor_retornos)
    set_tests_properties(test_motor_retornos PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)

    # Los buses de oído (F2-01, F2-03): pulso sintético → anillo, lazo contra el sello.
    add_executable(test_lazo_latencia tests/test_lazo_latencia.cpp)
    target_link_libraries(test_lazo_latencia PRIVATE ba_motor)
    ba_gst_rpath(test_lazo_latencia "")
    target_compile_definitions(test_lazo_latencia PRIVATE BA_TEST_GST_RAIZ="${BA_GST_FW}/Versions/1.0"
        BA_TEST_GST_PLUGINS="${CMAKE_BINARY_DIR}/src/gst-plugins")
    add_dependencies(test_lazo_latencia gstbaaudio gstbaipc)
    add_test(NAME test_lazo_latencia COMMAND test_lazo_latencia)
    set_tests_properties(test_lazo_latencia PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)

    add_executable(test_composicion tests/test_composicion.cpp)
    target_link_libraries(test_composicion PRIVATE ba_motor)
    ba_gst_rpath(test_composicion "")
    target_compile_definitions(test_composicion PRIVATE BA_TEST_GST_RAIZ="${BA_GST_FW}/Versions/1.0"
        BA_TEST_GST_PLUGINS="${CMAKE_BINARY_DIR}/src/gst-plugins")
    add_dependencies(test_composicion gstbafiltros) # basombra
    add_test(NAME test_composicion COMMAND test_composicion)
    set_tests_properties(test_composicion PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)

    add_executable(test_capturas tests/test_capturas.cpp)
    target_link_libraries(test_capturas PRIVATE ba_motor)
    ba_gst_rpath(test_capturas "")
    target_compile_definitions(test_capturas PRIVATE BA_TEST_GST_RAIZ="${BA_GST_FW}/Versions/1.0"
        BA_TEST_GST_PLUGINS="${CMAKE_BINARY_DIR}/src/gst-plugins")
    if(TARGET gstbacaptura)
        add_dependencies(test_capturas gstbacaptura)
    endif()
    add_test(NAME test_capturas COMMAND test_capturas)
    set_tests_properties(test_capturas PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)

    add_executable(test_salidas tests/test_salidas.cpp)
    target_link_libraries(test_salidas PRIVATE ba_motor)
    ba_gst_rpath(test_salidas "")
    target_compile_definitions(test_salidas PRIVATE BA_TEST_GST_RAIZ="${BA_GST_FW}/Versions/1.0"
        BA_TEST_GST_PLUGINS="${CMAKE_BINARY_DIR}/src/gst-plugins")
    add_dependencies(test_salidas gstbaaudio gstbaipc)
    add_test(NAME test_salidas COMMAND test_salidas)
    set_tests_properties(test_salidas PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)

    # Las salidas que se rearman solas: el tee (un codificador para todas), el
    # codificador que falla en el aire, el disco que falla, el respaldo de ingest.
    add_executable(test_salidas_fallas tests/test_salidas_fallas.cpp)
    target_link_libraries(test_salidas_fallas PRIVATE ba_motor)
    ba_gst_rpath(test_salidas_fallas "")
    target_compile_definitions(test_salidas_fallas PRIVATE BA_TEST_GST_RAIZ="${BA_GST_FW}/Versions/1.0"
        BA_TEST_GST_PLUGINS="${CMAKE_BINARY_DIR}/src/gst-plugins")
    add_dependencies(test_salidas_fallas gstbaaudio gstbaipc)
    add_test(NAME test_salidas_fallas COMMAND test_salidas_fallas)
    set_tests_properties(test_salidas_fallas PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)

    add_executable(test_retraso_propio tests/test_retraso_propio.cpp)
    target_link_libraries(test_retraso_propio PRIVATE ba_motor)
    ba_gst_rpath(test_retraso_propio "")
    target_compile_definitions(test_retraso_propio PRIVATE BA_TEST_GST_RAIZ="${BA_GST_FW}/Versions/1.0")
    add_test(NAME test_retraso_propio COMMAND test_retraso_propio)
    set_tests_properties(test_retraso_propio PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)

    add_executable(test_motor_app tests/test_motor_app.cpp)
    target_link_libraries(test_motor_app PRIVATE ba_motor)
    ba_gst_rpath(test_motor_app "")
    target_compile_definitions(test_motor_app PRIVATE BA_TEST_GST_RAIZ="${BA_GST_FW}/Versions/1.0"
        BA_TEST_GST_PLUGINS="${CMAKE_BINARY_DIR}/src/gst-plugins")
    add_dependencies(test_motor_app gstbaaudio gstbaipc)
    add_test(NAME test_motor_app COMMAND test_motor_app)
    # Mide cuadros en tiempo real (GPU): solo, que con la CPU repartida llegan tarde.
    set_tests_properties(test_motor_app PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RUN_SERIAL TRUE)

    # Muchas salidas (docs/planes/PLAN-SALIDAS.md, etapa 1): cada una con su aire y su mezcla.
    add_executable(test_motor_salidas tests/test_motor_salidas.cpp)
    target_link_libraries(test_motor_salidas PRIVATE ba_motor)
    ba_gst_rpath(test_motor_salidas "")
    target_compile_definitions(test_motor_salidas PRIVATE BA_TEST_GST_RAIZ="${BA_GST_FW}/Versions/1.0"
        BA_TEST_GST_PLUGINS="${CMAKE_BINARY_DIR}/src/gst-plugins")
    add_dependencies(test_motor_salidas gstbaaudio gstbaipc)
    add_test(NAME test_motor_salidas COMMAND test_motor_salidas)
    set_tests_properties(test_motor_salidas PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)

    # La interfaz nunca espera al motor (docs/planes/PLAN-MOTOR-SIN-ESPERAS.md).
    add_executable(test_motor_sin_esperas tests/test_motor_sin_esperas.cpp)
    target_link_libraries(test_motor_sin_esperas PRIVATE ba_motor)
    ba_gst_rpath(test_motor_sin_esperas "")
    target_compile_definitions(test_motor_sin_esperas PRIVATE BA_TEST_GST_RAIZ="${BA_GST_FW}/Versions/1.0"
        BA_TEST_GST_PLUGINS="${CMAKE_BINARY_DIR}/src/gst-plugins")
    add_dependencies(test_motor_sin_esperas gstbaaudio gstbaipc)
    add_test(NAME test_motor_sin_esperas COMMAND test_motor_sin_esperas)
    set_tests_properties(test_motor_sin_esperas PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)

    # Los convertidores de D3D12 de a uno (la caída del heap de las placas). Solo Windows.
    add_executable(test_d3d12_de_a_uno tests/test_d3d12_de_a_uno.cpp)
    target_link_libraries(test_d3d12_de_a_uno PRIVATE ba_motor)
    ba_gst_rpath(test_d3d12_de_a_uno "")
    target_compile_definitions(test_d3d12_de_a_uno PRIVATE BA_TEST_GST_RAIZ="${BA_GST_FW}/Versions/1.0"
        BA_TEST_GST_PLUGINS="${CMAKE_BINARY_DIR}/src/gst-plugins")
    add_test(NAME test_d3d12_de_a_uno COMMAND test_d3d12_de_a_uno)
    set_tests_properties(test_d3d12_de_a_uno PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)
endif()
