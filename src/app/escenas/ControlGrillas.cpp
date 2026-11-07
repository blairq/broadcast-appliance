// ============================================================================
// src/app/escenas/ControlGrillas.cpp — ver ControlGrillas.hpp
// ============================================================================

#include "escenas/ControlGrillas.hpp"

#include "i18n/I18nTexto.hpp"

#include "ajustes/Ajustes.hpp"
#include "arranque/RuntimeMotor.hpp"
#include "asamblea/Cuerpo.hpp"
#include "escenas/FuenteGrilla.hpp"
#include "asamblea/SesionRemota.hpp"
#include "emision/ApiMotor.hpp"
#include "emision/FotoMotor.hpp"
#include "emision/VentanillaMotor.hpp"
#include "emision/FuentesApp.hpp"
#include "sala/PruebaSalaReglas.hpp"
#include "emision/InputSpecs.hpp"
#include "ui/ConferenceClient.hpp"

#include "MedidorCeldas.hpp" // motor
#include "Motor.hpp"
#include "platform/Hilo.hpp"
#include "diag/SondasCuadros.hpp"

#include <QDateTime>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPointer>

#include <gst/gst.h>

#include <algorithm>
#include <cmath>
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

/// Las celdas de una grilla del pedido al gateway, en fracciones de su lienzo, con lo que
/// manda la capa de cada una (gateway/Capas.hpp: la baja, 15 cps; la alta, la cámara, 30).
std::vector<motor::AnalisisCeldas::Celda> celdasAMedir(const json& g) {
    std::vector<motor::AnalisisCeldas::Celda> out;
    const double W = g.value("ancho", 0.0), H = g.value("alto", 0.0);
    if (W <= 0 || H <= 0) return out;
    for (const auto& c : g.value("celdas", json::array())) {
        const std::string capa = c.value("capa", std::string());
        // La media es la alta topada a 360p: los cps de la cámara, como la alta.
        const double ref = capa == "baja" ? 15.0 : capa == "alta" || capa == "media" ? 30.0 : 0.0;
        out.push_back({c.value("persona", std::string()), capa, c.value("x", 0.0) / W, c.value("y", 0.0) / H,
                       c.value("w", 0.0) / W, c.value("h", 0.0) / H, ref});
    }
    return out;
}

/// Las mismas, donde está la grilla en el programa (`programa_rect`).
std::vector<motor::AnalisisCeldas::Celda> enPrograma(std::vector<motor::AnalisisCeldas::Celda> celdas, double px, double py,
                                                     double pw, double ph) {
    for (auto& c : celdas) {
        c.x = px + c.x * pw;
        c.y = py + c.y * ph;
        c.w *= pw;
        c.h *= ph;
    }
    return celdas;
}

const char* nombreOrigen(grilla::Origen o) {
    return o == grilla::Origen::Sala ? "sala" : o == grilla::Origen::Asamblea ? "asamblea" : "ambas";
}
const char* nombreOrden(grilla::Orden o) {
    return o == grilla::Orden::Habla ? "habla" : o == grilla::Orden::Prioridad ? "prioridad" : "llegada";
}

const char* nombreAlHablar(grilla::AlHablar a) {
    switch (a) {
        case grilla::AlHablar::Nada: return "nada";
        case grilla::AlHablar::SinCambios: return "sin_cambios";
        case grilla::AlHablar::Destacar: return "destacar";
        case grilla::AlHablar::Superponer: return "superponer";
        case grilla::AlHablar::Escena: return "escena";
        case grilla::AlHablar::Resaltar: break;
    }
    return "resaltar";
}

/// Un rótulo como máscara A8 (el texto en blanco sobre nada: el alfa es la tinta). El
/// nombre en negrita y, abajo, el cargo más chico. Alto fijo: el gateway lo escala al de
/// cada celda. Con la letra de la interfaz (cualquier escritura: la de cada idioma).
QImage dibujarRotulo(const QString& nombre, const QString& cargo) {
    constexpr int kNombre = 40, kCargo = 28, kMargen = 4, kMaxAncho = 1200;
    QFont fn = QGuiApplication::font();
    fn.setPixelSize(kNombre);
    fn.setBold(true);
    QFont fc = QGuiApplication::font();
    fc.setPixelSize(kCargo);
    const QFontMetrics mn(fn), mc(fc);
    const int altoN = mn.height(), altoC = cargo.isEmpty() ? 0 : mc.height();
    // Con holgura: elidedText recorta con «…» si el ancho es justo el del texto (redondeo de
    // la negrita): «carga-00…» en la prueba de sala del 2026-11-02. Solo se recorta pasando kMaxAncho.
    const int ancho = std::min(kMaxAncho, std::max(mn.horizontalAdvance(nombre), cargo.isEmpty() ? 0 : mc.horizontalAdvance(cargo)) +
                                              2 * kMargen + kNombre / 2);
    QImage img(std::max(1, ancho), altoN + altoC, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setPen(Qt::white);
    p.setFont(fn);
    p.drawText(QRect(kMargen, 0, ancho - 2 * kMargen, altoN), Qt::AlignLeft | Qt::AlignVCenter,
               mn.elidedText(nombre, Qt::ElideRight, ancho - 2 * kMargen));
    if (!cargo.isEmpty()) {
        p.setFont(fc);
        p.drawText(QRect(kMargen, altoN, ancho - 2 * kMargen, altoC), Qt::AlignLeft | Qt::AlignVCenter,
                   mc.elidedText(cargo, Qt::ElideRight, ancho - 2 * kMargen));
    }
    p.end();
    return img;
}

} // namespace

