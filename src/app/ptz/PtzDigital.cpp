// src/app/ptz/PtzDigital.cpp — ver PtzDigital.hpp

#include "ptz/PtzDigital.hpp"

#include <obs.h>

namespace ba::app::ptz {

PtzDigital::PtzDigital(std::string fuente, QObject* parent) : PtzBackend(parent), fuente_(std::move(fuente)) {
    setEstado(true, QStringLiteral("Digital (recorte en la GPU)"));
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

obs_source* PtzDigital::filtro() const {
    obs_source_t* src = obs_get_source_by_name(fuente_.c_str());
    if (src == nullptr) return nullptr;
    obs_source_t* f = obs_source_get_filter_by_name(src, kFiltroPtz);
    obs_source_release(src);
    return f;
}

void PtzDigital::mover(double pan, double tilt, double zoom) {
    obs_source_t* f = filtro();
    if (f == nullptr) {
        setEstado(false, QStringLiteral("El PTZ digital está apagado en esta entrada"));
        return;
    }
    calldata_t cd;
    calldata_init(&cd);
    calldata_set_float(&cd, "vx", pan);
    // En la imagen y crece hacia abajo: tilt + (arriba) es y -.
    calldata_set_float(&cd, "vy", -tilt);
    calldata_set_float(&cd, "vz", zoom);
    proc_handler_call(obs_source_get_proc_handler(f), "mover", &cd);
    calldata_free(&cd);
    obs_source_release(f);
}

void PtzDigital::parar() { mover(0, 0, 0); }

bool PtzDigital::ir(double x, double y, double zoom, double segundos) {
    obs_source_t* f = filtro();
    if (f == nullptr) {
        setEstado(false, QStringLiteral("El PTZ digital está apagado en esta entrada"));
        return false;
    }
    calldata_t cd;
    calldata_init(&cd);
    calldata_set_float(&cd, "x", x);
    calldata_set_float(&cd, "y", y);
    calldata_set_float(&cd, "zoom", zoom);
    calldata_set_float(&cd, "segundos", segundos);
    proc_handler_call(obs_source_get_proc_handler(f), "ir", &cd);
    calldata_free(&cd);
    obs_source_release(f);
    return true;
}

QVariantMap PtzDigital::posicion() const {
    obs_source_t* f = filtro();
    if (f == nullptr) return {};
    calldata_t cd;
    calldata_init(&cd);
    proc_handler_call(obs_source_get_proc_handler(f), "posicion", &cd);
    QVariantMap m{{"x", calldata_float(&cd, "x")},
                  {"y", calldata_float(&cd, "y")},
                  {"zoom", calldata_float(&cd, "zoom")},
                  {"moviendo", calldata_bool(&cd, "moviendo")}};
    calldata_free(&cd);
    obs_source_release(f);
    return m;
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
