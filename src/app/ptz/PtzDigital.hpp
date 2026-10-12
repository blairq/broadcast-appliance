// src/app/ptz/PtzDigital.hpp — el PTZ que tiene cualquier entrada
//
// Maneja el elemento `baptz` (src/gst-plugins/filtros/ptz.cpp) de la cadena
// de una entrada: recorte y zoom, con joystick y presets. Los presets
// digitales (x, y, zoom) los guarda la app (ControlCamaras, en
// Config/camaras.json); la cámara no se entera.
//
// Si la entrada tiene además un backend de hardware, los dos conviven: la
// interfaz elige qué mueve el joystick (la cabeza o el recorte).

#pragma once

#include "ptz/PtzBackend.hpp"

#include <QJsonObject>
#include <QVariantMap>

#include <map>
#include <string>

namespace ba::app::ptz {

/// El id del filtro en la cadena de la entrada, y el nombre de su elemento.
inline constexpr const char* kFiltroPtz = "ptz";
inline constexpr const char* kElementoPtz = "ba_f_ptz";

class PtzDigital : public PtzBackend {
    Q_OBJECT
   public:
    explicit PtzDigital(std::string fuente, QObject* parent = nullptr);

    QString tipo() const override { return QStringLiteral("digital"); }
    Capacidades capacidades() const override;
    void configurar(const QJsonObject& conf) override;

    void mover(double pan, double tilt, double zoom) override;
    void parar() override;
    void irAPreset(int numero) override;
    void guardarPreset(int numero) override;
    void home() override;

    /// Ir a una posición (centro x, y 0..1; zoom 1..8), en `segundos`.
    bool ir(double x, double y, double zoom, double segundos);
    /// {x, y, zoom, moviendo} (vacío si el filtro no está).
    QVariantMap posicion() const;

    /// Presets digitales: {"1": {x, y, zoom}, ...}.
    QJsonObject presets() const { return presets_; }
    void setPresets(const QJsonObject& p) { presets_ = p; }

   private:
    /// Una propiedad del elemento; false (y el estado lo dice) si no está.
    bool poner(const char* prop, const std::string& valor);

    std::string fuente_;
    QJsonObject presets_;
    double transicion_ = 1.0; ///< segundos para ir a un preset
};

} // namespace ba::app::ptz
