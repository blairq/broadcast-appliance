// SPDX-License-Identifier: GPL-2.0-or-later
#include "control/Dsk.hpp"
#include "i18n/I18n.hpp"
#include "arranque/ArchivoConfig.hpp"

#include "emision/VentanillaMotor.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>

namespace ba::app::control {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
QVariant aQv(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", j}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().value("v").toVariant();
}
json deQv(const QVariantMap& m) {
    return json::parse(QJsonDocument(QJsonObject::fromVariantMap(m)).toJson(QJsonDocument::Compact).toStdString(),
                       nullptr, false);
}
std::vector<std::string> contenidoDe(const json& capa) {
    std::vector<std::string> cont;
    for (const auto& n : capa.value("contenido", json::array()))
        if (n.is_string()) cont.push_back(n.get<std::string>());
    return cont;
}
/// La salida por omisión: BroadcastEngine::kSalidaPorOmision (sin traer el motor: el
/// modelo anda en un test con un motor sintético).
constexpr const char* kPorOmision = "principal";
} // namespace

// ============================================================================
// CapasDsk: el modelo, en el dueño del motor
// ============================================================================

CapasDsk::CapasDsk(const fs::path& datos, Motor motor) : archivo_(datos / "Config" / "dsk.json"), motor_(std::move(motor)) {
    cargar();
}

void CapasDsk::cargar() {
    const json j = config::leerObjeto(archivo_);
    capas_ = json::array();
    for (int c = 1; c <= 4; ++c) {
        json capa{{"capa", c}, {"nombre", "DSK " + std::to_string(c)}, {"contenido", json::array()},
                  {"recuadro", json::object()}, {"fijo", false}, {"transicion_ms", 300}, {"visible", false}};
        for (const auto& g : j.value("capas", json::array()))
            if (g.value("capa", 0) == c) capa.update(g);
        capas_.push_back(capa);
    }
}

void CapasDsk::guardar() const {
    config::guardar(archivo_, {{"capas", capas_}});
}

json* CapasDsk::buscar(int capa) {
    for (auto& c : capas_)
        if (c.value("capa", 0) == capa) return &c;
    return nullptr;
}

bool CapasDsk::esPorOmision(const std::string& salida) { return salida.empty() || salida == kPorOmision; }

bool CapasDsk::arriba(int capa, const std::string& salida) const {
    for (const auto& c : capas_)
        if (c.value("capa", 0) == capa) {
            if (esPorOmision(salida)) return c.value("visible", false);
            const json en = c.value("visible_en", json::object());
            return en.is_object() && en.value(salida, false);
        }
    return false;
}

json CapasDsk::capas(const std::string& salida) const {
    json out = capas_;
    const json e = motor_.estado ? motor_.estado(esPorOmision(salida) ? std::string() : salida) : json::array();
    for (auto& c : out) {
        c["visible"] = arriba(c.value("capa", 0), salida);
        c.erase("visible_en");
        for (const auto& x : e)
            if (x.value("capa", 0) == c.value("capa", 0)) c["opacidad"] = x.value("opacidad", 0.0);
    }
    return out;
}

json CapasDsk::paraLaFoto(const std::vector<std::string>& salidas) const {
    json por = json::object();
    for (const std::string& s : salidas) por[s] = capas(s);
    return {{"capas", capas_}, {"salidas", por}};
}

void CapasDsk::avisar() {
    guardar();
    if (alCambiar_) alCambiar_();
}

std::string CapasDsk::revisarCapa(int capa) {
    return capa < 1 || capa > 4 ? ba::i18n::traducir("dsk", "la capa va de 1 a 4") : std::string();
}

std::string CapasDsk::revisarDefinicion(int capa, const json& def) {
    if (std::string e = revisarCapa(capa); !e.empty()) return e;
    if (!def.is_object()) return ba::i18n::traducir("dsk", "la definición es un objeto");
    return {};
}

std::string CapasDsk::revisarMostrar(const json& capa, bool visible, bool confirmada) {
    if (!capa.is_object()) return ba::i18n::traducir("dsk", "la capa va de 1 a 4");
    if (!visible && capa.value("fijo", false) && !confirmada)
        return ba::i18n::llenar(ba::i18n::traducir("dsk", "la capa «%1» es fija (p. ej. el intérprete de LSA): bajarla "
                                                          "necesita confirmación"),
                                {capa.value("nombre", std::string())});
    if (visible && capa.value("contenido", json::array()).empty())
        return ba::i18n::traducir("dsk", "la capa no tiene contenido: definila primero");
    return {};
}

