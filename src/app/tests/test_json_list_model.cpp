// test_json_list_model.cpp — JsonListModel avisa solo lo que cambió
//
// La foto de la sala llega varias veces por segundo con la red de cada invitado (kbps,
// pérdida, RTT): si cada foto avisara todas las filas con todos los roles, cada delegado
// de la vista (40 invitados) se volvería a evaluar entero. Se prueba que:
//   - la misma lista no avisa nada;
//   - un cambio en una clave que no es rol no avisa nada (pero se guarda);
//   - un cambio en un rol avisa SOLO esa fila y ese rol;
//   - si cambian los ids (entra o se va uno, otro orden), se insertan, quitan y mueven
//     SOLO esas filas (sin reset: con 60 invitados, rehacer era recrear 60 delegados
//     con video por cada uno que entraba);
//   - sin ids (o repetidos) y otra cantidad, se rehace.
#include <QCoreApplication>

#include "ui/JsonListModel.hpp"

#include <cstdio>

using ba::app::JsonListModel;

namespace {
int fallos = 0;
#define CHECK(c, m)                                \
    do {                                           \
        if (!(c)) {                                \
            std::printf("FALLA: %s\n", m);         \
            ++fallos;                              \
        } else {                                   \
            std::printf("ok: %s\n", m);            \
        }                                          \
    } while (0)

QJsonObject fila(const char* id, const char* nombre, int kbps, const char* semaforo = "verde") {
    return QJsonObject{{"id", id}, {"name", nombre}, {"semaforo", semaforo}, {"red", QJsonObject{{"kbps_in", kbps}}}};
}

struct Aviso {
    int desde, hasta;
    QList<int> roles;
};
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    JsonListModel m(QStringList{"name", "semaforo"});
    const int rolName = Qt::UserRole + 1, rolSemaforo = Qt::UserRole + 2;
    std::vector<Aviso> avisos;
    int resets = 0, cuentas = 0;
    QObject::connect(&m, &QAbstractItemModel::dataChanged, [&](const QModelIndex& a, const QModelIndex& b, const QList<int>& r) {
        avisos.push_back({a.row(), b.row(), r});
    });
    QObject::connect(&m, &QAbstractItemModel::modelReset, [&]() { ++resets; });
    QObject::connect(&m, &JsonListModel::countChanged, [&]() { ++cuentas; });

    m.setItems({fila("a", "Ana", 100), fila("b", "Beto", 200), fila("c", "Ceci", 300)});
    CHECK(resets == 0 && cuentas == 1 && m.count() == 3, "la primera lista: se insertan sus filas");

    avisos.clear();
    m.setItems({fila("a", "Ana", 100), fila("b", "Beto", 200), fila("c", "Ceci", 300)});
    CHECK(avisos.empty(), "la misma lista no avisa nada");

    m.setItems({fila("a", "Ana", 110), fila("b", "Beto", 220), fila("c", "Ceci", 330)});
    CHECK(avisos.empty(), "cambia solo lo que no es rol (la red): no avisa");
    CHECK(m.items().at(1).toObject().value("red").toObject().value("kbps_in").toInt() == 220, "pero lo guarda");

    m.setItems({fila("a", "Ana", 110), fila("b", "Beto", 220, "rojo"), fila("c", "Ceci", 330)});
    CHECK(avisos.size() == 1, "un rol de una fila: un solo aviso");
    if (avisos.size() == 1) {
        CHECK(avisos[0].desde == 1 && avisos[0].hasta == 1, "solo esa fila");
        CHECK(avisos[0].roles == QList<int>{rolSemaforo}, "solo ese rol");
    }
    CHECK(m.data(m.index(1), rolSemaforo).toString() == "rojo", "el dato nuevo se lee");

    avisos.clear();
    m.setItems({fila("a", "Ana María", 110), fila("b", "Beto", 220, "rojo"), fila("c", "Ceci", 330, "ambar")});
    CHECK(avisos.size() == 2, "dos filas cambiadas: dos avisos");
    if (avisos.size() == 2) {
        CHECK(avisos[0].desde == 0 && avisos[0].roles == QList<int>{rolName}, "fila 0: el nombre");
        CHECK(avisos[1].desde == 2 && avisos[1].roles == QList<int>{rolSemaforo}, "fila 2: el semáforo");
    }

