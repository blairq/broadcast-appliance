// ============================================================================
// src/app/tests/test_reloj_niveles.cpp — los vúmetros a pedido (ui/RelojNiveles)
//
//   1. Sin nadie que mire, no da vueltas (aunque suene).
//   2. Con alguien mirando y todo en silencio: una vuelta y se duerme (no despierta a la
//      interfaz a 10 Hz).
//   3. Algo suena (lo publica OTRO hilo, como el motor): se despierta y da vueltas
//      mientras se mueva; vuelve el silencio y, con los vúmetros caídos, se duerme.
//   4. Nadie mira: se para y el despertador se desarma (sonar no lo llama).
// ============================================================================

#include "emision/NivelesMotor.hpp"
#include "NivelAudio.hpp"
#include "ui/RelojNiveles.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <atomic>
#include <cstdio>
#include <memory>
#include <thread>

namespace {
int g_fallas = 0;
#define CHECK(c, m)                                                       \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", m, #c, __LINE__);  \
            ++g_fallas;                                                   \
        }                                                                 \
    } while (0)

void atender(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

void ponerDesdeOtroHilo(ba::app::NivelesMotor& canal, float pico) {
    std::thread([&canal, pico]() {
        auto n = std::make_shared<ba::app::NivelesMotor::Niveles>();
        ba::motor::NivelAudio a;
        a.id = "Micrófono 1";
        a.pico[0] = a.pico[1] = pico;
        n->push_back(a);
        canal.ponerNiveles(n);
    }).join();
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    ba::app::NivelesMotor canal;
    ba::app::RelojNiveles reloj(canal, 100);
    // Un vúmetro con caída: 15 dB por vuelta hasta el piso.
    float mostrado = -100;
    reloj.medir([&]() {
        const float ahora = canal.picoEntrada("Micrófono 1");
        const float antes = mostrado;
        mostrado = std::max(ahora, mostrado - 15.0f);
        if (mostrado < -100) mostrado = -100;
        return mostrado != antes;
    });

    // 1.
    ponerDesdeOtroHilo(canal, -20);
    atender(300);
    CHECK(reloj.vueltas() == 0 && !reloj.andando(), "sin nadie mirando, ni una vuelta");
    ponerDesdeOtroHilo(canal, -100);

    // 2.
    QObject vista;
    reloj.usar(&vista, true);
    atender(600);
    CHECK(reloj.vueltas() == 1 && !reloj.andando(), "en silencio: una vuelta y a dormir");

    // 3.
    ponerDesdeOtroHilo(canal, -20);
    atender(550);
    const quint64 conSonido = reloj.vueltas();
    CHECK(conSonido >= 5 && conSonido <= 8 && reloj.andando(), "suena: se despierta y da vueltas (10 Hz)");
    CHECK(mostrado == -20, "el vúmetro muestra lo que suena");
    ponerDesdeOtroHilo(canal, -100);
    atender(1200); // cae 80 dB a 15 por vuelta: ~6 vueltas y se duerme
    CHECK(mostrado == -100 && !reloj.andando(), "vuelve el silencio: el vúmetro cae y se duerme");
    const quint64 dormido = reloj.vueltas();
    atender(500);
    CHECK(reloj.vueltas() == dormido, "dormido, no da vueltas");

    // 4.
    reloj.usar(&vista, false);
    ponerDesdeOtroHilo(canal, -20);
    atender(400);
    CHECK(reloj.vueltas() == dormido && !reloj.andando(), "nadie mira: sonar no lo despierta");
    // Y al volver a mirar con sonido, arranca enseguida.
    reloj.usar(&vista, true);
    atender(50);
    CHECK(reloj.vueltas() == dormido + 1 && reloj.andando(), "vuelve a mirar: la primera vuelta, ya");

    if (g_fallas == 0) std::printf("test_reloj_niveles: OK\n");
    return g_fallas == 0 ? 0 : 1;
}