std::string CapasDsk::revisarLsa(const std::string& entrada) {
    return entrada.empty() ? ba::i18n::traducir("dsk", "falta la entrada (la cámara del intérprete)") : std::string();
}

json CapasDsk::definicionLsa(const std::string& entrada, const std::string& posicion, double tamano) {
    return {{"nombre", ba::i18n::traducir("dsk", "Intérprete LSA")},
            {"contenido", json::array({entrada})},
            {"recuadro",
             {{"posicion", posicion.empty() ? "abajo_derecha" : posicion}, {"tamano", tamano > 0 ? tamano : 0.28}, {"margen", 0.03}}},
            {"fijo", true},
            {"transicion_ms", 500}};
}

std::string CapasDsk::definir(int capa, const json& def) {
    if (std::string e = revisarDefinicion(capa, def); !e.empty()) return e;
    json* c = buscar(capa);
    // Sobre una copia: solo se guarda si el motor la acepta (antes quedaba guardado un
    // contenido que el motor había rechazado, y el motor seguía con el viejo).
    json nueva = *c;
    for (const char* k : {"nombre", "contenido", "recuadro", "fijo", "transicion_ms"})
        if (def.contains(k)) nueva[k] = def[k];
    if (!motor_.armar(capa, contenidoDe(nueva), nueva.value("recuadro", json::object()), {})) return motor_.error();
    *c = std::move(nueva);
    avisar();
    return {};
}

std::string CapasDsk::mostrar(int capa, bool visible, bool confirmada, const std::string& salida) {
    json* c = buscar(capa);
    if (std::string e = revisarMostrar(c != nullptr ? *c : json(), visible, confirmada); !e.empty()) return e;
    const bool porOmision = esPorOmision(salida);
    // La definición es una para todas: se arma en esa salida antes de subirla.
    if (!porOmision && visible && !motor_.armar(capa, contenidoDe(*c), c->value("recuadro", json::object()), salida))
        return motor_.error();
    if (!motor_.mostrar(capa, visible, c->value("transicion_ms", 300), porOmision ? std::string() : salida))
        return motor_.error();
    if (porOmision) {
        (*c)["visible"] = visible;
    } else {
        json& en = (*c)["visible_en"];
        if (!en.is_object()) en = json::object();
        if (visible) en[salida] = true;
        else en.erase(salida);
    }
    avisar();
    return {};
}

std::string CapasDsk::plantillaLsa(int capa, const std::string& entrada, const std::string& posicion, double tamano) {
    if (std::string e = revisarLsa(entrada); !e.empty()) return e;
    return definir(capa, definicionLsa(entrada, posicion, tamano));
}

void CapasDsk::restaurar() {
    for (auto& c : capas_) {
        const int capa = c.value("capa", 0);
        const std::vector<std::string> cont = contenidoDe(c);
        if (cont.empty()) continue;
        motor_.armar(capa, cont, c.value("recuadro", json::object()), {});
        if (c.value("visible", false)) motor_.mostrar(capa, true, 0, {});
        // Las otras salidas: si todavía no existen (se crean después), no vuelve ahí.
        const json visibleEn = c.value("visible_en", json::object()); // con nombre: .items() de un temporal es UB
        for (const auto& [s, v] : visibleEn.items())
            if (v == true && motor_.armar(capa, cont, c.value("recuadro", json::object()), s)) motor_.mostrar(capa, true, 0, s);
    }
    if (alCambiar_) alCambiar_();
}

// ============================================================================
// Dsk: lo que ve la interfaz (la foto) y lo que pide (la ventanilla)
// ============================================================================

Dsk::Dsk(CapasDsk& modelo, VentanillaMotor& ventanilla, QObject* parent) : QObject(parent), capas_(modelo), v_(ventanilla) {
    connect(&v_, &VentanillaMotor::cambio, this, &Dsk::alCambiarFoto);
    ultimaFoto_ = v_.foto().dsk;
    ultimaUi_ = capas(salidaUi_);
}

