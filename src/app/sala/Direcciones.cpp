// ============================================================================
// src/app/sala/Direcciones.cpp — ver Direcciones.hpp
// ============================================================================
#include "sala/Direcciones.hpp"

#include <QJsonDocument>
#include <QPointer>

#include "sala/Pedido.hpp"
#include "ui/ConferenceClient.hpp"

namespace ba::app::sala {

using json = nlohmann::json;

namespace {
QVariant aQv(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump())).toVariant();
}
QString qs(const std::string& s) { return QString::fromStdString(s); }
} // namespace

Direcciones::Direcciones(ConferenceClient& sala, QObject* parent) : QObject(parent), sala_(sala) {
    connect(&sala_, &ConferenceClient::direccionesEvento, this, [this](const QJsonObject& o) {
        poner(json::parse(QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString(), nullptr, false));
    });
    connect(&sala_, &ConferenceClient::onlineChanged, this, [this](bool on) {
        if (on) pedir();
    });
    if (sala_.online()) pedir();
}

void Direcciones::pedir() {
    QPointer<Direcciones> yo(this);
    sala::pedir(sala_, "GET", "/api/v1/direcciones", {}, [yo](const std::string& e, const json& r) {
        if (yo && e.empty() && r.is_object()) yo->poner(r);
    });
}

void Direcciones::poner(const json& catalogo) {
    if (!catalogo.is_object() || !catalogo.contains("servicios")) return;
    if (catalogo == cat_) return;
    cat_ = catalogo;
    emit cambio();
}

const json* Direcciones::servicio(const std::string& id) const {
    if (!cat_.contains("servicios") || !cat_["servicios"].is_array()) return nullptr;
    for (const auto& s : cat_["servicios"])
        if (s.value("id", std::string()) == id) return &s;
    return nullptr;
}

std::string Direcciones::url(const std::string& id, bool afuera) const {
    const json* s = servicio(id);
    if (s == nullptr) return {};
    const json& u = (*s)[afuera ? "remoto" : "local"];
    return u.is_string() ? u.get<std::string>() : std::string();
}

std::string Direcciones::origen(const std::string& id, bool afuera) const {
    std::string o = url(id, afuera);
    if (const size_t sch = o.find("://"); sch != std::string::npos) {
        const size_t fin = o.find_first_of("/?#", sch + 3);
        if (fin != std::string::npos) o.resize(fin);
    }
    return o;
}

std::string Direcciones::motivo(const std::string& id, bool afuera) const {
    const json* s = servicio(id);
    if (s == nullptr) return sala_.online() ? "La sala no informó ese servicio." : "La sala no responde.";
    return s->value(afuera ? "motivo_remoto" : "motivo_local", std::string());
}

QVariantList Direcciones::servicios() const { return aQv(cat_.value("servicios", json::array())).toList(); }

QVariantMap Direcciones::porId() const {
    QVariantMap m;
    for (const auto& s : cat_.value("servicios", json::array())) m.insert(qs(s.value("id", std::string())), aQv(s));
    return m;
}

QString Direcciones::ip() const {
    const json l = cat_.value("local", json());
    return l.is_object() ? qs(l.value("ip", std::string())) : QString();
}

QString Direcciones::remoto() const {
    const json r = cat_.value("remoto", json());
    return r.is_object() ? qs(r.value("url", std::string())) : QString();
}

QString Direcciones::via() const {
    const json r = cat_.value("remoto", json());
    return r.is_object() ? qs(r.value("via", std::string())) : QString();
}

} // namespace ba::app::sala
