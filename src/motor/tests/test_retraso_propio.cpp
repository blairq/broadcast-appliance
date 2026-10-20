// test_retraso_propio.cpp — una entrada «con su propia demora» que manda pocos cuadros
//
// El caso que colgó la app (2026-10-20): los invitados vacíos (la placa a pocos cuadros por
// segundo) en clase «propia». Después de cambiar el retraso se juntaba una ventana nueva de
// edades, pero mientras tanto se sumaba el p90 VIEJO + el retraso NUEVO: cada vuelta pedía
// 30 ms más (30, 60… 510 ms) y con medio segundo de corrimiento se trababan la app y el gateway.
// Acá: 2 cuadros por segundo que tardan 80 ms, propia; el retraso se asienta y no crece, y
// nunca pasa el tope.

#include "Motor.hpp"
#include "Reloj.hpp"

#include <gst/gst.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

using namespace ba::motor;
using json = nlohmann::json;

namespace {
int fallos = 0;
void check(bool ok, const std::string& que) {
    std::printf("%s: %s\n", ok ? "ok" : "FALLA", que.c_str());
    if (!ok) ++fallos;
}
} // namespace

int main() {
#if defined(BA_TEST_GST_RAIZ)
    g_setenv("BA_GST_RAIZ", BA_TEST_GST_RAIZ, FALSE);
#endif
    std::string err;
    if (!iniciarGStreamer(&err)) {
        std::printf("SKIP: sin GStreamer (%s)\n", err.c_str());
        return 77;
    }
    for (const char* e : {"intersink", "compositor", "imagefreeze"})
        if (!hayFabrica(e)) {
            std::printf("SKIP: falta %s\n", e);
            return 77;
        }
    Motor::Config cfg;
    cfg.lienzo.ancho = 320;
    cfg.lienzo.alto = 180;
    cfg.prefijoAnillos = "";
    cfg.sinkEscucha = "fakesink";
    Motor m(cfg);
    check(m.iniciar().empty(), "el motor arranca");
    const json lenta{{"pipeline", "videotestsrc is-live=true ! video/x-raw,width=160,height=90,framerate=2/1 ! identity sleep-time=80000"},
                     {"normalizar", false},
                     {"estudio", false}};
    check(m.agregarEntrada("lenta", lenta).empty(), "una entrada de 2 cps que tarda 80 ms, con su propia demora");
    m.definirEscena({{"nombre", "Una"}, {"capas", {{{"entrada", "lenta"}}}}});
    m.alAire("Una");
    double maximo = 0, antes = -1, ultimo = -1;
    for (int t = 0; t < 50; t += 5) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        const json e = m.estado()["entradas"]["lenta"];
        ultimo = e.value("retraso_propio_ms", 0.0);
        if (t >= 40) antes = antes < 0 ? ultimo : antes;
        maximo = std::max(maximo, ultimo);
        std::printf("t+%2ds: retraso propio %.0f ms, pide %.1f\n", t + 5, ultimo, e.value("pide_ms", 0.0));
    }
    check(maximo <= 300, "nunca pasa el tope (300 ms)");
    check(maximo <= 150, "se queda cerca de lo que hace falta (~60 ms), no crece (máximo " + std::to_string(maximo) + ")");
    check(ultimo == antes, "al final ya no cambia");
    m.detener();
    std::printf("test_retraso_propio: %s\n", fallos == 0 ? "OK" : "FALLAS");
    return fallos == 0 ? 0 : 1;
}
