// ============================================================================
// src/app/ptz/PtzPlugin.hpp — el PTZ de una cámara lo maneja un plugin de Python
//
// Para las cámaras que se mueven "de una forma rara" (un USB propio, HID, la
// API de la marca): el plugin declara `@plugin.ptz("Entrada")` y la entrada
// queda con este backend. Cada orden (mover, parar, preset…) va al plugin
// como un pedido `ptz.<acción>` (plugins/Plugins.cpp) y la cámara se maneja
// igual que una VISCA u ONVIF: botonera, posiciones con nombre, asistente.
//
// No conoce a Plugins (está en otra capa): Plugins se anota en `enlace()`.
// ============================================================================
#pragma once

#include "ptz/PtzBackend.hpp"

#include <nlohmann/json.hpp>

#include <functional>

namespace ba::app::ptz {

class PtzPlugin : public PtzBackend {
    Q_OBJECT
   public:
    struct Enlace {
        /// Manda `accion` (ptz.mover, ptz.parar…) con `args` al plugin.
        std::function<void(const QString& plugin, const std::string& accion, const nlohmann::json& args)> pedir;
        /// Lo que declaró el plugin para esa entrada (vacío si no corre).
        std::function<nlohmann::json(const QString& plugin, const QString& entrada)> capacidades;
    };
    static Enlace& enlace();

    using PtzBackend::PtzBackend;
    QString tipo() const override { return QStringLiteral("plugin"); }
    Capacidades capacidades() const override;
    void configurar(const QJsonObject& conf) override;

    void mover(double pan, double tilt, double zoom) override;
    void parar() override;
    void enfocar(double velocidad) override;
    void autoFoco(bool automatico) override;
    void irAPreset(int numero) override;
    void guardarPreset(int numero) override;
    void home() override;

    /// Lo llama Plugins cuando el plugin arranca, se cae o vuelve.
    void refrescar();

   private:
    void pedir(const std::string& accion, nlohmann::json args);
    QString plugin_, entrada_;
};

} // namespace ba::app::ptz
