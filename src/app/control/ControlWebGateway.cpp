// SPDX-License-Identifier: GPL-2.0-or-later
// src/app/control/ControlWebGateway.cpp — ver el .hpp
#include "control/ControlWebGateway.hpp"

#include "control/BotoneraGateway.hpp"
#include "control/ControlWeb.hpp"
#include "sala/Pedido.hpp"
#include "ui/ConferenceClient.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

namespace ba::app::control {

namespace {
std::string texto(const nlohmann::json& j, const char* k) {
    return j.is_object() && j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string();
}
} // namespace

ControlWebGateway::ControlWebGateway(ControlWeb& control, ConferenceClient& gateway, QObject* parent)
    : QObject(parent), cw_(control), gw_(gateway) {
    connect(&gw_, &ConferenceClient::controlEvento, this, [this](const QJsonObject& o) {
        const json ev = json::parse(QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
        if (ev.is_object()) evento(ev);
    });
    connect(&cw_, &ControlWeb::enviar, this, [this](const QStringList& conexiones, const json& msg) {
        json ids = json::array();
        for (const QString& c : conexiones) ids.push_back(c.toStdString());
        enviar("/api/v1/control/enviar", {{"conexiones", ids}, {"msg", msg}});
    });
    connect(&cw_, &ControlWeb::cortar, this, [this](const QString& conexion, const QString& motivo) {
        enviar("/api/v1/control/cortar", {{"conexion", conexion.toStdString()}, {"motivo", motivo.toStdString()}});
    });
    connect(&cw_, &ControlWeb::credencialesCambio, this, [this]() { sincronizar(); });
    connect(&cw_, &ControlWeb::resuelta, this, [this](const json& cuerpo) { enviar("/api/v1/control/resolver", cuerpo); });
    connect(&gw_, &ConferenceClient::onlineChanged, this, [this](bool on) {
        if (!on) return;
        sincronizar();
        reconocerConexiones();
    });
}

void ControlWebGateway::enviar(const std::string& ruta, const json& cuerpo) {
    // Si el gateway no contesta, la próxima sincronización (al volver) lo arregla.
    if (gw_.online()) sala::pedir(gw_, "POST", ruta, cuerpo, {});
}

void ControlWebGateway::evento(const json& ev) {
    const std::string tipo = texto(ev, "tipo");
    if (tipo == "solicitud") {
        const std::string sol = texto(ev, "solicitud");
        const std::string e = cw_.solicitud(sol, texto(ev, "codigo"), texto(ev, "nombre"), texto(ev, "ip"));
        if (!e.empty()) enviar("/api/v1/control/resolver", {{"solicitud", sol}, {"estado", "invalido"}, {"error", e}});
    } else if (tipo == "conexion") {
        cw_.conexion(texto(ev, "conexion"), texto(ev, "aparato"), ev.value("conectado", false), texto(ev, "ip"),
                     texto(ev, "lugar"));
    } else if (tipo == "entrada") {
        cw_.entrada(texto(ev, "conexion"), texto(ev, "aparato"), ev.value("msg", json::object()));
    }
}

void ControlWebGateway::sincronizar() {
    if (!gw_.online()) return;
    enviar("/api/v1/control/credenciales", cw_.credencialesParaGateway());
}

void ControlWebGateway::reconocerConexiones() {
    QPointer<ControlWebGateway> yo(this);
    sala::pedir(gw_, "GET", "/api/v1/control/estado", {}, [yo](const std::string& error, const json& r) {
        if (!yo || !error.empty()) return;
        for (const auto& c : r.value("conexiones", json::array()))
            yo->cw_.conexion(texto(c, "conexion"), texto(c, "aparato"), true, texto(c, "ip"), texto(c, "lugar"));
    });
}

void ControlWebGateway::emparejar(const json& perfil, const std::string& nombre, std::function<void(const json&)> listo) {
    const json c = cw_.nuevoCodigo(perfil, nombre);
    if (c.contains("error")) {
        listo({{"error", texto(c, "error")}});
        return;
    }
    if (!gw_.online()) {
        listo({{"error", "El gateway no está andando: el navegador no tendría a quién conectarse."}});
        return;
    }
    const std::string codigo = texto(c, "codigo");
    const json base = {{"codigo", codigo}, {"vence_ms", c.value("vence_ms", std::int64_t{0})}, {"perfil", c["perfil"]}};
    BotoneraGateway::baseRedLocal(gw_, "control", [base, codigo, listo](const json& b) {
        if (b.value("sin_respuesta", false)) {
            listo({{"error", texto(b, "error")}});
            return;
        }
        json out = base;
        for (const char* k : {"huella", "aviso", "error"})
            if (b.contains(k)) out[k] = b[k];
        if (out.contains("error")) {
            listo(out);
            return;
        }
        // El código va en el fragmento (#p=): no sale en los registros ni en el Referer.
        const std::string url = texto(b, "base") + "control/#p=" + codigo;
        out["url"] = url;
        out["qr"] = BotoneraGateway::qrDataUrl(url);
        listo(out);
    });
}

void ControlWebGateway::estado(std::function<void(const json&)> listo) {
    if (!gw_.online()) {
        listo("apagado");
        return;
    }
    sala::pedir(gw_, "GET", "/api/v1/control/estado", {}, [listo](const std::string& error, const json& r) {
        listo(error.empty() ? r : json("sin respuesta"));
    });
}

} // namespace ba::app::control
