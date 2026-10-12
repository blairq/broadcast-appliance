// src/app/sala/VotacionSala.cpp — la votación de la sala (ver el .hpp)

#include "sala/VotacionSala.hpp"

#include "ui/ConferenceClient.hpp"

#include <QJsonDocument>
#include <QJsonObject>

namespace ba::app::sala {

namespace {
QJsonObject aQt(const nlohmann::json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
        .object();
}
nlohmann::json deQt(const QJsonObject& o) {
    return nlohmann::json::parse(QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
}
} // namespace

VotacionSala::VotacionSala(ConferenceClient& sala, Votantes votantes) : sala_(sala), votantes_(std::move(votantes)) {}

void VotacionSala::pedir(const char* metodo, const std::string& ruta, const nlohmann::json& cuerpo, Listo listo) {
    const auto alVolver = [listo](bool ok, const QJsonObject& r) {
        const nlohmann::json j = deQt(r);
        std::string error;
        if (!ok) error = r.value("error").toString("la sala no contesta").toStdString();
        else if (r.contains("ok") && !r.value("ok").toBool()) error = r.value("error").toString("la sala no lo aceptó").toStdString();
        if (listo) listo(error, j);
    };
    if (std::string(metodo) == "GET")
        sala_.get(QString::fromStdString(ruta), alVolver);
    else
        sala_.post(QString::fromStdString(ruta), aQt(cuerpo), alVolver);
}

std::string VotacionSala::abrir(const std::string& pregunta, const std::vector<std::string>& opcionesPedidas,
                                const std::string& modo, bool secreta, bool enVivo, Listo listo) {
    nlohmann::json opciones = nlohmann::json::array();
    for (const std::string& o : opcionesPedidas) {
        std::string t = o;
        t.erase(0, t.find_first_not_of(" \t"));
        t.erase(t.find_last_not_of(" \t") + 1);
        if (!t.empty()) opciones.push_back(t);
    }
    if (opciones.size() < 2) return "Hacen falta al menos dos opciones: con una sola no hay nada que decidir.";
    if (opciones.size() > 10) return "Van hasta diez opciones.";
    if (modo != "encuesta" && modo != "formal") return "modo: encuesta o formal";
    nlohmann::json cuerpo{{"accion", "abrir"}, {"pregunta", pregunta}, {"opciones", opciones},
                          {"modo", modo},      {"secreta", secreta},   {"enVivo", enVivo}};
    if (modo == "formal") {
        const auto v = votantes_ ? votantes_() : std::nullopt;
        if (!v) return "Una votación formal necesita el padrón, y el padrón vive en el evento: abrí uno.";
        if (v->habilitados.empty())
            return "No hay nadie con derecho a voto y presente en el padrón (hace falta «vota» y estado «llegó»). "
                   "Una votación formal donde nadie puede votar da un resultado que parece válido.";
        nlohmann::json hs = nlohmann::json::array();
        for (const auto& h : v->habilitados) hs.push_back({{"codigo", h.codigo}, {"nombre", h.nombre}});
        cuerpo["habilitados"] = hs;
        cuerpo["podian"] = v->podian;
    }
    pedir("POST", "/api/v1/votacion", cuerpo, std::move(listo));
    return {};
}

void VotacionSala::cerrar(Listo listo) { pedir("POST", "/api/v1/votacion", {{"accion", "cerrar"}}, std::move(listo)); }

void VotacionSala::setEnVivo(bool si, Listo listo) {
    pedir("POST", "/api/v1/votacion", {{"accion", "en_vivo"}, {"si", si}}, std::move(listo));
}

void VotacionSala::ver(bool historial, Listo listo) {
    pedir("GET", historial ? "/api/v1/votacion?historial=1" : "/api/v1/votacion", {}, std::move(listo));
}

} // namespace ba::app::sala
