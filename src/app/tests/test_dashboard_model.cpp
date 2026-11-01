// test_dashboard_model.cpp
//
// Logica de disposicion del dashboard (DashboardModel), sin interfaz: por eso
// vive en el ViewModel y no en QML.
//   1. La disposicion por defecto existe y queda dentro del area.
//   2. snap(): una tarjeta soltada cerca del borde de otra se pega a ese borde
//      con la separacion (efecto "stick"); nunca sale del area; tamaño minimo.
//   3. Persistencia: lo que se mueve se guarda en ui.json y se relee igual.
//   4. Prender y apagar tarjetas se recuerda.
//   5. Una escena en el fondo o en un monitor va por su id: lo guardado por el
//      nombre se migra al conocer las escenas y un renombre no la pierde.

#include <QCoreApplication>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>

#include "ui/DashboardModel.hpp"

using ba::app::DashboardModel;

namespace {
int g_failures = 0;
#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (linea %d)\n", #cond, __LINE__);                                          \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)
bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    const std::filesystem::path file = dir.filePath("ui.json").toStdString();

    // 1) Defaults
    DashboardModel m(file);
    CHECK(m.rowCount() >= 5);
    for (int i = 0; i < m.rowCount(); ++i) {
        const double x = m.data(m.index(i), DashboardModel::XRole).toDouble();
        const double y = m.data(m.index(i), DashboardModel::YRole).toDouble();
        const double w = m.data(m.index(i), DashboardModel::WRole).toDouble();
        const double h = m.data(m.index(i), DashboardModel::HRole).toDouble();
        CHECK(x >= 0 && y >= 0 && x + w <= 1.0 + 1e-9 && y + h <= 1.0 + 1e-9);
    }
    CHECK(m.background().contains("program"));

    // 2) snap: la tarjeta 1 (outputs, x 0..0.24) queda como vecina; se suelta
    //    la 2 un poquito a la derecha de su borde derecho -> se pega con gap.
    const int id2 = m.data(m.index(1), DashboardModel::IdRole).toInt();
    const double right1 = m.data(m.index(0), DashboardModel::XRole).toDouble() +
                          m.data(m.index(0), DashboardModel::WRole).toDouble();
    const double aspect = 16.0 / 9.0;
    const double gap = 12.0 / 1000.0 / aspect;
    const QRectF r = m.snap(id2, right1 + gap + 0.004, 0.41, 0.24, 0.29, aspect);
    CHECK(near(r.x(), right1 + gap, 1e-3));
    // Fuera del area -> adentro.
    const QRectF out = m.snap(id2, 1.2, -0.3, 0.3, 0.3, aspect);
    CHECK(out.x() + out.width() <= 1.0 + 1e-9);
    CHECK(out.y() >= 0.0);
    // Tamaño minimo.
    const QRectF tiny = m.snap(id2, 0.5, 0.5, 0.01, 0.01, aspect);
    CHECK(tiny.width() >= 0.10 - 1e-9 && tiny.height() >= 0.10 - 1e-9);

    // 3) Persistencia
    m.setCardRect(id2, 0.5, 0.25, 0.3, 0.3);
    m.setBackground({"program", "guest:1"});
    DashboardModel again(file);
    bool found = false;
    for (int i = 0; i < again.rowCount(); ++i) {
        if (near(again.data(again.index(i), DashboardModel::XRole).toDouble(), 0.5) &&
            near(again.data(again.index(i), DashboardModel::YRole).toDouble(), 0.25)) {
            found = true;
        }
    }
    CHECK(found);
    CHECK(again.background() == QStringList({"program", "guest:1"}));

    // 4) Prender y apagar tarjetas se recuerda al reabrir, incluso con todas apagadas
    //    (una lista vacía es una elección, no un archivo sin disposición).
    const auto tipos = [](const DashboardModel& d) {
        QStringList t;
        for (int i = 0; i < d.rowCount(); ++i) t << d.data(d.index(i), DashboardModel::KindRole).toString();
        return t;
    };
    QStringList antes = tipos(again);
    again.alternarTarjeta(antes.first());
    CHECK(tipos(DashboardModel(file)) == tipos(again));
    for (const QString& k : tipos(again)) again.alternarTarjeta(k);
    CHECK(again.rowCount() == 0);
    DashboardModel vacio(file);
    CHECK(vacio.rowCount() == 0);
    vacio.alternarTarjeta(antes.first());
    CHECK(tipos(DashboardModel(file)) == QStringList({antes.first()}));

    // 5) Escenas por id. Lo de antes, por el nombre (fondo, un monitor y una oculta).
    {
        const std::filesystem::path f5 = dir.filePath("ui-escenas.json").toStdString();
        {
            DashboardModel d(f5);
            d.setBackground({"program", "escena:Atril", "escena:Ninguna"});
            d.addCard("monitor", "escena:Atril");
            d.addCard("ptz", "escena:Atril");
            d.alternarTarjeta("ptz"); // queda oculta, con su fuente
        }
        DashboardModel d(f5);
        CHECK(d.background() == QStringList({"program", "escena:Atril", "escena:Ninguna"})); // sin escenas, como estaba
        ba::app::escenas::TablaEscenas tabla({{"esc-00000001", "Atril"}});
        CHECK(d.seguirEscenas(tabla));
        CHECK(d.background() == QStringList({"program", "escena:esc-00000001", "escena:Ninguna"}));
        const int fila = d.rowCount() - 1;
        CHECK(d.data(d.index(fila), DashboardModel::SourceRole).toString() == "escena:esc-00000001");
        CHECK(!d.seguirEscenas(tabla)); // ya estaba
        // Se guardó migrado; la oculta también.
        DashboardModel releido(f5);
        CHECK(releido.background() == QStringList({"program", "escena:esc-00000001", "escena:Ninguna"}));
        releido.seguirEscenas(tabla);
        releido.alternarTarjeta("ptz");
        CHECK(releido.data(releido.index(releido.rowCount() - 1), DashboardModel::SourceRole).toString() == "escena:esc-00000001");
        // Un renombre no cambia nada de lo guardado.
        CHECK(!d.seguirEscenas(ba::app::escenas::TablaEscenas({{"esc-00000001", "Atril grande"}})));
        CHECK(d.background().contains("escena:esc-00000001"));
        // Lo que se elige después por el nombre entra por el id.
        d.setBackground({"escena:Atril grande"});
        CHECK(d.background() == QStringList({"escena:esc-00000001"}));
        d.setCardSource(d.data(d.index(fila), DashboardModel::IdRole).toInt(), "escena:Atril grande");
        CHECK(d.data(d.index(fila), DashboardModel::SourceRole).toString() == "escena:esc-00000001");
    }

    if (g_failures == 0) {
        std::printf("test_dashboard_model: OK\n");
        return 0;
    }
    std::printf("test_dashboard_model: %d fallo(s)\n", g_failures);
    return 1;
}
