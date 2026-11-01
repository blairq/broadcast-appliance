// test_quien_mira.cpp — sala/QuienMira con fuentes falsas
//
// El retorno a los invitados se le pide al motor solo cuando cambia: al arrancar (nadie
// mira), cuando alguien empieza a mirar y cuando vence la gracia sin nadie. La gracia la
// vence un reloj de una vez: sin que nadie avise, igual se apaga. Cada salida por su lado;
// una que se borra se olvida.

#include "sala/QuienMira.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QObject>

#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {
int fallos = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__); \
            ++fallos;                                                        \
        } else {                                                             \
            std::printf("ok: %s\n", msg);                                    \
        }                                                                    \
    } while (0)

/// Una fuente que avisa (como la sala, la sesión remota o las pantallas).
class Fuente : public QObject {
    Q_OBJECT
   public:
    bool mira = false;
    void poner(bool si) {
        mira = si;
        emit cambio();
    }
   signals:
    void cambio();
};

void esperar(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    using Pedido = std::pair<std::string, bool>;
    std::vector<Pedido> pedidos;
    Fuente sala, pantallas;
    std::map<std::string, bool> otras;
    ba::app::sala::QuienMira q(
        {[&]() { return sala.mira || pantallas.mira; }, [&]() { return otras; }},
        [&](const std::string& s, bool si) { pedidos.emplace_back(s, si); }, std::chrono::milliseconds(400));
    q.escuchar(&sala, &Fuente::cambio);
    q.escuchar(&pantallas, &Fuente::cambio);

    q.revisar();
    CHECK((pedidos == std::vector<Pedido>{{"", false}}), "al arrancar, nadie mira: se le dice una vez");
    q.revisar();
    CHECK(pedidos.size() == 1, "revisar sin cambios no le pide nada al motor");

    sala.poner(true);
    CHECK(pedidos.size() == 2 && pedidos.back() == Pedido("", true), "alguien entra: se pide al toque, por el aviso");
    pantallas.poner(true);
    CHECK(pedidos.size() == 2, "otro que mira no cambia nada");
    sala.poner(false);
    pantallas.poner(false);
    CHECK(pedidos.size() == 2 && q.enUso("") == true, "nadie mira: sigue en uso durante la gracia");
    esperar(150);
    sala.poner(true);
    sala.poner(false);
    esperar(250);
    CHECK(pedidos.size() == 2, "alguien volvió en la gracia: la gracia empieza de nuevo");
    esperar(300);
    CHECK(pedidos.size() == 3 && pedidos.back() == Pedido("", false), "vence la gracia sin avisos: se apaga sola");

    // Las demás salidas: por su lado, solo mientras el gateway dice que alguien las mira.
    otras = {{"s2", false}};
    q.revisar();
    CHECK(pedidos.size() == 4 && pedidos.back() == Pedido("s2", false), "una salida nueva sin nadie: apagada");
    otras["s2"] = true;
    q.revisar();
    CHECK(pedidos.size() == 5 && pedidos.back() == Pedido("s2", true), "alguien la mira: prendida");
    CHECK(q.enUso("") == false, "la por omisión no se entera");
    otras.clear();
    q.revisar();
    CHECK(!q.enUso("s2").has_value() && pedidos.size() == 5, "la salida se borró: se olvida sin pedir nada");
    esperar(500);
    CHECK(pedidos.size() == 5, "y su gracia no queda armada");

    std::printf(fallos == 0 ? "test_quien_mira: OK\n" : "test_quien_mira: %d FALLOS\n", fallos);
    return fallos == 0 ? 0 : 1;
}

#include "test_quien_mira.moc"
