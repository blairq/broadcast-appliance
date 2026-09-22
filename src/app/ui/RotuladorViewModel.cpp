// ============================================================================
// src/app/ui/RotuladorViewModel.cpp — ver RotuladorViewModel.hpp
// ============================================================================
#include "ui/RotuladorViewModel.hpp"

#include <algorithm>
#include <map>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include "AppRuntime.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/ConferenceClient.hpp"
#include "graficos/Emisor.hpp"
#include "graficos/Vinculos.hpp"
#include "graficos/GraficoSource.hpp"

namespace ba::app {

struct RotuladorViewModel::Impl {
    AppRuntime& rt;
    graficos::Emisor& em;
    ConferenceViewModel* sala = nullptr;
    graficos::Vinculos* vin = nullptr;
    /// El ida y vuelta con el buzon de B: publica lo que hay y se lleva lo que
    /// encargaron desde los celulares. Un viaje por segundo, en los dos
    /// sentidos a la vez (ver Rotulador.hpp en el gateway).
    QTimer sincro;
    QString ultimoPublicado;
    /// Lo que se está escribiendo, por fuente. Sobrevive a un cambio de escena
    /// y vuelta: el operador escribió el nombre, se fue a otra escena y al
    /// volver lo encuentra donde lo dejó.
    std::map<quint64, QVariantMap> valores;
    /// Qué gráfico eligió el operador para cada fuente.
    std::map<quint64, QString> elegido;

    Impl(AppRuntime& r, graficos::Emisor& e, ConferenceViewModel* s) : rt(r), em(e), sala(s) {}

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

RotuladorViewModel::RotuladorViewModel(AppRuntime& rt, graficos::Emisor& emisor,
                                       ConferenceViewModel* sala, QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>(rt, emisor, sala)) {
    connect(&emisor, &graficos::Emisor::cambio, this, &RotuladorViewModel::cambio);
    d_->sincro.setInterval(1000);
    connect(&d_->sincro, &QTimer::timeout, this, [this]() { sincronizar(); });
    if (sala != nullptr) d_->sincro.start();
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

void RotuladorViewModel::setVinculos(graficos::Vinculos* v) {
    d_->vin = v;
    if (v != nullptr) connect(v, &graficos::Vinculos::cambio, this, &RotuladorViewModel::cambio);
    emit cambio();
}

QVariantList RotuladorViewModel::vinculos() const {
    if (d_->vin == nullptr) return {};
    return d_->vin->lista().toVariantList();
}

void RotuladorViewModel::activarVinculo(const QString& id, bool activo) {
    if (d_->vin != nullptr) d_->vin->activar(id, activo);
}

void RotuladorViewModel::quitarVinculo(const QString& id) {
    if (d_->vin != nullptr) d_->vin->quitar(id);
}

QString RotuladorViewModel::dispararVinculo(const QString& id) {
    if (d_->vin == nullptr) return "no hay motor de gráficos";
    const QString e = d_->vin->disparar(id);
    if (!e.isEmpty()) emit notify("No se pudo disparar el vínculo: " + e, "error");
    return e;
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

void RotuladorViewModel::sincronizar() {
    if (d_->sala == nullptr || !d_->sala->client().online()) return;

    QJsonArray fs;
    for (const QVariant& f : fuentes()) {
        const QVariantMap m = f.toMap();
        fs.append(QJsonObject{{"id", static_cast<double>(m.value("id").toULongLong())},
                              {"fuente", m.value("fuente").toString()},
                              {"grafico", m.value("grafico").toString()},
                              {"titulo", m.value("titulo").toString()},
                              {"enAire", m.value("enAire").toBool()},
                              {"campos", QJsonArray::fromVariantList(m.value("campos").toList())},
                              {"valores", QJsonObject::fromVariantMap(m.value("valores").toMap())}});
    }
    const QJsonObject estado{{"escena", escena()}, {"fuentes", fs}};

    // El estado se manda SIEMPRE, aunque no haya cambiado: es tambien el
    // latido con el que el celular sabe que la app sigue viva (ver `viva` en
    // Rotulador.hpp). Lo que se evita es redibujar de mas del otro lado, y de
    // eso se ocupa la pagina con su propia firma.
    d_->sala->client().post("/api/v1/rotulos", QJsonObject{{"estado", estado}, {"tomar", true}},
                            [this](bool ok, const QJsonObject& r) {
                                if (!ok) return;
                                const QJsonArray es = r.value("encargos").toArray();
                                if (es.isEmpty()) return;
                                for (const QJsonValue& e : es) atender(e.toObject());
                                emit cambio();
                            });
}

void RotuladorViewModel::atender(const QJsonObject& e) {
    const auto fuente = static_cast<quint64>(e.value("id").toDouble());
    if (fuente == 0) return;
    const QString accion = e.value("accion").toString("aire");

    if (accion == "sacar") {
        ocultar(fuente);
        return;
    }
    const QString grafico = e.value("grafico").toString();
    if (!grafico.isEmpty() && grafico != d_->elegido[fuente]) setGrafico(fuente, grafico);
    const QJsonObject vs = e.value("valores").toObject();
    for (auto it = vs.begin(); it != vs.end(); ++it)
        setValor(fuente, it.key(), it.value().toVariant().toString());

    const QString err = disparar(fuente, accion == "bucle");
    // Lo que se encargo desde un celular y fallo tiene que verse en la app: el
    // que lo mando esta mirando la pantalla, no el telefono.
    if (!err.isEmpty())
        emit notify("Un rótulo pedido desde el celular no salió: " + err, "error");
    else
        emit notify("Rótulo al aire desde el celular", "ok");
}

void RotuladorViewModel::pedirQr(const QString& target, bool conClave) {
    if (d_->sala == nullptr) {
        emit qrListo({}, {}, "La sala no está corriendo", {});
        return;
    }
    const QString t = target == "remote" ? "remote" : "local";
    d_->sala->client().get(
        "/api/v1/rotulos-link?target=" + t + "&conClave=" + (conClave ? "1" : "0"),
        [this](bool ok, const QJsonObject& r) {
            QStringList filas;
            for (const QJsonValue& v : r.value("modules").toArray()) filas << v.toString();
            QString msg = r.value("message").toString();
            const bool bien = ok && r.value("ok").toBool();
            if (!bien && msg.isEmpty()) msg = "La sala no responde";
            emit qrListo(bien ? r.value("url").toString() : QString(), filas, msg,
                         r.value("clave").toString());
        });
}

} // namespace ba::app
