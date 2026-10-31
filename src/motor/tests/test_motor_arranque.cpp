// test_motor_arranque.cpp — cuánto tarda el motor desde cero hasta el primer cuadro real (F3-10)
//
// La retoma de A (docs/arquitectura/04-plan-unificado.md, F3-10: < 3 s de negro) es,
// del lado del motor: iniciar GStreamer (con el registro de plugins ya escaneado),
// decidir la GPU (la autoprueba), armar el motor y que la escena al aire llegue al
// programa. Lo mide etapa por etapa, en un proceso nuevo como el que relanza el
// launcher, y falla si el total pasa de 3 s. Lo demás de la retoma (que el supervisor
// se dé cuenta, las fuentes de verdad, la reconexión RTMP) lo mide el banco de caos
// (tools/caos/banco.py, kill_a).
//
// Un lienzo de 1920×1080 con una entrada de color de 1920×1080 (como un fondo): lo
// que importa es el arranque de las piezas, no la fuente.

#include "Motor.hpp"
#include "Piezas.hpp"

#include <gst/gst.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
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
        }                                                                            \
    } while (0)

using Hora = std::chrono::steady_clock;
double ms(Hora::time_point a, Hora::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

/// ¿El centro del cuadro es (casi) ese color?
bool esColor(const Cuadro& c, int r, int g, int b) {
    if (c.bgra.empty() || c.ancho <= 0) return false;
    const std::uint8_t* p = c.bgra.data() + static_cast<std::size_t>(c.alto / 2) * c.stride + static_cast<std::size_t>(c.ancho / 2) * 4;
    return std::abs(p[2] - r) < 40 && std::abs(p[1] - g) < 40 && std::abs(p[0] - b) < 40;
}
} // namespace

int main() {
#if defined(BA_TEST_GST_RAIZ)
    g_setenv("BA_GST_RAIZ", BA_TEST_GST_RAIZ, FALSE);
#endif
    const auto t0 = Hora::now();
    std::string err;
    if (!iniciarGStreamer(&err)) {
        std::printf("SKIP: sin GStreamer (%s)\n", err.c_str());
        return 77;
    }
    const auto tGst = Hora::now();
    for (const char* e : {"intersink", "compositor", "audiomixer"})
        if (!hayFabrica(e)) {
            std::printf("SKIP: falta %s\n", e);
            return 77;
        }
    ponerGpuVideo(true); // la autoprueba (lo primero que pide gpu())
    const auto tGpu = Hora::now();
    Motor::Config cfg;
    cfg.lienzo.ancho = 1920;
    cfg.lienzo.alto = 1080;
    cfg.prefijoAnillos = "";
    cfg.sinkEscucha = "fakesink";
    Motor m(cfg);
    CHECK(m.iniciar().empty(), "el motor arranca");
    const auto tMotor = Hora::now();
    CHECK(m.agregarEntrada("fondo", json{{"tipo", "color"}, {"color", "#2050E0"}, {"ancho", 1920}, {"alto", 1080}}).empty(), "el fondo");
    m.definirEscena({{"nombre", "Al aire"}, {"capas", {{{"entrada", "fondo"}}}}});
    m.alAire("Al aire");
    CHECK(m.verPedir("programa", 640, 360).empty(), "ver el programa");
    Cuadro c;
    std::uint64_t serie = 0;
    bool llego = false;
    const auto tope = Hora::now() + std::chrono::seconds(10);
    while (!llego && Hora::now() < tope) {
        if (m.verUltimo("programa", c, serie) && esColor(c, 0x20, 0x50, 0xE0)) llego = true;
        else std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto tCuadro = Hora::now();
    CHECK(llego, "el primer cuadro real (el color de la escena) llega al programa");
    const double total = ms(t0, tCuadro);
    std::printf("arranque del motor (GPU: %s): GStreamer %.0f ms, autoprueba de la GPU %.0f ms, motor %.0f ms, "
                "escena → primer cuadro real %.0f ms; total %.0f ms\n",
                nombreGpu(), ms(t0, tGst), ms(tGst, tGpu), ms(tGpu, tMotor), ms(tMotor, tCuadro), total);
    CHECK(total < 3000, "del proceso nuevo al primer cuadro real, menos de 3 s");
    m.detener();
    if (fallos == 0) std::printf("OK\n");
    return fallos == 0 ? 0 : 1;
}
