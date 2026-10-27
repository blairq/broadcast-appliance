// src/app/sala/EnlacesSala.cpp — los enlaces de las páginas de la sala (ver el .hpp)

#include "sala/EnlacesSala.hpp"

#include "i18n/I18n.hpp"

#include "ui/ConferenceClient.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace ba::app::sala {

namespace {
const char* destino(bool remoto) { return remoto ? "remote" : "local"; }
} // namespace

EnlacesSala::EnlacesSala(ConferenceClient& sala) : sala_(sala) {}

void EnlacesSala::pedirEnlace(const std::string& ruta, Listo listo) {
    // Directo al cliente y no por sala::pedir: sin enlace la sala contesta 200
    // con {"ok": false, "message": …}, y ese mensaje es lo que hay que mostrar.
    sala_.get(QString::fromStdString(ruta), [listo](bool ok, const QJsonObject& r) {
        Enlace e;
        e.respuesta =
            nlohmann::json::parse(QJsonDocument(r).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
        if (!e.respuesta.is_object()) e.respuesta = nlohmann::json::object();
        e.ok = ok && r.value("ok").toBool();
        if (e.ok) e.url = r.value("url").toString().toStdString();
        for (const QJsonValue& v : r.value("modules").toArray()) e.filas.push_back(v.toString().toStdString());
        QString msg = r.value("message").toString();
        if (msg.isEmpty()) msg = r.value("warning").toString();
        if (!e.ok && msg.isEmpty()) msg = QString::fromStdString(ba::i18n::traducir("sala", "La conferencia no responde"));
        e.mensaje = msg.toStdString();
        e.clave = r.value("clave").toString().toStdString();
        if (listo) listo(e);
    });
}

void EnlacesSala::ingreso(bool remoto, bool conPin, Listo listo) {
    pedirEnlace(std::string("/api/v1/qr?target=") + destino(remoto) + (conPin ? "&pin=1" : ""), std::move(listo));
}

void EnlacesSala::tablero(bool remoto, Listo listo) {
    pedirEnlace(std::string("/api/v1/board-link?target=") + destino(remoto), std::move(listo));
}

void EnlacesSala::correccion(bool remoto, Listo listo) {
    // La clave del corrector es por arranque: sin la sala andando no hay una
    // que dar (y no se avisa «la sala no responde» como si fuera una falla).
    if (!sala_.online()) {
        Enlace e;
        e.mensaje = ba::i18n::traducir("sala", "La conferencia no está corriendo");
        e.respuesta = nlohmann::json::object();
        if (listo) listo(e);
        return;
    }
    pedirEnlace(std::string("/api/v1/correccion-link?target=") + destino(remoto), std::move(listo));
}

void EnlacesSala::publico(bool remoto, Listo listo) {
    pedirEnlace(std::string("/api/v1/publico-link?target=") + destino(remoto) + "&conClave=1", std::move(listo));
}

void EnlacesSala::prompter(bool remoto, const std::string& vista, bool conClave, Listo listo) {
    pedirEnlace(std::string("/api/v1/prompter-link?target=") + destino(remoto) +
                    "&vista=" + (vista == "orador" ? "orador" : "control") + "&conClave=" + (conClave ? "1" : "0"),
                std::move(listo));
}

void EnlacesSala::rotulos(bool remoto, bool conClave, Listo listo) {
    pedirEnlace(std::string("/api/v1/rotulos-link?target=") + destino(remoto) + "&conClave=" + (conClave ? "1" : "0"),
                std::move(listo));
}

void EnlacesSala::pantalla(const std::string& nombre, int canal, Listo listo) {
    std::string ruta = "/api/v1/cast-link?target=local";
    if (canal >= 0) ruta += "&canal=" + std::to_string(canal);
    ruta += "&nombre=" + QUrl::toPercentEncoding(QString::fromStdString(nombre)).toStdString();
    pedirEnlace(ruta, std::move(listo));
}

} // namespace ba::app::sala