ControlGrillas::ControlGrillas(Partes p, QObject* parent) : QObject(parent), p_(p) {
    pronto_.setSingleShot(true);
    pronto_.setInterval(40);
    connect(&pronto_, &QTimer::timeout, this, &ControlGrillas::recalcular);
    const auto pedir = [this]() { pronto_.start(); };
    // De la sala, solo quién está y cómo (y la fila de oradores): no la red de cada uno, que
    // cambia en cada foto y no cambia ninguna grilla.
    connect(&p_.sala, &ConferenceClient::salaCambio, this, [pedir](unsigned partes) {
        if ((partes & ::ba::app::sala::Gente) != 0) pedir();
    });
    connect(&p_.sala, &ConferenceClient::hablanEvento, this, [this, pedir](const QJsonObject& ev) {
        hablan_.clear();
        for (const QJsonValue& v : ev.value("ids").toArray()) hablan_.insert(v.toString().toStdString());
        // Desde cuándo habla cada uno sin parar (la voz sostenida del orador).
        const std::int64_t ahora = ahoraMs();
        for (const std::string& k : hablan_) hablaDesde_.try_emplace(k, ahora);
        for (auto it = hablaDesde_.begin(); it != hablaDesde_.end();)
            it = hablan_.count(it->first) ? std::next(it) : hablaDesde_.erase(it);
        pedir();
    });
    // El gateway se reinició (o recién arrancó): no sabe nada; se le manda todo de nuevo.
    p_.sala.registrarRepublicador("grillas", this, [this, pedir]() {
        ultimoPedido_ = json();
        rotulosEnviados_.clear();
        pedir();
    });
    // Sin gateway, nadie habla (lo dice él cuando vuelve).
    connect(&p_.sala, &ConferenceClient::onlineChanged, this, [this, pedir](bool on) {
        if (on) return;
        hablan_.clear();
        hablaDesde_.clear();
        rotulosEnviados_.clear();
        pedir();
    });
    if (p_.sesion) connect(p_.sesion, &asamblea::SesionRemota::cambio, this, pedir);
    // Lo que se ve en el programa cambió (un corte, una capa DSK, una fuente).
    p_.motor.escuchar(this, [pedir](unsigned partes) {
        if ((partes & (FotoMotor::Escenas | FotoMotor::Salidas | FotoMotor::Superficies | FotoMotor::Video)) != 0) pedir();
    });
    reloj_.setInterval(500);
    connect(&reloj_, &QTimer::timeout, this, &ControlGrillas::recalcular);
    reloj_.start();
    reacomodo_.setSingleShot(true);
    connect(&reacomodo_, &QTimer::timeout, this, &ControlGrillas::recalcular);
    // El botón «Armar la escena del orador» de las Propiedades.
    grilla::ponerArmarEscenaOrador([this](const std::string& fuente) { avisarError(armarEscenaOrador(fuente)); });
}

std::string ControlGrillas::armarEscenaOrador(const std::string& fuente, std::string* escenaOut, std::string* grillaOut) {
    if (std::string e = validarMano(fuente, ""); !e.empty()) return e;
    if (p_.rt == nullptr) return ba::i18n::traducir("escenas", "todavía no se pueden crear escenas");
    const json a = fuentes::conDefectos(grilla::kTipo, fuentes::ajustesDe(fuente));
    if (a.value("muestra", std::string("todos")) == "orador")
        return ba::i18n::llenar(ba::i18n::traducir("escenas", "«%1» ya muestra solo al orador: es la de una escena del orador"), {fuente});
    const std::string escena = ba::i18n::llenar(ba::i18n::traducir("escenas", "Orador · %1"), {fuente});
    const std::string nombre = ba::i18n::llenar(ba::i18n::traducir("escenas", "%1 · orador"), {fuente});
    if (escenaOut) *escenaOut = escena;
    if (grillaOut) *grillaOut = nombre;
    // Lo que ya está no se toca (se puede apretar dos veces).
    const FotoMotor& f = p_.motor.foto();
    const bool hayEscena = f.escena(escena) != nullptr;
    const auto fs = fuentes();
    const bool hayGrilla = std::find(fs.begin(), fs.end(), nombre) != fs.end();
    if (hayEscena && hayGrilla) {
        if (a.value("escena_orador", std::string()) != escena) fuentes::cambiarAjustes(fuente, json{{"escena_orador", escena}});
        return {};
    }
    // La grilla del orador: del tamaño de la otra, todo el lienzo, sin separación ni margen.
    json ajustes = grilla::Config::defectos();
    ajustes["canal"] = 0; // se le asigna el primero libre
    ajustes["muestra"] = "orador";
    ajustes["sigue"] = fuente;
    ajustes["nombres"] = "nombre_cargo";
    ajustes["nombre_tamano"] = 10;
    ajustes["ancho"] = a.value("ancho", 1280);
    ajustes["alto"] = a.value("alto", 720);
    ajustes["separacion"] = 0;
    ajustes["margen"] = 0;
    ajustes["proporcion"] = "llenar";
    ajustes["fundido_ms"] = a.value("fundido_ms", 300);
    SourceSpec spec;
    spec.id = grilla::kTipo;
    spec.name = nombre;
    spec.settingsJson = ajustes.dump(-1, ' ', false, json::error_handler_t::replace);
    spec.rect = CanvasRect{0.0f, 0.0f, 1.0f, 1.0f};
    p_.motor.pedir(
        this, "grilla_escena_orador",
        [rt = p_.rt, escena, spec, hayEscena, hayGrilla]() {
            if (!hayEscena && !rt->addScene(escena)) return rt->lastError();
            if (!hayGrilla && !rt->addSource(escena, spec)) return rt->lastError();
            rt->save();
            return std::string();
        },
        [this](const std::string& e) {
            avisarError(e);
            if (e.empty()) recalcular();
        });
    // La grilla recuerda cuál es su escena del orador (el pase la usa).
    if (a.value("escena_orador", std::string()) != escena) fuentes::cambiarAjustes(fuente, json{{"escena_orador", escena}});
    return {};
}

