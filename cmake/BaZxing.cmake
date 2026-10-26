# ============================================================================
# cmake/BaZxing.cmake — zxing-cpp: leer códigos de barras (DNI, QR, DataMatrix…)
#
# El lector de documentos (src/core/documentos/lector, biblioteca `ba_codigos`)
# lee el PDF417 del DNI argentino de la cámara del tótem. zxing-cpp (Apache-2.0)
# lo lee bien; zbar no. Se construye DESDE LA FUENTE en los tres sistemas, como
# libdeflate: el tarball del tag con su SHA-256 fijo (versions.cmake), o la copia
# de extern/zxing-cpp si `make deps-zxing` (o `make deps-src`) ya la bajó, y así
# el configure no toca la red.
#
# Solo lo necesario: lectores + el escritor viejo (sin zint; lo usa
# test_codigos para generar un PDF417 de prueba), biblioteca ESTÁTICA, sin
# ejemplos ni tests propios. Va fuera del sub-build /MT de Windows: el launcher y
# el gateway no leen códigos.
#
# Sin zxing no hay lector de documentos: es un error de configuración
# (ba_falta), no un aviso que se pierde.
# ============================================================================
include(FetchContent)

FetchContent_Declare(zxing_cpp
    URL https://codeload.github.com/zxing-cpp/zxing-cpp/tar.gz/refs/tags/v${ZXING_CPP_VERSION}
    URL_HASH SHA256=${ZXING_CPP_SHA256}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

if(EXISTS "${CMAKE_SOURCE_DIR}/extern/zxing-cpp/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_ZXING_CPP "${CMAKE_SOURCE_DIR}/extern/zxing-cpp")
    message(STATUS "zxing-cpp: usando vendored en extern/ (offline)")
else()
    message(STATUS "zxing-cpp: sin extern/zxing-cpp: se baja el tarball")
endif()

# En una función para que BUILD_SHARED_LIBS (que zxing declara con option(), ON
# fuera de Windows) quede estática SOLO para zxing y no cambie a nadie más.
function(_ba_traer_zxing)
    set(BUILD_SHARED_LIBS OFF)
    set(ZXING_READERS ON CACHE BOOL "" FORCE)
    set(ZXING_WRITERS "OLD" CACHE STRING "" FORCE)
    set(ZXING_USE_BUNDLED_ZINT OFF CACHE BOOL "" FORCE)
    set(ZXING_C_API OFF CACHE BOOL "" FORCE)
    set(ZXING_EXPERIMENTAL_API OFF CACHE BOOL "" FORCE)
    set(ZXING_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ZXING_BLACKBOX_TESTS OFF CACHE BOOL "" FORCE)
    set(ZXING_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(ZXING_PYTHON_MODULE OFF CACHE BOOL "" FORCE)
    set(ZXING_DEPENDENCIES "LOCAL" CACHE STRING "" FORCE)
    # Sus reglas de install a un componente propio: no van al bundle (ver el
    # comentario de las demás dependencias en el CMakeLists raíz).
    set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME thirdparty)
    FetchContent_MakeAvailable(zxing_cpp)
endfunction()
_ba_traer_zxing()

set(BA_HAVE_CODIGOS OFF)
if(TARGET ZXing::ZXing)
    set(BA_HAVE_CODIGOS ON)
    set_target_properties(ZXing PROPERTIES POSITION_INDEPENDENT_CODE ON)
    message(STATUS "zxing-cpp ${ZXING_CPP_VERSION}: lector de códigos de barras (documentos)")
    # Su licencia viaja con el programa, como la de las demás bibliotecas.
    FetchContent_GetProperties(zxing_cpp)
    install(FILES "${zxing_cpp_SOURCE_DIR}/LICENSE" DESTINATION data/licencias
            RENAME zxing-cpp-Apache-2.0.txt OPTIONAL)
else()
    ba_falta("Lector de documentos (zxing-cpp)" "no se pudo traer zxing-cpp ('make deps-zxing')")
endif()
