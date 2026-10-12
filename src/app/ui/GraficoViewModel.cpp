// ============================================================================
// src/app/ui/GraficoViewModel.cpp — ver GraficoViewModel.hpp
// ============================================================================
#include "ui/GraficoViewModel.hpp"

#include <algorithm>

#include <QBuffer>
#include <QJsonArray>
#include <QJsonObject>

#include "arranque/AppRuntime.hpp"
#include "graficos/Catalogo.hpp"
#include "graficos/Emisor.hpp"
#include "graficos/GraficoProceso.hpp"
#include "graficos/GraficoSource.hpp"

namespace ba::app {

struct GraficoViewModel::Impl {
    AppRuntime& rt;
    /// El renderizador es uno solo para toda la app: el editor comparte el
    /// mismo `ba-grafico` que el guion, el rotulador y el asistente.
    graficos::Emisor& em;
    QString abierto;
    QString fuente; ///< lo que está en el editor
    QString fuenteEnDisco;
    QJsonObject ficha;   ///< grafico.json
    QJsonObject valores; ///< valores actuales de los parámetros
    QImage vista;
    QString error;
    int duracionMs = 1500;
    int momentoMs = 0;

    Impl(AppRuntime& r, graficos::Emisor& e) : rt(r), em(e) {}
    graficos::GraficoProceso& proc() const { return em.proceso(); }

    graficos::Catalogo& catalogo() const { return em.catalogo(); }
    bool esDeclarativo() const { return ficha.contains("declarativo"); }

    /// El pedido para el renderizador con lo que hay EN EL EDITOR (no en
    /// disco): es lo que hace que la vista previa muestre lo que se escribe.
    /// Lo arma el catálogo, igual que para lo guardado.
    QJsonObject pedido(QString* err) const { return graficos::Catalogo::pedidoCon(ficha, fuente, valores, err); }
};

GraficoViewModel::GraficoViewModel(AppRuntime& rt, graficos::Emisor& emisor, QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>(rt, emisor)) {}

GraficoViewModel::~GraficoViewModel() = default;

bool GraficoViewModel::disponible() const { return d_->proc().disponible(); }
QString GraficoViewModel::abierto() const { return d_->abierto; }
QString GraficoViewModel::fuente() const { return d_->fuente; }
bool GraficoViewModel::declarativo() const { return d_->esDeclarativo(); }
bool GraficoViewModel::sucio() const { return d_->fuente != d_->fuenteEnDisco; }
QVariantMap GraficoViewModel::valores() const { return d_->valores.toVariantMap(); }
QVariantList GraficoViewModel::acciones() const {
    QVariantList out;
    for (const QJsonValue& v : d_->ficha.value("acciones").toArray()) {
        const QJsonObject a = v.toObject();
        out.append(QVariantMap{{"titulo", a.value("titulo").toString()},
                               {"prompt", a.value("prompt").toString()},
                               {"icono", a.value("icono").toString("✦")}});
    }
    return out;
}

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

QVariantList GraficoViewModel::graficos() const { return d_->catalogo().lista().toVariantList(); }

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
    QString err;
    if (d_->catalogo().carpeta(nombre, &err).isEmpty()) {
        emit notify(err, "error");
        return false;
    }
    d_->abierto = nombre;
    d_->ficha = d_->catalogo().ficha(nombre);
    d_->fuente = d_->catalogo().texto(nombre);
    d_->fuenteEnDisco = d_->fuente;
    // Los valores arrancan en los que el gráfico declara por defecto.
    d_->valores = graficos::Catalogo::valoresPorDefecto(d_->ficha);
    d_->momentoMs = 0;
    emit abiertoChanged();
    emit fuenteChanged();
    emit valoresChanged();
    emit sucioChanged();
    redibujar();
    return true;
}

QString GraficoViewModel::crear(const QString& nombre, bool declarativo) {
    QString creado;
    const QString err = d_->catalogo().crear(nombre, declarativo, &creado);
    if (!err.isEmpty()) return err;
    emit listaChanged();
    abrir(creado);
    return {};
}

bool GraficoViewModel::guardar() {
    if (d_->abierto.isEmpty()) return false;
    const QString err = d_->catalogo().guardar(d_->abierto, d_->fuente);
    if (!err.isEmpty()) {
        emit notify("No se guardó: " + err, "error");
        return false;
    }
    d_->ficha = d_->catalogo().ficha(d_->abierto);
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
    if (d_->abierto.isEmpty() || !d_->proc().disponible()) return;
    QString err;
    const QJsonObject p = d_->pedido(&err);
    if (!err.isEmpty()) {
        d_->error = err;
        d_->vista = {};
        emit vistaChanged();
        return;
    }
    d_->vista = d_->proc().dibujar(p, d_->momentoMs, &err);
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
    // Sale al aire lo que hay EN EL EDITOR, no lo que está guardado: probar un
    // cambio sin guardarlo es media razón de ser del editor. Por eso se arma
    // el pedido acá y se le pasa ya hecho al emisor, que es quien sabe
    // dibujarlo entero y reproducirlo.
    QString err;
    const QJsonObject p = d_->pedido(&err);
    if (!err.isEmpty()) return err;
    const QString e = d_->em.reproducir(p, d_->abierto, d_->valores, fuente, d_->duracionMs, repetir);
    if (e.isEmpty()) emit escenaChanged();
    return e;
}

QString GraficoViewModel::ocultarDeEscena(qulonglong fuente) {
    const QString e = d_->em.ocultar(fuente);
    if (e.isEmpty()) emit escenaChanged();
    return e;
}

} // namespace ba::app
