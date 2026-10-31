// test_uso_por_vistas.cpp — el contrato único de suscripción (comun/UsoPorVistas)
//
//   dos vistas sobre lo mismo: cerrar una no le apaga a la otra (el bug de las
//   Preguntas y la Votación); repetir no cuenta dos veces; soltar lo que no se usó no
//   resta; una vista destruida sin avisar se suelta sola; cada `que` con su cuenta;
//   el aviso sale solo al pasar de nadie a alguien y al revés.

#include <QCoreApplication>
#include <QObject>

#include <cstdio>
#include <memory>

#include "comun/UsoPorVistas.hpp"

using ba::app::UsoPorVistas;

namespace {
int g_fallas = 0;
#define CHECK(c, m)                                             \
    do {                                                        \
        if (!(c)) {                                             \
            std::printf("FALLO: %s (linea %d)\n", m, __LINE__); \
            ++g_fallas;                                         \
        } else {                                                \
            std::printf("ok: %s\n", m);                         \
        }                                                       \
    } while (0)
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QObject dueno;
    int avisos = 0;
    QString ultimo = "-";
    UsoPorVistas u(&dueno, [&](const QString& que) {
        ++avisos;
        ultimo = que;
    });

    QObject preguntas, votacion;
    CHECK(!u.activo() && !u.alguien(), "sin vistas no está activo");
    u.usar(&preguntas, true);
    CHECK(u.activo() && avisos == 1, "la primera vista lo prende (un aviso)");
    u.usar(&votacion, true);
    CHECK(u.cuantas() == 2 && avisos == 1, "la segunda no vuelve a avisar");
    u.usar(&preguntas, true);
    CHECK(u.cuantas() == 2, "repetir no cuenta dos veces");
    u.usar(&preguntas, false);
    CHECK(u.activo() && avisos == 1, "cerrar las Preguntas no le apaga a la Votación");
    u.usar(&preguntas, false);
    CHECK(u.cuantas() == 1, "soltar dos veces no resta de más");
    QObject ajena;
    u.usar(&ajena, false);
    CHECK(u.cuantas() == 1, "soltar una vista que no lo usaba no resta");
    u.usar(&votacion, false);
    CHECK(!u.activo() && avisos == 2, "la última que se va lo apaga (un aviso)");

    // Una vista que se destruye sin avisar.
    {
        auto vista = std::make_unique<QObject>();
        u.usar(vista.get(), true);
        CHECK(u.activo() && avisos == 3, "una vista nueva lo prende");
    }
    CHECK(!u.activo() && avisos == 4 && !u.alguien(), "destruida sin avisar, se suelta sola");

    // Cada `que` con su cuenta (claves de video, la multipista).
    QObject tarjeta, pagina;
    u.usar(&tarjeta, true, "entrada:Cam1");
    u.usar(&pagina, true, "entrada:Cam1");
    u.usar(&pagina, true, "entrada-entera:Cam1");
    CHECK(u.cuantas("entrada:Cam1") == 2 && u.cuantas("entrada-entera:Cam1") == 1, "cada clave con su cuenta");
    CHECK(!u.activo() && u.alguien(), "`activo()` es la clave vacía; `alguien()`, cualquiera");
    CHECK(u.usados() == QSet<QString>({"entrada:Cam1", "entrada-entera:Cam1"}), "`usados()`: cada clave usada, una vez");
    avisos = 0;
    u.usar(&pagina, false, "entrada:Cam1");
    CHECK(avisos == 0 && u.activo("entrada:Cam1"), "soltar una clave que otra vista usa no avisa");
    {
        auto vista = std::make_unique<QObject>();
        u.usar(vista.get(), true, "entrada-entera:Cam1");
        u.usar(vista.get(), true, "escena:A");
        CHECK(avisos == 1 && ultimo == "escena:A", "una clave nueva avisa con su nombre");
    }
    CHECK(u.cuantas("entrada-entera:Cam1") == 1 && !u.activo("escena:A") && avisos == 2,
          "destruida: suelta todas sus claves y avisa solo las que quedan sin nadie");
    u.usar(&pagina, false, "entrada-entera:Cam1");
    u.usar(&tarjeta, false, "entrada:Cam1");
    CHECK(!u.alguien() && avisos == 4, "sin nadie en ninguna clave");

    // El dueño que se va antes que la vista: la vista destruida después no rompe nada.
    auto vistaTardia = std::make_unique<QObject>();
    {
        QObject otroDueno;
        UsoPorVistas v(&otroDueno, [](const QString&) {});
        v.usar(vistaTardia.get(), true);
    }
    vistaTardia.reset();
    CHECK(true, "la vista que sobrevive al dueño se destruye sin problemas");

    std::printf("%s (%d fallas)\n", g_fallas == 0 ? "TODO OK" : "HAY FALLAS", g_fallas);
    return g_fallas == 0 ? 0 : 1;
}
