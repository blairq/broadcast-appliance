// src/app/sala/ParticipantesSala.cpp — lo que se le hace a una persona (ver el .hpp)

#include "sala/ParticipantesSala.hpp"

#include "sala/ReglasSala.hpp"
#include "ui/ConferenceClient.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

namespace ba::app::sala {

namespace {

/// Los que esperan, en orden de llegada, y cómo va la tanda.
struct Tanda {
    std::vector<std::string> ids;
    size_t siguiente = 0;
    int admitidos = 0;
    std::string error;
    ParticipantesSala::ListoTodos listo;
};

void admitirSiguiente(ParticipantesSala& p, const std::shared_ptr<Tanda>& t) {
    if (t->siguiente >= t->ids.size()) {
        if (t->listo) t->listo(static_cast<int>(t->ids.size()), t->admitidos, t->error);
        return;
    }
    const std::string id = t->ids[t->siguiente++];
    p.admitir(id, [&p, t](const std::string& err, const nlohmann::json&) {
        if (err.empty()) {
            ++t->admitidos;
        } else if (t->error.empty()) {
            t->error = err;
        }
        // Llena: los que siguen tampoco entrarían. Se corta acá.
        if (err == "full") t->siguiente = t->ids.size();
        admitirSiguiente(p, t);
    });
}

} // namespace

std::string rutaDe(const std::string& id, const std::string& accion) {
    return "/api/v1/participants/" + QUrl::toPercentEncoding(QString::fromStdString(id)).toStdString() + "/" + accion;
}

ParticipantesSala::ParticipantesSala(ConferenceClient& sala) : sala_(sala) {}

void ParticipantesSala::accion(const std::string& id, const std::string& accion, const nlohmann::json& cuerpo,
                               Listo listo) {
    pedir(sala_, "POST", rutaDe(id, accion), cuerpo, std::move(listo));
}

void ParticipantesSala::admitir(const std::string& id, Listo listo) {
    accion(id, "admit", nlohmann::json::object(), std::move(listo));
}

void ParticipantesSala::admitirATodos(ListoTodos listo) {
    struct Espera {
        std::string id;
        double desde;
    };
    std::vector<Espera> esperan;
    for (const QJsonValue& v : sala_.snapshot().value("participants").toArray()) {
        const QJsonObject p = v.toObject();
        if (p.value("state").toString() != "waiting") continue;
        esperan.push_back({p.value("id").toString().toStdString(), p.value("joinedMs").toDouble()});
    }
    std::stable_sort(esperan.begin(), esperan.end(), [](const Espera& a, const Espera& b) { return a.desde < b.desde; });
    auto t = std::make_shared<Tanda>();
    for (const Espera& e : esperan) t->ids.push_back(e.id);
    t->listo = std::move(listo);
    admitirSiguiente(*this, t);
}

void ParticipantesSala::rechazar(const std::string& id, Listo listo) {
    accion(id, "reject", nlohmann::json::object(), std::move(listo));
}

void ParticipantesSala::sacar(const std::string& id, Listo listo) {
    accion(id, "kick", nlohmann::json::object(), std::move(listo));
}

void ParticipantesSala::hablar(const std::string& id, bool on, Listo listo) {
    accion(id, "talk", {{"on", on}}, std::move(listo));
}

std::string ParticipantesSala::pedirle(const std::string& id, const std::string& que, Listo listo) {
    static const std::array<const char*, 9> kPedidos{"mute-mic",    "mute-cam",   "unmute-mic",
                                                     "unmute-cam",  "stop-screen", "lower-hand",
                                                     "calidad-max", "calidad-720p", "calidad-360p"};
    if (std::none_of(kPedidos.begin(), kPedidos.end(), [&](const char* p) { return que == p; }))
        return "pedido desconocido: " + que;
    accion(id, "request", {{"what", que}}, std::move(listo));
    return {};
}

void ParticipantesSala::perfil(const std::string& id, const std::optional<std::string>& nombre,
                               const std::optional<std::string>& info, const std::optional<std::string>& contacto,
                               Listo listo) {
    nlohmann::json b = nlohmann::json::object();
    if (nombre) b["name"] = *nombre;
    if (info) b["info"] = *info;
    if (contacto) b["contact"] = *contacto;
    accion(id, "profile", b, std::move(listo));
}

void ParticipantesSala::prueba(const std::string& id, Listo listo) {
    // Tarda ~5 s (tono por su retorno y lo que vuelve por su micrófono): por
    // eso va directo al cliente, con una espera más larga que la de siempre.
    sala_.post(
        QString::fromStdString(rutaDe(id, "prueba")), QJsonObject{{"ms", 4000}},
        [listo](bool ok, const QJsonObject& r) {
            nlohmann::json j =
                nlohmann::json::parse(QJsonDocument(r).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
            if (!j.is_object()) j = nlohmann::json::object();
            j["recomendacion"] = recomendacionPrueba(j);
            std::string error;
            if (!ok) error = j.value("error", std::string("la sala no pudo hacer la prueba"));
            if (listo) listo(error, j);
        },
        15000);
}

void ParticipantesSala::foto(const std::string& id, ListoFoto listo) {
    sala_.getRaw(QString::fromStdString(rutaDe(id, "avatar")), std::move(listo));
}

} // namespace ba::app::sala
