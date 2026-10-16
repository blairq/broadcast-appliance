// SPDX-License-Identifier: GPL-2.0-or-later
// src/app/control/BotoneraGateway.cpp — ver el .hpp
#include "control/BotoneraGateway.hpp"

#include "control/Botonera.hpp"
#include "qr/Qr.hpp"
#include "sala/Pedido.hpp"
#include "ui/ConferenceClient.hpp"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QUrl>

#include <algorithm>
#include <vector>

namespace ba::app::control {

namespace {
std::string texto(const nlohmann::json& j, const char* k) {
    return j.is_object() && j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string();
}
} // namespace

BotoneraGateway::BotoneraGateway(Botonera& botonera, ConferenceClient& gateway, QObject* parent)
    : QObject(parent), bot_(botonera), gw_(gateway) {
    connect(&gw_, &ConferenceClient::botoneraEvento, this, [this](const QJsonObject& o) {
        const json ev = json::parse(QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
        if (ev.is_object()) evento(ev);
    });
    // Lo que la botonera le manda a una tablet sale por el gateway (los aparatos
    // de plugins y del satélite tienen su propio camino, fuera de acá).
    connect(&bot_, &Botonera::mensaje, this, [this](const QString& dev, const json& msg) {
        if (bot_.origenDe(dev.toStdString()) == "tablet")
            enviar("/api/v1/botonera/enviar", {{"dispositivo", dev.toStdString()}, {"msg", msg}});
    });
    connect(&bot_, &Botonera::credencialesCambio, this, [this]() { sincronizar(); });
    connect(&gw_, &ConferenceClient::onlineChanged, this, [this](bool on) {
        if (on) sincronizar();
    });
}

void BotoneraGateway::enviar(const std::string& ruta, const json& cuerpo) {
    // Si el gateway no contesta, la próxima sincronización (al volver) lo arregla.
    sala::pedir(gw_, "POST", ruta, cuerpo, {});
}

void BotoneraGateway::evento(const json& ev) {
    const std::string tipo = texto(ev, "tipo");
    if (tipo == "solicitud") {
        const std::string sol = texto(ev, "solicitud");
        const std::string e = bot_.solicitud(sol, texto(ev, "codigo"), texto(ev, "nombre"), texto(ev, "ip"));
        if (!e.empty()) enviar("/api/v1/botonera/resolver", {{"solicitud", sol}, {"estado", "invalido"}, {"error", e}});
    } else if (tipo == "entrada") {
        bot_.entrada(texto(ev, "dispositivo"), ev.value("msg", json::object()));
    } else if (tipo == "remota") {
        const std::string e = bot_.entradaRemota(texto(ev, "dispositivo"), texto(ev, "perfil"), texto(ev, "pagina"),
                                                 ev.value("x", -1), ev.value("y", -1), texto(ev, "accion"), texto(ev, "ip"));
        if (!e.empty()) emit aviso(QString::fromStdString("API remota de la botonera: " + e), "aviso");
    } else if (tipo == "conexion") {
        bot_.marcarConexion(texto(ev, "dispositivo"), ev.value("conectado", false), texto(ev, "ip"));
    }
}

void BotoneraGateway::sincronizar() {
    if (!gw_.online()) return;
    enviar("/api/v1/botonera/credenciales", {{"dispositivos", bot_.credencialesParaGateway()}});
}

void BotoneraGateway::emparejar(const std::string& perfil, const std::string& nombre, int venceDias,
                                ListoEmparejar listo) {
    const json c = bot_.nuevoCodigo(perfil, nombre, venceDias);
    if (c.contains("error")) {
        listo({{"error", texto(c, "error")}});
        return;
    }
    if (!gw_.online()) {
        listo({{"error", "El gateway no está andando: la tablet no tendría a quién conectarse."}});
        return;
    }
    const std::string codigo = texto(c, "codigo");
    const std::int64_t vence = c.value("vence_ms", std::int64_t{0});
    baseRedLocal(gw_, "botonera", [codigo, vence, listo](const json& b) {
        if (b.value("sin_respuesta", false)) {
            listo({{"error", texto(b, "error")}});
            return;
        }
        json out{{"codigo", codigo}, {"vence_ms", vence}};
        for (const char* k : {"huella", "aviso", "error"})
            if (b.contains(k)) out[k] = b[k];
        if (out.contains("error")) {
            listo(out);
            return;
        }
        const std::string url = texto(b, "base") + "botonera/#p=" + codigo;
        out["url"] = url;
        out["qr"] = qrDataUrl(url);
        listo(out);
    });
}

std::string BotoneraGateway::qrDataUrl(const std::string& url) {
    ba::qr::Estilo e;
    e.correccion = ba::qr::Correccion::Media;
    const std::string svg = ba::qr::svg(url, e);
    return "data:image/svg+xml;base64," + QByteArray::fromStdString(svg).toBase64().toStdString();
}

void BotoneraGateway::baseRedLocal(ConferenceClient& gw, const std::string& servicio,
                                   std::function<void(const json&)> listo) {
    // La dirección la decide la sala (src/gateway/Direcciones): la de la red local de ese
    // servicio, por la puerta de la sala. Se pide en el momento (es lo que va en el QR).
    sala::pedir(gw, "GET", "/api/v1/direcciones", {}, [servicio, listo](const std::string& error, const json& r) {
        if (!error.empty() || !r.is_object()) {
            listo({{"error", "No pude preguntarle a la sala su dirección en la red local."}, {"sin_respuesta", true}});
            return;
        }
        for (const auto& s : r.value("servicios", json::array())) {
            if (s.value("id", std::string()) != servicio) continue;
            const json& u = s["local"];
            if (!u.is_string()) {
                listo({{"error", s.value("motivo_local", std::string("No hay dirección en la red local."))}});
                return;
            }
            std::string base = u.get<std::string>();
            if (const size_t sch = base.find("://"); sch != std::string::npos) {
                const size_t fin = base.find('/', sch + 3);
                if (fin != std::string::npos) base.resize(fin);
            }
            json out{{"base", base + "/"}};
            const json local = r.value("local", json());
            if (local.is_object() && !local.value("huella", std::string()).empty()) out["huella"] = local["huella"];
            listo(out);
            return;
        }
        listo({{"error", "La sala no informó la dirección de «" + servicio + "»."}});
    });
}

std::string BotoneraGateway::resolver(const std::string& solicitud, bool aprobar, const std::string& perfil) {
    const json r = bot_.resolverSolicitud(solicitud, aprobar, perfil);
    if (r.contains("error")) return texto(r, "error");
    json cuerpo{{"solicitud", solicitud}, {"estado", aprobar ? "aprobada" : "rechazada"}};
    if (aprobar) {
        cuerpo["dispositivo"] = texto(r, "dispositivo");
        cuerpo["token"] = texto(r, "token");
    }
    // Primero las credenciales (para que el token ya valga) y después el token a la tablet.
    sincronizar();
    enviar("/api/v1/botonera/resolver", cuerpo);
    return {};
}

void BotoneraGateway::estado(std::function<void(const json&)> listo) {
    if (!gw_.online()) {
        listo("apagado");
        return;
    }
    sala::pedir(gw_, "GET", "/api/v1/botonera/estado", {}, [listo](const std::string& error, const json& r) {
        listo(error.empty() ? r : json("sin respuesta"));
    });
}

} // namespace ba::app::control
