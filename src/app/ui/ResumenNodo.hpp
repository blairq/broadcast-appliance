// src/app/ui/ResumenNodo.hpp — lo que dice la tarjeta de cada nodo de video (abajo del nombre)
//
// Puro (JSON del grafo → texto traducido, contexto VideoPage): lo usa VideoViewModel y se
// prueba sin la app (tests/test_resumen_nodo.cpp). Nunca tira con un nodo raro: el grafo
// viene de disco y de versiones anteriores (a42d4a31: «controlador» como texto tiraba al
// arrancar y la app se caía).

#pragma once

#include <QString>

#include <nlohmann/json.hpp>

namespace ba::app {

/// Qué hace el nodo, con lo principal de sus ajustes. `conectadas`: los cables que le
/// llegan (para una mezcla).
QString resumenDeNodo(const nlohmann::json& n, int conectadas);

/// El grafo (o el estudio) con el `resumen` de cada nodo, para la vista.
nlohmann::json conResumenes(nlohmann::json g);

} // namespace ba::app
