// src/app/ptz/PtzDigital.hpp — el PTZ que tiene cualquier entrada
//
// Maneja el filtro `ba_ptz_digital` (src/obs-module/ba_ptz_digital.cpp) de
// una fuente: recorte y zoom en la GPU, con joystick y presets. Los presets
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

struct obs_source;

namespace ba::app::ptz {

/// Nombre del filtro en cada fuente (el de Mejorar imagen es otro).
inline constexpr const char* kFiltroPtz = "BA · PTZ digital";

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
    /// El filtro de la fuente (con referencia: hay que soltarla), o nulo.
    obs_source* filtro() const;

    std::string fuente_;
    QJsonObject presets_;
    double transicion_ = 1.0; ///< segundos para ir a un preset
};

} // namespace ba::app::ptz
