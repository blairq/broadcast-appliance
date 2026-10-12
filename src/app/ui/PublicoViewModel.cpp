// ============================================================================
// src/app/ui/PublicoViewModel.cpp — ver PublicoViewModel.hpp
// ============================================================================
#include "ui/PublicoViewModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"

namespace ba::app {
namespace {

/// Cada dos segundos mientras alguien mira. Es una cola de preguntas: ver
/// aparecer una con tres segundos de retraso no cambia nada, y preguntar mas
/// seguido es ruido en la red del lugar, que suele ser la misma que lleva la
/// transmision.
constexpr int kRondaMs = 2000;

QVariantList aLista(const QJsonArray& a) {
    QVariantList out;
    for (const QJsonValue& v : a)
        out.append(v.toVariant());
    return out;
}

} // namespace

PublicoViewModel::PublicoViewModel(ConferenceViewModel* sala, QObject* parent)
    : QObject(parent), sala_(sala) {
    reloj_.setInterval(kRondaMs);
    connect(&reloj_, &QTimer::timeout, this, &PublicoViewModel::consultar);
}

PublicoViewModel::~PublicoViewModel() = default;

QVariantList PublicoViewModel::preguntas() const { return preguntas_; }
QVariantMap PublicoViewModel::recuento() const { return recuento_; }
QVariantMap PublicoViewModel::votacion() const { return votacion_; }
QVariantMap PublicoViewModel::enlace() const { return enlace_; }
bool PublicoViewModel::enLinea() const { return enLinea_; }
bool PublicoViewModel::activo() const { return mirando_ > 0; }

void PublicoViewModel::setActivo(bool a) {
    const int antes = mirando_;
    mirando_ = std::max(0, mirando_ + (a ? 1 : -1));
    if (mirando_ > 0 && antes == 0) {
        // Se consulta YA y no en dos segundos: abrir la pestaña y ver un
        // recuadro vacio dos segundos parece que esta roto.
        consultar();
        reloj_.start();
    } else if (mirando_ == 0) {
        reloj_.stop();
    }
    emit cambio();
}

void PublicoViewModel::consultar() {
    if (sala_ == nullptr) return;
    sala_->client().get("/api/v1/preguntas", [this](bool ok, const QJsonObject& r) {
        // Sin conexion NO se vacia lo que ya esta en pantalla: una cola vacia
        // significa «nadie pregunto» y eso es otra cosa muy distinta de «no
        // puedo ver las preguntas». Se marca `enLinea` y la vista lo dice.
        if (enLinea_ != ok) {
            enLinea_ = ok;
            emit cambio();
        }
        if (!ok) return;
        preguntas_ = aLista(r.value("preguntas").toArray());
        recuento_ = r.value("recuento").toObject().toVariantMap();
        emit cambio();
    });
    sala_->client().get("/api/v1/votacion", [this](bool ok, const QJsonObject& r) {
        if (!ok) return;
        votacion_ = r.value("votacion").toObject().toVariantMap();
        emit cambio();
    });
}

void PublicoViewModel::moderar(const QString& accion, const QString& id) {
    if (sala_ == nullptr) return;
    sala_->client().post("/api/v1/preguntas", QJsonObject{{"accion", accion}, {"id", id}},
                         [this](bool ok, const QJsonObject& r) {
                             if (!ok) return emit notify("No pude hablar con la sala.", "error");
                             preguntas_ = aLista(r.value("preguntas").toArray());
                             recuento_ = r.value("recuento").toObject().toVariantMap();
                             emit cambio();
                         });
}

void PublicoViewModel::sumar(const QString& texto, const QString& nombre, const QString& deDonde) {
    if (sala_ == nullptr || texto.trimmed().isEmpty()) return;
    sala_->client().post("/api/v1/preguntas",
                         QJsonObject{{"accion", "preguntar"},
                                     {"texto", texto.trimmed()},
                                     {"nombre", nombre},
                                     {"deDonde", deDonde.isEmpty() ? QString("sala") : deDonde}},
                         [this](bool ok, const QJsonObject& r) {
                             if (!ok) return emit notify("No pude sumarla.", "error");
                             preguntas_ = aLista(r.value("preguntas").toArray());
                             recuento_ = r.value("recuento").toObject().toVariantMap();
                             emit cambio();
                         });
}

void PublicoViewModel::abrirVotacion(const QString& pregunta, const QStringList& opciones,
                                     const QString& modo, bool secreta, bool enVivo,
                                     const QVariantList& habilitados, int podian) {
    if (sala_ == nullptr) return;
    QJsonArray ops;
    for (const QString& o : opciones)
        if (!o.trimmed().isEmpty()) ops.append(o.trimmed());
    if (ops.size() < 2)
        return emit notify("Hacen falta al menos dos opciones: con una sola no hay nada que decidir.",
                           "error");
    if (modo == "formal" && habilitados.isEmpty())
        return emit notify("No hay nadie con derecho a voto y presente en el padrón. Una votación "
                           "formal donde nadie puede votar da un resultado que parece válido.",
                           "error");
    QJsonArray hs;
    for (const QVariant& h : habilitados) {
        const QVariantMap m = h.toMap();
        hs.append(
            QJsonObject{{"codigo", m.value("codigo").toString()}, {"nombre", m.value("nombre").toString()}});
    }
    sala_->client().post("/api/v1/votacion",
                         QJsonObject{{"accion", "abrir"},
                                     {"pregunta", pregunta.trimmed()},
                                     {"opciones", ops},
                                     {"modo", modo},
                                     {"secreta", secreta},
                                     {"enVivo", enVivo},
                                     {"habilitados", hs},
                                     {"podian", podian}},
                         [this](bool ok, const QJsonObject& r) {
                             if (!ok || !r.value("ok").toBool())
                                 return emit notify("No se pudo abrir la votación.", "error");
                             votacion_ = r.value("votacion").toObject().toVariantMap();
                             emit cambio();
                         });
}

void PublicoViewModel::cerrarVotacion() {
    if (sala_ == nullptr) return;
    sala_->client().post("/api/v1/votacion", QJsonObject{{"accion", "cerrar"}},
                         [this](bool ok, const QJsonObject& r) {
                             if (!ok) return emit notify("No pude cerrarla.", "error");
                             votacion_ = r.value("votacion").toObject().toVariantMap();
                             emit cambio();
                         });
}

void PublicoViewModel::mostrarEnVivo(bool si) {
    if (sala_ == nullptr) return;
    sala_->client().post("/api/v1/votacion", QJsonObject{{"accion", "en_vivo"}, {"si", si}},
                         [this](bool ok, const QJsonObject& r) {
                             if (!ok) return;
                             votacion_ = r.value("votacion").toObject().toVariantMap();
                             emit cambio();
                         });
}

void PublicoViewModel::pedirEnlace(bool remoto) {
    if (sala_ == nullptr) return;
    sala_->client().get(QString("/api/v1/publico-link?target=") + (remoto ? "remote" : "local") +
                            "&conClave=1",
                        [this](bool ok, const QJsonObject& r) {
                            if (!ok || !r.value("ok").toBool()) {
                                // Por qué no hay QR (sin red, solo esta máquina…): lo dice el gateway.
                                enlace_ = QVariantMap{{"ok", false},
                                                      {"mensaje", ok ? r.value("message").toString()
                                                                     : QStringLiteral("No pude hablar con la sala (gateway).")}};
                                emit enlaceCambio();
                                return;
                            }
                            enlace_ = QVariantMap{{"ok", true},
                                                  {"url", r.value("url").toString()},
                                                  {"modulos", aLista(r.value("modules").toArray())}};
                            emit enlaceCambio();
                        });
}

} // namespace ba::app
