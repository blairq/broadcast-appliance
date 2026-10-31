// ============================================================================
// src/core/platform/Ritmo_win.cpp — ver Ritmo.hpp (Windows)
// ============================================================================
#include "platform/Ritmo.hpp"
#include "platform/Energia.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <mutex>
#include <thread>

// Windows 10 1803+. Los encabezados viejos (y algunos de mingw) no lo traen: el valor es el
// del SDK (synchapi.h). Un Windows que no lo conoce rechaza la creación y queda sleep_until.
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace ba::platform {

namespace {

/// El temporizador de ESTE hilo (uno por hilo: el que espera es el que lo arma). Se cierra
/// cuando el hilo termina.
struct Temporizador {
    HANDLE h = nullptr;
    bool probado = false;
    ~Temporizador() {
        if (h != nullptr) CloseHandle(h);
    }
    HANDLE obtener() {
        if (!probado) {
            probado = true;
            h = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        }
        return h;
    }
};

std::once_flag g_proceso;

} // namespace

void dormirHasta(std::chrono::steady_clock::time_point plazo) {
    // Windows 11 ignora la resolución que pide un proceso sin ventana a la vista (el gateway
    // nunca tiene) salvo que se le diga lo contrario: una vez por proceso, el mismo pedido
    // que hacen el motor y el gateway al arrancar (fuera de EcoQoS y respetando el reloj).
    std::call_once(g_proceso, []() { sinAhorroDeEnergia(); });
    thread_local Temporizador t;
    const auto falta = plazo - std::chrono::steady_clock::now();
    if (falta <= std::chrono::steady_clock::duration::zero()) return;
    HANDLE h = t.obtener();
    if (h == nullptr) {
        std::this_thread::sleep_until(plazo);
        return;
    }
    // Relativo y en unidades de 100 ns (negativo = desde ahora).
    LARGE_INTEGER vence;
    vence.QuadPart = -static_cast<LONGLONG>(std::chrono::duration_cast<std::chrono::nanoseconds>(falta).count() / 100);
    if (vence.QuadPart == 0) return;
    if (!SetWaitableTimer(h, &vence, 0, nullptr, nullptr, FALSE)) {
        std::this_thread::sleep_until(plazo);
        return;
    }
    WaitForSingleObject(h, INFINITE);
}

} // namespace ba::platform
