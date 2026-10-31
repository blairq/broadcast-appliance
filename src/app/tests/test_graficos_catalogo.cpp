// src/app/tests/test_graficos_catalogo.cpp — el catálogo de gráficos (graficos/Catalogo)
//
// Lo usan el editor, el Emisor (el aire) y el MCP: dónde están, qué nombre
// sirve, los valores por defecto y el pedido para el renderizador tienen que
// dar lo mismo para todos. En una carpeta temporal, sin renderizador.

#include "graficos/Catalogo.hpp"

#include <QCoreApplication>
#include <QDateTime>
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
    // El acento del tema del evento abierto le gana al porDefecto de un parámetro de color
    // que se llama así (o que lo nombra con «tema»); a uno de texto con el mismo nombre, no.
    {
        const QJsonObject ficha{{"parametros", QJsonArray{QJsonObject{{"clave", "acento"}, {"tipo", "color"}, {"porDefecto", "#ff6a2b"}},
                                                         QJsonObject{{"clave", "barra"}, {"tipo", "color"}, {"tema", "acento"}, {"porDefecto", "#111111"}},
                                                         QJsonObject{{"clave", "otro"}, {"tipo", "color"}, {"porDefecto", "#222222"}},
                                                         QJsonObject{{"clave", "acento2"}, {"tipo", "texto"}, {"tema", "acento"}, {"porDefecto", "hola"}}}}};
        CHECK(Catalogo::valoresPorDefecto(ficha).value("acento").toString() == "#ff6a2b", "sin evento: el de fábrica");
        Catalogo::ponerValoresDelEvento(QJsonObject{{"acento", "#00a86b"}});
        const QJsonObject v = Catalogo::valoresPorDefecto(ficha);
        CHECK(v.value("acento").toString() == "#00a86b", "con evento: el acento del tema");
        CHECK(v.value("barra").toString() == "#00a86b", "el que lo nombra con «tema» también");
        CHECK(v.value("otro").toString() == "#222222", "otro color queda");
        CHECK(v.value("acento2").toString() == "hola", "un texto no toma un color");
        Catalogo::ponerValoresDelEvento({});
        CHECK(Catalogo::valoresPorDefecto(ficha).value("acento").toString() == "#ff6a2b", "sin evento otra vez: el de fábrica");
    }

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

    // La carpeta va en el pedido: contra ella resuelve el renderizador «medios/…»,
    // también con el texto del editor sin guardar.
    p = cat.pedido("placa", {}, &err);
    CHECK(p.value("carpeta").toString() == cat.carpeta("placa"), "pedido: lleva la carpeta del gráfico");
    p = Catalogo::pedidoCon(cat.ficha("placa"), "import QtQuick\nItem {}\n", {}, &err, cat.carpeta("placa"));
    CHECK(p.value("carpeta").toString() == cat.carpeta("placa"), "pedidoCon: la carpeta que le pasan");
    CHECK(!Catalogo::pedidoCon(cat.ficha("placa"), "import QtQuick\nItem {}\n", {}, &err).contains("carpeta"),
          "pedidoCon: sin carpeta, no la inventa");

    // Sabe animar un cambio: con un parámetro de datos, una torta o unas barras, o si lo dice.
    CHECK(!Catalogo::sabeCambiar(cat.ficha("placa")), "sabeCambiar: un QML común no");
    CHECK(Catalogo::sabeCambiar(QJsonObject{{"parametros", QJsonArray{QJsonObject{{"clave", "v"}, {"tipo", "datos"}}}}}),
          "sabeCambiar: un parámetro de tipo datos");
    CHECK(Catalogo::sabeCambiar(QJsonObject{
              {"declarativo", QJsonObject{{"capas", QJsonArray{QJsonObject{{"tipo", "barras"}}}}}}}),
          "sabeCambiar: unas barras");
    CHECK(Catalogo::sabeCambiar(QJsonObject{{"cambio", QJsonObject{{"duracion", 400}}}}) &&
              Catalogo::duracionCambio(QJsonObject{{"cambio", QJsonObject{{"duracion", 400}}}}) == 400,
          "sabeCambiar: lo declara, con su duración");
    CHECK(!Catalogo::sabeCambiar(QJsonObject{{"cambio", false}}), "sabeCambiar: «cambio»: false manda");
    CHECK(Catalogo::duracionCambio(QJsonObject{}) == 700, "duracionCambio: 700 de fábrica");

    // Un gráfico de un plugin: carpeta propia, ficha con «plugin», sin texto, sin pedido directo.
    const QJsonObject decl{{"nombre", "marcador"},
                           {"titulo", "Marcador"},
                           {"parametros", QJsonArray{QJsonObject{{"clave", "puntos"}, {"tipo", "datos"}, {"porDefecto", "A: 1"}}}},
                           {"ancho", 1000},
                           {"alto", 420},
                           {"modo", "describe"}};
    QString deplugin;
    CHECK(cat.guardarDePlugin("mi-plugin", decl, &deplugin).isEmpty() && deplugin == "plugin-mi-plugin-marcador",
          "guardarDePlugin: la carpeta plugin-<id>-<nombre>");
    const QJsonObject fp = cat.ficha(deplugin);
    CHECK(Catalogo::pluginDe(fp) == "mi-plugin" && fp.value("graficoPlugin").toString() == "marcador" &&
              fp.value("categoria").toString() == "Plugins" && fp.value("ancho").toInt() == 1000,
          "guardarDePlugin: la ficha");
    CHECK(cat.texto(deplugin).isEmpty(), "de un plugin: sin texto que editar");
    CHECK(cat.pedido(deplugin, {}, &err).isEmpty() && err.contains("mi-plugin"), "de un plugin: el pedido es del Emisor");
    CHECK(Catalogo::sabeCambiar(fp), "de un plugin con datos: sabe cambiar");
    const QDateTime antes = QFileInfo(QDir(cat.carpeta(deplugin)).filePath("grafico.json")).lastModified();
    CHECK(cat.guardarDePlugin("mi-plugin", decl).isEmpty() &&
              QFileInfo(QDir(cat.carpeta(deplugin)).filePath("grafico.json")).lastModified() == antes,
          "guardarDePlugin: la misma declaración no reescribe");
    bool listado = false;
    for (const QJsonValue& v : cat.lista())
        listado = listado || (v.toObject().value("nombre").toString() == deplugin &&
                              v.toObject().value("plugin").toString() == "mi-plugin");
    CHECK(listado, "lista: el de un plugin, con su plugin");
    CHECK(!cat.guardarDePlugin("mi-plugin", QJsonObject{}).isEmpty(), "guardarDePlugin: sin nombre, error");

    std::printf(fallos == 0 ? "OK: catálogo de gráficos\n" : "%d fallos\n", fallos);
    return fallos == 0 ? 0 : 1;
}
