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
    baseRedLocal(gw_, [codigo, vence, listo](const json& b) {
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

void BotoneraGateway::baseRedLocal(ConferenceClient& gw, std::function<void(const json&)> listo) {
    sala::pedir(gw, "GET", "/api/v1/network", {}, [listo](const std::string& error, const json& r) {
        if (!error.empty()) {
            listo({{"error", "No pude preguntarle al gateway la dirección de la red local."}, {"sin_respuesta", true}});
            return;
        }
        // Una URL de la red local (no la del túnel), HTTPS si está.
        std::vector<std::string> lan;
        for (const auto& v : r.value("lan", json::array()))
            if (v.is_string()) lan.push_back(v.get<std::string>());
        const json urls = r.value("urls", json::array());
        std::string base;
        for (const auto& v : urls) {
            if (!v.is_string()) continue;
            const std::string u = v.get<std::string>();
            const bool deLan = std::any_of(lan.begin(), lan.end(), [&](const std::string& ip) {
                return u.find("//" + ip + ":") != std::string::npos || u.find("//" + ip + "/") != std::string::npos;
            });
            if (!deLan) continue;
            if (base.empty() || (u.rfind("https", 0) == 0 && base.rfind("https", 0) != 0)) base = u;
        }
        json out = json::object();
        const json https = r.value("https", json::object());
        const bool conHttps = https.is_object() && https.value("enabled", false);
        if (conHttps) out["huella"] = texto(https, "fingerprint");
        if (texto(r, "bindAddress") == "127.0.0.1")
            out["aviso"] = "El gateway escucha solo en esta máquina (127.0.0.1): una tablet no llega. Cambiá la "
                           "dirección de la sala a la red local en Configuración → Red de la sala (cualquiera en la red podría "
                           "ver las páginas públicas; la botonera igual pide su token).";
        // El gateway escucha solo en esta máquina: la URL igual se arma con la IP
        // de la red (anda en cuanto se abra el gateway a la red; el aviso lo dice).
        if (base.empty() && !lan.empty()) {
            if (conHttps) {
                base = "https://" + lan.front() + ":" + std::to_string(https.value("port", 8789)) + "/";
            } else {
                for (const auto& v : urls) {
                    if (!v.is_string()) continue;
                    const QUrl u(QString::fromStdString(v.get<std::string>()));
                    if (u.scheme() == "http" && u.port() > 0) {
                        base = "http://" + lan.front() + ":" + std::to_string(u.port()) + "/";
                        break;
                    }
                }
            }
        }
        if (base.empty()) {
            out["error"] = "No encontré una dirección de la red local para la tablet (¿está conectada la red?).";
            listo(out);
            return;
        }
        if (base.back() != '/') base += '/';
        out["base"] = base;
        listo(out);
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
