// src/app/tests/test_estado_aire.cpp — las pastillas del aire (ui/EstadoAireViewModel), sin app
//
// Lo que se prueba (análisis MVC, V-A1/V-A2):
//   - una grabación que ya andaba cuando se arma la vista (se abre la interfaz con la
//     grabación en curso, o se rehace) muestra el tiempo de verdad: el inicio lo anota el
//     dueño en la foto, la interfaz no lo infiere;
//   - con el aire quieto (fotos que no cambian lo que sale) no sale ningún aviso de grupo;
//   - las estadísticas (los cuadros, que el motor mueve en cada vuelta) no despiertan a
//     quien mira qué sale (aireChanged), y salen a lo sumo una vez por segundo;
//   - empezar y terminar avisan aireChanged y los tiempos de una.

#include "emision/FotoMotor.hpp"
#include "emision/VentanillaMotor.hpp"
#include "ui/EstadoAireViewModel.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <cstdio>
#include <memory>

using namespace ba::app;

namespace {

int fallas = 0;

void comprobar(bool ok, const char* que) {
    std::printf("%s %s\n", ok ? "ok  " : "FALLA", que);
    if (!ok) ++fallas;
}

/// Publica `f` como lo haría el dueño y deja que llegue (el aviso va encolado).
void publicar(VentanillaMotor& v, const FotoMotor& f, unsigned partes) {
    v.publicar(std::make_shared<const FotoMotor>(f), partes);
    QCoreApplication::processEvents();
}

void esperar(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    VentanillaMotor v({.encolar = [](std::function<void()> fn) { fn(); }, .alTerminarOrden = []() {}, .adoptar = {}});

    const std::int64_t ahora = 1'800'000'000'000; // la hora de pared del test (fija)
    // La grabación arrancó hace 1 h 02 min 03 s, antes de que existiera la vista.
    FotoMotor f;
    f.estado.graba = true;
    f.estado.haySalida = true;
    f.estado.grabaDesdeMs = ahora - (3600 + 2 * 60 + 3) * 1000;
    f.estado.corteGrabacionPedido = true;
    v.publicar(std::make_shared<const FotoMotor>(f), ~0u);
    v.recibirAhora();

    EstadoAireViewModel aire({.ventanilla = v, .critico = {}, .ahoraMs = [&]() { return ahora; }});
    comprobar(aire.recording(), "graba al armar la vista");
    comprobar(aire.recordTime() == "01:02:03", "el tiempo de una grabación que ya andaba es el de verdad");
    comprobar(aire.recordStartMs() == f.estado.grabaDesdeMs, "recordStartMs sale de la foto");

    int aireN = 0, tiemposN = 0, estadisticasN = 0, avisos = 0;
    QObject::connect(&aire, &EstadoAireViewModel::aireChanged, [&]() { ++aireN; });
    QObject::connect(&aire, &EstadoAireViewModel::tiemposChanged, [&]() { ++tiemposN; });
    QObject::connect(&aire, &EstadoAireViewModel::estadisticasChanged, [&]() { ++estadisticasN; });
    QObject::connect(&aire, &EstadoAireViewModel::notify, [&](const QString&, const QString&) { ++avisos; });

    // El aire quieto: diez fotos iguales (lo que antes hacía el sondeo de 10 Hz).
    for (int i = 0; i < 10; ++i) publicar(v, f, FotoMotor::Estado);
    comprobar(aireN == 0 && estadisticasN == 0, "con el aire quieto no sale ningún aviso");

    // Solo se mueven los cuadros (cada vuelta del motor): estadísticas sí, aire no, y juntas.
    for (int i = 0; i < 10; ++i) {
        f.estado.cuadros += 30;
        publicar(v, f, FotoMotor::Estado);
    }
    comprobar(aireN == 0, "los cuadros no despiertan a quien mira qué sale");
    comprobar(estadisticasN == 1, "diez fotos con cuadros nuevos: un solo aviso de estadísticas en el segundo");
    esperar(1100);
    comprobar(estadisticasN == 2, "lo que quedó pendiente sale al cumplirse el segundo");

    // Termina la grabación (pedida): aire y tiempos ya, y el aviso corto.
    const int tiemposAntes = tiemposN;
    f.estado.graba = false;
    f.estado.haySalida = false;
    f.estado.grabaDesdeMs = 0;
    publicar(v, f, FotoMotor::Estado);
    comprobar(aireN == 1, "terminar avisa aireChanged una vez");
    comprobar(tiemposN > tiemposAntes, "y los tiempos de una (vuelven a 00:00:00)");
    comprobar(aire.recordTime() == "00:00:00", "sin grabar, el reloj en cero");
    comprobar(avisos == 1, "«Grabación detenida»");

    // Una grabación que se cae y el motor rearma conserva su inicio (lo anota el dueño).
    f.estado.graba = true;
    f.estado.haySalida = true;
    f.estado.grabaDesdeMs = ahora - 10'000;
    publicar(v, f, FotoMotor::Estado);
    f.estado.graba = false;
    f.estado.grabacionCaida = true;
    f.estado.corteGrabacionPedido = false;
    publicar(v, f, FotoMotor::Estado);
    comprobar(aire.recordTime() == "00:00:10", "una grabación caída que se rearma sigue con su tiempo");

    std::printf("%s\n", fallas == 0 ? "TODO OK" : "HAY FALLAS");
    return fallas == 0 ? 0 : 1;
}
