// ============================================================================
// src/app/ui/PublicoViewModel.cpp — ver PublicoViewModel.hpp
// ============================================================================
#include "ui/PublicoViewModel.hpp"
#include <QPointer>
#include "sala/VotacionSala.hpp"
#include "sala/PreguntasSala.hpp"
#include "sala/EnlacesSala.hpp"
#include "sala/PaginaPublico.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"

namespace ba::app {
namespace {

QVariantList aLista(const QJsonArray& a) {
    QVariantList out;
    for (const QJsonValue& v : a)
        out.append(v.toVariant());
    return out;
}

} // namespace

PublicoViewModel::PublicoViewModel(ConferenceViewModel* sala, QObject* parent)
    : QObject(parent), sala_(sala) {
    if (sala_ == nullptr) return;
    // Nadie pregunta cada dos segundos: la sala avisa cada cambio, con todo adentro.
    ConferenceClient& c = sala_->client();
    connect(&c, &ConferenceClient::estadoEvento, this, [this](const QString& nombre, const QJsonObject& o) {
        const nlohmann::json r = nlohmann::json::parse(QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
        if (!r.is_object()) return;
        if (nombre == "preguntas") {
            if (!enLinea_) enLinea_ = true;
            tomarPreguntas(r);
        } else if (nombre == "votacion") {
            alVolverVotacion({}, r, {});
        }
    });
    // Sin conexión NO se vacía lo que ya está en pantalla (una cola vacía es «nadie
    // preguntó», que es otra cosa): se marca `enLinea` y la vista lo dice.
    connect(&c, &ConferenceClient::onlineChanged, this, [this](bool on) {
        if (on) {
            if (activo()) consultar();
        } else if (enLinea_) {
            enLinea_ = false;
            emit cambio();
        }
    });
}

PublicoViewModel::~PublicoViewModel() = default;

QVariantList PublicoViewModel::preguntas() const { return preguntas_; }
QVariantMap PublicoViewModel::recuento() const { return recuento_; }
QVariantMap PublicoViewModel::votacion() const { return votacion_; }
QVariantMap PublicoViewModel::enlace() const { return enlace_; }
bool PublicoViewModel::enLinea() const { return enLinea_; }

nlohmann::json PublicoViewModel::preguntasParaLaWeb() const {
    nlohmann::json alAire = nlohmann::json::array(), elegidas = nlohmann::json::array(), respondidas = nlohmann::json::array();
    for (const QVariant& v : preguntas_) {
        const QVariantMap p = v.toMap();
        const QString est = p.value("estado").toString();
        nlohmann::json q{{"texto", p.value("texto").toString().toStdString()},
                         {"nombre", p.value("nombre").toString().toStdString()},
                         {"votos", p.value("votos").toInt()},
                         {"estado", est.toStdString()}};
        if (est == "al_aire") alAire.push_back(q);
        else if (est == "elegida") elegidas.push_back(q);
        else if (est == "respondida") respondidas.push_back(q);
    }
    nlohmann::json out = alAire;
    for (auto& q : elegidas) out.push_back(q);
    for (auto& q : respondidas) out.push_back(q);
    if (out.size() > 40) out.erase(out.begin() + 40, out.end());
    return out;
}

nlohmann::json PublicoViewModel::votacionParaLaWeb() const {
    const QVariantMap& v = votacion_;
    if (!v.value("hubo").toBool()) return nullptr;
    const bool abierta = v.value("abierta").toBool();
    nlohmann::json vo{{"pregunta", v.value("pregunta").toString().toStdString()},
                      {"abierta", abierta},
                      {"modo", v.value("modo").toString().toStdString()},
                      {"votaron", v.value("votaron").toInt()},
                      {"podian", v.value("podian").toInt()}};
    if (!abierta || v.value("enVivo").toBool()) {
        nlohmann::json res = nlohmann::json::array();
        for (const QVariant& r : v.value("resultado").toList())
            res.push_back({{"opcion", r.toMap().value("opcion").toString().toStdString()}, {"votos", r.toMap().value("votos").toInt()}});
        vo["resultado"] = res;
    }
    return vo;
}
void PublicoViewModel::alCambiarMirada() {
    // Al empezar a mirar, lo de ahora (lo que cambie después llega solo: eventos de la sala).
    if (activo()) consultar();
    emit cambio();
}

void PublicoViewModel::consultar() {
    if (preguntasSala_ == nullptr || votacionSala_ == nullptr) return;
    QPointer<PublicoViewModel> yo(this);
    preguntasSala_->ver([yo](const std::string& err, const nlohmann::json& r) {
        if (!yo) return;
        // Sin conexion NO se vacia lo que ya esta en pantalla: una cola vacia
        // significa «nadie pregunto» y eso es otra cosa muy distinta de «no
        // puedo ver las preguntas». Se marca `enLinea` y la vista lo dice.
        if (yo->enLinea_ != err.empty()) {
            yo->enLinea_ = err.empty();
            emit yo->cambio();
        }
        if (err.empty()) yo->tomarPreguntas(r);
    });
    votacionSala_->ver(false, [yo](const std::string& err, const nlohmann::json& r) {
        if (yo && err.empty()) yo->alVolverVotacion(err, r, {});
    });
}

void PublicoViewModel::moderar(const QString& accion, const QString& id) {
    if (preguntasSala_ == nullptr) return;
    QPointer<PublicoViewModel> yo(this);
    const std::string e = preguntasSala_->moderar(accion.toStdString(), id.toStdString(),
                                                  [yo](const std::string& err, const nlohmann::json& r) {
                                                      if (!yo) return;
                                                      if (!err.empty()) return emit yo->notify(tr("No pude hablar con el servidor."), "error");
                                                      yo->tomarPreguntas(r);
                                                  });
    if (!e.empty()) emit notify(QString::fromStdString(e), "error");
}

void PublicoViewModel::sumar(const QString& texto, const QString& nombre, const QString& deDonde) {
    if (preguntasSala_ == nullptr) return;
    QPointer<PublicoViewModel> yo(this);
    const std::string e = preguntasSala_->sumar(texto.toStdString(), nombre.toStdString(), deDonde.toStdString(),
                                                [yo](const std::string& err, const nlohmann::json& r) {
                                                    if (!yo) return;
                                                    if (!err.empty()) return emit yo->notify(tr("No pude sumarla."), "error");
                                                    yo->tomarPreguntas(r);
                                                });
    if (!e.empty()) emit notify(QString::fromStdString(e), "error");
}

void PublicoViewModel::tomarPreguntas(const nlohmann::json& r) {
    const auto aQv = [](const nlohmann::json& j) {
        return QJsonDocument::fromJson(QByteArray::fromStdString(nlohmann::json{{"x", j}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
            .object()
            .value("x");
    };
    preguntas_ = aLista(aQv(r.value("preguntas", nlohmann::json::array())).toArray());
    recuento_ = aQv(r.value("recuento", nlohmann::json::object())).toObject().toVariantMap();
    emit cambio();
}

void PublicoViewModel::abrirVotacion(const QString& pregunta, const QStringList& opciones, const QString& modo,
                                     bool secreta, bool enVivo) {
    if (votacionSala_ == nullptr) return;
    std::vector<std::string> ops;
    for (const QString& o : opciones) ops.push_back(o.toStdString());
    QPointer<PublicoViewModel> yo(this);
    const std::string e = votacionSala_->abrir(pregunta.trimmed().toStdString(), ops, modo.toStdString(), secreta, enVivo,
                                               [yo](const std::string& err, const nlohmann::json& r) {
                                                   if (yo) yo->alVolverVotacion(err, r, tr("No se pudo abrir la votación"));
                                               });
    if (!e.empty()) emit notify(QString::fromStdString(e), "error");
}

void PublicoViewModel::cerrarVotacion() {
    if (votacionSala_ == nullptr) return;
    QPointer<PublicoViewModel> yo(this);
    votacionSala_->cerrar([yo](const std::string& err, const nlohmann::json& r) {
        if (yo) yo->alVolverVotacion(err, r, tr("No pude cerrarla"));
    });
}

void PublicoViewModel::mostrarEnVivo(bool si) {
    if (votacionSala_ == nullptr) return;
    QPointer<PublicoViewModel> yo(this);
    votacionSala_->setEnVivo(si, [yo](const std::string& err, const nlohmann::json& r) {
        if (yo) yo->alVolverVotacion(err, r, tr("No pude cambiarlo"));
    });
}

void PublicoViewModel::alVolverVotacion(const std::string& error, const nlohmann::json& r, const QString& queFallo) {
    if (!error.empty()) {
        if (!queFallo.isEmpty()) emit notify(queFallo + ": " + QString::fromStdString(error), "error");
        return;
    }
    const nlohmann::json v = r.value("votacion", nlohmann::json::object());
    votacion_ = QJsonDocument::fromJson(QByteArray::fromStdString(v.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
                    .object()
                    .toVariantMap();
    emit cambio();
}

void PublicoViewModel::pedirEnlace(bool remoto) {
    if (sala_ == nullptr) return;
    QPointer<PublicoViewModel> yo(this);
    sala_->enlaces().publico(remoto, [yo](const sala::Enlace& e) {
        if (!yo) return;
        if (!e.ok) {
            // Por qué no hay QR (sin red, solo esta máquina…): lo dice el gateway.
            yo->enlace_ = QVariantMap{{"ok", false}, {"mensaje", QString::fromStdString(e.mensaje)}};
            emit yo->enlaceCambio();
            return;
        }
        QVariantList modulos;
        for (const std::string& f : e.filas) modulos.append(QString::fromStdString(f));
        yo->enlace_ = QVariantMap{{"ok", true}, {"url", QString::fromStdString(e.url)}, {"modulos", modulos}};
        emit yo->enlaceCambio();
    });
}

} // namespace ba::app

#include "moc_PublicoViewModel.cpp"
