# ============================================================================
# cmake/BaEcoEnlace.cmake — el eco del medidor del enlace, adentro del gateway
#
# tools/vps/ba-enlace-eco.py corre en el VPS del túnel (docs/funciones/ENLACE.md). El
# gateway arma el instalador para el servidor (GET /api/v1/enlace/instalador) con el
# archivo de ESTE build: acá se lo convierte en bytes (EcoEnlace.inc, lo incluye
# src/gateway/net/EnlaceServicio.cpp). Lo usan el build principal y el subproyecto
# cmake/gateway-webrtc.
#
#   ba_eco_enlace(<raíz del repo> <directorio de salida>)
# ============================================================================
function(ba_eco_enlace raiz salida)
    set(_py "${raiz}/tools/vps/ba-enlace-eco.py")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_py}")
    file(READ "${_py}" _hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _hex "${_hex}")
    file(CONFIGURE OUTPUT "${salida}/EcoEnlace.inc" CONTENT "${_hex}\n")
endfunction()
