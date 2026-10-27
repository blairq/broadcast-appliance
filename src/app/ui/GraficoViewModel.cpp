// ============================================================================
// src/app/ui/GraficoViewModel.cpp — ver GraficoViewModel.hpp
// ============================================================================
#include "ui/GraficoViewModel.hpp"

#include <algorithm>

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>

#include "arranque/AppRuntime.hpp"
#include "graficos/Catalogo.hpp"
#include "graficos/Emisor.hpp"
#include "graficos/GraficoProceso.hpp"
#include "graficos/GraficoSource.hpp"
#include "graficos/Medios.hpp"
#include "graficos/VideoEnGrafico.hpp"

namespace ba::app {

namespace {
/// Cada cuánto avanza la reproducción de la vista previa. Es el pedido de un
/// cuadro por vez al renderizador (que sigue la misma instancia hacia adelante:
/// barato); si un cuadro tarda más, el reloj no se atrasa, se saltea.
constexpr int kPasoReproduccionMs = 33;
} // namespace

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
    /// Cada redibujado lleva un número: lo que contesta tarde un plugin no pisa
    /// un dibujo más nuevo.
    int pedidoN = 0;
    QTimer reloj;
    QElapsedTimer desde;
    int desdeMs = 0;

    Impl(AppRuntime& r, graficos::Emisor& e) : rt(r), em(e) {}
    graficos::GraficoProceso& proc() const { return em.proceso(); }

    graficos::Catalogo& catalogo() const { return em.catalogo(); }
    bool esDeclarativo() const { return ficha.contains("declarativo"); }
    QString plugin() const { return graficos::Catalogo::pluginDe(ficha); }

    /// El pedido para el renderizador con lo que hay EN EL EDITOR (no en
    /// disco): es lo que hace que la vista previa muestre lo que se escribe.
    /// Lo arma el catálogo, igual que para lo guardado, con la carpeta del
    /// gráfico para sus medios.
    QJsonObject pedido(QString* err) const {
        return graficos::Catalogo::pedidoCon(ficha, fuente, valores, err, catalogo().carpeta(abierto));
    }
};

GraficoViewModel::GraficoViewModel(AppRuntime& rt, graficos::Emisor& emisor, QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>(rt, emisor)) {
    d_->reloj.setInterval(kPasoReproduccionMs);
    connect(&d_->reloj, &QTimer::timeout, this, &GraficoViewModel::avanzarReproduccion);
    // La lista cambia sola cuando un plugin declara un gráfico.
    connect(&emisor, &graficos::Emisor::catalogoCambio, this, &GraficoViewModel::listaChanged);
    connect(&emisor.medios(), &graficos::Medios::cambio, this, [this](const QString& g) {
        if (g == d_->abierto) {
            emit mediosChanged();
            redibujar();
        }
    });
    connect(&emisor.medios(), &graficos::Medios::importandoChanged, this, &GraficoViewModel::importandoChanged);
}

GraficoViewModel::~GraficoViewModel() = default;

bool GraficoViewModel::disponible() const { return d_->proc().disponible(); }
QString GraficoViewModel::abierto() const { return d_->abierto; }
QString GraficoViewModel::fuente() const { return d_->fuente; }
bool GraficoViewModel::declarativo() const { return d_->esDeclarativo(); }
QString GraficoViewModel::plugin() const { return d_->plugin(); }
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

QImage GraficoViewModel::imagen() const { return d_->vista; }
QString GraficoViewModel::error() const { return d_->error; }
int GraficoViewModel::duracionMs() const { return d_->duracionMs; }
int GraficoViewModel::momentoMs() const { return d_->momentoMs; }
bool GraficoViewModel::reproduciendo() const { return d_->reloj.isActive(); }
bool GraficoViewModel::importando() const { return d_->em.medios().importando(); }

QVariantList GraficoViewModel::medios() const {
    return d_->abierto.isEmpty() ? QVariantList{} : d_->em.medios().lista(d_->abierto).toVariantList();
}

