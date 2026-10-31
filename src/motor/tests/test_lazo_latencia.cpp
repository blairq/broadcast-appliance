// test_lazo_latencia.cpp — cuánto tarda un pulso de una entrada hasta cada oído
//
// docs/arquitectura/04-plan-unificado.md, F2-01 y F2-03 (y T4: dos clases de buses).
// Con el GStreamer del bundle y nuestros plugins (sin ellos: SKIP), nada por los parlantes:
// una entrada con pulsos sintéticos (audiotestsrc wave=ticks, bloques de 10 ms como un
// micrófono) y lo que sale se lee de los anillos como lo lee el gateway. El lazo de cada
// pulso es la hora en que se leyó menos la hora de captura de su primera muestra (el sello
// del bloque, que pone baanillosink desde el PTS):
//   - el retorno del invitado (mix-minus, bus de oído) de la salida por omisión y el de
//     otra salida: lo que el bus espera a sus entradas (audio.oidoMs, 20 ms) más el bloque
//     del anillo (10 ms) y unos pocos del limitador y del sink: ~30 ms (el plan pide ≤ 30;
//     la mediana medida en una Mac M: 29-30). El tope es ese, con 2 ms de aire para el
//     sondeo de la prueba (BA_LAZO_TOPE_MS lo cambia; BA_LAZO_OIDO_MS, la espera del oído);
//   - el programa para el gateway (bus de programa) espera la latencia del estudio: más;
//   - la escucha de oído (directo, pfl, afl, selección: F2-03), que sale por un anillo en
//     vez de por una placa: lo del bus de oído más su margen corto (10 ms); la del
//     programa, más que eso;
//   - una entrada que llega tarde al oído (su audio tarda más que lo de oído) se informa.

#include "LectorAnillo.hpp"
#include "Motor.hpp"
#include "ajustes/Ajustes.hpp"
#include "ipc/CanalesSalida.hpp"

#include <gst/gst.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#if defined(_WIN32)
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

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

