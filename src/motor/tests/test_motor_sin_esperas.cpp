// ============================================================================
// src/motor/tests/test_motor_sin_esperas.cpp — la interfaz nunca espera al motor
//
// docs/planes/PLAN-MOTOR-SIN-ESPERAS.md: el hilo de la interfaz (acá, el del
// test, marcado con marcarHiloInterfaz) llama a toda la API del motor —también
// con una cámara de red que no contesta, que tarda decenas de segundos en
// desarmarse— y ninguna llamada toma el candado del motor ni tarda.
// ============================================================================
#include "Candado.hpp"
#include "Motor.hpp"

#include <gst/gst.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

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

void esperar(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

/// Corre `f` midiendo cuánto tardó; se queda con la peor.
double peorMs = 0;
std::string peorQue;
template <typename F> void medir(const char* que, F&& f) {
    const auto t0 = std::chrono::steady_clock::now();
    f();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (ms > peorMs) {
        peorMs = ms;
        peorQue = que;
    }
}

/// esperarCola desde OTRO hilo (la interfaz no espera).
void asentar(Motor& m) {
    std::thread([&m]() { m.esperarCola(); }).join();
    esperar(300);
}

json color(const char* c) { return {{"tipo", "color"}, {"color", c}, {"ancho", 320}, {"alto", 180}}; }
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
    CHECK(m.iniciar().empty(), "el motor arranca"); // arrancar sí (detrás del cartel de carga)
    marcarHiloInterfaz();
    const int antes = tomasDeLaInterfaz();

    medir("agregarEntrada", [&]() { m.agregarEntrada("rojo", color("#FF0000")); });
    medir("agregarEntrada", [&]() { m.agregarEntrada("verde", color("#00FF00")); });
    medir("agregarEntrada (app)", [&]() { m.agregarEntrada("lienzo", json{{"tipo", "app"}}); });
    medir("agregarEntrada (audio)", [&]() {
        m.agregarEntrada("tono", json{{"pipeline_audio", "audiotestsrc is-live=true wave=sine volume=0.25 freq=440"}},
                         {{"global", true}});
    });
    // Una cámara de red que no contesta: armarla y desarmarla tarda lo que tarde
    // la conexión en rendirse. Nadie tiene que esperarla.
    medir("agregarEntrada (red muerta)", [&]() {
        m.agregarEntrada("muerta", json{{"uri", "rtsp://10.255.255.1:554/nada"}});
    });
    medir("definirEscena", [&]() {
        m.definirEscena({{"nombre", "A"}, {"capas", {{{"entrada", "rojo"}}, {{"entrada", "muerta"}}}}});
    });
    medir("definirEscena", [&]() { m.definirEscena({{"nombre", "B"}, {"capas", {{{"entrada", "verde"}}}}}); });
    medir("alAire", [&]() { m.alAire("A"); });
    medir("armar", [&]() { m.armar("B"); });
    medir("verPedir", [&]() { m.verPedir("escena:B@prueba", 160, 90); });
    medir("verAvisar", [&]() { m.verAvisar("escena:B@prueba", [](const Cuadro&) {}); });
    medir("ponerNivel", [&]() { m.ponerNivel("tono", 0.5); });
    medir("ponerMute", [&]() { m.ponerMute("tono", false); });
    medir("masterVolumen", [&]() { m.masterVolumen(1.0, 0); });
    medir("escuchar", [&]() { m.escuchar("programa"); });
    int toma = 0;
    medir("escucharEntrada", [&]() { toma = m.escucharEntrada("tono", [](const float*, std::size_t) {}); });
    medir("tomaViva", [&]() { (void)m.tomaViva(toma); });
    asentar(m);
    std::vector<std::uint8_t> azul(160u * 90u * 4u, 0);
    for (std::size_t i = 0; i < azul.size(); i += 4) azul[i] = 255;
    medir("empujarCuadro", [&]() { m.empujarCuadro("lienzo", azul.data(), 160, 90, 160 * 4); });
    medir("propiedadesEntrada", [&]() { (void)m.propiedadesEntrada("rojo", "ba_f_0"); });
    medir("medio", [&]() { (void)m.medio("rojo"); });
    medir("tbarHacia", [&]() { (void)m.tbarHacia(); });
    medir("instantanea", [&]() { (void)m.instantanea(); });
    medir("tbar", [&]() { m.tbar("B", 0.3, false); });
    medir("tbar", [&]() { m.tbar("B", 1.0, true); });
    medir("verAvisar (soltar)", [&]() { m.verAvisar("escena:B@prueba", nullptr); });
    medir("verSoltar", [&]() { m.verSoltar("escena:B@prueba"); });
    medir("dejarDeEscucharEntrada", [&]() { m.dejarDeEscucharEntrada(toma); });
    // Sacar la cámara muerta: desarmarla es lo que más tarda.
    medir("quitarEntrada (red muerta)", [&]() { m.quitarEntrada("muerta"); });
    medir("quitarEntrada", [&]() { m.quitarEntrada("verde"); });
    asentar(m);

    const int tomas = tomasDeLaInterfaz() - antes;
    std::printf("la interfaz tomó el candado %d veces; la llamada más lenta: %s, %.1f ms\n", tomas, peorQue.c_str(),
                peorMs);
    CHECK(tomas == 0, "la interfaz no toma el candado del motor");
    CHECK(peorMs < 50.0, "ninguna llamada de la interfaz tarda (< 50 ms)");
    const auto foto = m.instantanea();
    CHECK(foto && foto->contains("entradas") && (*foto)["entradas"].contains("rojo"), "la foto tiene las entradas");
    m.detener(); // apagar sí
    if (fallos == 0) std::printf("== test_motor_sin_esperas: OK ==\n");
    return fallos == 0 ? 0 : 1;
}
