// test_motor_reposo.cpp — un lugar vacío en reposo y el audio de un invitado sin hilo propio
//
// Con el GStreamer del bundle y nuestros plugins (sin ellos: SKIP). Hace de gateway: un
// anillo con un seno de 0,5 (como BA.Audio.Guest.<n>). La entrada del motor que lo lee
// (`baanillo canal=…`, como la arma la app) va por el lector común (baanillocomun) y llega
// al programa por baentrega/batoma:
//   - suena en el programa (-9 dBFS) y en su retorno no está (mix-minus);
//   - Motor::ponerLugares con su canal vacío: en reposo (READY), el programa callado;
//   - con alguien de nuevo: vuelve a sonar, y lo que tarda en volver es poco (< 1 s);
//   - un canal que no está en la lista no se toca.

#include "Motor.hpp"
#include "Piezas.hpp"
#include "Reloj.hpp"
#include "ipc/SharedAudioRing.hpp"

#include <gst/gst.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>
#include <thread>
#include <vector>
#if defined(_WIN32)
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

using namespace ba::motor;
using json = nlohmann::json;
namespace ar = ba::ipc::audio_ring;

namespace {
int fallos = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__); \
            ++fallos;                                                        \
        }                                                                    \
    } while (0)

void esperar(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
std::atomic<bool> g_fin{false};

void gateway(const std::string& nombre) {
    ba::ipc::SharedAudioRing ring;
    const std::wstring w(nombre.begin(), nombre.end());
    if (!ba::ipc::SharedAudioRing::init(w.c_str(), 2, ar::kDefaultCapacityBlocks, 1234, 1, ring).ok) {
        std::printf("no se pudo crear el anillo\n");
        return;
    }
    std::vector<float> b(ar::kBlockFrames * 2);
    double fase = 0;
    auto t = std::chrono::steady_clock::now();
    while (!g_fin) {
        for (std::uint32_t k = 0; k < ar::kBlockFrames; ++k) {
            const float v = 0.5f * static_cast<float>(std::sin(fase));
            fase += 2 * std::numbers::pi * 440.0 / ar::kSampleRate;
            b[2 * k] = b[2 * k + 1] = v;
        }
        ring.push(b.data(), static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch()).count()));
        t += std::chrono::milliseconds(10);
        std::this_thread::sleep_until(t);
    }
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
    for (const char* e : {"audiomixer", "baanillosink", "baanillocomun", "baentrega", "batoma", "balimitador"})
        if (!hayFabrica(e)) {
            std::printf("SKIP: falta %s\n", e);
            return 77;
        }
    const std::string canal = "BA.Audio.PruebaReposo" + std::to_string(getpid()) + ".1";
    std::thread g([&]() { gateway(canal); });
    Motor::Config cfg;
    cfg.prefijoAnillos = "BA.Audio.PruebaR" + std::to_string(getpid()) + ".";
    cfg.sinkEscucha = "fakesink sync=true"; // nada por los parlantes
    cfg.video = false;
    Motor motor(cfg);
    CHECK(motor.iniciar().empty(), "el motor arranca");
    CHECK(motor.agregarEntrada("invitado1", {{"pipeline", ""}, {"pipeline_audio", "baanillo canal=" + canal}, {"audio", true},
                                             {"estudio", false}},
                               {{"global", true}})
              .empty(),
          "el invitado");
    // Un lugar que no está en la lista de ponerLugares (otro canal): siempre anda.
    CHECK(motor.agregarEntrada("otro", {{"pipeline_audio", "audiotestsrc is-live=true volume=0"}}, {{"global", true}}).empty(),
          "otra entrada");
    esperar(1500);
    auto rms = [&]() { return motor.estado()["programa"].value("rms_db", -120.0); };
    auto entrada = [&](const char* id) { return motor.estado()["entradas"][id]; };
    std::printf("programa con el invitado: %.2f dBFS (la entrada: %d bloques, pico %.1f dB, %s)\n", rms(),
                entrada("invitado1").value("bloques_audio", 0), entrada("invitado1").value("pico_audio_db", -100.0),
                entrada("invitado1").value("error", std::string()).c_str());
    CHECK(std::fabs(rms() - -9.03) < 1.0, "el invitado suena en el programa");
    CHECK(!entrada("invitado1").value("en_reposo", true), "andando");

    motor.ponerLugares({{canal, false}, {"BA.Guest.1", false}});
    esperar(1000);
    std::printf("lugar vacío: %.2f dBFS, en reposo=%d\n", rms(), entrada("invitado1").value("en_reposo", false) ? 1 : 0);
    CHECK(entrada("invitado1").value("en_reposo", false), "en reposo");
    CHECK(rms() < -60, "en reposo no suena");
    CHECK(!entrada("otro").value("en_reposo", true), "la que no lee un lugar de la lista sigue andando");

    const auto t0 = std::chrono::steady_clock::now();
    motor.ponerLugares({{canal, true}});
    double ms = -1;
    while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3)) {
        if (rms() > -12) {
            ms = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
            break;
        }
        esperar(20);
    }
    std::printf("llegó alguien: suena a los %.0f ms (el medidor promedia ~300 ms)\n", ms);
    CHECK(ms >= 0 && ms < 1000, "vuelve a sonar enseguida");
    CHECK(!entrada("invitado1").value("en_reposo", true), "andando de nuevo");

    motor.detener();
    g_fin = true;
    g.join();
    if (fallos == 0) std::printf("test_motor_reposo: OK\n");
    return fallos == 0 ? 0 : 1;
}
