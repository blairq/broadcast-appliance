// SPDX-License-Identifier: GPL-2.0-or-later
#include "control/Acciones.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QVariant>

#include <algorithm>
#include <fstream>

namespace ba::app::control {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
QVariant aQv(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", j}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().value("v").toVariant();
}

json herr(const std::string& id, const std::string& nombre, const std::string& grupo, const std::string& h,
          json args = json::object()) {
    return {{"id", id}, {"nombre", nombre}, {"grupo", grupo}, {"herramienta", h}, {"args", std::move(args)}};
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
    json c = json::array({
        herr("take", "TAKE (el preview al aire)", "Aire", "scene_take"),
        herr("ftb", "FTB: a negro", "Aire", "aire_ftb", {{"activar", true}}),
        herr("ftb_sacar", "Sacar el negro", "Aire", "aire_ftb", {{"activar", false}}),
        herr("placa", "Placa «Volvemos enseguida»", "Aire", "aire_placa", {{"activar", true}}),
        herr("placa_sacar", "Sacar la placa", "Aire", "aire_placa", {{"activar", false}}),
        herr("grabar", "Grabar", "Salidas", "record_start"),
        herr("banca_siguiente", "Siguiente banca (armar)", "Oradores", "banca_al_aire"),
        herr("banca_terminar", "Terminar al orador presencial", "Oradores", "banca_terminar"),
        herr("seguir_aceptar", "Armar la banca que pidió la palabra", "Oradores", "seguir_microfono", {{"accion", "aceptar"}}),
        herr("punto_siguiente", "Siguiente punto del orden del día", "Sesión", "orden_del_dia_punto", {{"siguiente", true}}),
        herr("prompter_correr", "Prompter: correr", "Prompter", "prompter_control", {{"corriendo", true}}),
        herr("prompter_parar", "Prompter: parar", "Prompter", "prompter_control", {{"corriendo", false}}),
        herr("prompter_panico", "Prompter: pánico", "Prompter", "prompter_control", {{"panico", true}}),
        herr("alarmas_reconocer", "Reconocer las alarmas", "Seguridad", "alert_reconocer", {{"todas", true}}),
    });
    for (int capa = 1; capa <= 4; ++capa)
        c.push_back({{"id", "dsk:" + std::to_string(capa)}, {"nombre", "Subir/bajar la capa DSK " + std::to_string(capa)},
                     {"grupo", "Capas"}, {"interna", true}});
    if (g_.escenas)
        for (const auto& e : g_.escenas()) {
            c.push_back(herr("armar:" + e, "Armar «" + e + "»", "Escenas", "scene_preview", {{"name", e}}));
            c.push_back(herr("cortar:" + e, "Cortar a «" + e + "»", "Escenas", "scene_switch", {{"name", e}}));
        }
    if (g_.fuentesAudio)
        for (const auto& f : g_.fuentesAudio())
            c.push_back({{"id", "mudo:" + f}, {"nombre", "Mutear/desmutear «" + f + "»"}, {"grupo", "Audio"}, {"interna", true}});
    return c;
}

json Acciones::resolver(const std::string& id) const {
    // Las internas: alternar algo que depende del estado.
    if (id.rfind("dsk:", 0) == 0) {
        const int capa = std::atoi(id.c_str() + 4);
        if (capa < 1 || capa > 4) return {{"error", "capa inválida"}};
        const bool arriba = g_.dskArriba && g_.dskArriba(capa);
        return {{"herramienta", "dsk_mostrar"}, {"args", {{"capa", capa}, {"visible", !arriba}}}};
    }
    if (id.rfind("mudo:", 0) == 0) {
        const std::string f = id.substr(5);
        const bool mudo = g_.mudo && g_.mudo(f);
        return {{"herramienta", "audio_set"}, {"args", {{"name", f}, {"muted", !mudo}}}};
    }
    for (const auto& a : catalogo())
        if (a.value("id", std::string()) == id && a.contains("herramienta"))
            return {{"herramienta", a.value("herramienta", std::string())}, {"args", a.value("args", json::object())}};
    return {{"error", "no hay una acción «" + id + "» (acciones_listar)"}};
}

std::string Acciones::ejecutar(const std::string& id, Listo listo) {
    if (!g_.ejecutar) return "todavía no está listo";
    if (!listo) listo = [](bool, const std::string&) {};
    const json r = resolver(id);
    if (r.contains("error")) return r.value("error", std::string());
    g_.ejecutar(r.value("herramienta", std::string()), r.value("args", json::object()), listo);
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
    if (t.empty()) return "la tecla «" + tecla + "» no se entiende (p. ej. «Ctrl+Shift+T» o «F5»)";
    bool existe = false;
    for (const auto& a : catalogo()) existe = existe || a.value("id", std::string()) == accion;
    if (!existe) return "no hay una acción «" + accion + "»";
    // Conflicto: la misma tecla en otra acción.
    for (const auto& a : atajos_)
        if (a.value("tecla", std::string()) == t && a.value("accion", std::string()) != accion)
            return "«" + t + "» ya es de «" + a.value("accion", std::string()) + "»: sacala primero";
    if (global && !globales_) return "este sistema todavía no tiene atajos globales (sirve como local)";
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
