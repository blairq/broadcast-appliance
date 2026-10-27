// ============================================================================
// src/app/ui/RotuladorViewModel.cpp — ver RotuladorViewModel.hpp
// ============================================================================
#include "ui/RotuladorViewModel.hpp"
#include "sala/ControlSala.hpp"
#include "sala/EnlacesSala.hpp"
#include <QPointer>
#include <QJsonDocument>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include "arranque/AppRuntime.hpp"
#include "graficos/Emisor.hpp"
#include "graficos/GraficoSource.hpp"
#include "graficos/Vinculos.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"

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

RotuladorViewModel::RotuladorViewModel(AppRuntime& rt, graficos::Emisor& emisor, ConferenceViewModel* sala,
                                       QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>(rt, emisor, sala)) {
    connect(&emisor, &graficos::Emisor::cambio, this, &RotuladorViewModel::cambio);
    d_->sincro.setInterval(1000);
    connect(&d_->sincro, &QTimer::timeout, this, [this]() { sincronizar(); });
    if (sala != nullptr) d_->sincro.start();
}

RotuladorViewModel::~RotuladorViewModel() = default;

QString RotuladorViewModel::escena() const { return QString::fromStdString(d_->rt.engine().currentScene()); }

QStringList RotuladorViewModel::graficos() const { return d_->em.nombres(); }

QVariantList RotuladorViewModel::fuentes() const {
    QVariantList out;
    const std::string escenaActual = d_->rt.engine().currentScene();
    const std::vector<std::string> enEscena = d_->rt.engine().sceneSources(escenaActual);

    // Primero las de la escena al aire; después las de las otras escenas, que
    // no se ven y por eso se preparan ahí y se ARMAN en el preview (sale con
    // TAKE): es el gráfico que se revisa antes de que lo vea el público.
    std::vector<std::pair<graficos::InstanciaInfo, std::string>> lista;
    for (const auto& i : graficos::instancias())
        if (std::find(enEscena.begin(), enEscena.end(), i.nombre) != enEscena.end()) lista.emplace_back(i, escenaActual);
    for (const auto& i : graficos::instancias()) {
        if (std::find(enEscena.begin(), enEscena.end(), i.nombre) != enEscena.end()) continue;
        for (const std::string& sc : d_->rt.engine().sceneNames()) {
            const auto ss = d_->rt.engine().sceneSources(sc);
            if (std::find(ss.begin(), ss.end(), i.nombre) != ss.end()) {
                lista.emplace_back(i, sc);
                break;
            }
        }
    }
    for (const auto& [i, escena] : lista) {

        const QString grafico = d_->graficoDe(i);
        const QJsonObject ficha = d_->em.ficha(grafico);

        // Los valores que se muestran: lo que se está escribiendo, y si no, lo
        // último que salió al aire, y si no, el por defecto del gráfico.
        const QJsonObject ultimos = d_->em.ultimosValores(i.id);
        QVariantMap valores;
        for (const QJsonValue& v : ficha.value("parametros").toArray()) {
            const QString clave = v.toObject().value("clave").toString();
            if (clave.isEmpty()) continue;
            if (ultimos.contains(clave))
                valores[clave] = ultimos.value(clave).toVariant();
            else
                valores[clave] = v.toObject().value("porDefecto").toVariant();
        }
        const auto it = d_->valores.find(i.id);
        if (it != d_->valores.end())
            for (auto v = it->second.begin(); v != it->second.end(); ++v)
                valores[v.key()] = v.value();

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
                               {"enAire", i.cuadros > 0 && escena == escenaActual},
                               {"escenaAlAire", escena == escenaActual},
                               {"escena", QString::fromStdString(escena)},
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
    if (d_->vin == nullptr) return tr("no hay motor de gráficos");
    const QString e = d_->vin->disparar(id);
    if (!e.isEmpty()) emit notify(tr("No se pudo disparar el vínculo: %1").arg(e), "error");
    return e;
}

void RotuladorViewModel::refrescar() { emit cambio(); }

void RotuladorViewModel::setGrafico(qulonglong fuente, const QString& grafico) {
    d_->elegido[fuente] = grafico;
    // Y queda anotado en la propia fuente, que es lo que libobs guarda con la
    // escena: si no, elegir un grafico valia solo hasta el proximo arranque.
    graficos::setGrafico(fuente, grafico.toStdString());
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
    if (grafico.isEmpty()) return tr("esa fuente no tiene ningún gráfico elegido");

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
    // Si ese mismo gráfico ya está en pantalla y sabe animar un cambio (una
    // votación con tortas o barras), los valores nuevos se mueven en su lugar en
    // vez de hacerlo entrar de nuevo.
    const QString err = d_->em.mostrarOCambiar(grafico, valores, fuente, repetir);
    if (!err.isEmpty()) emit notify(tr("No se pudo sacar el gráfico al aire: %1").arg(err), "error");
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
    QPointer<RotuladorViewModel> yo(this);
    d_->sala->control().publicarRotulos(
        nlohmann::json::parse(QJsonDocument(estado).toJson(QJsonDocument::Compact).toStdString(), nullptr, false),
        [yo](const std::string& err, const nlohmann::json& r) {
            if (!yo || !err.empty()) return;
            const nlohmann::json es = r.value("encargos", nlohmann::json::array());
            if (!es.is_array() || es.empty()) return;
            for (const nlohmann::json& e : es)
                yo->atender(QJsonDocument::fromJson(QByteArray::fromStdString(e.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object());
            emit yo->cambio();
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
        emit notify(tr("Un rótulo pedido desde el celular no salió: %1").arg(err), "error");
    else
        emit notify(tr("Rótulo al aire desde el celular"), "ok");
}

void RotuladorViewModel::pedirQr(const QString& target, bool conClave) {
    if (d_->sala == nullptr) {
        emit qrListo({}, {}, tr("El servidor no está corriendo"), {});
        return;
    }
    QPointer<RotuladorViewModel> yo(this);
    d_->sala->enlaces().rotulos(target == "remote", conClave, [yo](const sala::Enlace& e) {
        if (!yo) return;
        QStringList filas;
        for (const std::string& f : e.filas) filas << QString::fromStdString(f);
        const QString msg = e.mensaje.empty() && !e.ok ? tr("El servidor no responde") : QString::fromStdString(e.mensaje);
        emit yo->qrListo(e.ok ? QString::fromStdString(e.url) : QString(), filas, msg, QString::fromStdString(e.clave));
    });
}

} // namespace ba::app
