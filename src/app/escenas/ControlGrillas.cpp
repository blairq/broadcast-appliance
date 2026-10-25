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
#include <optional>

namespace ba::app::escenas {

using json = nlohmann::json;

namespace {

std::int64_t ahoraMs() { return QDateTime::currentMSecsSinceEpoch(); }

QJsonObject aQt(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump(-1, ' ', false, json::error_handler_t::replace))).object();
}
QJsonArray aQtLista(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump(-1, ' ', false, json::error_handler_t::replace))).array();
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

std::vector<std::string> ControlGrillas::queSeVe() const {
    const BroadcastEngine& m = p_.motor;
    std::vector<std::string> raices;
    if (!m.currentScene().empty()) raices.push_back(m.currentScene());
    // Las capas DSK que están arriba (escenas o fuentes) también se ven en el programa.
    for (const json& d : m.dskEstado())
        if (d.value("visible", false))
            for (const json& c : d.value("contenido", json::array()))
                if (c.is_string()) raices.push_back(c.get<std::string>());
    const std::vector<std::string> nombres = m.sceneNames();
    const std::set<std::string> escenas(nombres.begin(), nombres.end());
    return grilla::fuentesQueSeVen(raices, [&](const std::string& n) -> std::optional<std::vector<std::string>> {
        if (!escenas.count(n)) return std::nullopt;
        std::vector<std::string> hijos;
        for (const auto& [f, rect] : m.itemsVisibles(n)) hijos.push_back(f);
        return hijos;
    });
}