/// El lazo de los pulsos de `canal` durante 2 s (p50 y p95, ms; -1 si no llegó ninguno).
struct Lazo {
    double p50 = -1, p95 = -1;
    /// La mediana bajo el tope, y el p95 con 5 ms más: se lee mirando cada 1 ms y el
    /// hilo de la prueba no es de tiempo real (lo que se mide es el motor, no el sondeo).
    bool bajo(double tope) const { return p50 > 0 && p50 <= tope && p95 <= tope + 5.0; }
};
Lazo lazo(const std::string& que, const std::string& canal) {
    const prueba::Lectura l = prueba::leer(canal, 2000);
    Lazo r;
    prueba::percentiles(l.lazosMs, r.p50, r.p95);
    std::printf("%-28s %s: %zu pulsos, lazo p50 %.1f ms, p95 %.1f ms\n", que.c_str(), l.abierto ? "abierto" : "SIN ANILLO",
                l.lazosMs.size(), r.p50, r.p95);
    return r;
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
    for (const char* e : {"intersink", "audiomixer", "baanillosink", "balimitador"})
        if (!hayFabrica(e)) {
            std::printf("SKIP: falta %s\n", e);
            return 77;
        }
    if (const char* o = std::getenv("BA_LAZO_OIDO_MS")) ba::ajustes::poner("audio.oidoMs", std::atoi(o));
    const double oidoMs = static_cast<double>(latenciaOidoNs()) / 1e6;
    const double tope = std::getenv("BA_LAZO_TOPE_MS") != nullptr ? std::atof(std::getenv("BA_LAZO_TOPE_MS")) : oidoMs + 12.0;
    Motor::Config cfg;
    cfg.video = false;
    cfg.prefijoAnillos = "BA.Audio.Lazo" + std::to_string(getpid()) + ".";
    // La escucha a un anillo (no a la placa): así se mide lo que oiría el operador.
    cfg.sinkEscucha = "baanillosink canal=\"" + cfg.prefijoAnillos + "Escucha\"";
    Motor motor(cfg);
    CHECK(motor.iniciar().empty(), "el motor arranca");
    motor.reloj().saltearArranque();
    std::string fallo;
    motor.alFallar([&fallo](const std::string&, const std::string& e) { fallo = e; });

    // Un pulso cada 200 ms, en bloques de 10 ms (como una placa con latency-time de 10 ms).
    const json pulsos = {{"pipeline_audio", "audiotestsrc is-live=true wave=ticks tick-interval=200000000 freq=2000 volume=0.8 "
                                            "samplesperbuffer=480"}};
    CHECK(motor.agregarEntrada("pulso", pulsos, {{"global", true}}).empty(), "la entrada de pulsos");
    CHECK(motor.agregarEntrada("invitado1", {{"pipeline_audio", "audiotestsrc is-live=true wave=silence samplesperbuffer=480"}},
                               {{"global", true}})
              .empty(),
          "el invitado (callado)");
    // Una entrada lenta (como una cámara IP con audio): 120 ms de cola antes de entregar.
    CHECK(motor.agregarEntrada("lenta", {{"pipeline_audio", "audiotestsrc is-live=true wave=silence samplesperbuffer=480 ! "
                                                           "queue min-threshold-time=120000000 max-size-time=0 max-size-buffers=0 "
                                                           "max-size-bytes=0"}},
                               {{"global", true}})
              .empty(),
          "una entrada lenta");
    CHECK(motor.crearSalida("b", "B").empty(), "otra salida");
    CHECK(motor.retorno(1, "mix_minus", "invitado1").empty(), "el retorno del invitado 1");
    motor.esperarCola();
    esperar(13000); // las entradas miden su edad (~10 s de ventana)

    // --- Los retornos (buses de oído) contra el programa (bus de programa) ---
    {
        const json st = motor.estado();
        std::printf("latencia del estudio %.1f ms (la marca «%s»); retorno 1: %s; edad del pulso: %s\n",
                    st["estudio"].value("latencia_ms", 0.0), st["estudio"].value("la_marca", std::string()).c_str(),
                    st["retornos"]["1"].dump().c_str(), st["entradas"]["pulso"]["edad_audio"].dump().c_str());
    }
    const Lazo lazoRet = lazo("retorno 1 (por omisión)", cfg.prefijoAnillos + ba::ipc::sufijoRetorno("", 1));
    const Lazo lazoRetB = lazo("retorno 1 de la salida b", cfg.prefijoAnillos + ba::ipc::sufijoRetorno("b", 1));
    const Lazo lazoProg = lazo("programa para el gateway", cfg.prefijoAnillos + "ContentForReturn");
    CHECK(lazoRet.bajo(tope), "el retorno de la por omisión llega en ~30 ms (lo del oído + el bloque)");
    CHECK(lazoRetB.bajo(tope), "el de la otra salida, también");
    CHECK(lazoProg.p50 > lazoRet.p50, "el programa espera más (la latencia del estudio)");

    // --- La escucha de oído (F2-03) ---
    const std::string esc = cfg.prefijoAnillos + "Escucha";
    const double topeEscucha = tope + 10.0;
    CHECK(motor.escuchar("directo").empty(), "escucha directo");
    esperar(600);
    const Lazo lDirecto = lazo("escucha directo", esc);
    CHECK(motor.escuchar("pfl", "pulso").empty(), "escucha pfl");
    esperar(600);
    const Lazo lPfl = lazo("escucha pfl", esc);
    CHECK(motor.escuchar("afl", "pulso", "b").empty(), "escucha afl con el fader de la salida b");
    esperar(600);
    const Lazo lAfl = lazo("escucha afl (salida b)", esc);
    // Marcar una entrada con un solo prendido no lo pisa: la selección vuelve al soltarlo.
    CHECK(motor.ponerEscucha("pulso", true).empty(), "marcada para la selección");
    motor.esperarCola();
    CHECK(motor.estado()["escucha"].value("modo", "") == "afl", "con el afl prendido, marcar no lo pisa");
    CHECK(motor.escuchar("seleccion", "", "b").empty(), "soltar el solo: vuelve la selección (salida b)");
    motor.esperarCola();
    CHECK(motor.estado()["escucha"].value("modo", "") == "seleccion", "la selección, de vuelta");
    esperar(600);
    const Lazo lSel = lazo("escucha selección", esc);
    CHECK(motor.escuchar("programa").empty(), "escucha programa");
    esperar(600);
    const Lazo lProg = lazo("escucha programa", esc);
    CHECK(lDirecto.bajo(topeEscucha), "directo: de oído");
    CHECK(lPfl.bajo(topeEscucha), "pfl: de oído, antes del bus");
    CHECK(lAfl.bajo(topeEscucha), "afl: de oído");
    CHECK(lSel.bajo(topeEscucha), "selección: de oído");
    CHECK(lProg.p50 > lDirecto.p50, "el programa en los auriculares va con el video: más");
    motor.esperarCola();
    CHECK(fallo.empty(), "ninguna orden falló");

    // --- Lo que no entra al oído se informa ---
    const json st = motor.estado();
    std::printf("oído: %s\n", st["oido"].dump().c_str());
    CHECK(st["entradas"]["pulso"].value("entra_al_oido", false), "el pulso (10 ms) entra al oído");
    CHECK(!st["entradas"]["lenta"].value("entra_al_oido", true), "la lenta (120 ms) no entra: se informa");
    bool listada = false;
    for (const auto& x : st["oido"]["no_entran"]) listada = listada || x == "lenta";
    CHECK(listada, "y queda en la lista de las que no entran");
    CHECK(st["retornos"]["1"]["salidas"].contains("b"), "el retorno, por salida en el estado");

    motor.escuchar("apagado");
    motor.detener();
    std::printf("== test_lazo_latencia: %s ==\n", fallos == 0 ? "OK" : "FALLOS");
    return fallos == 0 ? 0 : 1;
}
