// ============================================================================
// src/gst-plugins/ipc/adelantado.hpp — ¿cuándo se le puede empujar un bloque a un agregador?
//
// Un pad de un agregador (audiomixer, baretornos) guarda lo que le llega hasta que el
// agregador lo usa; si ya tiene guardado más que su tope (las propiedades latency +
// min-upstream-latency), el que empuja ESPERA adentro de gst_pad_push hasta que lo
// guardado baje del tope (gst_aggregator_pad_has_space en gstaggregator.c). En un bus de
// oído el tope es su espera (20 ms): lo fresco queda justo en el borde y casi cada bloque
// espera la próxima vuelta del agregador. Por eso batoma no empuja en el hilo de la
// entrega sino en el repartidor de su mezclador (entrega.cpp), y este decide con esto
// cuándo le toca a cada bloque:
//
//   - TARDE: el agregador ya armó lo que cubría (su fin + la latencia del agregador + una
//     holgura ya pasó). Empujarlo no sirve (lo tira) y solo ocupa el pad: se descarta.
//   - DESPUÉS: el pad ya tiene más que su tope y no se vacía hasta dentro de más de la
//     holgura (una entrada con sus horas adelantadas: una demora propia, un realineo).
//     Se queda en la cola de esa toma hasta que le toque; las demás siguen.
//   - YA: entra ahora (a lo sumo espera una vuelta del agregador, la holgura).
//
// El agregador usa lo que le llega en orden, a su hora más su latencia. Lo guardado baja
// del tope cuando usa lo que termina en (lo último entregado − el tope): a la hora (lo
// último entregado − tope + latencia).
//
// Lógica pura (sin GStreamer): la prueba la recorre en test_anillocomun.
// ============================================================================
#pragma once

#include <cstdint>

namespace ba::ipc_gst {

/// Lo que mira batoma de un agregador antes de empujarle (en ns; las horas, en tiempo de
/// ejecución del pipeline del agregador).
struct PadAgregador {
    std::uint64_t ultimoFin = 0; ///< el fin de lo último que se le entregó a ese pad
    std::uint64_t latencia = 0;  ///< la del agregador (gst_aggregator_get_latency): usa lo de la hora t a t + esto
    std::uint64_t tope = 0;      ///< lo que guarda como mucho por pad (latency + min-upstream-latency)
    std::uint64_t ahora = 0;     ///< la hora de ahora
};

/// ¿Empujar un bloque más haría esperar al que empuja más que `holgura`?
constexpr bool llegaAdelantado(const PadAgregador& p, std::uint64_t holgura) {
    const std::uint64_t libre = p.ultimoFin + p.latencia; // cuándo usa lo último entregado
    if (libre <= p.tope) return false;
    return libre - p.tope > p.ahora + holgura; // cuándo lo guardado baja del tope
}

enum class Turno { Tarde, Ya, Despues };

/// El turno de un bloque que termina en `fin` (tiempo de ejecución del agregador; `p.ultimoFin`
/// es lo último que ya se le entregó a ese pad, 0 = nada todavía).
constexpr Turno turno(const PadAgregador& p, std::uint64_t fin, std::uint64_t holgura) {
    if (fin + p.latencia + holgura < p.ahora) return Turno::Tarde;
    if (p.ultimoFin != 0 && llegaAdelantado(p, holgura)) return Turno::Despues;
    return Turno::Ya;
}

} // namespace ba::ipc_gst
