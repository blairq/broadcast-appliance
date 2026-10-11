// SPDX-License-Identifier: GPL-2.0-or-later
#include "control/Dsk.hpp"

#include "emision/BroadcastEngine.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>

#include <fstream>

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
} // namespace

Dsk::Dsk(const fs::path& datos, BroadcastEngine& engine, QObject* parent)
    : QObject(parent), archivo_(datos / "Config" / "dsk.json"), engine_(engine) {
    cargar();
}

void Dsk::cargar() {
    std::ifstream f(archivo_);
    const json j = json::parse(f, nullptr, false);
    capas_ = json::array();
    for (int c = 1; c <= 4; ++c) {
        json capa{{"capa", c}, {"nombre", "DSK " + std::to_string(c)}, {"contenido", json::array()},
                  {"recuadro", json::object()}, {"fijo", false}, {"transicion_ms", 300}, {"visible", false}};
        if (j.is_object())
            for (const auto& g : j.value("capas", json::array()))
                if (g.value("capa", 0) == c) capa.update(g);
        capas_.push_back(capa);
    }
}

void Dsk::guardar() const {
    std::error_code ec;
    fs::create_directories(archivo_.parent_path(), ec);
    std::ofstream o(archivo_);
    o << json{{"capas", capas_}}.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
}

json* Dsk::buscar(int capa) {
    for (auto& c : capas_)
        if (c.value("capa", 0) == capa) return &c;
    return nullptr;
}

json Dsk::capas() const {
    json out = capas_;
    const json e = engine_.dskEstado();
    for (auto& c : out)
        for (const auto& x : e)
            if (x.value("capa", 0) == c.value("capa", 0)) c["opacidad"] = x.value("opacidad", 0.0);
    return out;
}

void Dsk::avisar() {
    guardar();
    if (alCambiar_) alCambiar_(capas_);
    emit cambio();
}

std::string Dsk::definir(int capa, const json& def) {
    json* c = buscar(capa);
    if (c == nullptr) return "la capa va de 1 a 4";
    if (!def.is_object()) return "la definición es un objeto";
    for (const char* k : {"nombre", "contenido", "recuadro", "fijo", "transicion_ms"})
        if (def.contains(k)) (*c)[k] = def[k];
    std::vector<std::string> cont;
    for (const auto& n : c->value("contenido", json::array()))
        if (n.is_string()) cont.push_back(n.get<std::string>());
    std::string err;
    if (!engine_.dskArmar(capa, cont, c->value("recuadro", json::object()))) err = engine_.lastError();
    avisar();
    return err;
}

std::string Dsk::mostrar(int capa, bool visible, bool confirmada) {
    json* c = buscar(capa);
    if (c == nullptr) return "la capa va de 1 a 4";
    if (!visible && c->value("fijo", false) && !confirmada)
        return "la capa «" + c->value("nombre", std::string()) + "» es fija (p. ej. el intérprete de LSA): bajarla necesita confirmación";
    if (visible && c->value("contenido", json::array()).empty()) return "la capa no tiene contenido: definila primero";
    if (!engine_.dskMostrar(capa, visible, c->value("transicion_ms", 300))) return engine_.lastError();
    (*c)["visible"] = visible;
    avisar();
    return {};
}

std::string Dsk::plantillaLsa(int capa, const std::string& entrada, const std::string& posicion, double tamano) {
    if (entrada.empty()) return "falta la entrada (la cámara del intérprete)";
    return definir(capa, {{"nombre", "Intérprete LSA"},
                          {"contenido", json::array({entrada})},
                          {"recuadro", {{"posicion", posicion.empty() ? "abajo_derecha" : posicion},
                                        {"tamano", tamano > 0 ? tamano : 0.28},
                                        {"margen", 0.03}}},
                          {"fijo", true},
                          {"transicion_ms", 500}});
}

void Dsk::restaurar() {
    for (auto& c : capas_) {
        const int capa = c.value("capa", 0);
        std::vector<std::string> cont;
        for (const auto& n : c.value("contenido", json::array()))
            if (n.is_string()) cont.push_back(n.get<std::string>());
        if (cont.empty()) continue;
        engine_.dskArmar(capa, cont, c.value("recuadro", json::object()));
        if (c.value("visible", false)) engine_.dskMostrar(capa, true, 0);
    }
    if (alCambiar_) alCambiar_(capas_);
    emit cambio();
}

QVariantList Dsk::capasUi() const { return aQv(capas()).toList(); }
QString Dsk::definirUi(int capa, const QVariantMap& def) { return QString::fromStdString(definir(capa, deQv(def))); }
QString Dsk::mostrarUi(int capa, bool visible, bool confirmada) {
    return QString::fromStdString(mostrar(capa, visible, confirmada));
}
QString Dsk::plantillaLsaUi(int capa, const QString& entrada) {
    return QString::fromStdString(plantillaLsa(capa, entrada.toStdString(), "abajo_derecha", 0.28));
}
bool Dsk::fija(int capa) const {
    for (const auto& c : capas_)
        if (c.value("capa", 0) == capa) return c.value("fijo", false);
    return false;
}

} // namespace ba::app::control
