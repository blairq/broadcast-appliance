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
    /// El último análisis por elemento de cada entrada (motor::Sondas): {elementos, cruces,
    /// segundos, t_ms}. Lo medido manda sobre lo que informa cada tarjeta.
    std::map<std::string, nlohmann::json> pipelines;
};

/// {nodos: [...], tramos: [...], salidas: [{id, titulo, total_ms, fuente, camino: [ids]}],
///  estudio: {latencia_ms, tope_ms}}. Ver el encabezado para cómo se cuenta.
/// El nodo de cada entrada analizada trae en su detalle `pipeline` (lo medido) y
/// `diagnostico` (ver diagnosticar).
nlohmann::json armar(const Fotos& f);

/// Una tarjeta en el pipeline: su nodo y su título (por su instancia, ba_f_<nombre>).
struct TarjetaEnPipeline {
    std::string id, titulo;
};
/// Lo que se ve en el recorrido medido de una entrada (motor::Sondas), con la tarjeta a la
/// que corresponde cada cosa (sus elementos y los conversores que pide):
/// {resumen: {elementos, total_ms, cruces, idas_y_vueltas, cuadro_ms},
///  hallazgos: [{tipo, gravedad: "problema"|"aviso"|"info", texto, ms, elementos, tarjetas: [{id, titulo}]}]}
/// tipos: ida_y_vuelta (baja de la GPU y vuelve a subir: lo del medio trabaja en RAM, por
/// sus pads o por lo que negoció), termina_en_ram, retiene (agrega más de un cuadro y medio),
/// declara_menos / declara_de_mas (lo que le dice al pipeline contra lo que mide: con eso
/// compensa el estudio), conversion_cpu (convierte formato o tamaño en la CPU).
/// Ordenados por gravedad y por ms.
nlohmann::json diagnosticar(const nlohmann::json& pipeline, const std::map<std::string, TarjetaEnPipeline>& tarjetas);

} // namespace ba::app::tiempos
