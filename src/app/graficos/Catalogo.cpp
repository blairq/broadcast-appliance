// ============================================================================
// src/app/graficos/Catalogo.cpp — ver Catalogo.hpp
// ============================================================================
#include "graficos/Catalogo.hpp"
#include <QCoreApplication>
#include "i18n/I18n.hpp"
#include "platform/Paths.hpp"

#include <algorithm>
#include <mutex>
#include <vector>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>

namespace ba::app::graficos {
namespace {

QJsonObject leerJson(const QString& ruta) {
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

bool escribir(const QString& ruta, const QByteArray& datos) {
    // A un temporal y renombrar: si se corta la luz, el archivo que estaba
    // sigue entero.
    QSaveFile f(ruta);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(datos);
    return f.commit();
}

} // namespace

Catalogo::Catalogo(const std::filesystem::path& raizDatos)
    : raiz_(QString::fromStdString((raizDatos / "Graficos").string())) {
    QDir().mkpath(raiz_);
}

QString Catalogo::raiz() const { return raiz_; }

QString Catalogo::carpeta(const QString& nombre, QString* error) const {
    if (nombre.isEmpty() || nombre.contains("..") || nombre.contains('/') || nombre.contains('\\')) {
        if (error != nullptr) *error = QCoreApplication::translate("graficos", "nombre de gráfico inválido");
        return {};
    }
    const QString c = QDir(raiz_).filePath(nombre);
    if (!QFileInfo(c).isDir()) {
        if (error != nullptr) *error = QCoreApplication::translate("graficos", "no existe el gráfico «%1»").arg(nombre);
        return {};
    }
    return c;
}

QStringList Catalogo::nombres() const {
    QStringList out;
    for (const QString& n : QDir(raiz_).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        if (QFile::exists(QDir(QDir(raiz_).filePath(n)).filePath("grafico.json"))) out << n;
    return out;
}

QJsonArray Catalogo::lista() const {
    std::vector<QJsonObject> todos;
    for (const QString& n : nombres()) {
        const QJsonObject f = ficha(n);
        QJsonArray params;
        for (const QJsonValue& v : f.value("parametros").toArray()) {
            const QJsonObject p = v.toObject();
            params.append(QJsonObject{{"clave", p.value("clave").toString()},
                                      {"titulo", p.value("titulo").toString()},
                                      {"tipo", p.value("tipo").toString("texto")},
                                      {"ayuda", p.value("ayuda").toString()}});
        }
        QJsonArray acciones;
        for (const QJsonValue& v : f.value("acciones").toArray()) {
            const QJsonObject a = v.toObject();
            acciones.append(QJsonObject{{"titulo", a.value("titulo").toString()},
                                        {"prompt", a.value("prompt").toString()},
                                        {"icono", a.value("icono").toString("✦")}});
        }
        todos.push_back(QJsonObject{{"nombre", n},
                                    {"titulo", f.value("nombre").toString(n)},
                                    {"descripcion", f.value("descripcion").toString()},
                                    {"categoria", f.value("categoria").toString("General")},
                                    {"declarativo", f.contains("declarativo")},
                                    {"plugin", pluginDe(f)},
                                    {"parametros", params},
                                    {"acciones", acciones}});
    }
    // Agrupados por categoría (la lista los muestra con su título): «General» primero.
    std::stable_sort(todos.begin(), todos.end(), [](const QJsonObject& a, const QJsonObject& b) {
        const QString ca = a.value("categoria").toString(), cb = b.value("categoria").toString();
        if (ca == cb) return false;
        if (ca == "General") return true;
        if (cb == "General") return false;
        return ca < cb;
    });
    QJsonArray out;
    for (const QJsonObject& o : todos) out.append(o);
    return out;
}

QJsonObject Catalogo::ficha(const QString& nombre) const {
    const QString c = carpeta(nombre);
    if (c.isEmpty()) return {};
    return leerJson(QDir(c).filePath("grafico.json"));
}

namespace {
std::mutex g_eventoMu;
QJsonObject g_evento; ///< bajo g_eventoMu: los valores del evento abierto (el acento del tema)
} // namespace

void Catalogo::ponerValoresDelEvento(const QJsonObject& valores) {
    std::lock_guard<std::mutex> l(g_eventoMu);
    g_evento = valores;
}

QJsonObject Catalogo::valoresDelEvento() {
    std::lock_guard<std::mutex> l(g_eventoMu);
    return g_evento;
}

QJsonObject Catalogo::valoresPorDefecto(const QJsonObject& ficha) {
    const QJsonObject evento = valoresDelEvento();
    QJsonObject out;
    for (const QJsonValue& v : ficha.value("parametros").toArray()) {
        const QJsonObject p = v.toObject();
        const QString clave = p.value("clave").toString();
        if (clave.isEmpty() || !p.contains("porDefecto")) continue;
        // Un color del evento (el acento de su tema) le gana al de fábrica del gráfico: el
        // zócalo sale con el color del evento sin que nadie lo elija. Lo que se pase a mano
        // (o guarde la escena) le gana a los dos.
        const QString delEvento = p.value("tema").toString(clave);
        if (p.value("tipo").toString() == "color" && evento.value(delEvento).isString())
            out[clave] = evento.value(delEvento);
        else
            out[clave] = p.value("porDefecto");
    }
    return out;
}

QString Catalogo::texto(const QString& nombre) const {
    const QString c = carpeta(nombre);
    if (c.isEmpty()) return {};
    const QJsonObject f = leerJson(QDir(c).filePath("grafico.json"));
    // El de un plugin no tiene texto: su código es el del plugin.
    if (!pluginDe(f).isEmpty()) return {};
    // Se edita la DESCRIPCIÓN, no el QML: el QML lo genera el motor.
    if (f.contains("declarativo"))
        return QString::fromUtf8(QJsonDocument(f.value("declarativo").toObject()).toJson(QJsonDocument::Indented));
    QFile qml(QDir(c).filePath("grafico.qml"));
    return qml.open(QIODevice::ReadOnly) ? QString::fromUtf8(qml.readAll()) : QString();
}

QJsonObject Catalogo::pedido(const QString& nombre, const QJsonObject& valores, QString* error) const {
    QString err;
    if (carpeta(nombre, &err).isEmpty()) {
        if (error != nullptr) *error = err;
        return {};
    }
    const QJsonObject f = ficha(nombre);
    if (f.isEmpty()) {
        if (error != nullptr) *error = QCoreApplication::translate("graficos", "el gráfico «%1» no tiene grafico.json").arg(nombre);
        return {};
    }
    if (!pluginDe(f).isEmpty()) {
        if (error != nullptr)
            *error = QCoreApplication::translate("graficos", "«%1» lo dibuja el plugin «%2»: se pide con Emisor::resolver")
                         .arg(nombre, pluginDe(f));
        return {};
    }
    return pedidoCon(f, texto(nombre), valores, error, carpeta(nombre));
}

QJsonObject Catalogo::pedidoCon(const QJsonObject& ficha, const QString& texto, const QJsonObject& valores,
                                QString* error, const QString& carpeta) {
    const auto falla = [error](const QString& m) {
        if (error != nullptr) *error = m;
        return QJsonObject{};
    };
    // Lo que no venga sale del porDefecto de la ficha: el guion suele traer
    // solo el nombre del orador, y probar sin un color dejaría «{acento}».
    QJsonObject v = valoresPorDefecto(ficha);
    for (auto it = valores.begin(); it != valores.end(); ++it) v[it.key()] = it.value();
    QJsonObject p{{"parametros", v}};
    if (!carpeta.isEmpty()) p["carpeta"] = carpeta;
    if (ficha.contains("declarativo")) {
        QJsonParseError pe{};
        const QJsonDocument d = QJsonDocument::fromJson(texto.toUtf8(), &pe);
        if (!d.isObject()) return falla(QCoreApplication::translate("graficos", "la descripción no es JSON válido: %1").arg(pe.errorString()));
        // La descripción trae su propio tamaño: no se lo pisa.
        p["declarativo"] = d.object();
    } else {
        if (texto.trimmed().isEmpty())
            return falla(QCoreApplication::translate("graficos", "el gráfico no tiene ni «declarativo» en su grafico.json ni un grafico.qml"));
        // Como texto, igual que sale al aire: lo que se prueba es lo que se ve.
        p["fuente"] = texto;
        p["ancho"] = ficha.value("ancho").toInt(900);
        p["alto"] = ficha.value("alto").toInt(220);
    }
    if (error != nullptr) error->clear();
    return p;
}

QString Catalogo::crear(const QString& nombre, bool declarativo, QString* creado) {
    const QString limpio = QString(nombre).remove(QRegularExpression("[^A-Za-z0-9 _-]")).trimmed();
    if (limpio.isEmpty()) return QCoreApplication::translate("graficos", "ese nombre no sirve");
    const QString c = QDir(raiz_).filePath(limpio);
    if (QFileInfo::exists(c)) return QCoreApplication::translate("graficos", "ya hay un gráfico con ese nombre");
    if (!QDir().mkpath(c)) return QCoreApplication::translate("graficos", "no se pudo crear la carpeta");

    // El texto de muestra sale al aire: en el idioma del evento al crearlo. Queda escrito
    // en el gráfico (el operador lo edita): no se rehace si después cambia el idioma.
    const QString hola = QString::fromStdString(ba::i18n::traducirEvento("graficos", "Hola"));
    QString holaQml = hola;
    holaQml.replace(QLatin1Char('\\'), QLatin1String("\\\\")).replace(QLatin1Char('"'), QLatin1String("\\\""));
    QJsonObject f{{"nombre", limpio},
                  {"descripcion", ""},
                  {"ancho", 900},
                  {"alto", 220},
                  {"parametros", QJsonArray{QJsonObject{{"clave", "texto"},
                                                        {"titulo", QCoreApplication::translate("graficos", "Texto")},
                                                        {"tipo", "texto"},
                                                        {"porDefecto", hola},
                                                        {"ayuda", QCoreApplication::translate("graficos", "Lo que dice el gráfico.")}}}}};
    if (declarativo) {
        f["declarativo"] =
            QJsonObject{{"ancho", 900},
                        {"alto", 220},
                        {"capas", QJsonArray{QJsonObject{{"tipo", "panel"},
                                                         {"izquierda", 0},
                                                         {"abajo", 30},
                                                         {"alto", 100},
                                                         {"ancho", 500},
                                                         {"color", "#12151a"}},
                                             QJsonObject{{"tipo", "texto"},
                                                         {"id", "t"},
                                                         {"valor", "{texto}"},
                                                         {"tamano", 40},
                                                         {"izquierda", 28},
                                                         {"abajo", 60},
                                                         {"aparece", 200}}}},
                        {"entrada", QJsonObject{{"efecto", "desliza-izquierda"}, {"duracion", 450}}}};
    } else if (!escribir(QDir(c).filePath("grafico.qml"),
                         (QStringLiteral("import QtQuick\n\nItem {\n    id: raiz\n    width: 900\n    height: 220\n\n"
                                         "    property string texto: \"")
                          + holaQml
                          + QStringLiteral("\"\n\n"
                                           "    Text {\n        text: raiz.texto\n        color: \"white\"\n"
                                           "        font.pixelSize: 40\n        renderType: Text.QtRendering\n"
                                           "        y: raiz.height - 90\n        opacity: 0\n"
                                           "        NumberAnimation on opacity { to: 1; duration: 400; running: true }\n"
                                           "    }\n}\n"))
                             .toUtf8())) {
        return QCoreApplication::translate("graficos", "no se pudo escribir el grafico.qml");
    }
    if (!escribir(QDir(c).filePath("grafico.json"), QJsonDocument(f).toJson()))
        return QCoreApplication::translate("graficos", "no se pudo escribir el grafico.json");
    if (creado != nullptr) *creado = limpio;
    return {};
}

QString Catalogo::guardar(const QString& nombre, const QString& texto) {
    QString err;
    const QString c = carpeta(nombre, &err);
    if (c.isEmpty()) return err;
    QJsonObject f = leerJson(QDir(c).filePath("grafico.json"));
    if (f.contains("declarativo")) {
        QJsonParseError pe{};
        const QJsonDocument doc = QJsonDocument::fromJson(texto.toUtf8(), &pe);
        if (!doc.isObject()) return QCoreApplication::translate("graficos", "la descripción no es JSON válido (%1)").arg(pe.errorString());
        f["declarativo"] = doc.object();
        if (!escribir(QDir(c).filePath("grafico.json"), QJsonDocument(f).toJson()))
            return QCoreApplication::translate("graficos", "no se pudo escribir el grafico.json");
    } else if (!escribir(QDir(c).filePath("grafico.qml"), texto.toUtf8())) {
        return QCoreApplication::translate("graficos", "no se pudo escribir el grafico.qml");
    }
    return {};
}

bool Catalogo::sabeCambiar(const QJsonObject& ficha) {
    if (ficha.contains("cambio")) return ficha.value("cambio").toBool(true) || ficha.value("cambio").isObject();
    for (const QJsonValue& v : ficha.value("parametros").toArray())
        if (v.toObject().value("tipo").toString() == "datos") return true;
    // Un declarativo con una torta o unas barras, aunque sus datos vengan fijos.
    for (const QJsonValue& v : ficha.value("declarativo").toObject().value("capas").toArray()) {
        const QString t = v.toObject().value("tipo").toString();
        if (t == "torta" || t == "barras") return true;
    }
    return false;
}

int Catalogo::duracionCambio(const QJsonObject& ficha) {
    return std::clamp(ficha.value("cambio").toObject().value("duracion").toInt(700), 100, 5000);
}

QString Catalogo::guardarDePlugin(const QString& plugin, const QJsonObject& d, QString* nombre) {
    const QString n = d.value("nombre").toString();
    const QString limpio = QString("plugin-%1-%2").arg(plugin, n).remove(QRegularExpression("[^A-Za-z0-9 _-]")).trimmed();
    if (plugin.isEmpty() || n.isEmpty()) return QCoreApplication::translate("graficos", "el gráfico del plugin no tiene nombre");
    const QString c = QDir(raiz_).filePath(limpio);
    if (!QDir().mkpath(c)) return QCoreApplication::translate("graficos", "no se pudo crear la carpeta del gráfico");
    // La ficha sale de la declaración: lo que el resto de la app necesita para
    // listarlo, llenarle los campos y saber a quién pedírselo.
    QJsonObject f{{"nombre", d.value("titulo").toString(n)},
                  {"descripcion", d.value("descripcion").toString()},
                  {"categoria", d.value("categoria").toString("Plugins")},
                  {"ancho", d.value("ancho").toInt(900)},
                  {"alto", d.value("alto").toInt(220)},
                  {"duracionMs", std::clamp(d.value("duracion_ms").toInt(2000), 100, 60000)},
                  {"parametros", d.value("parametros").toArray()},
                  {"plugin", plugin},
                  {"graficoPlugin", n},
                  {"modo", d.value("modo").toString("describe")},
                  {"bucle", d.value("bucle").toBool(false)}};
    if (d.contains("cambio")) f["cambio"] = d.value("cambio");
    if (d.contains("acciones")) f["acciones"] = d.value("acciones");
    if (d.contains("seVa")) f["seVa"] = d.value("seVa");
    const QString ruta = QDir(c).filePath("grafico.json");
    if (leerJson(ruta) != f && !escribir(ruta, QJsonDocument(f).toJson()))
        return QCoreApplication::translate("graficos", "no se pudo escribir el grafico.json");
    if (nombre != nullptr) *nombre = limpio;
    return {};
}

QString Catalogo::seVaDe(const QString& nombre) const {
    const QString propio = ficha(nombre).value("seVa").toString();
    if (!propio.isEmpty()) return propio;
    if (nombre.contains('/') || nombre.contains('\\') || nombre.contains("..")) return "queda";
    QFile f(QString::fromStdU16String((ba::platform::bundleRoot() / "Graficos" / nombre.toStdU16String() / "grafico.json").u16string()));
    if (f.open(QIODevice::ReadOnly)) {
        const QString fab = QJsonDocument::fromJson(f.readAll()).object().value("seVa").toString();
        if (!fab.isEmpty()) return fab;
    }
    return "queda";
}

} // namespace ba::app::graficos
