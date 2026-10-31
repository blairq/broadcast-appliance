// ============================================================================
// src/app/emision/SalidasConfig.hpp — las salidas guardadas y su aire (modelo, sin GStreamer)
//
// docs/planes/PLAN-SALIDAS.md, etapa 2. Lo que se guarda de las salidas, en JSON
// puro (lo usan AppRuntime y BroadcastEngine; los tests lo prueban sin motor):
//
//   Config/salidas.json   {"tope": 4, "salidas": [{id, nombre, color, orden}]}
//                         La por omisión («principal») está siempre, aunque el
//                         archivo no exista o no la nombre: es la de siempre.
//   Config/destinos.json  cada destino con `salida` (sin campo = la por omisión).
//   Runtime/al-aire.json  lo de siempre (streaming, recording, escena, cerrado) es
//                         de la por omisión, y `salidas` trae cada una:
//                         {"salidas": {"<id>": {escena, streaming, recording, destinos}}}
//                         (`destinos`: los ids de destinos.json que transmitían). Un
//                         archivo viejo (sin `salidas`) se lee igual: es la por omisión.
//                         En el disco va en dos generaciones (al-aire.A.json y
//                         al-aire.B.json, core/persist/Generaciones.hpp).
// ============================================================================
#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::app::salidas {

/// La que existe desde el arranque (la misma que motor::Motor::kSalidaPorOmision).
inline constexpr const char* kPorOmision = "principal";
/// Cuántas salidas se pueden tener a la vez si salidas.json no dice otra cosa:
/// cada una es un programa, un preview y una mezcla más (ver sistema_recursos).
inline constexpr int kTopeDeFabrica = 4;
inline constexpr int kTopeMaximo = 16;

struct Salida {
    std::string id;
    std::string nombre;
    std::string color; ///< "#RRGGBB": el del tally, el multiview y las superficies
    int orden = 0;
};

struct Lista {
    std::vector<Salida> salidas; ///< en orden; la por omisión siempre está
    int tope = kTopeDeFabrica;
    const Salida* buscar(const std::string& id) const;
};

/// De salidas.json (o {} si no hay): siempre con la por omisión, ordenada por `orden`.
Lista leer(const nlohmann::json& j);
nlohmann::json aJson(const Lista& l);
/// "" si sirve como id de una salida; si no, por qué no (solo letras, números, «-» y «_»:
/// va en los nombres de los productores del motor, de los canales del gateway y de los archivos).
std::string validarId(const std::string& id);
/// Suma una salida (con su color si no trae, y al final). "" o por qué no (repetida, tope).
std::string agregar(Lista& l, Salida s);
/// La saca (la por omisión no). "" o por qué no.
std::string quitar(Lista& l, const std::string& id);
/// Cambia nombre y color (lo que traiga `cambios`). "" o por qué no.
std::string editar(Lista& l, const std::string& id, const nlohmann::json& cambios);
/// El color que le toca a la salida número `n` (0 = la por omisión) si no eligió uno.
std::string colorDeFabrica(int n);

/// La salida de un destino de destinos.json (sin campo, o vacío = la por omisión).
std::string salidaDeDestino(const nlohmann::json& destino);

/// Lo que tenía al aire cada salida, para volver tras un corte (Runtime/al-aire.json).
struct Aire {
    std::string escena;
    bool transmitiendo = false;           ///< su transmisión principal
    bool grabando = false;
    std::vector<std::string> destinos;    ///< los destinos (destinos.json) que transmitían su programa
};
struct AlAire {
    bool cerrado = true; ///< la corrida que lo escribió se cerró en orden
    std::map<std::string, Aire> salidas;
    const Aire& de(const std::string& id) const;
};
/// Lee el viejo (campos sueltos = la por omisión) y el nuevo (con `salidas`).
AlAire leerAlAire(const nlohmann::json& j);
/// Escribe los campos sueltos de la por omisión (un arranque viejo lo sigue
/// entendiendo) y `salidas` con todas.
nlohmann::json aJson(const AlAire& a);

} // namespace ba::app::salidas
