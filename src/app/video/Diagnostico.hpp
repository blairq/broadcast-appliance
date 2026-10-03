// src/app/video/Diagnostico.hpp — qué le pasa a una entrada de video, y qué hacer
//
// Del estado de una entrada (VideoGrafos::estadoEntrada) saca la lista de
// problemas, cada uno con su gravedad y el ARREGLO concreto (la herramienta y
// sus argumentos). Es lo que necesita el asistente para armar bien un
// pipeline sin adivinar: el formato, los cuadros por segundo, el color, el
// entrelazado, el reloj, el colchón de una fuente de red y lo que dicen las
// tarjetas.
#pragma once

#include <nlohmann/json.hpp>

namespace ba::app::video {

/// [{gravedad: "error"|"aviso"|"info", tema, problema, arreglo}]
nlohmann::json diagnosticar(const nlohmann::json& estado);

} // namespace ba::app::video
