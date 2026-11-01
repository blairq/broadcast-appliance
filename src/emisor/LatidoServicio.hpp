// ============================================================================
// src/emisor/LatidoServicio.hpp — ¿el emisor ATIENDE? (el latido que ve el launcher)
//
// El latido en BA.Health no dice que el proceso existe: dice que el bucle del emisor
// (pedido, entrada, salidas, estado: main.cpp) da vueltas. Como /_ba/vivo de ba-web,
// que contesta el servicio y no el sistema. Lo emite un hilo aparte cada 100 ms, solo
// si el bucle dio una vuelta hace menos de `vueltaMaxMs`; mientras el bucle está en
// algo largo que se sabe largo (arrancar, aplicar un pedido que para una grabación,
// cerrar los archivos y pasarlos a MP4) late hasta `ocupadoMaxMs` desde que empezó.
//
// Así el launcher puede darlo por colgado enseguida (`emisor.cuelgueS`, 2 s: el emisor
// lleva el aire y un cuelgue es negro en la transmisión) sin falsos con la máquina
// cargada: el hilo que late no hace nada más, y lo largo legítimo está declarado.
//
// Sin relojes propios (el que llama pasa la hora): se prueba solo
// (tests/test_latido_servicio.cpp). Los dos hilos (el bucle y el que late) lo tocan
// por atómicos.
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>

namespace ba::emisor {

class LatidoServicio {
   public:
    LatidoServicio(std::int64_t vueltaMaxMs, std::int64_t ocupadoMaxMs) : vueltaMaxMs_(vueltaMaxMs), ocupadoMaxMs_(ocupadoMaxMs) {}

    /// El bucle dio una vuelta (y terminó lo largo, si estaba en algo).
    void vuelta(std::int64_t ahoraMs) {
        ultimaVuelta_.store(ahoraMs);
        ocupadoDesde_.store(kLibre);
    }
    /// Empieza algo largo y declarado (termina con la próxima vuelta()).
    void ocupado(std::int64_t ahoraMs) { ocupadoDesde_.store(ahoraMs); }

    /// ¿Se late ahora? (lo pregunta el hilo que late)
    bool late(std::int64_t ahoraMs) const {
        const std::int64_t o = ocupadoDesde_.load();
        if (o != kLibre) return ahoraMs - o < ocupadoMaxMs_;
        return ahoraMs - ultimaVuelta_.load() < vueltaMaxMs_;
    }

   private:
    static constexpr std::int64_t kLibre = INT64_MIN;
    const std::int64_t vueltaMaxMs_;
    const std::int64_t ocupadoMaxMs_;
    std::atomic<std::int64_t> ultimaVuelta_{0};
    std::atomic<std::int64_t> ocupadoDesde_{kLibre};
};

} // namespace ba::emisor
