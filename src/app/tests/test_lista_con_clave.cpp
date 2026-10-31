// test_lista_con_clave.cpp
//
// ListaObjetos (ui/ListaObjetos): la lista que se actualiza en su lugar por clave.
//   1. Una vista que solo escucha las señales (insert/remove/move/dataChanged) termina
//      igual que la lista nueva, también en cientos de cambios al azar con claves
//      repetidas y filas sin clave.
//   2. Lo justo: lo que no cambió no avisa; un cambio de un campo avisa esa fila y ese
//      rol; reordenar mueve (no saca y vuelve a poner); entrar/salir avisa una fila.
//   3. Roles: la clave, los pedidos y «datos».

#include <QCoreApplication>
#include <QRandomGenerator>

#include <cstdio>

#include "ui/ListaObjetos.hpp"

using ba::app::ListaObjetos;

namespace {
int g_failures = 0;
#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (linea %d)\n", #cond, __LINE__);                                          \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

/// Una «vista» que copia el modelo solo con lo que le avisan.
struct Espejo {
    QVariantList filas;
    int insertadas = 0, sacadas = 0, movidas = 0, cambiadas = 0, resets = 0;
    QList<int> ultimosRoles;
    explicit Espejo(ListaObjetos& m) {
        QObject::connect(&m, &QAbstractItemModel::rowsInserted, [this, &m](const QModelIndex&, int a, int b) {
            for (int i = a; i <= b; ++i) filas.insert(i, m.fila(i));
            insertadas += b - a + 1;
        });
        QObject::connect(&m, &QAbstractItemModel::rowsRemoved, [this](const QModelIndex&, int a, int b) {
            for (int i = b; i >= a; --i) filas.removeAt(i);
            sacadas += b - a + 1;
        });
        QObject::connect(&m, &QAbstractItemModel::rowsMoved,
                         [this](const QModelIndex&, int a, int, const QModelIndex&, int dest) {
                             // Una sola fila: dest es la posición ANTES de sacarla.
                             const QVariant v = filas.takeAt(a);
                             filas.insert(dest > a ? dest - 1 : dest, v);
                             ++movidas;
                         });
        QObject::connect(&m, &QAbstractItemModel::dataChanged,
                         [this, &m](const QModelIndex& a, const QModelIndex& b, const QList<int>& roles) {
                             for (int i = a.row(); i <= b.row(); ++i) filas[i] = m.fila(i);
                             cambiadas += b.row() - a.row() + 1;
                             ultimosRoles = roles;
                         });
        QObject::connect(&m, &QAbstractItemModel::modelReset, [this, &m] {
            filas = m.lista();
            ++resets;
        });
    }
    void cero() { insertadas = sacadas = movidas = cambiadas = resets = 0; }
};

QVariantMap obj(const QString& id, const QString& nombre, int n = 0) {
    return {{"id", id}, {"nombre", nombre}, {"n", n}};
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    // --- 2) Lo justo ---------------------------------------------------------------------
    {
        ListaObjetos m(QStringLiteral("id"), {QStringLiteral("nombre"), QStringLiteral("n")});
        Espejo e(m);
        const QVariantList a{obj("a", "Ana"), obj("b", "Beto"), obj("c", "Ceci")};
        m.poner(a);
        CHECK(e.insertadas == 3 && e.sacadas == 0);
        CHECK(e.filas == a);
        CHECK(m.rowCount() == 3);

        e.cero();
        m.poner(a); // nada cambió
        CHECK(e.insertadas == 0 && e.sacadas == 0 && e.movidas == 0 && e.cambiadas == 0);

        e.cero();
        QVariantList b = a;
        b[1] = obj("b", "Beto", 7); // un campo
        m.poner(b);
        CHECK(e.cambiadas == 1 && e.insertadas == 0 && e.sacadas == 0 && e.movidas == 0);
        const auto nombres = m.roleNames();
        const int rolN = nombres.key("n"), rolNombre = nombres.key("nombre"), rolDatos = nombres.key("datos");
        CHECK(e.ultimosRoles.contains(rolN) && e.ultimosRoles.contains(rolDatos) && !e.ultimosRoles.contains(rolNombre));
        CHECK(e.filas == b);

        e.cero();
        const QVariantList c{b[2], b[0], b[1]}; // reordenar
        m.poner(c);
        CHECK(e.insertadas == 0 && e.sacadas == 0 && e.movidas >= 1 && e.cambiadas == 0);
        CHECK(e.filas == c);

        e.cero();
        const QVariantList d{c[0], obj("z", "Zoe"), c[2]}; // sale una, entra otra
        m.poner(d);
        CHECK(e.insertadas == 1 && e.sacadas == 1 && e.cambiadas == 0);
        CHECK(e.filas == d);
        CHECK(m.indiceDe("z") == 1 && m.indiceDe("a") == -1);

        // --- 3) Roles
        CHECK(nombres.values().contains("id") && nombres.values().contains("datos"));
        CHECK(m.data(m.index(1), nombres.key("id")).toString() == "z");
        CHECK(m.data(m.index(1), rolNombre).toString() == "Zoe");
        CHECK(m.data(m.index(1), rolDatos).toMap().value("nombre").toString() == "Zoe");

        e.cero();
        m.poner({});
        CHECK(e.sacadas == 3 && m.rowCount() == 0 && e.filas.isEmpty());
    }

    // --- 1) Al azar: el espejo siempre queda igual ---------------------------------------
    {
        ListaObjetos m(QStringLiteral("id"), {QStringLiteral("n")});
        Espejo e(m);
        auto* r = QRandomGenerator::global();
        int pasos = 0;
        for (int vuelta = 0; vuelta < 600; ++vuelta) {
            QVariantList l;
            const int tam = int(r->bounded(9));
            for (int i = 0; i < tam; ++i) {
                QVariantMap o{{"n", int(r->bounded(3))}};
                // claves de un alfabeto chico (se repiten) y a veces sin clave
                if (r->bounded(6) != 0) o["id"] = QString(QChar('a' + int(r->bounded(5))));
                l.push_back(o);
            }
            m.poner(l);
            if (e.filas != l) {
                std::printf("FALLO al azar en la vuelta %d\n", vuelta);
                ++g_failures;
                break;
            }
            ++pasos;
        }
        CHECK(pasos == 600);
        CHECK(e.resets == 0);
    }

    // --- Cambiar la clave rearma (y sigue igual) -------------------------------------------
    {
        ListaObjetos m;
        Espejo e(m);
        m.setClave("fuente");
        const QVariantList l{QVariantMap{{"fuente", "x"}}, QVariantMap{{"fuente", "y"}}};
        m.poner(l);
        CHECK(e.filas == l);
        CHECK(m.roleNames().values().contains("fuente"));
    }

    if (g_failures == 0) std::printf("test_lista_con_clave: OK\n");
    return g_failures == 0 ? 0 : 1;
}
