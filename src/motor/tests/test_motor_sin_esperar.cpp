// test_motor_sin_esperar.cpp — el motor no espera a GStreamer con su candado tomado
//
// El 2026-11-01 la app se clavó 16 s: el motor armaba la foto con su candado tomado y, por
// cada entrada, buscaba sus filtros por nombre (gst_bin_get_by_name, el candado del bin) y
// leía su bus en el tic; con GStreamer en fila, todo el motor esperaba. Acá otro hilo toma
// el candado del pipeline de una entrada (con un filtro) durante 3 s y:
//   - las órdenes al motor siguen saliendo (un fader de otra entrada, esperarCola);
//   - la foto del motor (instantanea) se sigue publicando;
//   - el programa sigue sonando (el pico del bus).
// Al soltarlo, el estado de la entrada vuelve a estar al día.

#include "Motor.hpp"
#include "Reloj.hpp"

#include <gst/gst.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

using namespace ba::motor;
using json = nlohmann::json;

namespace {
int fallos = 0;
#define CHECK(cond, msg)                                                             \
    do {                                                                             \
        if (!(cond)) {                                                               \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);         \
            ++fallos;                                                                \
        } else {                                                                     \
            std::printf("ok: %s\n", msg);                                            \
        }                                                                            \
    } while (0)

void esperar(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
std::int64_t ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

int main() {
#if defined(BA_TEST_GST_RAIZ)
    g_setenv("BA_GST_RAIZ", BA_TEST_GST_RAIZ, FALSE);
#endif
#if defined(BA_TEST_GST_PLUGINS)
    g_setenv("BA_GST_PLUGINS_EXTRA", BA_TEST_GST_PLUGINS, FALSE);
#endif
    std::string err;
    if (!iniciarGStreamer(&err)) {
        std::printf("SKIP: sin GStreamer (%s)\n", err.c_str());
        return 77;
    }
    Motor::Config cfg;
    cfg.prefijoAnillos = ""; // sin gateway
    cfg.sinkEscucha = "fakesink sync=true";
    cfg.video = false;
    Motor motor(cfg);
    CHECK(motor.iniciar().empty(), "el motor arranca");
    const json tono = {{"pipeline_audio", "audiotestsrc is-live=true wave=sine volume=0.25 freq=440"},
                       {"filtros_audio", json::array({{{"id", "vol"}, {"elemento", "volume"}}})}};
    CHECK(motor.agregarEntrada("trabada", tono, {{"global", true}}).empty(), "una entrada con un filtro");
    CHECK(motor.agregarEntrada("otra", {{"pipeline_audio", "audiotestsrc is-live=true wave=sine volume=0.25 freq=660"}},
                               {{"global", true}})
              .empty(),
          "y otra");
    motor.esperarCola();
    esperar(1500);
    CHECK(motor.estado()["entradas"]["trabada"].value("filtros", json::array()).size() == 1, "el estado ve su filtro");

    GstElement* p = motor.pipelineDeEntrada("trabada");
    CHECK(p != nullptr, "su pipeline");
    if (p == nullptr) return 1;
    std::atomic<bool> tomado{false};
    std::thread traba([&]() {
        GST_OBJECT_LOCK(p); // lo que toman gst_bin_get_by_name y gst_element_get_bus
        tomado = true;
        esperar(3000);
        GST_OBJECT_UNLOCK(p);
        tomado = false;
    });
    while (!tomado.load()) esperar(5);

    const auto foto0 = motor.instantanea();
    const std::int64_t t0 = ms();
    motor.ponerNivel("otra", 0.5);
    motor.esperarCola();
    const std::int64_t orden = ms() - t0;
    std::printf("una orden con el candado de la entrada tomado: %lld ms\n", static_cast<long long>(orden));
    CHECK(orden < 1000, "las órdenes siguen saliendo");
    bool fotoNueva = false;
    for (int i = 0; i < 40 && tomado.load() && !fotoNueva; ++i) {
        esperar(50);
        fotoNueva = motor.instantanea() != foto0;
    }
    CHECK(fotoNueva, "la foto del motor se sigue publicando");
    const auto foto = motor.instantanea();
    const double pico = foto ? (*foto)["programa"].value("pico_db", -120.0) : -120.0;
    std::printf("pico del programa con la entrada trabada: %.1f dB\n", pico);
    CHECK(pico > -40, "el programa sigue sonando");
    traba.join();
    gst_object_unref(p);

    esperar(800);
    CHECK(motor.estado()["entradas"]["trabada"].value("anda", false), "al soltarlo, la entrada al día");
    motor.detener();
    std::printf("== test_motor_sin_esperar: %s ==\n", fallos == 0 ? "OK" : "FALLOS");
    return fallos == 0 ? 0 : 1;
}
