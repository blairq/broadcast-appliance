// SPDX-License-Identifier: GPL-2.0-or-later
#include "control/Acciones.hpp"

#include "i18n/I18n.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QVariant>

#include <algorithm>
#include <set>
#include <fstream>

namespace ba::app::control {

using json = nlohmann::json;
namespace fs = std::filesystem;
using ba::i18n::llenar;
using ba::i18n::traducir;

namespace {
QVariant aQv(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", j}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().value("v").toVariant();
}

json herr(const std::string& id, const std::string& nombre, const std::string& grupo, const std::string& h,
          json args = json::object()) {
    return {{"id", id}, {"nombre", nombre}, {"grupo", grupo}, {"herramienta", h}, {"args", std::move(args)}};
}

/// Las acciones de aire que se listan también para cada salida («take@cam2»).
const std::set<std::string>& dePorSalida() {
    static const std::set<std::string> s{"take", "ftb", "ftb_sacar", "placa", "placa_sacar", "grabar"};
    return s;
}

/// El texto canónico de una tecla («ctrl+shift+t» -> «Ctrl+Shift+T»): así no hay dos iguales escritas distinto.
std::string canonica(const std::string& tecla) {
    const QKeySequence s(QString::fromStdString(tecla));
    return s.isEmpty() ? std::string() : s.toString(QKeySequence::PortableText).toStdString();
}
} // namespace

Acciones::Acciones(const fs::path& datos, QObject* parent)
    : QObject(parent), archivo_(datos / "Config" / "atajos.json"), globales_(AtajosGlobales::crear()) {
    std::ifstream f(archivo_);
    const json j = json::parse(f, nullptr, false);
    if (j.is_array()) atajos_ = j;
    registrarGlobales();
}

Acciones::~Acciones() = default;

bool Acciones::globalesDisponibles() const { return globales_ != nullptr; }

json Acciones::catalogo() const {
    // El nombre y el grupo se muestran (el grupo arma las secciones de la lista); la clave es el id.
    const std::string aire = traducir("acciones", "Aire"), oradores = traducir("acciones", "Oradores");
    const std::string prompter = traducir("acciones", "Prompter");
    json c = json::array({
        herr("take", traducir("acciones", "TAKE (el preview al aire)"), aire, "preview_al_aire"),
        herr("ftb", traducir("acciones", "FTB: a negro"), aire, "aire_ftb", {{"activar", true}}),
        herr("ftb_sacar", traducir("acciones", "Sacar el negro"), aire, "aire_ftb", {{"activar", false}}),
        herr("placa", traducir("acciones", "Placa «Volvemos enseguida»"), aire, "aire_placa", {{"activar", true}}),
        herr("placa_sacar", traducir("acciones", "Sacar la placa"), aire, "aire_placa", {{"activar", false}}),
        herr("grabar", traducir("acciones", "Grabar"), traducir("acciones", "Salidas"), "record_start"),
        herr("banca_siguiente", traducir("acciones", "Siguiente banca (armar)"), oradores, "banca_al_aire"),
        herr("recinto_orador_terminar", traducir("acciones", "Terminar al orador presencial"), oradores,
             "recinto_orador_terminar"),
        herr("seguir_aceptar", traducir("acciones", "Armar la banca que pidió la palabra"), oradores, "seguir_microfono",
             {{"accion", "aceptar"}}),
        herr("punto_siguiente", traducir("acciones", "Siguiente punto del orden del día"), traducir("acciones", "Sesión"),
             "orden_del_dia_punto", {{"siguiente", true}}),
        herr("prompter_correr", traducir("acciones", "Prompter: correr"), prompter, "prompter_control", {{"corriendo", true}}),
        herr("prompter_parar", traducir("acciones", "Prompter: parar"), prompter, "prompter_control", {{"corriendo", false}}),
        herr("prompter_panico", traducir("acciones", "Prompter: pánico"), prompter, "prompter_control", {{"panico", true}}),
        herr("alarmas_reconocer", traducir("acciones", "Reconocer las alarmas"), traducir("acciones", "Seguridad"),
             "alert_reconocer", {{"todas", true}}),
    });
    for (int capa = 1; capa <= 4; ++capa)
        c.push_back({{"id", "dsk:" + std::to_string(capa)},
                     {"nombre", llenar(traducir("acciones", "Subir/bajar la capa DSK %1"), {std::to_string(capa)})},
                     {"grupo", traducir("acciones", "Capas")}, {"interna", true}});
    if (g_.escenas)
        for (const auto& e : g_.escenas()) {
            const std::string escenas = traducir("acciones", "Escenas");
            c.push_back(herr("armar:" + e, llenar(traducir("acciones", "Armar «%1»"), {e}), escenas, "scene_preview",
                             {{"name", e}}));
            c.push_back(herr("cortar:" + e, llenar(traducir("acciones", "Cortar a «%1»"), {e}), escenas, "scene_switch",
                             {{"name", e}}));
        }
    if (g_.salidas) {
        // Con varias salidas: cambiar la que manejan los atajos, y las de aire de cada una.
        const json ss = g_.salidas();
        if (ss.size() > 1) {
            const std::string grupo = traducir("acciones", "Salidas");
            c.push_back({{"id", "salida:siguiente"}, {"nombre", traducir("acciones", "Manejar la salida siguiente")},
                         {"grupo", grupo}, {"interna", true}});
            c.push_back({{"id", "salida:anterior"}, {"nombre", traducir("acciones", "Manejar la salida anterior")},
                         {"grupo", grupo}, {"interna", true}});
            const json base = c; // las de aire de la por omisión, para copiarlas por salida
            for (const auto& s : ss) {
                const std::string id = s.value("id", std::string()), nombre = s.value("nombre", id);
                c.push_back({{"id", "salida:" + id}, {"nombre", llenar(traducir("acciones", "Manejar «%1»"), {nombre})},
                             {"grupo", grupo}, {"interna", true}});
                for (const auto& a : base) {
                    if (!dePorSalida().count(a.value("id", std::string()))) continue;
                    json x = a;
                    x["id"] = a.value("id", std::string()) + "@" + id;
                    x["nombre"] = a.value("nombre", std::string()) + " — " + nombre;
                    x["grupo"] = llenar(traducir("acciones", "Salida «%1»"), {nombre});
                    c.push_back(std::move(x));
                }
            }
        }
    }
    if (g_.fuentesAudio)
        for (const auto& f : g_.fuentesAudio())
            c.push_back({{"id", "mudo:" + f},
                         {"nombre", llenar(traducir("acciones", "Mutear/desmutear «%1»"), {f})},
                         {"grupo", traducir("acciones", "Audio")},
                         {"interna", true}});
    return c;
}

std::string Acciones::salidaExistente(const std::string& id) const {
    if (id.empty() || !g_.salidas) return {};
    const QString pedida = QString::fromStdString(id);
    for (const auto& s : g_.salidas())
        if (s.value("id", std::string()) == id ||
            QString::fromStdString(s.value("nombre", std::string())).compare(pedida, Qt::CaseInsensitive) == 0)
            return s.value("id", std::string());
    return {};
}

std::string Acciones::setSalida(const std::string& salida) {
    std::string nueva;
    if (salida == "siguiente" || salida == "anterior") {
        const json ss = g_.salidas ? g_.salidas() : json::array();
        if (ss.empty()) return {};
        int i = 0;
        for (int k = 0; k < static_cast<int>(ss.size()); ++k)
            if (ss[k].value("id", std::string()) == salida_ || (salida_.empty() && ss[k].value("por_omision", false))) i = k;
        const int n = static_cast<int>(ss.size());
        nueva = ss[((i + (salida == "siguiente" ? 1 : -1)) % n + n) % n].value("id", std::string());
    } else if (!salida.empty()) {
        nueva = salidaExistente(salida);
        if (nueva.empty()) return llenar(traducir("acciones", "no hay una salida «%1»"), {salida});
    }
    // La por omisión se guarda como "" (lo que no dice salida va a esa).
    if (g_.salidas)
        for (const auto& s : g_.salidas())
            if (s.value("id", std::string()) == nueva && s.value("por_omision", false)) nueva.clear();
    if (nueva == salida_) return {};
    salida_ = nueva;
    emit salidaCambio();
    return {};
}

json Acciones::resolver(const std::string& pedido) const {
    // «<acción>@<salida>»: esa acción en esa salida (si lo que sigue a la @ es una salida;
    // si no, la @ es parte del nombre, p. ej. de una escena).
    std::string id = pedido, salida = salida_;
    bool pedida = false;
    if (const auto arroba = pedido.rfind('@'); arroba != std::string::npos)
        if (const std::string s = salidaExistente(pedido.substr(arroba + 1)); !s.empty()) {
            id = pedido.substr(0, arroba);
            salida = s;
            pedida = true;
        }
    json r = resolverSinSalida(id, salida);
    if (!r.contains("error") && !r.contains("elegir_salida")) {
        r["salida"] = salida;
        r["salida_pedida"] = pedida; // la dice la acción («@<salida>»), no los atajos
    }
    return r;
}

json Acciones::resolverSinSalida(const std::string& id, const std::string& salida) const {
    if (id.rfind("salida:", 0) == 0) {
        const std::string s = id.substr(7);
        if (s != "siguiente" && s != "anterior" && salidaExistente(s).empty())
            return {{"error", llenar(traducir("acciones", "no hay una salida «%1»"), {s})}};
        return {{"elegir_salida", s}};
    }
    // Las internas: alternar algo que depende del estado.
    if (id.rfind("dsk:", 0) == 0) {
        const int capa = std::atoi(id.c_str() + 4);
        if (capa < 1 || capa > 4) return {{"error", traducir("acciones", "capa inválida")}};
        const bool arriba = g_.dskArriba && g_.dskArriba(capa);
        return {{"herramienta", "dsk_mostrar"}, {"args", {{"capa", capa}, {"visible", !arriba}}}};
    }
    if (id.rfind("mudo:", 0) == 0) {
        const std::string f = id.substr(5);
        const bool mudo = g_.mudo && g_.mudo(f, salida); // el mudo es de cada salida
        return {{"herramienta", "audio_set"}, {"args", {{"name", f}, {"muted", !mudo}}}};
    }
    for (const auto& a : catalogo())
        if (a.value("id", std::string()) == id && a.contains("herramienta"))
            return {{"herramienta", a.value("herramienta", std::string())}, {"args", a.value("args", json::object())}};
    return {{"error", llenar(traducir("acciones", "no hay una acción «%1» (acciones_listar)"), {id})}};
}

std::string Acciones::ejecutar(const std::string& id, Listo listo) {
    if (!g_.ejecutar) return traducir("acciones", "todavía no está listo");
    if (!listo) listo = [](bool, const std::string&) {};
    const json r = resolver(id);
    if (r.contains("error")) return r.value("error", std::string());
    if (r.contains("elegir_salida")) {
        const std::string e = setSalida(r.value("elegir_salida", std::string()));
        listo(e.empty(), e);
        return e;
    }
    g_.ejecutar(r.value("herramienta", std::string()), r.value("args", json::object()), r.value("salida", std::string()),
                listo);
    return {};
}

void Acciones::guardar() const {
    std::error_code ec;
    fs::create_directories(archivo_.parent_path(), ec);
    std::ofstream o(archivo_);
    o << atajos_.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
}

void Acciones::registrarGlobales() {
    if (!globales_) return;
    globales_->limpiar();
    for (const auto& a : atajos_) {
        if (!a.value("global", false)) continue;
        const std::string accion = a.value("accion", std::string());
        globales_->registrar(a.value("tecla", std::string()), [this, accion]() { ejecutar(accion); });
    }
}

std::string Acciones::asignar(const std::string& tecla, const std::string& accion, bool global) {
    const std::string t = canonica(tecla);
    if (t.empty()) return llenar(traducir("acciones", "la tecla «%1» no se entiende (p. ej. «Ctrl+Shift+T» o «F5»)"), {tecla});
    // Lo que resuelve existe (también «armar:<escena>@<salida>», que no se lista).
    if (resolver(accion).contains("error")) return llenar(traducir("acciones", "no hay una acción «%1»"), {accion});
    // Conflicto: la misma tecla en otra acción.
    for (const auto& a : atajos_)
        if (a.value("tecla", std::string()) == t && a.value("accion", std::string()) != accion)
            return llenar(traducir("acciones", "«%1» ya es de «%2»: sacala primero"), {t, a.value("accion", std::string())});
    if (global && !globales_) return traducir("acciones", "este sistema todavía no tiene atajos globales (sirve como local)");
    atajos_.erase(std::remove_if(atajos_.begin(), atajos_.end(),
                                 [&](const json& a) { return a.value("accion", std::string()) == accion; }),
                  atajos_.end());
    atajos_.push_back({{"tecla", t}, {"accion", accion}, {"global", global}});
    guardar();
    registrarGlobales();
    emit cambio();
    return {};
}

bool Acciones::quitar(const std::string& tecla) {
    const std::string t = canonica(tecla);
    const auto n = atajos_.size();
    atajos_.erase(std::remove_if(atajos_.begin(), atajos_.end(),
                                 [&](const json& a) { return a.value("tecla", std::string()) == t || a.value("accion", std::string()) == tecla; }),
                  atajos_.end());
    if (atajos_.size() == n) return false;
    guardar();
    registrarGlobales();
    emit cambio();
    return true;
}

QVariantList Acciones::atajosUi() const { return aQv(atajos_).toList(); }
QVariantList Acciones::catalogoUi() const { return aQv(catalogo()).toList(); }
QString Acciones::ejecutarUi(const QString& id) { return QString::fromStdString(ejecutar(id.toStdString())); }
QString Acciones::asignarUi(const QString& tecla, const QString& accion, bool global) {
    return QString::fromStdString(asignar(tecla.toStdString(), accion.toStdString(), global));
}
bool Acciones::quitarUi(const QString& tecla) { return quitar(tecla.toStdString()); }
QString Acciones::atajoDe(const QString& accion) const {
    for (const auto& a : atajos_)
        if (a.value("accion", std::string()) == accion.toStdString())
            return QString::fromStdString(a.value("tecla", std::string()));
    return {};
}

} // namespace ba::app::control
