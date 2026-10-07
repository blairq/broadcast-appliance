// src/app/ptz/PtzDigital.cpp — ver PtzDigital.hpp

#include "ptz/PtzDigital.hpp"

#include "emision/FuentesApp.hpp"

#include <cstdio>

namespace ba::app::ptz {

PtzDigital::PtzDigital(std::string fuente, QObject* parent) : PtzBackend(parent), fuente_(std::move(fuente)) {
    setEstado(true, QStringLiteral("Digital (recorte en la entrada)"));
}

Capacidades PtzDigital::capacidades() const {
    Capacidades c;
    c.panTilt = c.zoom = c.home = true;
    c.presets = 16;
    return c;
}

void PtzDigital::configurar(const QJsonObject& conf) {
    transicion_ = qBound(0.0, conf.value("transicion").toDouble(1.0), 10.0);
}

namespace {
/// Un decimal con punto (las propiedades de GStreamer no miran el locale).
std::string decimal(double v) {
    char b[48];
    std::snprintf(b, sizeof b, "%.6f", v);
    for (char* c = b; *c; ++c)
        if (*c == ',') *c = '.';
    return b;
}
} // namespace

bool PtzDigital::poner(const char* prop, const std::string& valor) {
    const std::string e = fuentes::propiedadElemento(fuente_, kElementoPtz, prop, valor);
    if (!e.empty()) {
        setEstado(false, QStringLiteral("El PTZ digital está apagado en esta entrada"));
        return false;
    }
    return true;
}

void PtzDigital::mover(double pan, double tilt, double zoom) {
    // En la imagen y crece hacia abajo: tilt + (arriba) es y -.
    poner("vx", decimal(pan)) && poner("vy", decimal(-tilt)) && poner("vz", decimal(zoom));
}

void PtzDigital::parar() { mover(0, 0, 0); }

bool PtzDigital::ir(double x, double y, double zoom, double segundos) {
    return poner("ir", decimal(x) + "," + decimal(y) + "," + decimal(zoom) + "," + decimal(segundos));
}

QVariantMap PtzDigital::posicion() const {
    const nlohmann::json p = fuentes::propiedadesElemento(fuente_, kElementoPtz);
    if (!p.contains("zoom")) return {};
    return {{"x", p.value("x", 0.5)}, {"y", p.value("y", 0.5)}, {"zoom", p.value("zoom", 1.0)}, {"moviendo", p.value("moviendo", false)}};
}

void PtzDigital::irAPreset(int n) {
    const QJsonObject p = presets_.value(QString::number(n)).toObject();
    if (p.isEmpty()) {
        setEstado(true, QStringLiteral("El preset digital %1 está vacío").arg(n));
        return;
    }
    if (ir(p.value("x").toDouble(0.5), p.value("y").toDouble(0.5), p.value("zoom").toDouble(1), transicion_))
        setEstado(true, QStringLiteral("Yendo al preset digital %1").arg(n));
}

void PtzDigital::guardarPreset(int n) {
    const QVariantMap p = posicion();
    if (p.isEmpty()) {
        setEstado(false, QStringLiteral("El PTZ digital está apagado en esta entrada"));
        return;
    }
    presets_[QString::number(n)] =
        QJsonObject{{"x", p["x"].toDouble()}, {"y", p["y"].toDouble()}, {"zoom", p["zoom"].toDouble()}};
    setEstado(true, QStringLiteral("Preset digital %1 guardado").arg(n));
    emit estadoCambio(); // para que se guarde aunque el texto no cambie
}

void PtzDigital::home() {
    if (ir(0.5, 0.5, 1.0, transicion_)) setEstado(true, QStringLiteral("Cuadro completo"));
}

} // namespace ba::app::ptz
