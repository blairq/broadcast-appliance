// ============================================================================
// src/app/ui/RotuladorViewModel.cpp — ver RotuladorViewModel.hpp
// ============================================================================
#include "ui/RotuladorViewModel.hpp"

#include <algorithm>
#include <map>

#include <QJsonArray>
#include <QJsonObject>

#include "AppRuntime.hpp"
#include "graficos/Emisor.hpp"
#include "graficos/GraficoSource.hpp"

namespace ba::app {

struct RotuladorViewModel::Impl {
    AppRuntime& rt;
    graficos::Emisor& em;
    /// Lo que se está escribiendo, por fuente. Sobrevive a un cambio de escena
    /// y vuelta: el operador escribió el nombre, se fue a otra escena y al
    /// volver lo encuentra donde lo dejó.
    std::map<quint64, QVariantMap> valores;
    /// Qué gráfico eligió el operador para cada fuente.
    std::map<quint64, QString> elegido;

    Impl(AppRuntime& r, graficos::Emisor& e) : rt(r), em(e) {}

    /// Qué gráfico muestra esa fuente: el que eligieron, o el último que salió
    /// al aire, o el que diga la fuente en sus propiedades.
    QString graficoDe(const graficos::InstanciaInfo& i) const {
        const auto it = elegido.find(i.id);
        if (it != elegido.end() && !it->second.isEmpty()) return it->second;
        const QString ultimo = em.ultimoGrafico(i.id);
        if (!ultimo.isEmpty()) return ultimo;
        return QString::fromStdString(i.grafico);
    }
};

RotuladorViewModel::RotuladorViewModel(AppRuntime& rt, graficos::Emisor& emisor, QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>(rt, emisor)) {
    connect(&emisor, &graficos::Emisor::cambio, this, &RotuladorViewModel::cambio);
}

RotuladorViewModel::~RotuladorViewModel() = default;

QString RotuladorViewModel::escena() const {
    return QString::fromStdString(d_->rt.engine().currentScene());
}

QStringList RotuladorViewModel::graficos() const { return d_->em.nombres(); }

QVariantList RotuladorViewModel::fuentes() const {
    QVariantList out;
    const std::string escenaActual = d_->rt.engine().currentScene();
    const std::vector<std::string> enEscena = d_->rt.engine().sceneSources(escenaActual);

    for (const auto& i : graficos::instancias()) {
        // Solo las de la escena que se está viendo: en vivo, lo que no se ve
        // no se toca.
        if (std::find(enEscena.begin(), enEscena.end(), i.nombre) == enEscena.end()) continue;

        const QString grafico = d_->graficoDe(i);
        const QJsonObject ficha = d_->em.ficha(grafico);

        // Los valores que se muestran: lo que se está escribiendo, y si no, lo
        // último que salió al aire, y si no, el por defecto del gráfico.
        const QJsonObject ultimos = d_->em.ultimosValores(i.id);
        QVariantMap valores;
        for (const QJsonValue& v : ficha.value("parametros").toArray()) {
            const QString clave = v.toObject().value("clave").toString();
            if (clave.isEmpty()) continue;
            if (ultimos.contains(clave)) valores[clave] = ultimos.value(clave).toVariant();
            else valores[clave] = v.toObject().value("porDefecto").toVariant();
        }
        const auto it = d_->valores.find(i.id);
        if (it != d_->valores.end())
            for (auto v = it->second.begin(); v != it->second.end(); ++v) valores[v.key()] = v.value();

        QVariantList campos;
        for (const QJsonValue& v : ficha.value("parametros").toArray()) {
            const QJsonObject p = v.toObject();
            campos.append(QVariantMap{{"clave", p.value("clave").toString()},
                                      {"titulo", p.value("titulo").toString()},
                                      {"tipo", p.value("tipo").toString("texto")},
                                      {"ayuda", p.value("ayuda").toString()}});
        }

        // Los botones de prompt prearmado que trae el gráfico: lo que el
        // operador va a querer pedirle al asistente estando en esta tarjeta.
        QVariantList acciones;
        for (const QJsonValue& v : ficha.value("acciones").toArray()) {
            const QJsonObject a = v.toObject();
            acciones.append(QVariantMap{{"titulo", a.value("titulo").toString()},
                                        {"prompt", a.value("prompt").toString()},
                                        {"icono", a.value("icono").toString("✦")}});
        }

        out.append(QVariantMap{{"id", static_cast<qulonglong>(i.id)},
                               {"fuente", QString::fromStdString(i.nombre)},
                               {"grafico", grafico},
                               {"titulo", ficha.value("nombre").toString(grafico)},
                               {"campos", campos},
                               {"valores", valores},
                               {"acciones", acciones},
                               {"enAire", i.cuadros > 0},
                               {"reproduciendo", i.reproduciendo}});
    }
    return out;
}

void RotuladorViewModel::refrescar() { emit cambio(); }

void RotuladorViewModel::setGrafico(qulonglong fuente, const QString& grafico) {
    d_->elegido[fuente] = grafico;
    // Los valores que había eran de otro gráfico: no significan nada acá.
    d_->valores.erase(fuente);
    emit cambio();
}

void RotuladorViewModel::setValor(qulonglong fuente, const QString& clave, const QString& valor) {
    if (clave.isEmpty()) return;
    d_->valores[fuente][clave] = valor;
    // Sin `cambio`: el que escribe ya tiene el texto en pantalla, y refrescar
    // la lista en cada tecla le movería el cursor.
}

QString RotuladorViewModel::disparar(qulonglong fuente, bool repetir) {
    QString grafico;
    for (const auto& i : graficos::instancias())
        if (i.id == fuente) grafico = d_->graficoDe(i);
    if (grafico.isEmpty()) return "esa fuente no tiene ningún gráfico elegido";

    // Se manda la lista completa de valores (lo escrito sobre lo que ya había),
    // no solo lo que se tocó: así lo que sale al aire es lo que se ve en la
    // tarjeta, sin sorpresas.
    QJsonObject valores;
    for (const QVariant& f : fuentes()) {
        const QVariantMap m = f.toMap();
        if (m.value("id").toULongLong() != fuente) continue;
        const QVariantMap vs = m.value("valores").toMap();
        for (auto it = vs.begin(); it != vs.end(); ++it)
            valores[it.key()] = QJsonValue::fromVariant(it.value());
    }
    const QString err = d_->em.mostrar(grafico, valores, fuente, 0, repetir);
    if (!err.isEmpty()) emit notify("No se pudo sacar el gráfico al aire: " + err, "error");
    return err;
}

QString RotuladorViewModel::ocultar(qulonglong fuente) { return d_->em.ocultar(fuente); }

} // namespace ba::app