    int quitadas = 0, insertadas = 0, movidas = 0;
    QObject::connect(&m, &QAbstractItemModel::rowsRemoved, [&](const QModelIndex&, int a, int b) { quitadas += b - a + 1; });
    QObject::connect(&m, &QAbstractItemModel::rowsInserted, [&](const QModelIndex&, int a, int b) { insertadas += b - a + 1; });
    QObject::connect(&m, &QAbstractItemModel::rowsMoved, [&]() { ++movidas; });
    const auto nombres = [&m]() {
        QStringList out;
        for (int i = 0; i < m.count(); ++i) out << m.data(m.index(i), rolName).toString();
        return out.join(",");
    };

    avisos.clear();
    m.setItems({fila("a", "Ana María", 110), fila("c", "Ceci", 330, "ambar")});
    CHECK(resets == 0 && quitadas == 1 && cuentas == 2 && m.count() == 2 && avisos.empty(),
          "se fue uno: se quita esa fila (sin rehacer)");
    m.setItems({fila("c", "Ceci", 330, "ambar"), fila("a", "Ana María", 110)});
    CHECK(resets == 0 && movidas == 1 && cuentas == 2 && nombres() == "Ceci,Ana María", "otro orden: se mueve (sin rehacer)");
    m.setItems({fila("c", "Ceci", 330, "ambar"), fila("d", "Dani", 50), fila("a", "Ana María", 110)});
    CHECK(resets == 0 && insertadas == 1 && cuentas == 3 && nombres() == "Ceci,Dani,Ana María",
          "entra uno en el medio: se inserta solo esa fila");
    avisos.clear();
    m.setItems({fila("d", "Dani", 50, "rojo"), fila("e", "Eli", 10), fila("a", "Ana", 110)});
    CHECK(resets == 0 && nombres() == "Dani,Eli,Ana", "se va uno, entra otro, cambia un orden y un nombre: queda igual a la lista");
    CHECK(m.data(m.index(0), rolSemaforo).toString() == "rojo", "con los datos nuevos");
    // Ids repetidos (o sin id) y otra cantidad: se rehace.
    m.setItems({fila("x", "X", 1), fila("x", "X2", 1)});
    CHECK(resets == 1 && m.count() == 2, "ids repetidos: se rehace");
    // 60 que entran de a uno: ni un reset.
    {
        JsonListModel sala(QStringList{"name"});
        int r = 0;
        QObject::connect(&sala, &QAbstractItemModel::modelReset, [&]() { ++r; });
        QJsonArray lista;
        sala.setItems(lista);
        r = 0;
        for (int i = 0; i < 60; ++i) {
            lista.append(QJsonObject{{"id", QString("p%1").arg(i)}, {"name", QString("Invitado %1").arg(i)}});
            sala.setItems(lista);
        }
        CHECK(r == 0 && sala.count() == 60, "60 que entran de a uno: ningún reset");
    }

    // Filas sin "id" (los lugares de invitado): misma cantidad = mismas filas, y avisa por rol.
    JsonListModel lugares(QStringList{"nombre", "red"});
    lugares.setItems({QJsonObject{{"nombre", ""}, {"red", QJsonObject{}}}, QJsonObject{{"nombre", ""}, {"red", QJsonObject{}}}});
    std::vector<Aviso> avisosL;
    QObject::connect(&lugares, &QAbstractItemModel::dataChanged,
                     [&](const QModelIndex& a, const QModelIndex&, const QList<int>& r) { avisosL.push_back({a.row(), a.row(), r}); });
    lugares.setItems({QJsonObject{{"nombre", ""}, {"red", QJsonObject{}}},
                      QJsonObject{{"nombre", ""}, {"red", QJsonObject{{"kbps_in", 900}}}}});
    CHECK(avisosL.size() == 1 && avisosL[0].desde == 1 && avisosL[0].roles == QList<int>{Qt::UserRole + 2},
          "sin id: la red de un lugar avisa solo ese rol de esa fila");

    std::printf(fallos ? "\n%d FALLAS\n" : "\nTODO OK\n", fallos);
    return fallos ? 1 : 0;
}
