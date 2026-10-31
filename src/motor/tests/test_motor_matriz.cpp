// test_motor_matriz.cpp — los buses de la Matriz en el motor (F3-08)
//
// docs/arquitectura/04-plan-unificado.md, F3-08. Con el GStreamer del bundle y señales
// sintéticas (senos; nada por los parlantes: la «placa» es un fakesink):
//   - un bus con un envío suena con el nivel de la entrada (un seno de 0,25: −15 dBFS RMS);
//   - el pan es una ganancia por canal: todo a la izquierda, la derecha callada; con
//     nivel y pan a la vez, cada canal con lo suyo (la ley de la Matriz: potencia constante);
//   - dos envíos suman; sacar uno no corta al otro;
//   - el mute de la entrada vale en el bus (como valía en la toma de la app);
//   - con placa sale por ella (una Escucha de oído: suena en el sink de prueba); con una
//     placa que no está, muda (no va a la del sistema: un cable virtual que se fue no
//     termina en los parlantes);
//   - una entrada virtual lee el productor del bus (intersrc) y suena como él;
//   - quitar el bus lo saca de los niveles y del estado.

#include "Motor.hpp"

#include <gst/gst.h>

#include <chrono>
#include <cmath>
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

bool cerca(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
void esperar(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

json tono(int frec) {
    return {{"pipeline_audio", "audiotestsrc is-live=true wave=sine volume=0.25 freq=" + std::to_string(frec)}};
}

/// El RMS (dBFS) de cada canal de un bus o una entrada, de lo que publica el motor para los
/// vúmetros: la media de varias lecturas (el `level` mide de a 50 ms).
struct Canales {
    double izq = -100, der = -100;
};
Canales nivel(Motor& m, const std::string& id) {
    double l = 0, r = 0;
    int n = 0;
    for (int i = 0; i < 8; ++i) {
        esperar(60);
        const auto v = m.niveles();
        if (!v) continue;
        for (const auto& x : *v)
            if (x.id == id) {
                l += std::pow(10.0, x.rms[0] / 10.0);
                r += std::pow(10.0, x.rms[1] / 10.0);
                ++n;
            }
    }
    if (n == 0) return {};
    return {10.0 * std::log10(std::max(l / n, 1e-10)), 10.0 * std::log10(std::max(r / n, 1e-10))};
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
    for (const char* e : {"intersink", "intersrc", "audiomixer", "audiopanorama"})
        if (!hayFabrica(e)) {
            std::printf("SKIP: falta %s\n", e);
            return 77;
        }
    Motor::Config cfg;
    cfg.sinkEscucha = "fakesink sync=true"; // la «placa»: nada por los parlantes
    Motor motor(cfg);
    CHECK(motor.iniciar().empty(), "el motor arranca");
    CHECK(motor.agregarEntrada("a", tono(440), {{"global", true}}).empty(), "la entrada a (seno de 0,25)");
    CHECK(motor.agregarEntrada("b", tono(1000), {{"global", true}}).empty(), "la entrada b (seno de 0,25)");
    esperar(1000);

    // Un envío, al centro y a 0 dB.
    CHECK(motor.matrizBus("s1", {{"a", 1.0, 1.0}}, "").empty(), "el bus s1 con a");
    esperar(800);
    Canales c = nivel(motor, "matriz:s1");
    std::printf("s1 = a: %.2f | %.2f dBFS\n", c.izq, c.der);
    CHECK(cerca(c.izq, -15.05, 1.0) && cerca(c.der, -15.05, 1.0), "un envío: el nivel de la entrada en los dos canales");

    // Todo a la izquierda (la ley de la Matriz: pan −1 → izq √2, der 0).
    motor.matrizBus("s1", {{"a", 1.41421356, 0.0}}, "");
    esperar(500);
    c = nivel(motor, "matriz:s1");
    std::printf("s1 pan izq: %.2f | %.2f dBFS\n", c.izq, c.der);
    CHECK(cerca(c.izq, -12.04, 1.0) && c.der < -60, "pan a la izquierda: +3 dB a la izquierda, la derecha callada");

    // Nivel y pan a la vez: izq 0,5 (−6 dB), der 0,25 (−12 dB).
    motor.matrizBus("s1", {{"a", 0.5, 0.25}}, "");
    esperar(500);
    c = nivel(motor, "matriz:s1");
    std::printf("s1 izq 0,5 der 0,25: %.2f | %.2f dBFS\n", c.izq, c.der);
    CHECK(cerca(c.izq, -21.07, 1.0) && cerca(c.der, -27.09, 1.0), "una ganancia por canal (cualquier par)");

    // Dos envíos suman (dos senos de distinta frecuencia: +3 dB).
    motor.matrizBus("s1", {{"a", 1.0, 1.0}, {"b", 1.0, 1.0}}, "");
    esperar(500);
    c = nivel(motor, "matriz:s1");
    std::printf("s1 = a + b: %.2f dBFS\n", c.izq);
    CHECK(cerca(c.izq, -12.04, 1.0), "dos envíos suman");
    motor.matrizBus("s1", {{"b", 1.0, 1.0}}, "");
    esperar(500);
    c = nivel(motor, "matriz:s1");
    CHECK(cerca(c.izq, -15.05, 1.0), "sacar uno no corta al otro");

    // El mute de la entrada vale en el bus.
    motor.ponerMute("b", true);
    esperar(500);
    c = nivel(motor, "matriz:s1");
    std::printf("s1 con b en mute: %.2f dBFS\n", c.izq);
    CHECK(c.izq < -60, "el mute de la entrada vale en el bus");
    motor.ponerMute("b", false);

    // Una placa que no está: muda, sin ir a la del sistema.
    CHECK(motor.matrizBus("s1", {{"b", 1.0, 1.0}}, "placa-que-no-esta").empty(), "s1 a una placa que no está");
    esperar(1000);
    json st = motor.estado()["matriz"]["s1"];
    std::printf("s1 a una placa que no está: %s\n", st["salida"].dump().c_str());
    CHECK(!st["salida"].value("anda", true) && !st["salida"].value("en_respaldo", true), "sin la placa, muda (no a la del sistema)");
    // Con placa (de prueba, el sink falso como «la del sistema»): sale por ella.
    CHECK(motor.matrizBus("s1", {{"b", 1.0, 1.0}}, "default").empty(), "s1 sale por la placa del sistema");
    esperar(1500);
    st = motor.estado()["matriz"]["s1"];
    std::printf("s1 a la placa: %s\n", st["salida"].dump().c_str());
    CHECK(st["salida"].value("anda", false), "la salida a la placa anda");
    CHECK(st["salida"].value("de_oido", false), "como una escucha de oído (no espera al video)");
    CHECK(st["salida"].value("pico_db", -120.0) > -20, "por la placa suena");

    // Una entrada virtual: lee el productor del bus.
    CHECK(motor.matrizBus("e1", {{"a", 1.0, 1.0}}, "").empty(), "el bus e1 (para una entrada virtual)");
    CHECK(motor.agregarEntrada("virtual", json{{"pipeline_audio", "intersrc producer-name=" + Motor::productorMatriz("e1")}},
                               {{"global", true}})
              .empty(),
          "la entrada virtual");
    esperar(1500);
    c = nivel(motor, "virtual");
    std::printf("la entrada virtual: %.2f dBFS\n", c.izq);
    CHECK(cerca(c.izq, -15.05, 1.5), "la entrada virtual suena como su bus");

    // Quitar.
    motor.quitarMatrizBus("s1");
    motor.esperarCola();
    esperar(200);
    bool esta = false;
    if (const auto v = motor.niveles())
        for (const auto& x : *v) esta = esta || x.id == "matriz:s1";
    CHECK(!esta && !motor.estado()["matriz"].contains("s1"), "quitado, no está");
    CHECK(faltasDelCandado() == 0, "nada de la matriz corrió sin el candado");

    motor.detener();
    if (fallos == 0) std::printf("== test_motor_matriz: OK ==\n");
    return fallos == 0 ? 0 : 1;
}
