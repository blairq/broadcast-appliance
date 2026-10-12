// ============================================================================
// src/app/ui/PublicoViewModel.cpp — ver PublicoViewModel.hpp
// ============================================================================
#include "ui/PublicoViewModel.hpp"
#include <QPointer>
#include "sala/VotacionSala.hpp"

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

void PublicoViewModel::abrirVotacion(const QString& pregunta, const QStringList& opciones, const QString& modo,
                                     bool secreta, bool enVivo) {
    if (votacionSala_ == nullptr) return;
    std::vector<std::string> ops;
    for (const QString& o : opciones) ops.push_back(o.toStdString());
    QPointer<PublicoViewModel> yo(this);
    const std::string e = votacionSala_->abrir(pregunta.trimmed().toStdString(), ops, modo.toStdString(), secreta, enVivo,
                                               [yo](const std::string& err, const nlohmann::json& r) {
                                                   if (yo) yo->alVolverVotacion(err, r, "No se pudo abrir la votación");
                                               });
    if (!e.empty()) emit notify(QString::fromStdString(e), "error");
}

void PublicoViewModel::cerrarVotacion() {
    if (votacionSala_ == nullptr) return;
    QPointer<PublicoViewModel> yo(this);
    votacionSala_->cerrar([yo](const std::string& err, const nlohmann::json& r) {
        if (yo) yo->alVolverVotacion(err, r, "No pude cerrarla");
    });
}

void PublicoViewModel::mostrarEnVivo(bool si) {
    if (votacionSala_ == nullptr) return;
    QPointer<PublicoViewModel> yo(this);
    votacionSala_->setEnVivo(si, [yo](const std::string& err, const nlohmann::json& r) {
        if (yo) yo->alVolverVotacion(err, r, "No pude cambiarlo");
    });
}

void PublicoViewModel::alVolverVotacion(const std::string& error, const nlohmann::json& r, const QString& queFallo) {
    if (!error.empty()) return emit notify(queFallo + ": " + QString::fromStdString(error), "error");
    const nlohmann::json v = r.value("votacion", nlohmann::json::object());
    votacion_ = QJsonDocument::fromJson(QByteArray::fromStdString(v.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
                    .object()
                    .toVariantMap();
    emit cambio();
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
