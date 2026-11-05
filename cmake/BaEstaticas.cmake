# ============================================================================
# cmake/BaEstaticas.cmake — mbedtls, libsamplerate, argon2, libdeflate y zxing-cpp
#
# Ya compiladas, de extern/estaticas/<plataforma>-<huella> (make deps-estaticas, parte de
# make deps): objetivos IMPORTED con los nombres de siempre (mbedcrypto mbedx509 mbedtls
# samplerate argon2 libdeflate::libdeflate_static ZXing::ZXing). Antes entraban por
# FetchContent: se configuraban en cada cmake y se compilaban en cada build y en cada
# worktree, y nunca cambian. Cómo se compilan: cmake/estaticas/.
#
# La huella (cmake/estaticas/huella.cmake) es de las fuentes fijadas, la configuración de
# mbedtls y la receta: si cambia algo de eso, la carpeta es otra y la configuración frena
# hasta `make deps-estaticas` (nunca se usan unas viejas sin avisar).
#
# Si faltan, la configuración FRENA (no hay respaldo callado que las baje y compile).
# -DBA_ESTATICAS_DESDE_FUENTE=ON las compila adentro del build, de extern/ (make
# deps-src), con las banderas del build: para los sanitizers (TSan quiere instrumentado
# también lo de terceros).
# ============================================================================
option(BA_ESTATICAS_DESDE_FUENTE "Compilar mbedtls, libsamplerate, argon2, libdeflate y zxing adentro del build (sanitizers)" OFF)

if(APPLE)
    set(BA_ESTATICAS_PLATAFORMA macos)
elseif(WIN32 AND BA_WIN_STATIC_PART)
    set(BA_ESTATICAS_PLATAFORMA win-mt)
elseif(WIN32)
    set(BA_ESTATICAS_PLATAFORMA win)
else()
    set(BA_ESTATICAS_PLATAFORMA linux)
endif()

if(BA_ESTATICAS_DESDE_FUENTE)
    set(BA_ESTATICAS_FUENTES "${CMAKE_SOURCE_DIR}/extern")
    set(BA_MBEDTLS_USER_CONFIG "${CMAKE_SOURCE_DIR}/cmake/mbedtls_user_config.h")
    if(BA_WIN_STATIC_PART)
        set(BA_ESTATICAS_ZXING OFF)
    else()
        set(BA_ESTATICAS_ZXING ON)
    endif()
    include("${CMAKE_SOURCE_DIR}/cmake/estaticas/compilar.cmake")
    set(BA_ESTATICAS_LICENCIAS "")
    set(BA_ESTATICAS_INCLUDES "${BA_ESTATICAS_FUENTES}/mbedtls/include"
        "${BA_ESTATICAS_FUENTES}/libsamplerate/include" "${BA_ESTATICAS_FUENTES}/argon2/include")
    message(STATUS "estáticas de terceros: desde la fuente, adentro del build (BA_ESTATICAS_DESDE_FUENTE)")
else()
    include("${CMAKE_SOURCE_DIR}/cmake/estaticas/huella.cmake")
    ba_estaticas_huella(_ba_est_h "${BA_ESTATICAS_PLATAFORMA}" "${CMAKE_SOURCE_DIR}")
    foreach(_e IN LISTS BA_ESTATICAS_ENTRADAS)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/${_e}")
    endforeach()
    set(BA_ESTATICAS_DIR "${CMAKE_SOURCE_DIR}/extern/estaticas/${BA_ESTATICAS_PLATAFORMA}-${_ba_est_h}")
    if(NOT EXISTS "${BA_ESTATICAS_DIR}/ba-estaticas.cmake")
        message(FATAL_ERROR
            "Faltan las bibliotecas estáticas de terceros (mbedtls, libsamplerate, argon2, "
            "libdeflate, zxing) para ${BA_ESTATICAS_PLATAFORMA} con esta receta:\n"
            "  ${BA_ESTATICAS_DIR}\n"
            "Se compilan una vez por máquina: make deps-estaticas (o make deps).")
    endif()
    include("${BA_ESTATICAS_DIR}/ba-estaticas.cmake")
    # Para lo que se compila aparte y no ve los objetivos (cmake/gateway-webrtc).
    set(BA_ESTATICAS_INCLUDES "${BA_ESTATICAS_DIR}/include")
    message(STATUS "estáticas de terceros: ${BA_ESTATICAS_DIR}")
endif()
