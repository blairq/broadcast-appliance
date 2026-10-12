// src/app/tests/test_graficos_catalogo.cpp — el catálogo de gráficos (graficos/Catalogo)
//
// Lo usan el editor, el Emisor (el aire) y el MCP: dónde están, qué nombre
// sirve, los valores por defecto y el pedido para el renderizador tienen que
// dar lo mismo para todos. En una carpeta temporal, sin renderizador.

#include "graficos/Catalogo.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <cstdio>

using ba::app::graficos::Catalogo;

namespace {

int fallos = 0;

#define CHECK(cond, msg)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            std::fprintf(stderr, "FALLA: %s\n", msg);       \
            ++fallos;                                       \
        }                                                   \
    } while (0)

void escribir(const QString& ruta, const QByteArray& datos) {
    QDir().mkpath(QFileInfo(ruta).absolutePath());
    QFile f(ruta);
    if (f.open(QIODevice::WriteOnly)) f.write(datos);
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "carpeta temporal");
    Catalogo cat(tmp.path().toStdString());
    CHECK(QDir(cat.raiz()).exists(), "crea Graficos/");

    // Crear: uno declarativo y uno en QML.
    QString creado;
    CHECK(cat.crear("Zócalo!", true, &creado).isEmpty() && creado == "Zcalo", "crear limpia el nombre");
    CHECK(!cat.crear("Zcalo", true, nullptr).isEmpty(), "crear no pisa uno que existe");
    CHECK(cat.crear("placa", false, &creado).isEmpty(), "crear uno en QML");
    CHECK(!cat.crear("///", false, nullptr).isEmpty(), "un nombre sin nada que sirva");

    // Nombres inválidos: nunca fuera de Graficos/.
    QString err;
    CHECK(cat.carpeta("../afuera", &err).isEmpty() && !err.isEmpty(), "carpeta rechaza ..");
    CHECK(cat.carpeta("a/b", &err).isEmpty(), "carpeta rechaza barras");
    CHECK(cat.ficha("..").isEmpty(), "ficha de un nombre inválido: vacía");
    CHECK(!cat.carpeta("placa").isEmpty(), "carpeta de uno que existe");

    // Una carpeta sin grafico.json no es un gráfico.
    QDir(cat.raiz()).mkpath("suelta");
    CHECK(!cat.nombres().contains("suelta") && cat.nombres().contains("placa"), "nombres: solo con grafico.json");

    // Lista agrupada por categoría, «General» primero.
    escribir(QDir(cat.raiz()).filePath("aaa/grafico.json"),
             R"({"nombre": "A", "categoria": "Tickers", "parametros": [{"clave": "x", "titulo": "X"}]})");
    const QJsonArray l = cat.lista();
    CHECK(l.size() == 3, "lista: los tres");
    CHECK(l.last().toObject().value("categoria").toString() == "Tickers", "lista: General primero");
    CHECK(l.last().toObject().value("parametros").toArray().first().toObject().value("tipo").toString() == "texto",
          "lista: el tipo de fábrica es texto");

    // Valores por defecto: solo los que lo declaran.
    const QJsonObject vd = Catalogo::valoresPorDefecto(cat.ficha("aaa"));
    CHECK(!vd.contains("x"), "valoresPorDefecto: sin porDefecto no va (ni null)");
    CHECK(Catalogo::valoresPorDefecto(cat.ficha("placa")).value("texto").toString() == "Hola",
          "valoresPorDefecto: el de la ficha");

    // Pedido de un QML: como texto, con el tamaño de la ficha y los valores encima.
    QJsonObject p = cat.pedido("placa", QJsonObject{{"otro", 1}}, &err);
    CHECK(err.isEmpty() && p.value("fuente").toString().contains("property string texto"), "pedido QML: el texto");
    CHECK(p.value("ancho").toInt() == 900 && p.value("alto").toInt() == 220, "pedido QML: el tamaño");
    CHECK(p.value("parametros").toObject().value("texto").toString() == "Hola" &&
              p.value("parametros").toObject().value("otro").toInt() == 1,
          "pedido: por defecto + lo pedido");

    // Pedido declarativo: la descripción, sin pisarle el tamaño.
    p = cat.pedido("Zcalo", QJsonObject{{"texto", "Chau"}}, &err);
    CHECK(err.isEmpty() && p.value("declarativo").isObject() && !p.contains("ancho"), "pedido declarativo");
    CHECK(p.value("parametros").toObject().value("texto").toString() == "Chau", "pedido declarativo: valores");
    CHECK(cat.pedido("nada", {}, &err).isEmpty() && !err.isEmpty(), "pedido de uno que no está");

    // Guardar: una descripción rota no se guarda; una buena, sí.
    CHECK(!cat.guardar("Zcalo", "{ roto").isEmpty(), "guardar: JSON roto");
    CHECK(cat.guardar("Zcalo", R"({"ancho": 10, "capas": []})").isEmpty(), "guardar: descripción");
    CHECK(cat.ficha("Zcalo").value("declarativo").toObject().value("ancho").toInt() == 10, "guardar: quedó");
    CHECK(cat.guardar("placa", "import QtQuick\nItem {}\n").isEmpty() && cat.texto("placa").contains("Item {}"),
          "guardar: QML");

    std::printf(fallos == 0 ? "OK: catálogo de gráficos\n" : "%d fallos\n", fallos);
    return fallos == 0 ? 0 : 1;
}
