# ============================================================================
# cmake/BaZxing.cmake — zxing-cpp: leer códigos de barras (DNI, QR, DataMatrix…)
#
# El lector de documentos (src/core/documentos/lector, biblioteca `ba_codigos`)
# lee el PDF417 del DNI argentino de la cámara del tótem. zxing-cpp (Apache-2.0)
# lo lee bien; zbar no. Viene ya compilada con las demás estáticas de terceros
# (cmake/BaEstaticas.cmake, extern/estaticas: make deps-estaticas): lectores + el
# escritor viejo (sin zint; lo usa test_codigos para generar un PDF417 de prueba).
# Va fuera del sub-build /MT de Windows: el launcher y el gateway no leen códigos.
#
# Sin zxing no hay lector de documentos: es un error de configuración
# (ba_falta), no un aviso que se pierde.
# ============================================================================
set(BA_HAVE_CODIGOS OFF)
if(BA_WIN_STATIC_PART)
    return()
endif()
if(TARGET ZXing::ZXing)
    set(BA_HAVE_CODIGOS ON)
    message(STATUS "zxing-cpp ${ZXING_CPP_VERSION}: lector de códigos de barras (documentos)")
    # Su licencia viaja con el programa, como la de las demás bibliotecas.
    if(BA_ESTATICAS_LICENCIAS)
        set(_ba_zx_lic "${BA_ESTATICAS_LICENCIAS}/zxing-cpp-Apache-2.0.txt")
    else()
        set(_ba_zx_lic "${CMAKE_SOURCE_DIR}/extern/zxing-cpp/LICENSE")
    endif()
    install(FILES "${_ba_zx_lic}" DESTINATION data/licencias RENAME zxing-cpp-Apache-2.0.txt OPTIONAL)
else()
    ba_falta("Lector de documentos (zxing-cpp)" "falta zxing-cpp en las estáticas de terceros ('make deps-estaticas')")
endif()
