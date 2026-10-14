// test_selector_herramientas.cpp — el selector de herramientas de la Botonera
// (controls/SelectorHerramientas.qml) busca y filtra por sección como se espera
//
// Con el motor de QML de verdad y una Botonera de mentira (su catálogo y el
// buscador del asistente). Lo que se prueba es lo que ve el operador:
//   - sin buscar, todas; con una sección, las de esa sección;
//   - buscando, las que devuelve el buscador del asistente, en su orden, con por
//     qué salieron, y la sección elegida acota la búsqueda (antes la soltaba);
//   - los contadores de cada sección cuentan lo encontrado;
//   - si el buscador no devuelve nada, la búsqueda simple (todas las palabras,
//     sin tildes) sigue andando.
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QUrl>
#include <QVariant>

#include <cstdio>

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

QVariantMap herr(const char* nombre, const char* titulo, const char* desc, const char* etiquetas = "") {
    return {{"nombre", nombre}, {"titulo", titulo}, {"descripcion", desc}, {"etiquetas", etiquetas},
            {"ejemplo", ""},    {"solo_lectura", false}, {"destructiva", false}};
}
} // namespace

/// Lo que el selector le pide a Studio.botonera.
class BotoneraFalsa : public QObject {
    Q_OBJECT
   public:
    bool buscadorVacio = false;
    Q_INVOKABLE QVariantList catalogoUi() const {
        return {QVariantMap{{"id", "escenas"}, {"titulo", "Escenas"}, {"resumen", ""},
                            {"herramientas", QVariantList{herr("scene_switch", "Poner una escena al aire", "Corte directo"),
                                                          herr("scene_list", "Las escenas", "Lista")}}},
                QVariantMap{{"id", "medios"}, {"titulo", "Medios"}, {"resumen", ""},
                            {"herramientas", QVariantList{herr("medio_poner", "Usar un archivo de medios", "En una escena", "musica cancion"),
                                                          herr("playlist_crear", "Armar una lista", "Videos y temas", "musica de fondo"),
                                                          herr("media_find", "Buscar un archivo", "Por nombre")}}}};
    }
    Q_INVOKABLE QVariantList buscarUi(const QString& texto) const {
        if (buscadorVacio || !texto.contains("sica")) return {};
        // Como el índice: la mejor primero y por qué.
        return {QVariantMap{{"nombre", "medio_poner"}, {"puntaje", 9.5}, {"porque", QVariantList{"musica"}}},
                QVariantMap{{"nombre", "playlist_crear"}, {"puntaje", 4.0}, {"porque", QVariantList{"musica"}}}};
    }
};

class StudioFalso : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* botonera READ botonera CONSTANT)
   public:
    BotoneraFalsa b;
    QObject* botonera() { return &b; }
};

namespace {
QVariantList filas(QObject* sel) { return QQmlProperty::read(sel, "filas").toList(); }
QString nombres(QObject* sel) {
    QString s;
    for (const auto& f : filas(sel)) s += (s.isEmpty() ? "" : ",") + f.toMap().value("nombre").toString();
    return s;
}
void abrir(QObject* sel, const QString& busqueda) {
    QMetaObject::invokeMethod(sel, "abrir", Q_ARG(QVariant, QString()), Q_ARG(QVariant, busqueda));
}
void seccion(QObject* sel, const QString& id) {
    QQmlProperty::write(sel, "seccion", id);
    QMetaObject::invokeMethod(sel, "filtrar");
}
} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    const QString dir = QStringLiteral(BA_QML_DIR);
    static StudioFalso studio;
    qmlRegisterSingletonInstance("BA", 1, 0, "Studio", &studio);
    qmlRegisterSingletonType(QUrl::fromLocalFile(dir + "/Theme.qml"), "BA", 1, 0, "Theme");

    QQmlEngine engine;
    QQmlComponent c(&engine);
    c.setData(("import QtQuick\nimport QtQuick.Window\nimport \"" + QUrl::fromLocalFile(dir + "/controls").toString() +
               "\"\nWindow { width: 1200; height: 900; property alias sel: s; SelectorHerramientas { id: s } }")
                  .toUtf8(),
              QUrl::fromLocalFile(dir + "/prueba.qml"));
    QObject* win = c.create();
    if (win == nullptr) {
        for (const auto& e : c.errors()) std::printf("%s\n", qPrintable(e.toString()));
        return 1;
    }
    QObject* sel = QQmlProperty::read(win, "sel").value<QObject*>();

    abrir(sel, "");
    CHECK(filas(sel).size() == 5, "sin buscar: todas");
    seccion(sel, "medios");
    CHECK(nombres(sel) == "medio_poner,playlist_crear,media_find", "con la sección Medios: solo las de Medios");

    seccion(sel, "");
    abrir(sel, "música");
    CHECK(nombres(sel) == "medio_poner,playlist_crear", "buscando: las del buscador, en su orden");
    CHECK(!filas(sel).isEmpty() && filas(sel)[0].toMap().value("porque").toString() == "musica",
          "buscando: dice por qué salió");
    QVariant n;
    QMetaObject::invokeMethod(sel, "cuantasEn", Q_RETURN_ARG(QVariant, n), Q_ARG(QVariant, QString("medios")));
    CHECK(n.toInt() == 2, "buscando: el contador de Medios cuenta lo encontrado");
    QMetaObject::invokeMethod(sel, "cuantasEn", Q_RETURN_ARG(QVariant, n), Q_ARG(QVariant, QString("escenas")));
    CHECK(n.toInt() == 0, "buscando: el de Escenas, cero");

    seccion(sel, "escenas");
    CHECK(filas(sel).isEmpty(), "buscando con la sección Escenas elegida: la acota (nada de música ahí)");
    seccion(sel, "medios");
    CHECK(nombres(sel) == "medio_poner,playlist_crear", "buscando con la sección Medios: las de Medios");

    studio.b.buscadorVacio = true;
    seccion(sel, "");
    abrir(sel, "escena aire");
    CHECK(nombres(sel) == "scene_switch", "sin el buscador: la búsqueda simple (todas las palabras)");
    abrir(sel, "cancion");
    CHECK(nombres(sel) == "medio_poner", "sin el buscador: también en las etiquetas");

    delete win;
    std::printf(fallos ? "\n%d fallas\n" : "\ntodo ok\n", fallos);
    return fallos ? 1 : 0;
}

#include "test_selector_herramientas.moc"
