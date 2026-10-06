// test_motor_retornos.cpp — el motor entero: programa, escena al aire y mix-minus
//
// Con el GStreamer del bundle y nuestros plugins (sin ellos: SKIP):
//   - dos entradas globales (un invitado y un micrófono) suenan en el programa;
//   - una de escena suena solo cuando su escena está al aire;
//   - el retorno del invitado en mix_minus lleva todo MENOS su voz, y se lee del
//     anillo como lo lee el gateway (baanillo);
//   - el programa para el gateway (ContentForReturn) lleva todo;
//   - el mute de una entrada vale también en el retorno.
// Los anillos usan otro prefijo: no pisan los de una app andando.

#include "Motor.hpp"

#include <gst/gst.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <unistd.h>

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

bool cerca(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
void esperar(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

/// Lo que se lee de un anillo (como el gateway), en dBFS RMS, durante ~1 s.
double leerAnillo(const std::string& canal) {
    const std::string d = "baanillo canal=\"" + canal + "\" ! level name=n interval=300000000 ! fakesink sync=false";
    GError* e = nullptr;
    GstElement* p = gst_parse_launch(d.c_str(), &e);
    if (e != nullptr) {
        std::printf("leer %s: %s\n", canal.c_str(), e->message);
        g_error_free(e);
        return 99;
    }
    gst_element_set_state(p, GST_STATE_PLAYING);
    GstBus* bus = gst_element_get_bus(p);
    double ultimo = -120;
    const auto fin = std::chrono::steady_clock::now() + std::chrono::milliseconds(1300);
    while (std::chrono::steady_clock::now() < fin) {
        GstMessage* m = gst_bus_timed_pop_filtered(bus, 100 * GST_MSECOND, GST_MESSAGE_ELEMENT);
        if (m == nullptr) continue;
        const GstStructure* s = gst_message_get_structure(m);
        if (gst_structure_has_name(s, "level")) {
            G_GNUC_BEGIN_IGNORE_DEPRECATIONS
            const GValue* v = gst_structure_get_value(s, "rms");
            auto* a = static_cast<GValueArray*>(g_value_get_boxed(v));
            ultimo = g_value_get_double(g_value_array_get_nth(a, 0));
            G_GNUC_END_IGNORE_DEPRECATIONS
        }
        gst_message_unref(m);
    }
    gst_object_unref(bus);
    gst_element_set_state(p, GST_STATE_NULL);
    gst_object_unref(p);
    return std::isfinite(ultimo) ? std::max(ultimo, -120.0) : -120.0;
}

json tono(int frec) {
    return {{"pipeline_audio", "audiotestsrc is-live=true wave=sine volume=0.25 freq=" + std::to_string(frec)}};
}
} // namespace

int main() {
#if defined(BA_TEST_GST_RAIZ)
    setenv("BA_GST_RAIZ", BA_TEST_GST_RAIZ, 1);
#endif
#if defined(BA_TEST_GST_PLUGINS)
    setenv("BA_GST_PLUGINS_EXTRA", BA_TEST_GST_PLUGINS, 1);
#endif
    std::string err;
    if (!iniciarGStreamer(&err)) {
        std::printf("SKIP: sin GStreamer (%s)\n", err.c_str());
        return 77;
    }
    for (const char* e : {"intersink", "audiomixer", "baanillosink", "baanillo", "balimitador"})
        if (!hayFabrica(e)) {
            std::printf("SKIP: falta %s\n", e);
            return 77;
        }
    Motor::Config cfg;
    cfg.prefijoAnillos = "BA.Audio.Prueba" + std::to_string(getpid()) + ".";
    Motor motor(cfg);
    CHECK(motor.iniciar().empty(), "el motor arranca");

    CHECK(motor.agregarEntrada("invitado1", tono(440), {{"global", true}}).empty(), "el invitado");
    CHECK(motor.agregarEntrada("mic", tono(1000), {{"global", true}}).empty(), "el micrófono");
    CHECK(motor.agregarEntrada("video", tono(2000), {{"global", false}}).empty(), "una de escena");
    CHECK(!motor.agregarEntrada("mic", tono(1), {}).empty(), "no dos con el mismo id");
    esperar(1500);
    auto rms = [&]() { return motor.estado()["programa"].value("rms_db", -120.0); };
    std::printf("programa (invitado + mic): %.2f dBFS\n", rms());
    CHECK(cerca(rms(), -12.04, 1.0), "las dos globales suenan; la de escena no (no está al aire)");

    motor.escenaAlAire({"video"}, 200);
    esperar(800);
    std::printf("con la escena al aire: %.2f dBFS\n", rms());
    CHECK(cerca(rms(), -10.28, 1.0), "al aire, la de escena suma (tres senos de 0,25)");
    motor.escenaAlAire({}, 0);
    esperar(600);
    CHECK(cerca(rms(), -12.04, 1.0), "fuera del aire se va");

    CHECK(motor.retorno(1, "mix_minus", "invitado1").empty(), "retorno en mix_minus");
    esperar(800);
    const double ret = leerAnillo(cfg.prefijoAnillos + "Return.1");
    std::printf("retorno del invitado 1 (sin él): %.2f dBFS\n", ret);
    CHECK(cerca(ret, -15.05, 1.0), "el retorno lleva todo menos su voz (solo el micrófono)");
    const double prog = leerAnillo(cfg.prefijoAnillos + "ContentForReturn");
    std::printf("programa para el gateway: %.2f dBFS\n", prog);
    CHECK(cerca(prog, -12.04, 1.0), "el programa para el gateway lleva todo");

    motor.ponerMute("mic", true);
    esperar(400);
    const double retMudo = leerAnillo(cfg.prefijoAnillos + "Return.1");
    std::printf("retorno con el mic en mute: %.2f dBFS\n", retMudo);
    CHECK(retMudo < -60, "el mute vale también en el retorno");
    motor.ponerMute("mic", false);

    // La escena también vale en el retorno.
    motor.escenaAlAire({"video"}, 0);
    esperar(500);
    const double retEscena = leerAnillo(cfg.prefijoAnillos + "Return.1");
    CHECK(cerca(retEscena, -12.04, 1.0), "con la escena al aire, el retorno la oye");

    const json st = motor.estado();
    CHECK(st["retornos"].contains("1"), "el retorno en el estado");
    CHECK(st["entradas"]["mic"].value("global", false), "el estado sabe cuáles son globales");
    CHECK(motor.retorno(1, "programa", "").empty(), "volver a programa");
    CHECK(motor.quitarEntrada("video").empty(), "sacar una entrada");
    CHECK(!motor.quitarEntrada("video").empty(), "sacarla dos veces avisa");

    motor.detener();
    std::printf("== test_motor_retornos: %s ==\n", fallos == 0 ? "OK" : "FALLOS");
    return fallos == 0 ? 0 : 1;
}
