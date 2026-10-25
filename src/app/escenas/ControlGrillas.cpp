// ============================================================================
// src/app/escenas/ControlGrillas.cpp — ver ControlGrillas.hpp
// ============================================================================

#include "escenas/ControlGrillas.hpp"

#include "asamblea/Cuerpo.hpp"
#include "asamblea/SesionRemota.hpp"
#include "emision/BroadcastEngine.hpp"
#include "emision/FuentesApp.hpp"
#include "emision/InputSpecs.hpp"
#include "ui/ConferenceClient.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace ba::app::escenas {

using json = nlohmann::json;

namespace {

std::int64_t ahoraMs() { return QDateTime::currentMSecsSinceEpoch(); }

QJsonObject aQt(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump(-1, ' ', false, json::error_handler_t::replace))).object();
}

const char* nombreOrigen(grilla::Origen o) {
    return o == grilla::Origen::Sala ? "sala" : o == grilla::Origen::Asamblea ? "asamblea" : "ambas";
}
const char* nombreOrden(grilla::Orden o) {
    return o == grilla::Orden::Habla ? "habla" : o == grilla::Orden::Prioridad ? "prioridad" : "llegada";
}

} // namespace

ControlGrillas::ControlGrillas(Partes p, QObject* parent) : QObject(parent), p_(p) {
    pronto_.setSingleShot(true);
    pronto_.setInterval(40);
    connect(&pronto_, &QTimer::timeout, this, &ControlGrillas::recalcular);
    const auto pedir = [this]() { pronto_.start(); };
    connect(&p_.sala, &ConferenceClient::snapshotChanged, this, pedir);
    connect(&p_.sala, &ConferenceClient::hablanEvento, this, [this, pedir](const QJsonObject& ev) {
        hablan_.clear();
        for (const QJsonValue& v : ev.value("ids").toArray()) hablan_.insert(v.toString().toStdString());
        pedir();
    });
    // El gateway se reinició (o recién arrancó): no sabe nada; se le manda todo de nuevo.
    connect(&p_.sala, &ConferenceClient::onlineChanged, this, [this, pedir](bool on) {
        ultimoPedido_ = json();
        if (!on) hablan_.clear();
        pedir();
    });
    if (p_.sesion) connect(p_.sesion, &asamblea::SesionRemota::cambio, this, pedir);
    reloj_.setInterval(500);
    connect(&reloj_, &QTimer::timeout, this, &ControlGrillas::recalcular);
    reloj_.start();
}

