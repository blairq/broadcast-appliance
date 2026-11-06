// ============================================================================
// src/motor/TimbreVistas.hpp — el aviso de «hay un cuadro nuevo en alguna vista»
//
// La interfaz (app/ui/FrameService) leía las vistas del motor con un temporizador de
// 16 ms: el programa sale a 30 parejo, pero cada cuadro se tomaba en la vuelta siguiente
// del temporizador, que no tiene nada que ver con su fase. Medido el 2026-11-05 con el
// programa en la ventana: lo que avanza el video, 33,3 ms exactos; cuándo se tomaba cada
// cuadro, p50 32,6 y p95 48,6 ms (unos quedaban 17 ms en pantalla y otros 50: a saltos).
// Con el timbre, la vista avisa apenas tiene uno nuevo y la interfaz lo toma en ese momento.
//
// Uno por proceso (lo pone quien muestra las vistas). Se toca en el hilo de GStreamer de la
// vista, fuera de sus candados: tiene que volver enseguida (juntar avisos y pasar al hilo
// que corresponda). Sin nada puesto no cuesta más que leer un puntero.
// ============================================================================
#pragma once

#include <atomic>
#include <functional>
#include <memory>

namespace ba::motor {

using TimbreVistas = std::function<void()>;

namespace detalle {
inline std::shared_ptr<const TimbreVistas>& timbreVistas() {
    static std::shared_ptr<const TimbreVistas> t;
    return t;
}
} // namespace detalle

/// Pone (o saca, con nullptr) el aviso. Desde cualquier hilo.
inline void ponerTimbreVistas(TimbreVistas fn) {
    std::atomic_store(&detalle::timbreVistas(), fn ? std::make_shared<const TimbreVistas>(std::move(fn)) : nullptr);
}

/// Lo llama la vista con cada cuadro nuevo.
inline void tocarTimbreVistas() {
    if (const auto t = std::atomic_load(&detalle::timbreVistas())) (*t)();
}

} // namespace ba::motor
