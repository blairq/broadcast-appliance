// src/app/sala/GuardadoEnEvento.cpp — lo que llega a la sala y guarda el evento (ver el .hpp)

#include "sala/GuardadoEnEvento.hpp"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>

#include "crypto/Sha256.hpp"
#include "persist/ColaEscritura.hpp"
#include "sala/Pedido.hpp"
#include "ui/ConferenceClient.hpp"

namespace ba::app::sala {

namespace {
/// Un rato para juntar los votos que llegan seguidos en una sola escritura.
constexpr int kJuntarMs = 500;
} // namespace

GuardadoEnEvento::GuardadoEnEvento(ConferenceClient& sala, std::string que, std::function<std::filesystem::path()> carpeta,
                                   QObject* parent)
    : QObject(parent), sala_(sala), que_(std::move(que)), carpeta_(std::move(carpeta)) {
    guardar_.setSingleShot(true);
    guardar_.setInterval(kJuntarMs);
    connect(&guardar_, &QTimer::timeout, this, [this]() {
        if (deEste_.empty() || !foto_.is_object()) return;
        ba::persist::cola().escribir(deEste_ / (que_ + ".json"), foto_.dump(1, ' ', false, nlohmann::json::error_handler_t::replace));
    });
    connect(&sala_, &ConferenceClient::estadoEvento, this, [this](const QString& nombre, const QJsonObject& o) {
        if (nombre.toStdString() != que_) return;
        alEvento(nlohmann::json::parse(QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString(), nullptr, false));
    });
    sala_.registrarRepublicador(QString::fromStdString(que_), this, [this]() { sincronizar(); });
    eventoCambio();
}

std::string GuardadoEnEvento::etiquetaDe(const std::filesystem::path& carpeta) {
    if (carpeta.empty()) return {};
    const std::string ruta = carpeta.lexically_normal().generic_string();
    return ba::crypto::sha256Hex(ruta.data(), ruta.size()).substr(0, 16);
}

void GuardadoEnEvento::eventoCambio() {
    const std::filesystem::path carpeta = carpeta_ ? carpeta_() : std::filesystem::path();
    if (carpeta == deEste_ && !foto_.is_null()) return;
    // Lo pendiente del evento anterior se escribe ya (es de él, no del nuevo).
    if (guardar_.isActive()) {
        guardar_.stop();
        if (!deEste_.empty() && foto_.is_object())
            ba::persist::cola().escribir(deEste_ / (que_ + ".json"), foto_.dump(1, ' ', false, nlohmann::json::error_handler_t::replace));
    }
    deEste_ = carpeta;
    etiqueta_ = etiquetaDe(carpeta);
    foto_ = nullptr;
    if (carpeta.empty()) return; // sin evento no hay dónde guardar: la sala sigue con lo suyo
    const int n = ++lectura_;
    QPointer<GuardadoEnEvento> yo(this);
    const std::filesystem::path archivo = carpeta / (que_ + ".json");
    QThreadPool::globalInstance()->start([yo, n, archivo]() {
        std::string texto;
        nlohmann::json f = ba::persist::cola().leer(archivo, texto) ? nlohmann::json::parse(texto, nullptr, false)
                                                                    : nlohmann::json::object();
        if (!f.is_object()) f = nlohmann::json::object();
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [yo, n, f = std::move(f)]() {
                if (!yo || n != yo->lectura_) return;
                yo->foto_ = f;
                // A la sala: si ya tiene lo de este evento (se reinició la app) manda lo de
                // ella; si no (otro evento, o una sala nueva), lo guardado.
                yo->sincronizar();
            },
            Qt::QueuedConnection);
    });
}

void GuardadoEnEvento::alEvento(const nlohmann::json& ev) {
    // Solo lo de este evento: uno que llega del anterior (antes del restaurar) no se mezcla.
    if (!ev.is_object() || foto_.is_null() || ev.value("evento", std::string()) != etiqueta_) return;
    foto_ = ev.value("foto", nlohmann::json::object());
    guardarPronto();
}

void GuardadoEnEvento::guardarPronto() {
    if (!guardar_.isActive()) guardar_.start();
}

void GuardadoEnEvento::sincronizar() {
    if (foto_.is_null() || !sala_.online()) return; // cuando termine de leer, restaura
    QPointer<GuardadoEnEvento> yo(this);
    const std::string etiqueta = etiqueta_;
    pedir(sala_, "GET", "/api/v1/" + que_ + "?foto=1", {}, [yo, etiqueta](const std::string& e, const nlohmann::json& r) {
        if (!yo || !e.empty() || etiqueta != yo->etiqueta_) return;
        if (r.value("evento", std::string()) == etiqueta) {
            // La sala ya tiene lo de este evento (se reinició la app): lo de ella es más nuevo.
            yo->foto_ = r.value("foto", nlohmann::json::object());
            yo->guardarPronto();
        } else {
            yo->restaurar(); // una sala nueva (o con otro evento): lo guardado
        }
    });
}

void GuardadoEnEvento::restaurar() {
    if (foto_.is_null() || !sala_.online()) return;
    pedir(sala_, "POST", "/api/v1/" + que_, {{"accion", "restaurar"}, {"foto", foto_}, {"evento", etiqueta_}}, {});
}

} // namespace ba::app::sala

#include "moc_GuardadoEnEvento.cpp"