std::vector<std::string> ControlGrillas::fuentes() const {
    std::vector<std::string> out;
    for (const auto& [n, t] : fuentes::listar())
        if (t == grilla::kTipo) out.push_back(n);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> ControlGrillas::queSeVe() const {
    const FotoMotor& f = p_.motor.foto();
    std::vector<std::string> raices;
    const FotoMotor::Salida* programa = f.salida({});
    if (programa != nullptr && !programa->alAire.empty()) raices.push_back(programa->alAire);
    // Las capas DSK que están arriba (escenas o fuentes) también se ven en el programa.
    if (programa != nullptr)
        for (const json& d : f.dsk.value("salidas", json::object()).value(programa->id, json::array()))
            if (d.value("visible", false))
                for (const json& c : d.value("contenido", json::array()))
                    if (c.is_string()) raices.push_back(c.get<std::string>());
    return grilla::fuentesQueSeVen(raices, [&f](const std::string& n) -> std::optional<std::vector<std::string>> {
        const FotoMotor::Escena* e = f.escena(n);
        if (e == nullptr) return std::nullopt;
        std::vector<std::string> hijos;
        for (const FotoMotor::Fuente& x : e->fuentes)
            if (x.visible) hijos.push_back(x.nombre);
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
    const FotoMotor& f = p_.motor.foto();
    for (const std::string& n : seVe) {
        const std::string tipo = f.tipoDe(n);
        if (tipo == ApiMotor::kTipoBanca) {
            bancaAlAire = true;
            continue;
        }
        const json s = f.fuentesVideo.value(n, json::object()).value("ajustes", json::object());
        if (!s.is_object() || !inputs::esVideoInvitado(tipo, s.dump())) continue;
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
    const auto desde = [&](const std::string& clave) {
        const auto it = hablaDesde_.find(clave);
        return it != hablaDesde_.end() ? it->second : std::int64_t(0);
    };
    // El siguiente orador de la conferencia (modo oradores): el primero de la fila que espera.
    const QJsonObject snap = p_.sala.snapshot();
    std::string siguienteSala;
    {
        std::map<QString, QJsonObject> porId;
        for (const QJsonValue& v : snap.value("participants").toArray()) porId[v.toObject().value("id").toString()] = v.toObject();
        for (const QJsonValue& v : snap.value("order").toArray()) {
            const auto it = porId.find(v.toString());
            if (it == porId.end() || it->second.value("speaker").toString() != "queued" || !it->second.value("connected").toBool())
                continue;
            siguienteSala = "sala:" + v.toString().toStdString();
            break;
        }
    }
    // La Sala: los admitidos conectados (al aire o en la fila de oradores).
    for (const QJsonValue& v : snap.value("participants").toArray()) {
        const QJsonObject o = v.toObject();
        if (o.value("state").toString() != "admitted" || !o.value("connected").toBool()) continue;
        grilla::Persona p;
        p.clave = "sala:" + o.value("id").toString().toStdString();
        // Los de la prueba de sala llevan su rótulo: «nombre · prueba».
        p.nombre = ::ba::app::sala::prueba::nombreConRotulo(o.value("name").toString().toStdString(), o.value("prueba").toBool());
        p.deSala = true;
        const qint64 adm = static_cast<qint64>(o.value("admittedMs").toDouble());
        p.llegadaMs = adm > 0 ? adm : static_cast<qint64>(o.value("joinedMs").toDouble());
        p.presentador = o.value("presenter").toBool();
        p.prioridad = p.presentador ? 1 : 0;
        p.conCamara = o.value("cam").toBool(true) && o.value("mediaUp").toBool();
        const int slot = o.value("slot").toInt();
        p.alAire = slot > 0 && slotsAlAire.count(slot) > 0;
        p.hablando = hablaAhora(p.clave);
        p.vozMs = voz_.count(p.clave) ? voz_[p.clave] : 0;
        p.hablaDesdeMs = p.hablando ? desde(p.clave) : 0;
        p.cargo = o.value("info").toString().toStdString();
        p.orador = o.value("speaker").toString() == "onair";
        p.siguiente = p.clave == siguienteSala;
        out.push_back(std::move(p));
    }
    // La Asamblea: los integrantes habilitados en la sesión remota.
    if (p_.sesion) {
        const std::vector<std::string> palabra = p_.sesion->conPalabraTodos();
        const std::string siguiente = p_.sesion->siguiente();
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
                // El rótulo: su cargo y su banca (lo que haya).
                if (c.is_object()) {
                    const std::string cargo = c.value("cargo_oficial", std::string()), banca = c.value("banca", std::string());
                    p.cargo = cargo;
                    if (!banca.empty())
                        p.cargo += (cargo.empty() ? std::string() : std::string(" · ")) +
                                   ba::i18n::llenar(ba::i18n::traducir("escenas", "banca %1"), {banca});
                }
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
            p.hablaDesdeMs = hablan_.count(p.clave) ? desde(p.clave) : 0;
            p.palabra = conPalabra;
            p.siguiente = !siguiente.empty() && siguiente == id;
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
    // Las entradas se juntan (GrillaPersonas.hpp, «LAS ENTRADAS SE JUNTAN»): en caliente.
    rep_.reacomodoMinMs(ba::ajustes::entero("grilla.reacomodoMinMs", 1500));
    const std::vector<grilla::Resultado> res = rep_.calcular(cfgs, g, manual_, ahora);
    if (const std::int64_t prox = rep_.proximoReacomodoMs(); prox > 0) {
        const int falta = static_cast<int>(std::clamp<std::int64_t>(prox - ahora + 5, 5, 60000));
        if (!reacomodo_.isActive() || reacomodo_.remainingTime() > falta) reacomodo_.start(falta);
    }
    pasarAEscenas(cfgs, res, g);

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
            std::string capa; // la que pide la grilla para su cámara (alta | baja)
            for (const grilla::Celda& x : res[i].celdas)
                if (x.clave == k) capa = x.capa;
            personas.push_back({{"clave", k},
                                {"nombre", it != porClave.end() ? it->second->nombre : k},
                                {"fijo", manual_.fijo(c.fuente, k)},
                                {"hablando", it != porClave.end() && it->second->hablando},
                                {"orador", k == res[i].orador},
                                {"capa", capa}});
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
               // Alta definición hasta N personas: con más, todas las miniaturas en baja.
               {"alta_hasta", c.altaHasta},
               // Nunca más que esto en alta a la vez (por prioridad: orador, presentador, siguiente…).
               {"alta_max", c.altaMax},
               {"en_baja", res[i].enBaja},
               {"personas", personas},
               {"fuera", fuera},
               {"mas", res[i].mas},
               // Ya llegaron y esperan el próximo reacomodo (grilla.reacomodoMinMs).
               {"entrando", res[i].entrando},
               // Cuando alguien habla: cómo, quién es el orador, el que se precarga y si se eligió a mano.
               {"al_hablar", c.soloOrador ? "solo_orador" : nombreAlHablar(c.alHablar)},
               // El orador de ESTA grilla (null = nadie): en un grupo, solo la grilla donde está.
               {"orador", res[i].orador.empty() ? json(nullptr) : json(res[i].orador)},
               {"siguiente", res[i].siguiente},
               {"orador_a_mano", !manual_.destacado(c.fuente).empty()}};
        // Cómo quedó la grilla: columnas × filas, la celda en px del lienzo y su proporción
        // (con «llenar», entre la mínima y la máxima de la forma), el mínimo y cuántas caben.
        {
            const grilla::Forma f = grilla::formaDe(c);
            const auto redondo = [](double v) { return std::round(v * 1000.0) / 1000.0; };
            const grilla::Resultado& rr = res[i];
            x["disposicion"] = {
                {"columnas", rr.columnas},
                {"filas", rr.filas},
                {"celda", {{"ancho", rr.celdaAncho}, {"alto", rr.celdaAlto}}},
                {"proporcion", rr.celdaAlto > 0 ? redondo(static_cast<double>(rr.celdaAncho) / rr.celdaAlto) : 0.0},
                {"proporcion_video", redondo(f.video)},
                {"proporcion_min", redondo(f.minAspecto)},
                {"proporcion_max", redondo(f.maxAspecto)},
                {"minimo", {{"ancho", f.minAncho}, {"alto", f.minAlto}}},
                {"caben", rr.caben},
                {"lienzo", {{"ancho", c.ancho}, {"alto", c.alto}}}};
        }
        if (c.canal == 0)
            x["aviso"] = ba::i18n::llenar(ba::i18n::traducir("escenas", "sin canal: hay más de %1 grillas"),
                                          {std::to_string(grilla::kMaxGrillas)});
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
        // Lo que lee la vista, armado UNA vez por cambio: QML vuelve a leer la propiedad en
        // cada acceso a un elemento (vg.gente[i], .filter), y convertir el JSON entero en
        // cada lectura eran 40 × 40 conversiones por aviso con 40 personas.
        grillasQt_ = aQtLista(estado_.value("grillas", json::array())).toVariantList();
        genteQt_ = aQtLista(estado_.value("gente", json::array())).toVariantList();
        emit cambio();
    }

    // Al gateway, solo si cambió (y otra vez si no lo tomó). Los rótulos nuevos, antes.
    if (p_.sala.online()) mandarRotulos(grilla::rotulosDe(cfgs, res, g));
    const json pedido = grilla::pedidoGateway(cfgs, res);
    if (pedido == ultimoPedido_ || !p_.sala.online()) return;
    ultimoPedido_ = pedido;
    avisarMedicion(pedido);
    p_.sala.post("/api/v1/grillas", aQt(pedido), [this](bool ok, const QJsonObject&) {
        if (!ok) ultimoPedido_ = json(); // la próxima vuelta lo vuelve a mandar
    });
}

ControlGrillas::~ControlGrillas() {
    {
        std::lock_guard<std::mutex> l(medirMu_);
        cortarMedir_ = true;
    }
    medirCv_.notify_all();
    if (hiloMedir_.joinable()) hiloMedir_.join();
}

void ControlGrillas::avisarMedicion(const json& pedido) {
    std::lock_guard<std::mutex> l(medirMu_);
    if (midiendoGrillas_.empty()) return;
    for (const Midiendo& m : midiendoGrillas_) {
        std::vector<motor::AnalisisCeldas::Celda> celdas; // la grilla que no viene se apagó: sin celdas
        for (const auto& g : pedido.value("grillas", json::array()))
            if (g.value("canal", 0) == m.canal) celdas = celdasAMedir(g);
        if (m.entrada) m.entrada->cambiar(celdas);
        if (m.programa) m.programa->cambiar(enPrograma(celdas, m.px, m.py, m.pw, m.ph));
    }
}

std::string ControlGrillas::medir(int segundos, const json& opciones, std::function<void(const json&)> listo) {
    motor::Motor* mot = p_.rt != nullptr ? p_.rt->engine().motor() : nullptr;
    if (mot == nullptr) return ba::i18n::traducir("escenas", "el motor todavía no arrancó");
    {
        std::lock_guard<std::mutex> l(medirMu_);
        if (midiendo_) return ba::i18n::traducir("escenas", "ya hay una medición en curso");
    }
    segundos = std::clamp(segundos, 2, 120);
    // Lo que se le mandó al gateway (dónde pinta a cada uno) y de qué fuente es cada canal.
    std::map<int, std::pair<std::string, bool>> fuenteDe; // canal → {fuente, al aire}
    for (const auto& g : estado_.value("grillas", json::array()))
        fuenteDe[g.value("canal", 0)] = {g.value("fuente", std::string()), g.value("al_aire", false)};
    struct Grilla {
        std::string fuente;
        int canal = 0;
        bool alAire = false;
        std::vector<motor::AnalisisCeldas::Celda> celdas;
        std::shared_ptr<motor::MedidorCeldas> entrada; ///< lo comparte avisarMedicion
        std::shared_ptr<motor::MedidorCeldas> programa; ///< lo comparte el aviso de la vista
        std::string error;
    };
    auto grillas = std::make_shared<std::vector<Grilla>>();
    const json pr = opciones.value("programa_rect", json::array({0, 0, 1, 1}));
    const double px = pr.is_array() && pr.size() == 4 ? pr[0].get<double>() : 0.0, py = pr.is_array() && pr.size() == 4 ? pr[1].get<double>() : 0.0;
    const double pw = pr.is_array() && pr.size() == 4 ? pr[2].get<double>() : 1.0, ph = pr.is_array() && pr.size() == 4 ? pr[3].get<double>() : 1.0;
    for (const auto& g : (ultimoPedido_.is_object() ? ultimoPedido_.value("grillas", json::array()) : json::array())) {
        Grilla x;
        x.canal = g.value("canal", 0);
        const auto f = fuenteDe.find(x.canal);
        if (f == fuenteDe.end() || f->second.first.empty()) continue;
        x.fuente = f->second.first;
        x.alAire = f->second.second;
        if (g.value("ancho", 0.0) <= 0 || g.value("alto", 0.0) <= 0) continue;
        x.celdas = celdasAMedir(g);
        grillas->push_back(std::move(x));
    }
    // `zonas` (para diagnosticar la fluidez de cualquier video del programa, no solo la grilla:
    // un invitado en su lugar, una cámara): [{id, x, y, w, h (fracciones del lienzo), cps}].
    auto zonas = std::make_shared<std::vector<motor::AnalisisCeldas::Celda>>();
    for (const auto& z : opciones.value("zonas", json::array())) {
        motor::AnalisisCeldas::Celda c;
        c.id = z.value("id", std::string("zona"));
        c.x = z.value("x", 0.0);
        c.y = z.value("y", 0.0);
        c.w = z.value("w", 0.0);
        c.h = z.value("h", 0.0);
        c.referenciaCps = z.value("cps", 0.0);
        if (c.w > 0 && c.h > 0) zonas->push_back(std::move(c));
    }
    if (grillas->empty() && zonas->empty()) return ba::i18n::traducir("escenas", "no hay ninguna grilla de personas andando");
    {
        std::lock_guard<std::mutex> l(medirMu_);
        midiendo_ = true;
    }
    if (hiloMedir_.joinable()) hiloMedir_.join();
    hiloMedir_ = std::thread([this, yo = QPointer<ControlGrillas>(this), mot, grillas, zonas, segundos, px, py, pw, ph,
                              listo = std::move(listo)]() {
        ba::platform::nombrarHilo("medir-grilla");
        const std::string kVista = "programa@medir_grilla";
        bool conVista = false;
        // Las vueltas del lector común del video (gst-plugins/ipc/comun.hpp), antes y después.
        GstElement* anillo = nullptr;
        const auto vueltas = [&anillo]() {
            json v;
            GstStructure* st = nullptr;
            if (anillo != nullptr && g_object_class_find_property(G_OBJECT_GET_CLASS(anillo), "vueltas") != nullptr)
                g_object_get(anillo, "vueltas", &st, nullptr);
            if (st == nullptr) return v;
            for (const char* k : {"n", "ns", "tarde", "muy-tarde", "max-ns"}) {
                guint64 x = 0;
                gst_structure_get_uint64(st, k, &x);
                v[k] = x;
            }
            gst_structure_free(st);
            return v;
        };
        json vueltas0;
        // Las zonas del programa (si las pidieron): su propia vista, sin grilla.
        std::shared_ptr<motor::MedidorCeldas> enZonas;
        if (!zonas->empty()) {
            enZonas = std::make_shared<motor::MedidorCeldas>(*zonas);
            if (mot->verPedir(kVista + "z", 960, 540).empty())
                mot->verAvisar(kVista + "z", [m = enZonas](const motor::Cuadro& c) {
                    if (!c.nv12 && !c.bgra.empty()) m->cuadro(c.bgra.data(), c.ancho, c.alto, c.stride);
                });
            else
                enZonas.reset();
        }
        for (Grilla& g : *grillas) {
            // En la entrada: apenas sale del lector común (ba_origen, memoria de sistema).
            g.entrada = std::make_shared<motor::MedidorCeldas>(g.celdas);
            if (GstElement* p = mot->pipelineDeEntrada(g.fuente)) {
                if (anillo == nullptr) {
                    anillo = gst_bin_get_by_name(GST_BIN(p), "ba_anillo");
                    vueltas0 = vueltas();
                }
                if (GstElement* o = gst_bin_get_by_name(GST_BIN(p), "ba_origen")) {
                    if (GstPad* pad = gst_element_get_static_pad(o, "src")) {
                        g.entrada->poner(pad);
                        gst_object_unref(pad);
                    }
                    gst_object_unref(o);
                } else {
                    g.error = ba::i18n::traducir("escenas", "la entrada de la grilla no lee el canal tal cual");
                }
                gst_object_unref(p);
            } else {
                g.error = ba::i18n::traducir("escenas", "la entrada de la grilla no está armada");
            }
            // En el programa: la primera que está al aire, donde diga programa_rect.
            if (g.alAire && !conVista) {
                g.programa = std::make_shared<motor::MedidorCeldas>(enPrograma(g.celdas, px, py, pw, ph));
                if (mot->verPedir(kVista, 960, 540).empty()) {
                    conVista = true;
                    mot->verAvisar(kVista, [m = g.programa](const motor::Cuadro& c) {
                        if (!c.nv12 && !c.bgra.empty()) m->cuadro(c.bgra.data(), c.ancho, c.alto, c.stride);
                    });
                }
            }
        }
        {
            std::unique_lock<std::mutex> l(medirMu_);
            // Mientras mide, otra disposición corta los tramos de las celdas que cambian.
            for (const Grilla& g : *grillas) midiendoGrillas_.push_back({g.canal, g.entrada, g.programa, px, py, pw, ph});
            medirCv_.wait_for(l, std::chrono::seconds(segundos), [this] { return cortarMedir_; });
            midiendoGrillas_.clear();
        }
        if (conVista) {
            mot->verAvisar(kVista, nullptr);
            mot->verSoltar(kVista);
        }
        if (enZonas) {
            mot->verAvisar(kVista + "z", nullptr);
            mot->verSoltar(kVista + "z");
        }
        json out = json::array();
        for (Grilla& g : *grillas) {
            if (g.entrada) g.entrada->sacar();
            json x{{"fuente", g.fuente}, {"canal", g.canal}, {"al_aire", g.alAire}, {"entrada", g.entrada ? g.entrada->resumen() : json()}};
            if (g.programa) x["programa"] = g.programa->resumen();
            if (!g.error.empty()) x["error"] = g.error;
            out.push_back(std::move(x));
        }
        json r{{"segundos", segundos}, {"grillas", out}};
        if (enZonas) r["zonas"] = enZonas->resumen();
        if (const json v1 = vueltas(); v1.is_object() && vueltas0.is_object()) {
            const auto d = [&](const char* k) { return v1.value(k, 0ULL) - vueltas0.value(k, 0ULL); };
            const double n = static_cast<double>(d("n"));
            r["lector_video"] = {{"vueltas", d("n")},
                                 {"prom_ms", n > 0 ? std::round(static_cast<double>(d("ns")) / n / 1e4) / 100 : 0.0},
                                 {"tarde", d("tarde")},
                                 {"muy_tarde", d("muy-tarde")},
                                 {"max_ms_desde_que_arranco", std::round(static_cast<double>(v1.value("max-ns", 0ULL)) / 1e4) / 100}};
        }
        if (anillo != nullptr) gst_object_unref(anillo);
        {
            std::lock_guard<std::mutex> l(medirMu_);
            midiendo_ = false;
            if (cortarMedir_) return;
        }
        QMetaObject::invokeMethod(
            this,
            [yo, r = std::move(r), listo]() mutable {
                if (!yo) return;
                // Lo del gateway, por celda: el ritmo de cada tramo (red, decodificado, guardado,
                // pintado en el mosaico, publicado; gateway/Grillas.hpp «ritmo»).
                yo->p_.sala.get("/api/v1/grillas", [yo, r = std::move(r), listo](bool ok, const QJsonObject& o) mutable {
                    if (!yo) return;
                    if (ok) {
                        const json g = json::parse(QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
                        json gw = json::array();
                        for (const auto& x : g.value("grillas", json::array())) {
                            json celdas = json::array();
                            for (const auto& c : x.value("celdas", json::array()))
                                celdas.push_back({{"persona", c.value("persona", std::string())},
                                                  {"capa", c.value("capa", std::string())},
                                                  {"ritmo", c.value("ritmo", json::object())},
                                                  {"atraso_ms", c.value("atraso_ms", json())}});
                            gw.push_back({{"canal", x.value("canal", 0)},
                                          {"publicado", x.value("ritmo_publicado", json::object())},
                                          {"espera_ms", x.value("espera_ms", json())},
                                          {"celdas", std::move(celdas)}});
                        }
                        r["gateway"] = std::move(gw);
                    }
                    if constexpr (ba::diag::kSondasCuadros) {
                        // SONDA: cada cuadro guardado y cada elección del pintor, por persona
                        // (los últimos 15 s; gateway/Grillas.cpp, /api/v1/grillas/trazas).
                        yo->p_.sala.get("/api/v1/grillas/trazas", [yo, r = std::move(r), listo](bool ok2, const QJsonObject& t) mutable {
                            if (!yo) return;
                            if (ok2) r["gateway_trazas"] = json::parse(QJsonDocument(t).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
                            yo->ultimaMedicion_ = r;
                            if (listo) listo(r);
                        });
                        return;
                    }
                    yo->ultimaMedicion_ = r;
                    if (listo) listo(r);
                });
            },
            Qt::QueuedConnection);
    });
    return {};
}

// El estado tal cual (la vista y el MCP ven lo mismo), ya convertido (ver recalcular).
QVariantList ControlGrillas::grillasUi() const { return grillasQt_; }

QVariantList ControlGrillas::genteUi() const { return genteQt_; }

std::string ControlGrillas::ajustar(const std::string& fuente, const json& cambios) {
    const auto fs = fuentes();
    if (std::find(fs.begin(), fs.end(), fuente) == fs.end())
        return ba::i18n::llenar(
            ba::i18n::traducir("escenas", "no hay una grilla de personas que se llame «%1» (crear_fuente con type ba_grilla)"),
            {fuente});
    if (const std::string e = grilla::Config::validar(cambios); !e.empty()) return e;
    if (!fuentes::cambiarAjustes(fuente, cambios))
        return ba::i18n::llenar(ba::i18n::traducir("escenas", "no se pudieron guardar los ajustes de «%1»"), {fuente});
    recalcular();
    return {};
}

std::string ControlGrillas::validarMano(const std::string& fuente, const std::string& clave) const {
    const auto fs = fuentes();
    if (std::find(fs.begin(), fs.end(), fuente) == fs.end())
        return ba::i18n::llenar(ba::i18n::traducir("escenas", "no hay una grilla de personas que se llame «%1»"), {fuente});
    if (clave.empty()) return {};
    for (const json& p : estado_.value("gente", json::array()))
        if (p.value("clave", std::string()) == clave) return {};
    return ba::i18n::llenar(ba::i18n::traducir("escenas", "«%1» no está conectado (las claves son sala:<id> o asamblea:<id>, "
                                                          "ver grilla_personas_estado)"),
                            {clave});
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
    if (clave.empty()) return ba::i18n::traducir("escenas", "falta a quién (la clave)");
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

void ControlGrillas::pasarAEscenas(const std::vector<grilla::Config>& cfgs, const std::vector<grilla::Resultado>& res,
                                   const std::vector<grilla::Persona>& g) {
    const FotoMotor& f = p_.motor.foto();
    std::set<std::string> vistas;
    for (std::size_t i = 0; i < cfgs.size() && i < res.size(); ++i) {
        const grilla::Config& c = cfgs[i];
        if (c.soloOrador || c.alHablar != grilla::AlHablar::Escena) continue;
        vistas.insert(c.fuente);
        const FotoMotor::Salida* s = f.salida(c.salidaOrador);
        if (s == nullptr) s = f.salida({});
        const std::string alAire = s != nullptr ? s->alAire : std::string();
        // Lo que se le pidió al asistente ya se ve: se le avisa al operador que pasó.
        if (const auto it = propuestos_.find(c.fuente); it != propuestos_.end() && it->second.escena == alAire) {
            emit notify(QString::fromStdString(ba::i18n::llenar(
                            ba::i18n::traducir("escenas", "El asistente pasó a «%1» (%2)."), {it->second.escena, it->second.quien})),
                        "info");
            propuestos_.erase(it);
        }
        const grilla::PaseEscenaOrador::Orden o = pases_[c.fuente].paso(c.pasarEscena, res[i].orador, c.escenaOrador, alAire);
        if (o.tipo == grilla::PaseEscenaOrador::Orden::Nada) continue;
        std::string quien = o.orador;
        for (const grilla::Persona& p : g)
            if (p.clave == o.orador) quien = p.nombre;
        if (o.tipo == grilla::PaseEscenaOrador::Orden::Poner) {
            if (p_.ponerAlAire) p_.ponerAlAire(c.salidaOrador, o.escena);
            emit notify(QString::fromStdString(
                            o.volver ? ba::i18n::llenar(ba::i18n::traducir("escenas", "Ya no hay orador: volví a «%1»."), {o.escena})
                                     : ba::i18n::llenar(ba::i18n::traducir("escenas", "Pasé a la escena del orador: %1."), {quien})),
                        "info");
            continue;
        }
        // Con el asistente: se le pide (si está andando él decide, según lo autorizado) y el
        // operador SIEMPRE se entera.
        const std::string salida = c.salidaOrador.empty() ? ba::i18n::traducir("escenas", "la principal") : c.salidaOrador;
        emit paraElAsistente(QString::fromStdString(ba::i18n::llenar(
            o.volver ? ba::i18n::traducir("escenas", "[GRILLA DE PERSONAS] Ya no hay orador en «%1». Si estás autorizado a "
                                                     "cortar, volvé a la escena «%2» en la salida %3; si no, no hagas nada.")
                     : ba::i18n::traducir("escenas", "[GRILLA DE PERSONAS] El orador de «%1» ahora es %4. Si estás autorizado a "
                                                     "cortar, poné al aire la escena «%2» en la salida %3; si no, no hagas nada."),
            {c.fuente, o.escena, salida, quien})));
        emit notify(QString::fromStdString(
                        o.volver ? ba::i18n::llenar(ba::i18n::traducir("escenas", "Ya no hay orador: ¿vuelvo a «%1»?"), {o.escena})
                                 : ba::i18n::llenar(ba::i18n::traducir("escenas", "¿Paso a la escena del orador: %1?"), {quien})),
                    "warn");
        propuestos_[c.fuente] = {c.salidaOrador, o.escena, o.volver ? o.escena : quien};
    }
    for (auto it = pases_.begin(); it != pases_.end();) it = vistas.count(it->first) ? std::next(it) : pases_.erase(it);
    for (auto it = propuestos_.begin(); it != propuestos_.end();)
        it = vistas.count(it->first) ? std::next(it) : propuestos_.erase(it);
}

void ControlGrillas::mandarRotulos(const std::map<std::string, std::pair<std::string, std::string>>& rotulos) {
    // Los que ya no se usan, el gateway los olvida al aplicar: acá también (si vuelven, se mandan de nuevo).
    for (auto it = rotulosEnviados_.begin(); it != rotulosEnviados_.end();)
        it = rotulos.count(*it) ? std::next(it) : rotulosEnviados_.erase(it);
    // De a tandas: el cuerpo de un POST al gateway no pasa de 1 MB.
    constexpr std::size_t kTanda = 600 * 1024;
    json tanda = json::array();
    std::vector<std::string> ids;
    std::size_t bytes = 0;
    const auto mandar = [this](json lista, std::vector<std::string> enTanda) {
        if (lista.empty()) return;
        p_.sala.post("/api/v1/grillas/rotulos", aQt(json{{"rotulos", std::move(lista)}}),
                     [this, enTanda = std::move(enTanda)](bool ok, const QJsonObject&) {
                         if (ok) return;
                         for (const std::string& id : enTanda) rotulosEnviados_.erase(id); // la próxima vuelta, otra vez
                     });
    };
    for (const auto& [id, texto] : rotulos) {
        if (rotulosEnviados_.count(id)) continue;
        const QImage img = dibujarRotulo(QString::fromStdString(texto.first), QString::fromStdString(texto.second));
        QByteArray a8;
        a8.resize(img.width() * img.height());
        for (int y = 0; y < img.height(); ++y) {
            const auto* fila = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            for (int x = 0; x < img.width(); ++x) a8[y * img.width() + x] = static_cast<char>(qAlpha(fila[x]));
        }
        const QByteArray b64 = a8.toBase64();
        if (bytes + static_cast<std::size_t>(b64.size()) > kTanda && !tanda.empty()) {
            mandar(std::move(tanda), std::move(ids));
            tanda = json::array();
            ids.clear();
            bytes = 0;
        }
        tanda.push_back({{"id", id}, {"w", img.width()}, {"h", img.height()}, {"a8", b64.toStdString()}});
        ids.push_back(id);
        bytes += static_cast<std::size_t>(b64.size());
        rotulosEnviados_.insert(id);
    }
    mandar(std::move(tanda), std::move(ids));
}

std::string ControlGrillas::destacar(const std::string& fuente, const std::string& clave) {
    if (std::string e = validarMano(fuente, clave); !e.empty()) return e;
    if (clave.empty())
        manual_.destacados.erase(fuente);
    else
        manual_.destacados[fuente] = clave;
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

#include "moc_ControlGrillas.cpp"
