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
        encolar("/api/v1/control/enviar", {{"conexiones", ids}, {"msg", msg}});
    });
    connect(&cw_, &ControlWeb::cortar, this, [this](const QString& conexion, const QString& motivo) {
        encolar("/api/v1/control/cortar", {{"conexion", conexion.toStdString()}, {"motivo", motivo.toStdString()}});
    });
    connect(&cw_, &ControlWeb::credencialesCambio, this, [this]() { sincronizar(); });
    connect(&cw_, &ControlWeb::resuelta, this, [this](const json& cuerpo) { enviar("/api/v1/control/resolver", cuerpo); });
    gw_.registrarRepublicador("control-web", this, [this]() {
        sincronizar();
        reconocerConexiones();
    });
}

void ControlWebGateway::enviar(const std::string& ruta, const json& cuerpo) {
    // Si el gateway no contesta, la próxima sincronización (al volver) lo arregla.
    if (gw_.online()) sala::pedir(gw_, "POST", ruta, cuerpo, {});
}

namespace {
/// Lo que entra en un pedido al gateway (su tope es 1 MiB): con aire de sobra.
constexpr std::size_t kBytesPorPedido = 512 * 1024;
/// Si el gateway no contesta, la cola no crece sin fin: lo más viejo se descarta.
constexpr std::size_t kMaxCola = 4000;
} // namespace

void ControlWebGateway::encolar(const std::string& ruta, json cuerpo) {
    if (!gw_.online()) return; // nadie a quién mandarle: al volver, las conexiones se reconocen de nuevo
    Envio e;
    e.ruta = ruta;
    const json& msg = cuerpo.contains("msg") ? cuerpo["msg"] : json();
    if (msg.is_object() && msg.value("type", std::string()) == "estado") e.tema = msg.value("tema", std::string());
    // Un estado que todavía espera (no salió) para las mismas conexiones: el nuevo lo reemplaza
    // en su lugar. Solo cuenta lo último de un tema (los cuadros de los monitores, sobre todo).
    if (!e.tema.empty())
        for (Envio& x : cola_)
            if (x.tema == e.tema && x.ruta == ruta && x.cuerpo.value("conexiones", json()) == cuerpo.value("conexiones", json())) {
                x.cuerpo = std::move(cuerpo);
                x.bytes = x.cuerpo.dump().size();
                return;
            }
    e.bytes = cuerpo.dump().size();
    e.cuerpo = std::move(cuerpo);
    cola_.push_back(std::move(e));
    while (cola_.size() > kMaxCola) cola_.pop_front();
    despachar();
}

void ControlWebGateway::despachar() {
    if (enVuelo_ || cola_.empty()) return;
    if (!gw_.online()) {
        cola_.clear();
        return;
    }
    std::string ruta;
    json cuerpo;
    if (cola_.front().ruta == "/api/v1/control/enviar") {
        // Los «enviar» seguidos van juntos, en orden, hasta el tope de un pedido.
        json envios = json::array();
        std::size_t bytes = 0;
        while (!cola_.empty() && cola_.front().ruta == "/api/v1/control/enviar" &&
               (envios.empty() || bytes + cola_.front().bytes <= kBytesPorPedido)) {
            bytes += cola_.front().bytes;
            envios.push_back(std::move(cola_.front().cuerpo));
            cola_.pop_front();
        }
        ruta = "/api/v1/control/enviar";
        cuerpo = envios.size() == 1 ? std::move(envios[0]) : json{{"envios", std::move(envios)}};
    } else {
        ruta = cola_.front().ruta;
        cuerpo = std::move(cola_.front().cuerpo);
        cola_.pop_front();
    }
    enVuelo_ = true;
    QPointer<ControlWebGateway> yo(this);
    // Bien o mal, sigue con lo que esperaba (si el gateway no estaba, al volver se reconoce todo).
    sala::pedir(gw_, "POST", ruta, cuerpo, [yo](const std::string&, const json&) {
        if (!yo) return;
        yo->enVuelo_ = false;
        yo->despachar();
    });
}

void ControlWebGateway::enviarEnOrden(const std::string& cid, const json& msg) {
    // La réplica del tablero web va por la cola en orden (el esquema de un objeto antes que lo
    // que lo nombra), como el resto de lo que va a las conexiones.
    encolar("/api/v1/control/enviar", {{"conexiones", json::array({cid})}, {"msg", msg}});
}

void ControlWebGateway::enviarCuadro(const std::vector<std::string>& conexiones, const QByteArray& msg) {
    if (!gw_.online() || msg.isEmpty()) return;
    // Las que todavía tienen lugar (las demás se lo pierden: les llega el próximo).
    json ids = json::array();
    for (const std::string& cid : conexiones) {
        int& n = cuadrosEnVuelo_[cid];
        if (n >= kCuadrosEnVuelo) continue;
        ++n;
        ids.push_back(cid);
    }
    if (ids.empty()) return;
    // El cuerpo pegado: {"conexiones":[…],"msg":<el cuadro, ya en texto>}. Nada de leer ni
    // de volver a escribir el JPEG en base64 en este hilo (era lo que más pesaba).
    const std::string cabeza = R"({"conexiones":)" + ids.dump() + R"(,"msg":)";
    QByteArray cuerpo;
    cuerpo.reserve(static_cast<qsizetype>(cabeza.size()) + msg.size() + 1);
    cuerpo.append(cabeza.data(), static_cast<qsizetype>(cabeza.size()));
    cuerpo.append(msg);
    cuerpo.append('}');
    std::vector<std::string> enVuelo;
    for (const auto& x : ids) enVuelo.push_back(x.get<std::string>());
    QPointer<ControlWebGateway> yo(this);
    gw_.postCrudo(QStringLiteral("/api/v1/control/enviar"), cuerpo, [yo, enVuelo](bool, const QJsonObject&) {
        if (!yo) return;
        for (const std::string& cid : enVuelo) {
            auto it = yo->cuadrosEnVuelo_.find(cid);
            if (it == yo->cuadrosEnVuelo_.end()) continue;
            if (--it->second <= 0) yo->cuadrosEnVuelo_.erase(it);
        }
    });
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
    // Las solicitudes de emparejamiento son estado del gateway: las que llegaron sin que esta
    // app las oyera (no estaba, o se perdió el evento) se van a buscar.
    QPointer<ControlWebGateway> yo(this);
    sala::pedir(gw_, "GET", "/api/v1/control/solicitudes", {}, [yo](const std::string& e, const json& r) {
        if (!yo || !e.empty()) return;
        for (const json& s : r.value("solicitudes", json::array()))
            if (s.is_object() && !yo->cw_.tieneSolicitud(texto(s, "solicitud")))
                yo->evento({{"tipo", "solicitud"}, {"solicitud", texto(s, "solicitud")}, {"codigo", texto(s, "codigo")},
                            {"nombre", texto(s, "nombre")}, {"ip", texto(s, "ip")}});
    });
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
        listo({{"error", tr("El gateway no está andando: el navegador no tendría a quién conectarse.").toStdString()}});
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
        listo(error.empty() ? r : json(tr("sin respuesta").toStdString()));
    });
}

} // namespace ba::app::control

#include "moc_ControlWebGateway.cpp"
