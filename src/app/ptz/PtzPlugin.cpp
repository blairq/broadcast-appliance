// src/app/ptz/PtzPlugin.cpp — ver PtzPlugin.hpp

#include "ptz/PtzPlugin.hpp"

#include <QJsonObject>

namespace ba::app::ptz {

PtzPlugin::Enlace& PtzPlugin::enlace() {
    static Enlace e;
    return e;
}

Capacidades PtzPlugin::capacidades() const {
    Capacidades c;
    if (!enlace().capacidades) return c;
    const nlohmann::json j = enlace().capacidades(plugin_, entrada_);
    if (!j.is_object()) return c;
    c.panTilt = j.value("panTilt", false);
    c.zoom = j.value("zoom", false);
    c.foco = j.value("foco", false);
    c.autoFoco = j.value("autoFoco", false);
    c.home = j.value("home", false);
    c.presets = j.value("presets", 0);
    return c;
}

void PtzPlugin::configurar(const QJsonObject& conf) {
    plugin_ = conf.value("plugin").toString();
    entrada_ = conf.value("entrada").toString();
    refrescar();
}

void PtzPlugin::refrescar() {
    const bool hay = enlace().capacidades && enlace().capacidades(plugin_, entrada_).is_object();
    setEstado(hay, hay ? QStringLiteral("Plugin «%1»").arg(plugin_) : QStringLiteral("El plugin «%1» no está corriendo").arg(plugin_));
}

void PtzPlugin::pedir(const std::string& accion, nlohmann::json args) {
    if (!enlace().pedir || plugin_.isEmpty()) return;
    args["entrada"] = entrada_.toStdString();
    enlace().pedir(plugin_, accion, args);
}

void PtzPlugin::mover(double pan, double tilt, double zoom) { pedir("ptz.mover", {{"pan", pan}, {"tilt", tilt}, {"zoom", zoom}}); }
void PtzPlugin::parar() { pedir("ptz.parar", nlohmann::json::object()); }
void PtzPlugin::enfocar(double velocidad) { pedir("ptz.enfocar", {{"velocidad", velocidad}}); }
void PtzPlugin::autoFoco(bool automatico) { pedir("ptz.auto_foco", {{"prendido", automatico}}); }
void PtzPlugin::irAPreset(int numero) { pedir("ptz.ir_a_preset", {{"numero", numero}}); }
void PtzPlugin::guardarPreset(int numero) { pedir("ptz.guardar_preset", {{"numero", numero}}); }
void PtzPlugin::home() { pedir("ptz.home", nlohmann::json::object()); }

} // namespace ba::app::ptz