QVariantList GraficoViewModel::recuadros() const {
    QVariantList out;
    if (d_->abierto.isEmpty()) return out;
    // Lo mismo que va a salir: la declaración del editor (o de la ficha), la línea de tiempo
    // con «cuándo se va» y la cuenta del motor (Claves.hpp) en el momento de la barra.
    QString err;
    QJsonObject p = d_->pedido(&err);
    if (!err.isEmpty()) p = {};
    const int salidaEn = d_->valores.value("salidaEn").toVariant().toInt(); // del campo llega como texto
    const int salidaDur = p.value("declarativo").toObject().value("salida").toObject().value("duracion").toInt(800);
    for (const auto& c : graficos::video::leer(d_->ficha, p, nullptr, true)) {
        const auto k = graficos::video::en(graficos::video::lineaDeTiempo(c, salidaEn, salidaDur), d_->momentoMs);
        out.append(QVariantMap{{"fuente", QString::fromStdString(c.fuente)},
                               {"x", k.x},
                               {"y", k.y},
                               {"ancho", k.ancho},
                               {"alto", k.alto},
                               {"alfa", k.alfa},
                               {"arriba", c.arriba}});
    }
    return out;
}

double GraficoViewModel::aspecto() const {
    if (d_->vista.isNull() || d_->vista.height() <= 0) return 0;
    return static_cast<double>(d_->vista.width()) / d_->vista.height();
}

void GraficoViewModel::setFuente(const QString& f) {
    if (d_->fuente == f) return;
    d_->fuente = f;
    emit fuenteChanged();
    emit sucioChanged();
    emit recuadrosChanged();
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
    emit vistaChanged();
    emit recuadrosChanged();
    redibujar();
}

void GraficoViewModel::setReproduciendo(bool r) {
    if (r == d_->reloj.isActive()) return;
    if (r) {
        // Terminada, vuelve a empezar (como el play de cualquier reproductor).
        if (d_->momentoMs >= d_->duracionMs) setMomentoMs(0);
        d_->desdeMs = d_->momentoMs;
        d_->desde.start();
        d_->reloj.start();
    } else {
        d_->reloj.stop();
    }
    emit reproduciendoChanged();
}

void GraficoViewModel::avanzarReproduccion() {
    // El tiempo de verdad, no «un paso por tic»: si el renderizador tarda, la
    // animación dura lo que dura y se saltean cuadros (se ve como va a salir).
    const int ms = d_->desdeMs + static_cast<int>(d_->desde.elapsed());
    if (ms >= d_->duracionMs) {
        setMomentoMs(d_->duracionMs);
        setReproduciendo(false);
        return;
    }
    setMomentoMs(ms);
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
    setReproduciendo(false);
    d_->abierto = nombre;
    d_->ficha = d_->catalogo().ficha(nombre);
    d_->fuente = d_->catalogo().texto(nombre);
    d_->fuenteEnDisco = d_->fuente;
    // Los valores arrancan en los que el gráfico declara por defecto.
    d_->valores = graficos::Catalogo::valoresPorDefecto(d_->ficha);
    d_->momentoMs = 0;
    if (d_->ficha.contains("duracionMs"))
        d_->duracionMs = std::clamp(d_->ficha.value("duracionMs").toInt(1500), 100, graficos::kMaxSegundos * 1000);
    d_->vista = {}; // el cuadro de otro gráfico no tiene nada que hacer acá
    emit imagenChanged();
    emit abiertoChanged();
    emit fuenteChanged();
    emit valoresChanged();
    emit sucioChanged();
    emit mediosChanged();
    emit vistaChanged();
    emit recuadrosChanged();
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
    if (d_->abierto.isEmpty() || !d_->plugin().isEmpty()) return false;
    const QString err = d_->catalogo().guardar(d_->abierto, d_->fuente);
    if (!err.isEmpty()) {
        emit notify(tr("No se guardó: %1").arg(err), "error");
        return false;
    }
    d_->ficha = d_->catalogo().ficha(d_->abierto);
    d_->fuenteEnDisco = d_->fuente;
    emit sucioChanged();
    emit recuadrosChanged();
    emit notify(tr("Gráfico guardado"), "ok");
    return true;
}

