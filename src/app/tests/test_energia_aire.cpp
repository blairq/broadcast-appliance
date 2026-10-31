// ============================================================================
// src/app/tests/test_energia_aire.cpp — el corte de luz con estados falsos (F1-11)
//
//   1. Con red: nada.
//   2. En batería con mucha carga: solo avisa (no cierra nada).
//   3. Al 15 %: cierra las grabaciones UNA vez (EOS ordenado: la acción de
//      AppRuntime) y avisa; `estado()` dice emergencia.
//   4. Vuelve la red: retoma las grabaciones.
//   5. Los minutos que quedan también disparan (con la política de fábrica).
// ============================================================================
#include "arranque/EnergiaAire.hpp"

#include <QCoreApplication>

#include <cstdio>

namespace {
int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (0)

ba::platform::EstadoEnergia estado(bool enBateria, int pct, int min = -1) {
    ba::platform::EstadoEnergia e;
    e.conocido = true;
    e.hayBateria = true;
    e.enBateria = enBateria;
    e.porcentaje = pct;
    e.minutos = min;
    e.fuente = "ups";
    return e;
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int cerradas = 0, retomadas = 0, avisos = 0;
    bool emergencia = false;
    ba::app::EnergiaAire::Acciones a;
    a.cerrarGrabaciones = [&]() {
        ++cerradas;
        emergencia = true;
        return std::vector<std::string>{"programa"};
    };
    a.retomarGrabaciones = [&]() {
        ++retomadas;
        emergencia = false;
    };
    a.emergencia = [&]() { return emergencia; };
    ba::app::EnergiaAire en(a);
    QObject::connect(&en, &ba::app::EnergiaAire::aviso, [&](const QString&, const QString&) { ++avisos; });

    en.evaluar(estado(false, 100));
    CHECK(cerradas == 0 && retomadas == 0 && en.estado()["accion"] == "nada", "1: con red, nada");

    en.evaluar(estado(true, 80, 40));
    CHECK(cerradas == 0 && en.estado()["accion"] == "avisar" && en.estado()["en_bateria"] == true, "2: en batería, avisa");

    en.evaluar(estado(true, 15, 30));
    en.evaluar(estado(true, 14, 28));
    CHECK(cerradas == 1 && avisos == 1, "3: cierra una vez");
    CHECK(en.estado()["emergencia"] == true && en.estado()["accion"] == "cerrar_grabaciones", "3: emergencia");

    en.evaluar(estado(false, 14));
    CHECK(retomadas == 1 && avisos == 2 && en.estado()["emergencia"] == false, "4: vuelve la red, retoma");

    en.evaluar(estado(true, 90, 6));
    CHECK(cerradas == 2, "5: los minutos también disparan");

    std::printf(g_fallos == 0 ? "test_energia_aire: OK\n" : "test_energia_aire: %d fallos\n", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
