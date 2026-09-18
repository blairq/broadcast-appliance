// test_dashboard_model.cpp
//
// Logica de disposicion del dashboard (DashboardModel), sin interfaz: por eso
// vive en el ViewModel y no en QML.
//   1. La disposicion por defecto existe y queda dentro del area.
//   2. snap(): una tarjeta soltada cerca del borde de otra se pega a ese borde
//      con la separacion (efecto "stick"); nunca sale del area; tamaño minimo.
//   3. Persistencia: lo que se mueve se guarda en ui.json y se relee igual.

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

    if (g_failures == 0) {
        std::printf("test_dashboard_model: OK\n");
        return 0;
    }
    std::printf("test_dashboard_model: %d fallo(s)\n", g_failures);
    return 1;
}