std::vector<grilla::Config> ControlGrillas::configs(const std::vector<std::string>& seVe) {
    std::vector<grilla::Config> out;
    const std::set<std::string> visibles(seVe.begin(), seVe.end());
    for (const std::string& n : fuentes()) {
        out.push_back(grilla::Config::desdeAjustes(n, fuentes::conDefectos(grilla::kTipo, fuentes::ajustesDe(n))));
        out.back().alAire = visibles.count(n) > 0;
    }
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

std::vector<grilla::Persona> ControlGrillas::gente(std::int64_t ahora, const std::vector<std::string>& seVe) {
    // Lo que se ve en el programa: los lugares de invitado, los canales del remoto, la banca.
    // (Las grillas al aire las cuenta el Repartidor: quién va en cada una se decide ahí.)
    std::set<int> slotsAlAire, remotosAlAire;
    bool bancaAlAire = false;
    BroadcastEngine& m = p_.motor;
    for (const std::string& n : seVe) {
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
    const std::vector<std::string> seVe = queSeVe();
    const std::vector<grilla::Config> cfgs = configs(seVe);
    if (cfgs.empty() && ultimoPedido_.is_object() && ultimoPedido_.value("grillas", json::array()).empty() &&
        estado_.value("grillas", json::array()).empty())
        return; // nada que mostrar, y el gateway ya lo sabe
    // Lo decidido a mano sobre grillas que ya no existen no sirve más.
    std::set<std::string> existen;
    for (const auto& c : cfgs) existen.insert(c.fuente);
    manual_.olvidarFuentesSalvo(existen);
    const std::vector<grilla::Persona> g = gente(ahora, seVe);
    const std::vector<grilla::Resultado> res = rep_.calcular(cfgs, g, manual_);

    // El estado (para la vista y el MCP).
    std::map<std::string, const grilla::Persona*> porClave;
    for (const auto& p : g) porClave[p.clave] = &p;
    std::map<std::string, std::vector<std::string>> enGrillas; // clave → las grillas donde se ve
    std::set<std::string> enGrillaAlAire;
    json grillas = json::array();
    for (std::size_t i = 0; i < cfgs.size(); ++i) {
        const grilla::Config& c = cfgs[i];
        json personas = json::array();
        for (const std::string& k : res[i].personas) {
            const auto it = porClave.find(k);
            personas.push_back({{"clave", k},
                                {"nombre", it != porClave.end() ? it->second->nombre : k},
                                {"fijo", manual_.fijo(c.fuente, k)},
                                {"hablando", it != porClave.end() && it->second->hablando}});
            enGrillas[k].push_back(c.fuente);
            if (c.alAire) enGrillaAlAire.insert(k);
        }
        // Los que se sacaron a mano de esta grilla y siguen conectados (para «devolver»).
        json fuera = json::array();
        for (const auto& [f, k] : manual_.fuera)
            if (f == c.fuente)
                if (const auto it = porClave.find(k); it != porClave.end())
                    fuera.push_back({{"clave", k}, {"nombre", it->second->nombre}});
        json x{{"fuente", c.fuente},
               {"canal", c.canal},
               {"grupo", c.grupo},
               {"origen", nombreOrigen(c.origen)},
               {"orden", nombreOrden(c.orden)},
               {"reparto", c.reparto == grilla::Reparto::Parejo ? "parejo" : "en_orden"},
               {"tope", c.tope},
               {"al_aire", c.alAire},
               {"personas", personas},
               {"fuera", fuera},
               {"mas", res[i].mas}};
        if (c.canal == 0) x["aviso"] = "sin canal: hay más de " + std::to_string(grilla::kMaxGrillas) + " grillas";
        grillas.push_back(std::move(x));
    }
    json gente = json::array();
    for (const auto& p : g) {
        const auto fijo = manual_.fijos.find(p.clave);
        const auto en = enGrillas.find(p.clave);
        gente.push_back({{"clave", p.clave},
                         {"nombre", p.nombre},
                         {"origen", p.deSala ? "sala" : "asamblea"},
                         {"con_camara", p.conCamara},
                         // Al aire en otra fuente o en una grilla que se ve.
                         {"al_aire", p.alAire || enGrillaAlAire.count(p.clave) > 0},
                         {"hablando", p.hablando},
                         {"prioridad", p.prioridad},
                         {"en", en != enGrillas.end() ? json(en->second) : json::array()},
                         {"fijo_en", fijo != manual_.fijos.end() ? fijo->second : std::string()}});
    }
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
    // El estado tal cual: la vista y el MCP ven lo mismo.
    return aQtLista(estado_.value("grillas", json::array())).toVariantList();
}

QVariantList ControlGrillas::genteUi() const { return aQtLista(estado_.value("gente", json::array())).toVariantList(); }

std::string ControlGrillas::ajustar(const std::string& fuente, const json& cambios) {
    const auto fs = fuentes();
    if (std::find(fs.begin(), fs.end(), fuente) == fs.end())
        return "no hay una grilla de personas que se llame «" + fuente + "» (crear_fuente con type ba_grilla)";
    if (const std::string e = grilla::Config::validar(cambios); !e.empty()) return e;
    if (!fuentes::cambiarAjustes(fuente, cambios)) return "no se pudieron guardar los ajustes de «" + fuente + "»";
    recalcular();
    return {};
}

std::string ControlGrillas::validarMano(const std::string& fuente, const std::string& clave) const {
    const auto fs = fuentes();
    if (std::find(fs.begin(), fs.end(), fuente) == fs.end()) return "no hay una grilla de personas que se llame «" + fuente + "»";
    if (clave.empty()) return {};
    for (const json& p : estado_.value("gente", json::array()))
        if (p.value("clave", std::string()) == clave) return {};
    return "«" + clave + "» no está conectado (las claves son sala:<id> o asamblea:<id>, ver grilla_personas_estado)";
}

std::string ControlGrillas::fijar(const std::string& fuente, const std::string& clave) {
    if (std::string e = validarMano(fuente, clave); !e.empty()) return e;
    manual_.fijar(fuente, clave);
    recalcular();
    return {};
}

std::string ControlGrillas::sacar(const std::string& fuente, const std::string& clave) {
    // Sacar no pide que esté conectado: se puede sacar a alguien que se está reconectando.
    if (std::string e = validarMano(fuente, ""); !e.empty()) return e;
    if (clave.empty()) return "falta a quién (la clave)";
    manual_.sacar(fuente, clave);
    recalcular();
    return {};
}

std::string ControlGrillas::mover(const std::string& clave, const std::string& desde, const std::string& hacia) {
    if (std::string e = validarMano(hacia, clave); !e.empty()) return e;
    if (std::string e = validarMano(desde, ""); !e.empty()) return e;
    if (desde == hacia) return {};
    // En el mismo grupo alcanza con fijar (adentro de un grupo nadie se repite); en grupos
    // distintos, además se la saca de la de origen (si no, seguiría en las dos).
    const auto grupoDe = [](const std::string& f) {
        const std::string g = fuentes::conDefectos(grilla::kTipo, fuentes::ajustesDe(f)).value("grupo", std::string());
        return g.empty() ? "\x01" + f : g;
    };
    if (grupoDe(desde) != grupoDe(hacia)) manual_.sacar(desde, clave);
    manual_.fijar(hacia, clave);
    recalcular();
    return {};
}

std::string ControlGrillas::devolver(const std::string& fuente, const std::string& clave) {
    if (std::string e = validarMano(fuente, ""); !e.empty()) return e;
    manual_.devolver(fuente, clave);
    recalcular();
    return {};
}

} // namespace ba::app::escenas
