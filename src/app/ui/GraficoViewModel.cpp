// ============================================================================
// src/app/ui/GraficoViewModel.cpp — ver GraficoViewModel.hpp
// ============================================================================
#include "ui/GraficoViewModel.hpp"

#include <algorithm>

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "AppRuntime.hpp"
#include "graficos/GraficoProceso.hpp"
#include "graficos/GraficoSource.hpp"

namespace ba::app {
namespace {

QJsonObject leerJson(const QString& ruta) {
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

bool escribir(const QString& ruta, const QByteArray& datos) {
    // A un temporal y renombrar: si se corta la luz, el archivo que estaba
    // sigue entero.
    QFile f(ruta + ".nuevo");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(datos);
    f.close();
    QFile::remove(ruta);
    return QFile::rename(ruta + ".nuevo", ruta);
}

} // namespace

struct GraficoViewModel::Impl {
    AppRuntime& rt;
    graficos::GraficoProceso proc;
    QString abierto;
    QString fuente;      ///< lo que está en el editor
    QString fuenteEnDisco;
    QJsonObject ficha;   ///< grafico.json
    QJsonObject valores; ///< valores actuales de los parámetros
    QImage vista;
    QString error;
    int duracionMs = 1500;
    int momentoMs = 0;

    explicit Impl(AppRuntime& r) : rt(r) {}

    QString raiz() const { return QString::fromStdString((rt.dataRoot() / "Graficos").string()); }
    QString carpeta(const QString& n) const { return QDir(raiz()).filePath(n); }
    bool esDeclarativo() const { return ficha.contains("declarativo"); }

