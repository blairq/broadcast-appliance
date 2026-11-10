# ============================================================================
# cmake/BaTableroWeb.cmake — el tablero web (src/tablero-web) adentro del bundle
#
# `make tablero-web` deja en build-tablero-web/salida/ lo que sirve el gateway en
# /control/app/: index.html, puente.js, qtloader.js, ba-tablero.js y ba-tablero.wasm (más
# sus .gz/.br). Acá solo se copia a Web/control/app. Lo usan las tres plataformas
# (src/gateway/CMakeLists.txt y cmake/BaWinStaticPart.cmake): el .wasm es el mismo.
#
# En Docker (tools/ci/compilar-en-docker.sh) build-tablero-web/ no está en el checkout (no
# está en git): se monta el de la Mac y llega por la variable de entorno BA_TABLERO_WEB_DIR.
# Sin el tablero el build se frena: antes salía el bundle de Windows sin /control/app/
# («no encontrado» en el navegador) y nadie se enteraba.
# ============================================================================
set(BA_TABLERO_WEB_DIR "${CMAKE_SOURCE_DIR}/build-tablero-web/salida" CACHE PATH
    "Lo armado por make tablero-web (el dashboard en WebAssembly)")
if(DEFINED ENV{BA_TABLERO_WEB_DIR})
    set(BA_TABLERO_WEB_DIR "$ENV{BA_TABLERO_WEB_DIR}" CACHE PATH
        "Lo armado por make tablero-web (el dashboard en WebAssembly)" FORCE)
endif()
if(EXISTS "${BA_TABLERO_WEB_DIR}/ba-tablero.wasm")
    install(DIRECTORY "${BA_TABLERO_WEB_DIR}/" DESTINATION Web/control/app COMPONENT bundle)
    message(STATUS "tablero web: ${BA_TABLERO_WEB_DIR} → Web/control/app")
else()
    ba_falta("Tablero web en WebAssembly (/control/app/)"
        "no está ${BA_TABLERO_WEB_DIR}/ba-tablero.wasm ('make tablero-web')")
endif()
