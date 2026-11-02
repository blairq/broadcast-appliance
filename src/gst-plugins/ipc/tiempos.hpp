// ============================================================================
// src/gst-plugins/ipc/tiempos.hpp — dónde se va el tiempo del audio de los invitados
//
// Diagnóstico (apagado no cuesta nada: una variable de entorno mirada una vez).
// Con BA_TIEMPOS_AUDIO=1 el plugin baipc junta, por tramo, cuántas veces, el promedio
// y el máximo, y el lector común lo escribe a stderr cada 5 s:
//   vuelta      lo que tarda una vuelta del hilo `anillos-audio` (todas las entradas)
//   anillo      lo viejo que sale un bloque del anillo (ahora − su sello del gateway)
//   cadena      lo que tarda empujar un bloque por la cadena de su entrada (hasta los
//               mezcladores y el intersink: todo corre en el hilo común)
//   cola        lo que espera un bloque en la cola de su batoma (de la entrega al repartidor)
//   toma        lo que espera gst_pad_push en el pad de un mezclador (el repartidor)
//   espera      lo que un mezclador espera a sus entradas (esperarEntradas, en su hilo)
// ============================================================================
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace ba::ipc_gst::tiempos {

struct Tramo {
    const char* nombre;
    std::atomic<std::uint64_t> n{0}, suma{0}, max{0};
    void anotar(std::uint64_t ns) {
        n.fetch_add(1, std::memory_order_relaxed);
        suma.fetch_add(ns, std::memory_order_relaxed);
        std::uint64_t m = max.load(std::memory_order_relaxed);
        while (ns > m && !max.compare_exchange_weak(m, ns, std::memory_order_relaxed)) {
        }
    }
    void volcar() {
        const std::uint64_t k = n.exchange(0), s = suma.exchange(0), m = max.exchange(0);
        std::fprintf(stderr, "  %-8s %8llu veces, prom %8.3f ms, máx %8.3f ms\n", nombre, static_cast<unsigned long long>(k),
                     k > 0 ? static_cast<double>(s) / static_cast<double>(k) / 1e6 : 0.0, static_cast<double>(m) / 1e6);
    }
};

inline bool activos() {
    static const bool si = [] {
        const char* v = std::getenv("BA_TIEMPOS_AUDIO");
        return v != nullptr && v[0] == '1';
    }();
    return si;
}

inline Tramo vuelta{"vuelta"}, anillo{"anillo"}, cadena{"cadena"}, cola{"cola"}, toma{"toma"}, espera{"espera"};

inline std::uint64_t ahora() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

/// Cada 5 s, desde el hilo común.
inline void volcarCada5s() {
    if (!activos()) return;
    static std::uint64_t ultimo = 0;
    const std::uint64_t t = ahora();
    if (ultimo == 0) ultimo = t;
    if (t - ultimo < 5'000'000'000ULL) return;
    ultimo = t;
    std::fprintf(stderr, "[tiempos del audio]\n");
    for (Tramo* x : {&vuelta, &anillo, &cadena, &cola, &toma, &espera}) x->volcar();
}

} // namespace ba::ipc_gst::tiempos
