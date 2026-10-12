// test_prueba_eco.cpp — el análisis de la prueba técnica (T33): un eco del tono vuelve
// con el código a cierta demora; un ruido periódico de 1 kHz (el pitido del micrófono
// falso de Chrome, una alarma) no tiene que parecer eco.

#include <cmath>
#include <cstdio>
#include <vector>

#include "MediaPump.hpp"

using ba::gateway::MediaPump;

namespace {
int g_fallas = 0;
#define CHECK(c, m)                                             \
    do {                                                        \
        if (!(c)) {                                             \
            std::printf("FALLO: %s (linea %d)\n", m, __LINE__); \
            ++g_fallas;                                         \
        } else {                                                \
            std::printf("ok: %s\n", m);                         \
        }                                                       \
    } while (0)
} // namespace

int main() {
    const int dur = 4000;
    const int n = (dur + 600) / 20;
    // 1) Eco: el código del tono, atrasado 300 ms, 20 dB arriba del piso.
    std::vector<double> eco(static_cast<size_t>(n), 1e-6);
    for (int b = 0; b < n; ++b) {
        const long origen = b - 15; // 300 ms
        if (origen >= 0 && origen * 20 < dur && MediaPump::codigoPrueba((origen * 20) / 250)) eco[static_cast<size_t>(b)] = 1e-4;
    }
    const auto r1 = MediaPump::analizarEco(eco, dur);
    std::printf("  eco: %s\n", r1.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).c_str());
    CHECK(r1.value("eco", false) && std::abs(r1.value("demora_ms", 0) - 300) <= 20, "el eco del código se detecta, con su demora");

    // 2) Un pitido de 1 kHz cada segundo (200 ms prendido): no es eco.
    std::vector<double> pitido(static_cast<size_t>(n), 1e-6);
    for (int b = 0; b < n; ++b)
        if ((b * 20) % 1000 < 200) pitido[static_cast<size_t>(b)] = 1e-4;
    const auto r2 = MediaPump::analizarEco(pitido, dur);
    std::printf("  pitido: %s\n", r2.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).c_str());
    CHECK(!r2.value("eco", true), "un pitido periódico no se confunde con eco");

    // 3) Silencio parejo: tampoco.
    std::vector<double> nada(static_cast<size_t>(n), 1e-6);
    CHECK(!MediaPump::analizarEco(nada, dur).value("eco", true), "silencio: sin eco");

    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
