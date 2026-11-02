// ============================================================================
// src/gst-plugins/ipc/adelantado.hpp — ¿empujarle a un agregador en vivo haría esperar?
//
// Un pad de un agregador (audiomixer, baretornos) guarda lo que le llega hasta que el
// agregador lo usa; si ya tiene guardado más que su tope (las propiedades latency +
// min-upstream-latency), el que empuja ESPERA adentro de gst_pad_push hasta que lo
// guardado baje del tope (gst_aggregator_pad_has_space en gstaggregator.c). Con un hilo
// por entrada esperaba esa entrada sola; con el lector común de los anillos (comun.hpp:
// un hilo para el audio de todos los invitados) esperaba TODO el audio: una entrada con
// sus horas adelantadas (una demora propia o un realineo de segundos) dejaba a las demás
// sin datos (2026-11-01, prueba de sala con 20/30/40 invitados: «no llegan datos hace
// 10 s»).
//
// El agregador usa lo que le llega en orden, a su hora más su latencia. Lo guardado
// baja del tope cuando usa lo que termina en (lo último entregado − el tope): a la hora
// (lo último entregado − tope + latencia). Si eso es ahora o dentro de una vuelta (lo de
// siempre: lo fresco queda justo en el borde), se empuja; si es más adelante, lo de esa
// entrada llega ADELANTADO y batoma (entrega.cpp) no lo empuja: se pierde ese bloque de
// ESA entrada y nadie más espera.
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

} // namespace ba::ipc_gst
