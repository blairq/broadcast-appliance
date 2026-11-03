# ============================================================================
# cmake/BaTableroWeb.cmake — el tablero web (src/tablero-web) adentro del bundle
#
# `make tablero-web` deja en build-tablero-web/salida/ lo que sirve el gateway en
# /control/app/: index.html, puente.js, qtloader.js, ba-tablero.js y ba-tablero.wasm (más
# sus .gz/.br). Acá solo se copia a Web/control/app. Lo usan las tres plataformas
# (src/gateway/CMakeLists.txt y cmake/BaWinStaticPart.cmake): el .wasm es el mismo.
# ============================================================================
set(BA_TABLERO_WEB_DIR "${CMAKE_SOURCE_DIR}/build-tablero-web/salida" CACHE PATH
    "Lo armado por make tablero-web (el dashboard en WebAssembly)")
if(EXISTS "${BA_TABLERO_WEB_DIR}/ba-tablero.wasm")
    install(DIRECTORY "${BA_TABLERO_WEB_DIR}/" DESTINATION Web/control/app COMPONENT bundle)
    message(STATUS "tablero web: ${BA_TABLERO_WEB_DIR} → Web/control/app")
else()
    message(STATUS "tablero web: no está armado (make tablero-web); /control/app/ no va en el bundle")
endif()