std::vector<std::string> ControlGrillas::fuentes() const {
    std::vector<std::string> out;
    for (const auto& [n, t] : fuentes::listar())
        if (t == grilla::kTipo) out.push_back(n);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<grilla::Config> ControlGrillas::configs() {
    std::vector<grilla::Config> out;
    for (const std::string& n : fuentes()) out.push_back(grilla::Config::desdeAjustes(n, fuentes::conDefectos(grilla::kTipo, fuentes::ajustesDe(n))));
    // Cada grilla, su canal: se respeta el que tiene (si es válido y nadie más lo usa) y a
    // las demás se les da el primero libre. Queda guardado en sus ajustes (y así el motor
    // la toma del canal correcto).
    std::set<int> usados;
    std::vector<grilla::Config*> sinCanal;
    for (auto& c : out) {
        if (c.canal >= 1 && c.canal <= grilla::kMaxGrillas && usados.insert(c.canal).second) continue;
        sinCanal.push_back(&c);
    }
    for (grilla::Config* c : sinCanal) {
        int libre = 0;
        for (int k = 1; k <= grilla::kMaxGrillas && libre == 0; ++k)
            if (!usados.count(k)) libre = k;
        if (libre == 0) {
            c->canal = 0; // más de kMaxGrillas: esta no se arma (el estado lo dice)
            continue;
        }
        usados.insert(libre);
        c->canal = libre;
        fuentes::cambiarAjustes(c->fuente, json{{"canal", libre}});
    }
    return out;
}

std::vector<grilla::Persona> ControlGrillas::gente(std::int64_t ahora) {
    // Lo que se ve en el programa: los lugares de invitado, los canales del remoto, la banca.
    std::set<int> slotsAlAire, remotosAlAire;
    bool bancaAlAire = false;
    BroadcastEngine& m = p_.motor;
    if (!m.currentScene().empty())
        for (const auto& [n, rect] : m.itemsVisibles(m.currentScene())) {
            const std::string tipo = m.sourceTypeId(n);
            if (tipo == BroadcastEngine::kTipoBanca) {
                bancaAlAire = true;
                continue;
            }
            const std::string aj = m.sourceSettingsJson(n);
            if (!inputs::esVideoInvitado(tipo, aj)) continue;
            const json s = json::parse(aj.empty() ? "{}" : aj, nullptr, false);
            if (!s.is_object()) continue;
            const std::string canal = s.value("channel", std::string("guest"));
            const int slot = s.value("slot", 0);
            if (canal == "remoto" || canal == "remoto_contenido")
                remotosAlAire.insert(slot);
            else
                slotsAlAire.insert(slot);
        }

    std::vector<grilla::Persona> out;
    const auto hablaAhora = [&](const std::string& clave) {
        const bool h = hablan_.count(clave) > 0;
        if (h) voz_[clave] = ahora;
        return h;
    };
    // La Sala: los admitidos conectados (al aire o en la fila de oradores).
    for (const QJsonValue& v : p_.sala.snapshot().value("participants").toArray()) {
        const QJsonObject o = v.toObject();
        if (o.value("state").toString() != "admitted" || !o.value("connected").toBool()) continue;
        grilla::Persona p;
        p.clave = "sala:" + o.value("id").toString().toStdString();
        p.nombre = o.value("name").toString().toStdString();
        p.deSala = true;
        const qint64 adm = static_cast<qint64>(o.value("admittedMs").toDouble());
        p.llegadaMs = adm > 0 ? adm : static_cast<qint64>(o.value("joinedMs").toDouble());
        p.prioridad = o.value("presenter").toBool() ? 1 : 0;
        p.conCamara = o.value("cam").toBool(true) && o.value("mediaUp").toBool();
        const int slot = o.value("slot").toInt();
        p.alAire = slot > 0 && slotsAlAire.count(slot) > 0;
        p.hablando = hablaAhora(p.clave);
        p.vozMs = voz_.count(p.clave) ? voz_[p.clave] : 0;
        out.push_back(std::move(p));
    }
    // La Asamblea: los integrantes habilitados en la sesión remota.
    if (p_.sesion) {
        const std::vector<std::string> palabra = p_.sesion->conPalabraTodos();
        for (const json& i : p_.sesion->participantes()) {
            if (i.value("estado", std::string()) != "habilitado") continue;
            const std::string id = i.value("id", std::string());
            grilla::Persona p;
            p.clave = "asamblea:" + id;
            p.nombre = i.value("nombre", std::string());
            p.deSala = false;
            p.llegadaMs = i.value("desde_ms", std::int64_t(0));
            if (p_.cuerpo) {
                const json c = p_.cuerpo->buscar(id);
                if (c.is_object() && c.contains("prioridad") && c["prioridad"].is_number()) p.prioridad = c["prioridad"].get<int>();
            }
            p.conCamara = true; // manda fotos de su cámara (el gateway muestra la silueta hasta la primera)
            const auto it = std::find(palabra.begin(), palabra.end(), id);
            const bool conPalabra = it != palabra.end();
            const int canal = conPalabra ? static_cast<int>(it - palabra.begin()) + 1 : 0;
            p.alAire = conPalabra && (bancaAlAire || remotosAlAire.count(canal) > 0);
            // Quien tiene la palabra cuenta como que habla (aunque haga una pausa).
            p.hablando = hablaAhora(p.clave) || conPalabra;
            if (conPalabra) voz_[p.clave] = ahora;
            p.vozMs = voz_.count(p.clave) ? voz_[p.clave] : 0;
            out.push_back(std::move(p));
        }
    }
    return out;
}

void ControlGrillas::recalcular() {
    const std::int64_t ahora = ahoraMs();
    const std::vector<grilla::Config> cfgs = configs();
    if (cfgs.empty() && ultimoPedido_.is_object() && ultimoPedido_.value("grillas", json::array()).empty() &&
        estado_.value("grillas", json::array()).empty())
        return; // nada que mostrar, y el gateway ya lo sabe
    const std::vector<grilla::Persona> g = gente(ahora);
    const std::vector<grilla::Resultado> res = rep_.calcular(cfgs, g);

    // El estado (para la vista y el MCP).
    std::map<std::string, const grilla::Persona*> porClave;
    for (const auto& p : g) porClave[p.clave] = &p;
    json grillas = json::array();
    for (std::size_t i = 0; i < cfgs.size(); ++i) {
        const grilla::Config& c = cfgs[i];
        json personas = json::array();
        for (const std::string& k : res[i].personas)
            personas.push_back({{"clave", k}, {"nombre", porClave.count(k) ? porClave[k]->nombre : k}});
        json x{{"fuente", c.fuente},
               {"canal", c.canal},
               {"grupo", c.grupo},
               {"origen", nombreOrigen(c.origen)},
               {"orden", nombreOrden(c.orden)},
               {"reparto", c.reparto == grilla::Reparto::Parejo ? "parejo" : "en_orden"},
               {"tope", c.tope},
               {"personas", personas},
               {"mas", res[i].mas}};
        if (c.canal == 0) x["aviso"] = "sin canal: hay más de " + std::to_string(grilla::kMaxGrillas) + " grillas";
        grillas.push_back(std::move(x));
    }
    json gente = json::array();
    for (const auto& p : g)
        gente.push_back({{"clave", p.clave},
                         {"nombre", p.nombre},
                         {"origen", p.deSala ? "sala" : "asamblea"},
                         {"con_camara", p.conCamara},
                         {"al_aire", p.alAire},
                         {"hablando", p.hablando},
                         {"prioridad", p.prioridad}});
    json nuevo{{"grillas", grillas}, {"gente", gente}};
    if (nuevo != estado_) {
        estado_ = std::move(nuevo);
        emit cambio();
    }

    // Al gateway, solo si cambió (y otra vez si no lo tomó).
    const json pedido = grilla::pedidoGateway(cfgs, res);
    if (pedido == ultimoPedido_ || !p_.sala.online()) return;
    ultimoPedido_ = pedido;
    p_.sala.post("/api/v1/grillas", aQt(pedido), [this](bool ok, const QJsonObject&) {
        if (!ok) ultimoPedido_ = json(); // la próxima vuelta lo vuelve a mandar
    });
}

QVariantList ControlGrillas::grillasUi() const {
    QVariantList out;
    for (const json& g : estado_.value("grillas", json::array())) {
        QVariantList personas;
        for (const json& p : g.value("personas", json::array()))
            personas << QVariantMap{{"clave", QString::fromStdString(p.value("clave", std::string()))},
                                    {"nombre", QString::fromStdString(p.value("nombre", std::string()))}};
        out << QVariantMap{{"fuente", QString::fromStdString(g.value("fuente", std::string()))},
                           {"canal", g.value("canal", 0)},
                           {"grupo", QString::fromStdString(g.value("grupo", std::string()))},
                           {"personas", personas},
                           {"mas", g.value("mas", 0)}};
    }
    return out;
}

std::string ControlGrillas::ajustar(const std::string& fuente, const json& cambios) {
    const auto fs = fuentes();
    if (std::find(fs.begin(), fs.end(), fuente) == fs.end())
        return "no hay una grilla de personas que se llame «" + fuente + "» (crear_fuente con type ba_grilla)";
    if (const std::string e = grilla::Config::validar(cambios); !e.empty()) return e;
    if (!fuentes::cambiarAjustes(fuente, cambios)) return "no se pudieron guardar los ajustes de «" + fuente + "»";
    recalcular();
    return {};
}

} // namespace ba::app::escenas
