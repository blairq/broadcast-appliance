// src/app/sala/Pedido.cpp — ver el .hpp

#include "sala/Pedido.hpp"

#include "ui/ConferenceClient.hpp"

#include <QJsonDocument>
#include <QJsonObject>

namespace ba::app::sala {

void pedir(ConferenceClient& cliente, const std::string& metodo, const std::string& ruta, const nlohmann::json& cuerpo,
           Listo listo) {
    const auto alVolver = [listo](bool ok, const QJsonObject& r) {
        const nlohmann::json j =
            nlohmann::json::parse(QJsonDocument(r).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
        std::string error;
        if (!ok)
            error = r.value("error").toString("la conferencia no contesta").toStdString();
        else if (r.contains("ok") && !r.value("ok").toBool())
            error = r.value("error").toString("la conferencia no lo aceptó").toStdString();
        if (listo) listo(error, j.is_discarded() ? nlohmann::json::object() : j);
    };
    const QString q = QString::fromStdString(ruta);
    if (metodo == "GET") {
        cliente.get(q, alVolver);
    } else if (metodo == "DELETE") {
        cliente.del(q, alVolver);
    } else {
        const QJsonObject o =
            QJsonDocument::fromJson(QByteArray::fromStdString(cuerpo.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
                .object();
        cliente.post(q, o, alVolver);
    }
}

} // namespace ba::app::sala