void GraficoViewModel::setValor(const QString& clave, const QVariant& valor) {
    d_->valores[clave] = QJsonValue::fromVariant(valor);
    emit valoresChanged();
    emit recuadrosChanged();
    redibujar();
}

void GraficoViewModel::redibujar() {
    if (d_->abierto.isEmpty() || !d_->proc().disponible()) return;
    const int n = ++d_->pedidoN;
    // Un cuadro con UNA tira de un momento: el renderizador sigue con la misma
    // instancia mientras el tiempo avance (la reproducción es barata) y por
    // memoria compartida (sin PNG ni base64 de ida y vuelta).
    const auto dibujar = [this](const QJsonObject& p) {
        QString err;
        const auto cuadros = d_->proc().tira(p, QVector<int>{d_->momentoMs}, &err);
        if (err.isEmpty() && !cuadros.isEmpty() && !cuadros.front().imagen.isNull()) {
            d_->vista = cuadros.front().imagen;
            emit imagenChanged();
        } else if (err.isEmpty()) {
            err = "el renderizador no devolvió el cuadro";
        }
        if (err != d_->error) {
            d_->error = err;
            emit vistaChanged();
        }
    };
    const auto conError = [this](const QString& err) {
        // Con un error queda el último cuadro bueno: lo que se ve es lo que había,
        // y el error se lee abajo (vaciar la vista era otro parpadeo).
        if (err != d_->error) {
            d_->error = err;
            emit vistaChanged();
        }
    };
    if (!d_->plugin().isEmpty()) {
        // Lo dibuja un plugin: el pedido lo arma el emisor (y lo recuerda, así
        // que pedirlo en cada cuadro no vuelve a molestar al plugin).
        QPointer<GraficoViewModel> yo(this);
        d_->em.resolver(d_->abierto, d_->valores, {}, [yo, n, dibujar, conError](QJsonObject p, QString err) {
            if (!yo || n != yo->d_->pedidoN) return; // ya se pidió otro
            if (!err.isEmpty()) return conError(err);
            dibujar(p);
        });
        return;
    }
    QString err;
    const QJsonObject p = d_->pedido(&err);
    if (!err.isEmpty()) return conError(err);
    dibujar(p);
}

void GraficoViewModel::agregarMedio(const QUrl& archivo) {
    if (d_->abierto.isEmpty()) return;
    const QString ruta = archivo.isLocalFile() ? archivo.toLocalFile() : archivo.toString();
    QPointer<GraficoViewModel> yo(this);
    d_->em.medios().importar(d_->abierto, ruta, {}, [yo](QJsonObject r, QString err) {
        if (!yo) return;
        if (!err.isEmpty()) return emit yo->notify(tr("No se agregó el medio: %1").arg(err), "error");
        QString aviso = "Medio agregado: " + r.value("archivo").toString();
        if (r.value("tipo").toString() == "secuencia" && r.contains("alfa") && !r.value("alfa").toBool())
            aviso += " (ojo: el video no trae transparencia, va a tapar lo de atrás)";
        emit yo->notify(aviso, "ok");
    });
}

void GraficoViewModel::quitarMedio(const QString& archivo) {
    if (d_->abierto.isEmpty()) return;
    const QString err = d_->em.medios().quitar(d_->abierto, archivo);
    if (!err.isEmpty()) emit notify(err, "error");
}

QString GraficoViewModel::mostrarEnEscena(qulonglong fuente, bool repetir) {
    // De un plugin: lo que sale es lo guardado (no hay texto en el editor).
    if (!d_->plugin().isEmpty()) {
        const QString e = d_->em.mostrar(d_->abierto, d_->valores, fuente, d_->duracionMs, repetir);
        if (e.isEmpty()) emit escenaChanged();
        return e;
    }
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
