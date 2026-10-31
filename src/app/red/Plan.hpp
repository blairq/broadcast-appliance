// src/app/red/Plan.hpp — cómo conectar bien una cámara de red al estudio
//
// Con la ficha ONVIF (sus perfiles: códec, tamaño, fps, URL; si tiene PTZ) y
// el análisis de GStreamer del stream (lo que de verdad manda), decide:
//   - QUÉ PERFIL: el más chico que cubra el lienzo (no se decodifica 4K para
//     mostrar 1080), y H.264/H.265 antes que MJPEG (los primeros se decodifican
//     por hardware: VideoToolbox en Mac, casi sin CPU; MJPEG va por CPU).
//   - QUÉ NORMALIZAR, solo lo que la entrada del motor no hace siempre:
//     desentrelazar, píxel no cuadrado, cuadros variables o distintos del lienzo.
//     El COLOR (matriz, rango, YUV->RGB) lo lleva siempre el motor al del lienzo
//     (en la GPU si hay) con lo que declara la fuente: no se gasta CPU en eso.
//   - LA LATENCIA del colchón (red local: 200 ms; Wi-Fi o internet: más).
//   - EL PTZ: ONVIF con el perfil que tiene PTZ.
// Devuelve el plan explicado y los argumentos para camara_agregar.
#pragma once

#include <nlohmann/json.hpp>

namespace ba::app::red {

/// :param ficha: lo de Red::ficha (perfiles con {token, nombre, codec, ancho,
///        alto, fps, bitrate, uri, ptz}, ptz {…}).
/// :param analisis: el de GStreamer del perfil elegido, si ya se hizo ({} si no).
/// :param lienzo: {ancho, alto, fps}.
nlohmann::json planDe(const nlohmann::json& ficha, const nlohmann::json& analisis,
                      const nlohmann::json& lienzo);

/// El perfil que conviene de una lista (índice, o -1).
int elegirPerfil(const nlohmann::json& perfiles, int altoLienzo);

} // namespace ba::app::red
