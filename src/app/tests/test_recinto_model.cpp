// test_recinto_model.cpp
//
// asamblea/RecintoModel: el recinto, una fila por banca con lo que muestra su celda ya
// cruzado de sus cinco fuentes (el cuerpo, la fila de oradores, la central, la sesión
// remota y las fotos de las posiciones PTZ).
//   1. Cada banca con su integrante, su turno en la fila y si tiene la palabra.
//   2. La central: micrófono, pedido, «le toca», presidente, sin unidad (y nada de eso
//      con la central desconectada).
//   3. La sesión remota: conectado/esperando/habilitado; la cara en vivo, la del Cuerpo
//      o la de su posición PTZ, en ese orden.
//   4. porLugar, presidencia, orden por número de banca y hayEsperando.

#include <QCoreApplication>

#include <cstdio>

#include "asamblea/RecintoModel.hpp"

using ba::app::asamblea::RecintoModel;

namespace {
int g_failures = 0;
#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (linea %d)\n", #cond, __LINE__);                                          \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

QVariantMap lugar(const char* banca, int f, int c, const char* id, QVariantMap extra = {}) {
    QVariantMap i{{"id", id}, {"nombre", QString("Integrante ") + id}};
    for (auto it = extra.cbegin(); it != extra.cend(); ++it) i.insert(it.key(), it.value());
    return {{"banca", banca}, {"fila", f}, {"columna", c}, {"integrante", i}};
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    bool central = true;
    QString fotoEnVivo = "data:en-vivo";
    RecintoModel::Fuentes f{
        .recinto = [] {
            return QVariantMap{{"bancas", QVariantList{lugar("10", 1, 0, "c"),
                                                       lugar("2", 0, 1, "b", {{"foto_url", "file:cuerpo.jpg"}}),
                                                       lugar("1", 0, 0, "a", {{"preset_ptz", QVariantMap{{"camara", "Cam1"}, {"posicion", "p1"}}}})}},
                               {"presidencia", lugar("P", -1, -1, "p")}};
        },
        .oradores = [] { return QVariantMap{{"cola", QVariantList{QVariantMap{{"id", "b"}}, QVariantMap{{"id", "a"}}}}, {"al_aire", QVariantMap{{"id", "c"}}}}; },
        .central = [] {
            return QVariantMap{{"unidades", QVariantList{QVariantMap{{"banca", "1"}, {"conectada", true}},
                                                         QVariantMap{{"banca", "P"}, {"tipo", "presidente"}, {"conectada", true}}}},
                               {"pedidos", QVariantList{QVariantMap{{"banca", "2"}, {"turno", 3}}}},
                               {"abiertos", QVariantList{"1"}},
                               {"siguiente", "2"}};
        },
        .centralConectada = [&central] { return central; },
        .participantes = [] {
            return QVariantList{QVariantMap{{"id", "a"}, {"conectado", true}, {"estado", "habilitado"}},
                                QVariantMap{{"id", "b"}, {"conectado", true}, {"estado", "esperando"}}};
        },
        .fotoRemota = [&fotoEnVivo](const QString& id) { return id == "a" ? fotoEnVivo : QString(); },
        .camaras = [] {
            return QVariantList{QVariantMap{{"entrada", "Cam1"},
                                            {"posiciones", QVariantList{QVariantMap{{"id", "p1"}, {"miniatura_url", "file:ptz.jpg"}}}}}};
        }};
    RecintoModel m(f);
    auto* lista = static_cast<ba::app::ListaObjetos*>(m.bancas());
    const QVariantMap porLugar = m.porLugar();
    const QVariantMap a = porLugar.value("0,0").toMap(), b = porLugar.value("0,1").toMap(), c = porLugar.value("1,0").toMap();

    // 1) La fila de oradores.
    CHECK(a.value("turno").toInt() == 2 && b.value("turno").toInt() == 1 && c.value("turno").toInt() == 0);
    CHECK(c.value("alAire").toBool() && !a.value("alAire").toBool());
    // 2) La central.
    CHECK(a.value("micAbierto").toBool() && !b.value("micAbierto").toBool());
    CHECK(b.value("turnoCentral").toInt() == 3 && b.value("leToca").toBool());
    CHECK(!a.value("sinUnidad").toBool() && b.value("sinUnidad").toBool());
    CHECK(m.presidencia().value("presidente").toBool() && m.presidencia().value("banca").toString() == "P");
    // 3) La sesión remota y las fotos.
    CHECK(a.value("conectado").toBool() && a.value("habilitado").toBool() && b.value("esperando").toBool());
    CHECK(a.value("fotoCara").toString() == "data:en-vivo");            // en vivo
    CHECK(b.value("fotoCara").toString() == "file:cuerpo.jpg");        // la del Cuerpo
    CHECK(a.value("fotoPtz").toString() == "file:ptz.jpg");
    fotoEnVivo.clear();
    m.recalcular();
    CHECK(m.porLugar().value("0,0").toMap().value("fotoCara").toString() == "file:ptz.jpg"); // la de la PTZ
    CHECK(m.hayEsperando());
    // 4) El orden por número de banca (natural: 2 antes que 10).
    CHECK(lista->rowCount() == 3);
    CHECK(lista->fila(0).value("banca").toString() == "1" && lista->fila(1).value("banca").toString() == "2" &&
          lista->fila(2).value("banca").toString() == "10");
    // Sin la central: nada de lo suyo.
    central = false;
    m.recalcular();
    const QVariantMap a2 = m.porLugar().value("0,0").toMap();
    CHECK(!a2.value("micAbierto").toBool() && !a2.value("sinUnidad").toBool() && !a2.value("unidad").isValid());

    if (g_failures == 0) std::printf("test_recinto_model: OK\n");
    return g_failures == 0 ? 0 : 1;
}