    /// El pedido para el renderizador con lo que hay EN EL EDITOR (no en
    /// disco): es lo que hace que la vista previa muestre lo que se escribe.
    QJsonObject pedido(QString* err) const {
        QJsonObject p{{"parametros", valores}};
        if (esDeclarativo()) {
            QJsonParseError pe{};
            const QJsonDocument d = QJsonDocument::fromJson(fuente.toUtf8(), &pe);
            if (!d.isObject()) {
                if (err != nullptr) *err = "la descripción no es JSON válido: " + pe.errorString();
                return {};
            }
            p["declarativo"] = d.object();
        } else {
            p["fuente"] = fuente;
            p["ancho"] = ficha.value("ancho").toInt(900);
            p["alto"] = ficha.value("alto").toInt(220);
        }
        return p;
    }
};

GraficoViewModel::GraficoViewModel(AppRuntime& rt, QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>(rt)) {
    QDir().mkpath(d_->raiz());
}

GraficoViewModel::~GraficoViewModel() = default;

bool GraficoViewModel::disponible() const { return d_->proc.disponible(); }
QString GraficoViewModel::abierto() const { return d_->abierto; }
QString GraficoViewModel::fuente() const { return d_->fuente; }
bool GraficoViewModel::declarativo() const { return d_->esDeclarativo(); }
bool GraficoViewModel::sucio() const { return d_->fuente != d_->fuenteEnDisco; }
QVariantMap GraficoViewModel::valores() const { return d_->valores.toVariantMap(); }
QString GraficoViewModel::error() const { return d_->error; }
int GraficoViewModel::duracionMs() const { return d_->duracionMs; }
int GraficoViewModel::momentoMs() const { return d_->momentoMs; }

void GraficoViewModel::setFuente(const QString& f) {
    if (d_->fuente == f) return;
    d_->fuente = f;
    emit fuenteChanged();
    emit sucioChanged();
    redibujar();
}

void GraficoViewModel::setDuracionMs(int ms) {
    d_->duracionMs = std::clamp(ms, 100, graficos::kMaxSegundos * 1000);
    emit vistaChanged();
}

void GraficoViewModel::setMomentoMs(int ms) {
    const int nuevo = std::clamp(ms, 0, d_->duracionMs);
    if (nuevo == d_->momentoMs) return;
    d_->momentoMs = nuevo;
    redibujar();
}

QVariantList GraficoViewModel::graficos() const {
    QVariantList out;
    for (const QString& n : QDir(d_->raiz()).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QJsonObject f = leerJson(QDir(d_->carpeta(n)).filePath("grafico.json"));
        out.append(QVariantMap{{"nombre", n},
                               {"titulo", f.value("nombre").toString(n)},
                               {"descripcion", f.value("descripcion").toString()},
                               {"declarativo", f.contains("declarativo")}});
    }
    return out;
}

QVariantList GraficoViewModel::parametros() const {
    QVariantList out;
    for (const QJsonValue& v : d_->ficha.value("parametros").toArray()) {
        const QJsonObject p = v.toObject();
        out.append(QVariantMap{{"clave", p.value("clave").toString()},
                               {"titulo", p.value("titulo").toString()},
                               {"tipo", p.value("tipo").toString("texto")},
                               {"ayuda", p.value("ayuda").toString()}});
    }
    return out;
}

QVariantList GraficoViewModel::enEscena() const {
    QVariantList out;
    for (const auto& i : graficos::instancias())
        out.append(QVariantMap{{"id", static_cast<qulonglong>(i.id)},
                               {"fuente", QString::fromStdString(i.nombre)},
                               {"ancho", i.ancho},
                               {"alto", i.alto},
                               {"reproduciendo", i.reproduciendo}});
    return out;
}

void GraficoViewModel::refrescar() {
    emit listaChanged();
    emit escenaChanged();
}

bool GraficoViewModel::abrir(const QString& nombre) {
    const QString c = d_->carpeta(nombre);
    if (!QFileInfo(c).isDir()) {
        emit notify("No existe el gráfico «" + nombre + "»", "error");
        return false;
    }
    d_->abierto = nombre;
    d_->ficha = leerJson(QDir(c).filePath("grafico.json"));

    if (d_->esDeclarativo()) {
        // Se edita la DESCRIPCIÓN, no el QML: el QML lo genera el motor.
        d_->fuente = QString::fromUtf8(
            QJsonDocument(d_->ficha.value("declarativo").toObject()).toJson(QJsonDocument::Indented));
    } else {
        QFile f(QDir(c).filePath("grafico.qml"));
        d_->fuente = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }
    d_->fuenteEnDisco = d_->fuente;

    // Los valores arrancan en los que el gráfico declara por defecto.
    d_->valores = {};
    for (const QJsonValue& v : d_->ficha.value("parametros").toArray()) {
        const QJsonObject p = v.toObject();
        d_->valores[p.value("clave").toString()] = p.value("porDefecto");
    }
    d_->momentoMs = 0;
    emit abiertoChanged();
    emit fuenteChanged();
    emit valoresChanged();
    emit sucioChanged();
    redibujar();
    return true;
}

QString GraficoViewModel::crear(const QString& nombre, bool declarativo) {
    const QString limpio = QString(nombre).remove(QRegularExpression("[^A-Za-z0-9 _-]")).trimmed();
    if (limpio.isEmpty()) return "ese nombre no sirve";
    const QString c = d_->carpeta(limpio);
    if (QFileInfo::exists(c)) return "ya hay un gráfico con ese nombre";
    if (!QDir().mkpath(c)) return "no se pudo crear la carpeta";

    QJsonObject ficha{{"nombre", limpio},
                      {"descripcion", ""},
                      {"ancho", 900},
                      {"alto", 220},
                      {"parametros", QJsonArray{QJsonObject{{"clave", "texto"},
                                                            {"titulo", "Texto"},
                                                            {"tipo", "texto"},
                                                            {"porDefecto", "Hola"},
                                                            {"ayuda", "Lo que dice el gráfico."}}}}};
    if (declarativo) {
        ficha["declarativo"] = QJsonObject{
            {"ancho", 900},
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
    } else {
        escribir(QDir(c).filePath("grafico.qml"),
                 "import QtQuick\n\nItem {\n    id: raiz\n    width: 900\n    height: 220\n\n"
                 "    property string texto: \"Hola\"\n\n"
                 "    Text {\n        text: raiz.texto\n        color: \"white\"\n"
                 "        font.pixelSize: 40\n        renderType: Text.QtRendering\n"
                 "        y: raiz.height - 90\n        opacity: 0\n"
                 "        NumberAnimation on opacity { to: 1; duration: 400; running: true }\n"
                 "    }\n}\n");
    }
    escribir(QDir(c).filePath("grafico.json"), QJsonDocument(ficha).toJson());
    emit listaChanged();
    abrir(limpio);
    return {};
}

bool GraficoViewModel::guardar() {
    if (d_->abierto.isEmpty()) return false;
    const QString c = d_->carpeta(d_->abierto);
    if (d_->esDeclarativo()) {
        QJsonParseError pe{};
        const QJsonDocument doc = QJsonDocument::fromJson(d_->fuente.toUtf8(), &pe);
        if (!doc.isObject()) {
            emit notify("No se guardó: la descripción no es JSON válido (" + pe.errorString() + ")",
                        "error");
            return false;
        }
        d_->ficha["declarativo"] = doc.object();
        escribir(QDir(c).filePath("grafico.json"), QJsonDocument(d_->ficha).toJson());
    } else {
        escribir(QDir(c).filePath("grafico.qml"), d_->fuente.toUtf8());
    }
    d_->fuenteEnDisco = d_->fuente;
    emit sucioChanged();
    emit notify("Gráfico guardado", "ok");
    return true;
}

void GraficoViewModel::setValor(const QString& clave, const QVariant& valor) {
    d_->valores[clave] = QJsonValue::fromVariant(valor);
    emit valoresChanged();
    redibujar();
}

void GraficoViewModel::redibujar() {
    if (d_->abierto.isEmpty() || !d_->proc.disponible()) return;
    QString err;
    const QJsonObject p = d_->pedido(&err);
    if (!err.isEmpty()) {
        d_->error = err;
        d_->vista = {};
        emit vistaChanged();
        return;
    }
    d_->vista = d_->proc.dibujar(p, d_->momentoMs, &err);
    d_->error = err;
    emit vistaChanged();
}

QString GraficoViewModel::vistaPreviaUrl() const {
    if (d_->vista.isNull()) return {};
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    d_->vista.save(&buf, "PNG");
    return "data:image/png;base64," + QString::fromLatin1(bytes.toBase64());
}

QString GraficoViewModel::mostrarEnEscena(qulonglong fuente, bool repetir) {
    if (!d_->proc.disponible()) return "el renderizador no está en este bundle";
    int ancho = 900;
    int alto = 220;
    bool existe = false;
    for (const auto& i : graficos::instancias())
        if (i.id == fuente) {
            ancho = i.ancho;
            alto = i.alto;
            existe = true;
        }
    if (!existe) return "esa fuente ya no está en ninguna escena";

    QString err;
    QJsonObject p = d_->pedido(&err);
    if (!err.isEmpty()) return err;
    p["ancho"] = ancho;
    p["alto"] = alto;

    constexpr int kFps = 30;
    const int total = std::max(1, d_->duracionMs * kFps / 1000);
    std::vector<QImage> cuadros;
    for (int desde = 0; desde < total;) {
        QVector<int> momentos;
        for (int k = 0; k < 12 && desde < total; ++k, ++desde)
            momentos.append(desde * 1000 / kFps);
        const auto parte = d_->proc.tira(p, momentos, &err);
        if (!err.isEmpty()) return err;
        for (const auto& c : parte)
            cuadros.push_back(c.imagen);
    }
    if (!graficos::reproducir(fuente, std::move(cuadros), kFps, repetir))
        return "no se pudo reproducir";
    emit escenaChanged();
    return {};
}

QString GraficoViewModel::ocultarDeEscena(qulonglong fuente) {
    if (!graficos::ocultar(fuente)) return "esa fuente ya no está";
    emit escenaChanged();
    return {};
}

} // namespace ba::app
