// ============================================================================
// src/app/tests/test_rack_latencia.cpp — la latencia del rack con un plugin sintético
//
// Con `clap_retardo256` (un CLAP que atrasa 256 muestras y lo declara):
//   1. El rack informa 256 muestras de latencia y el impulso sale en la 256.
//   2. Anulado a mano: la latencia informada no cambia y el impulso SIGUE
//      saliendo en la 256 (la línea de retardo equivalente), sin hueco.
//      (El bypass automático del vigía pasa por el mismo camino.)
//   3. En paralelo con la señal seca: la rama seca se atrasa 256 y las dos
//      llegan juntas (un solo impulso al doble, no dos).
// ============================================================================

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "audio/AudioRack.hpp"

using namespace ba::app::audio;

namespace {
int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (0)

struct Directo : HostServices {
    void runOnMain(std::function<void()> fn) override { fn(); }
    int addTimer(int, std::function<void()>) override { return 1; }
    void removeTimer(int) override {}
    bool addFd(int, uint32_t, std::function<void(uint32_t)>) override { return true; }
    void removeFd(int) override {}
};

/// Pasa un impulso (en la muestra `en` de una corrida de 4800, bloques de 480)
/// y devuelve dónde salió el mayor valor y cuánto vale.
std::pair<int, float> impulso(AudioRack& rack, int en = 0) {
    std::vector<float> l(480), r(480);
    float* planos[2] = {l.data(), r.data()};
    int donde = -1;
    float mayor = 0;
    for (int b = 0; b < 10; ++b) {
        for (int i = 0; i < 480; ++i) l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] = (b * 480 + i == en) ? 1.0f : 0.0f;
        rack.process(planos, 2, 480);
        for (int i = 0; i < 480; ++i)
            if (std::fabs(l[static_cast<size_t>(i)]) > mayor) {
                mayor = std::fabs(l[static_cast<size_t>(i)]);
                donde = b * 480 + i;
            }
    }
    return {donde, mayor};
}
} // namespace

int main() {
    const char* ruta = std::getenv("BA_CLAP_RETARDO");
    if (ruta == nullptr) ruta = BA_TEST_CLAP_RETARDO;
    std::string err;
    auto lib = ClapLibrary::open(ruta, &err);
    if (!lib) {
        std::printf("SKIP: %s (%s)\n", err.c_str(), ruta);
        return 77;
    }
    Directo svc;
    AudioRack rack(lib, &svc, 48000);
    const std::string id = rack.addPlugin("ba.prueba.retardo256", "Retardo", "", &err);
    CHECK(!id.empty(), "agrega el plugin sintético");
    CHECK(rack.plugin(id) != nullptr && rack.plugin(id)->latencia() == 256, "el plugin declara 256 muestras");

    // 1.
    CHECK(rack.latenciaMuestras() == 256, "el rack informa 256 muestras");
    auto [d1, v1] = impulso(rack, 1000);
    std::printf("con el módulo: impulso en %d (%.2f)\n", d1, v1);
    CHECK(d1 == 1256 && v1 > 0.99f, "el impulso sale 256 muestras después");

    // 2.
    CHECK(rack.setBypass(id, true), "se anula a mano");
    CHECK(rack.latenciaMuestras() == 256, "anulado, la latencia informada no cambia");
    auto [d2, v2] = impulso(rack, 1000);
    std::printf("anulado: impulso en %d (%.2f)\n", d2, v2);
    CHECK(d2 == 1256 && v2 > 0.99f, "anulado sale con el mismo retardo (línea equivalente)");
    rack.setBypass(id, false);

    // 3. En paralelo: in -> out directo además del módulo. Las dos ramas llegan
    // juntas (la seca atrasada 256) y suman.
    CHECK(rack.connect("in", "out", "out", "in", &err), "cable directo in -> out");
    CHECK(rack.latenciaMuestras() == 256, "la rama más lenta manda");
    auto [d4, v4] = impulso(rack, 1000);
    std::printf("en paralelo: impulso en %d (%.2f)\n", d4, v4);
    CHECK(d4 == 1256 && v4 > 1.99f, "las dos ramas llegan juntas (un impulso al doble)");
    rack.setBypass(id, true);
    auto [d5, v5] = impulso(rack, 1000);
    CHECK(d5 == 1256 && v5 > 1.99f, "y anulado también: sin filtro peine");

    if (g_fallos == 0) std::printf("test_rack_latencia: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
