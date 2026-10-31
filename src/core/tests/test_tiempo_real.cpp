// ============================================================================
// src/core/tests/test_tiempo_real.cpp — tiempo real para los hilos de audio con plazo
//
//   1. Un hilo pide tiempo real: queda contado (y en macOS, donde no hacen
//      falta permisos, tiene que poder); al soltarlo, deja de contar. Pedirlo
//      dos veces no cuenta dos.
//   2. Un hilo que termina subido sin soltarlo también deja de contar.
//   3. Con la CPU llena (un hilo ocupado por núcleo, y el doble), un bucle de
//      10 ms en tiempo real se despierta a tiempo: se informa el atraso p99 y
//      el máximo con y sin tiempo real. Solo se exige donde el sistema lo dio.
// ============================================================================

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

#include "platform/Ritmo.hpp"
#include "platform/TiempoReal.hpp"

using namespace ba::platform;
using Reloj = std::chrono::steady_clock;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (0)

struct Atraso {
    double p99 = 0, max = 0;
};

/// 300 vueltas de 10 ms (3 s): el atraso de cada despertar respecto de su plazo.
Atraso bucle(bool rt) {
    Atraso a;
    std::thread([&] {
        if (rt) tiempoRealAudio(10);
        std::vector<double> us;
        auto plazo = Reloj::now();
        for (int i = 0; i < 300; ++i) {
            plazo += std::chrono::milliseconds(10);
            dormirHasta(plazo);
            us.push_back(std::chrono::duration<double, std::micro>(Reloj::now() - plazo).count());
            // Un poco de trabajo, como mezclar un bloque.
            volatile double x = 0;
            for (int k = 0; k < 20000; ++k) x = x + k * 0.5;
        }
        if (rt) soltarTiempoReal();
        std::sort(us.begin(), us.end());
        a.p99 = us[us.size() * 99 / 100];
        a.max = us.back();
    }).join();
    return a;
}

} // namespace

int main() {
    // 1.
    bool subio = false;
    std::thread([&] {
        subio = tiempoRealAudio(10);
        const auto e = estadoTiempoReal();
        std::printf("tiempo real: %s (modo «%s», %d hilos, %d fallidos)\n", subio ? "sí" : "no", e.modo.c_str(), e.hilos,
                    e.fallidos);
        if (subio) {
            CHECK(e.hilos == 1, "un hilo en tiempo real");
            CHECK(tiempoRealAudio(10), "pedirlo de nuevo no falla");
            CHECK(estadoTiempoReal().hilos == 1, "ni cuenta dos");
            soltarTiempoReal();
            CHECK(estadoTiempoReal().hilos == 0, "al soltarlo deja de contar");
            soltarTiempoReal();
            CHECK(estadoTiempoReal().hilos == 0, "soltarlo dos veces no descuenta de más");
        } else {
            CHECK(e.fallidos == 1, "lo que no se pudo queda contado");
        }
    }).join();
#if defined(__APPLE__)
    CHECK(subio, "en macOS el time constraint no pide permisos");
#endif

    // 2.
    if (subio) {
        std::thread([] { tiempoRealAudio(10); }).join();
        CHECK(estadoTiempoReal().hilos == 0, "un hilo que termina subido deja de contar");
    }

    // 3. Carga sintética: dos hilos ocupados por núcleo.
    std::atomic<bool> parar{false};
    std::vector<std::thread> carga;
    const unsigned n = std::max(2u, std::thread::hardware_concurrency()) * 2;
    for (unsigned i = 0; i < n; ++i)
        carga.emplace_back([&] {
            volatile double x = 0;
            while (!parar.load(std::memory_order_relaxed)) x = x + 1.0;
        });
    const Atraso normal = bucle(false);
    const Atraso real = bucle(true);
    parar = true;
    for (auto& t : carga) t.join();
    std::printf("bucle de 10 ms con %u hilos ocupados: sin tiempo real p99 %.0f us, max %.0f us; con tiempo real p99 "
                "%.0f us, max %.0f us\n",
                n, normal.p99, normal.max, real.p99, real.max);
    if (subio) CHECK(real.p99 < 2000.0, "en tiempo real se despierta a tiempo aun con la CPU llena (p99 < 2 ms)");

    if (g_fallos == 0) std::printf("test_tiempo_real: todo bien\n");
    return g_fallos == 0 ? 0 : 1;
}
