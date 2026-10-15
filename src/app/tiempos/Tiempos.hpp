// ============================================================================
// src/app/tiempos/Tiempos.hpp — el grafo de demoras: de cada entrada a cada salida
//
// Junta lo que ya mide el motor en un solo grafo de NODOS (entrada, tarjeta, módulo
// de audio, escena, mezcla, salida) y TRAMOS (de un nodo a otro, de video o de
// audio), con cuánto agrega cada nodo y cuánto lleva acumulado desde la captura
// (docs/planes/PLAN-TIEMPOS.md, nivel 1). Es una función pura sobre las fotos que
// ya se publican (Motor::instantanea, BroadcastEngine::salidasAhora, el grafo de
// VideoGrafos y los racks): no toca el motor ni espera a nadie.
//
// Cómo se cuenta (todos los pipelines comparten el reloj del estudio, así que las
// edades valen entre pipelines):
//   - entrada: la EDAD de sus cuadros al salir de la entrada (captura → salida),
//     medida; lo que no es de sus tarjetas queda como «captura, decodificación y colas».
//   - tarjeta / módulo: lo que dice su elemento (ms por cuadro, ms por bloque).
//   - escena / mezcla: el compositor saca cada cuadro a su hora + la latencia que se
//     le fijó (declarada): acumula el máximo entre eso y lo que traen sus entradas.
//   - salida: el pipeline de la salida entrega a la hora del cuadro + su latencia
//     (declarada): ese es el total hasta su sink.
// Cada nodo dice si su número es «medido», «declarado» o «sin_medir».
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <map>
#include <string>

namespace ba::app::tiempos {

struct Fotos {
    nlohmann::json motor = nlohmann::json::object();   ///< Motor::estado() (la instantánea)
    nlohmann::json salidas = nlohmann::json::object(); ///< BroadcastEngine::salidasAhora()
    /// Los títulos de las tarjetas por id (de VideoGrafos::estudio: nodo id → etiqueta).
    std::map<std::string, std::string> titulos;
    /// El rack de audio de cada fuente (AudioRack::describe), si tiene.
    std::map<std::string, nlohmann::json> racks;
    /// Entradas que reciben algo pero no son nadie (el lugar de un invitado vacío: llega la
    /// placa del gateway): cuentan como en reposo. entrada → por qué.
    std::map<std::string, std::string> enEspera;
};

/// {nodos: [...], tramos: [...], salidas: [{id, titulo, total_ms, fuente, camino: [ids]}],
///  estudio: {latencia_ms, tope_ms}}. Ver el encabezado para cómo se cuenta.
nlohmann::json armar(const Fotos& f);

} // namespace ba::app::tiempos