json Dsk::capas(const std::string& salida) const {
    const FotoMotor& f = v_.foto();
    // La de la por omisión, por su id; si la foto todavía no tiene salidas, las definiciones.
    std::string id = salida;
    if (CapasDsk::esPorOmision(salida))
        if (const FotoMotor::Salida* s = f.salida({})) id = s->id;
    const json salidas = f.dsk.value("salidas", json::object());
    if (salidas.contains(id)) return salidas[id];
    json out = f.dsk.value("capas", json::array());
    for (auto& c : out) c.erase("visible_en");
    return out;
}

bool Dsk::arriba(int capa, const std::string& salida) const {
    for (const auto& c : capas(salida))
        if (c.value("capa", 0) == capa) return c.value("visible", false);
    return false;
}

json Dsk::definicion(int capa) const {
    for (const auto& c : v_.foto().dsk.value("capas", json::array()))
        if (c.value("capa", 0) == capa) return c;
    return nullptr;
}

void Dsk::setSalida(const std::string& salida) {
    const std::string s = CapasDsk::esPorOmision(salida) ? std::string() : salida;
    if (s == salidaUi_) return;
    salidaUi_ = s;
    ultimaUi_ = capas(salidaUi_);
    emit cambio();
}

void Dsk::alCambiarFoto(unsigned partes) {
    // Las capas por salida se arman con la lista de salidas: también cuando cambia esa.
    if ((partes & (FotoMotor::Superficies | FotoMotor::Salidas)) == 0) return;
    const json f = v_.foto().dsk;
    if (f != ultimaFoto_) {
        ultimaFoto_ = f;
        if (alCambiar_) alCambiar_(f.value("capas", json::array()));
    }
    json ui = capas(salidaUi_);
    // Lo que ve QML no lleva la opacidad (el fundido la mueve en cada vuelta del motor).
    const auto sinOpacidad = [](json j) {
        for (auto& c : j) c.erase("opacidad");
        return j;
    };
    if (sinOpacidad(ui) == sinOpacidad(ultimaUi_)) return;
    ultimaUi_ = std::move(ui);
    emit cambio();
}

void Dsk::pedir(const char* que, std::function<std::string(CapasDsk&)> orden) {
    v_.pedir(this, que, [c = &capas_, orden = std::move(orden)]() { return orden(*c); },
             [this](const std::string& e) {
                 if (!e.empty()) emit fallo(QString::fromStdString(e));
             });
}

std::string Dsk::definir(int capa, const json& def) {
    if (std::string e = CapasDsk::revisarDefinicion(capa, def); !e.empty()) return e;
    pedir("dsk_definir", [capa, def](CapasDsk& c) { return c.definir(capa, def); });
    return {};
}

std::string Dsk::mostrar(int capa, bool visible, bool confirmada, const std::string& salida) {
    if (std::string e = CapasDsk::revisarMostrar(definicion(capa), visible, confirmada); !e.empty()) return e;
    pedir("dsk_mostrar", [=](CapasDsk& c) { return c.mostrar(capa, visible, confirmada, salida); });
    return {};
}

std::string Dsk::plantillaLsa(int capa, const std::string& entrada, const std::string& posicion, double tamano) {
    if (std::string e = CapasDsk::revisarLsa(entrada); !e.empty()) return e;
    return definir(capa, CapasDsk::definicionLsa(entrada, posicion, tamano));
}

void Dsk::restaurar() {
    pedir("dsk_restaurar", [](CapasDsk& c) {
        c.restaurar();
        return std::string();
    });
}

QVariantList Dsk::capasUi() const { return aQv(ultimaUi_).toList(); }
QString Dsk::definirUi(int capa, const QVariantMap& def) { return QString::fromStdString(definir(capa, deQv(def))); }
QString Dsk::mostrarUi(int capa, bool visible, bool confirmada) {
    return QString::fromStdString(mostrar(capa, visible, confirmada, salidaUi_));
}
QString Dsk::plantillaLsaUi(int capa, const QString& entrada) {
    return QString::fromStdString(plantillaLsa(capa, entrada.toStdString(), "abajo_derecha", 0.28));
}
bool Dsk::fija(int capa) const {
    const json c = definicion(capa);
    return c.is_object() && c.value("fijo", false);
}

} // namespace ba::app::control
