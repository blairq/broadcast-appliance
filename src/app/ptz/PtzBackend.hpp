// src/app/ptz/PtzBackend.hpp — un PTZ, del protocolo que sea
//
// El frontend del PTZ es uno solo (la página «Cámaras PTZ», las herramientas
// MCP, el asistente) y habla siempre con esta interfaz; lo que cambia es el
// backend de cada entrada de video:
//
//   digital    recorte y zoom en la GPU (filtro ba_ptz_digital): lo tiene
//              CUALQUIER entrada, aunque la cámara sea fija.
//   visca      VISCA por IP: Sony (UDP 52381, con encabezado) o «crudo» por
//              UDP o TCP (PTZOptics, BirdDog, Avonic, Marshall, AVer, Lumens,
//              casi todas las chinas).
//   onvif      ONVIF Profile S (SOAP): cámaras IP en general.
//   panasonic  Panasonic AW/UE por HTTP.
//
// Cada backend dice qué puede (`capacidades`) y la interfaz muestra solo eso.
// Todo es asíncrono y sin bloquear: las órdenes salen y las respuestas (o la
// falta de respuesta) llegan por `estadoCambio`.
//
// Velocidades normalizadas -1..1 (pan + derecha, tilt + arriba, zoom +
// acercar, foco + lejos); 0 = parar ese eje. Presets numerados desde 1.

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace ba::app::ptz {

struct Capacidades {
    bool panTilt = false;
    bool zoom = false;
    bool foco = false;     ///< foco manual continuo
    bool autoFoco = false; ///< prender/apagar el automático
    bool home = false;
    int presets = 0;       ///< cuántos (0 = no tiene)

    QVariantMap mapa() const {
        return {{"panTilt", panTilt}, {"zoom", zoom},   {"foco", foco},
                {"autoFoco", autoFoco}, {"home", home}, {"presets", presets}};
    }
};

class PtzBackend : public QObject {
    Q_OBJECT
   public:
    using QObject::QObject;
    ~PtzBackend() override = default;

    /// "digital", "visca", "onvif", "panasonic".
    virtual QString tipo() const = 0;
    virtual Capacidades capacidades() const = 0;
    /// Toma la configuración (host, puerto, usuario…) y se conecta si hace falta.
    virtual void configurar(const QJsonObject& conf) = 0;

    virtual void mover(double pan, double tilt, double zoom) = 0;
    virtual void parar() = 0;
    virtual void enfocar(double velocidad) { Q_UNUSED(velocidad); }
    virtual void autoFoco(bool automatico) { Q_UNUSED(automatico); }
    virtual void irAPreset(int numero) { Q_UNUSED(numero); }
    virtual void guardarPreset(int numero) { Q_UNUSED(numero); }
    virtual void home() {}

    /// ¿Contesta? (para mostrar «conectada» / «sin respuesta»).
    bool listo() const { return listo_; }
    /// Lo último que pasó, para mostrar («Preset 3 guardado», «sin respuesta»…).
    QString estado() const { return estado_; }

   signals:
    void estadoCambio();

   protected:
    void setEstado(bool listo, const QString& texto) {
        if (listo == listo_ && texto == estado_) return;
        listo_ = listo;
        estado_ = texto;
        emit estadoCambio();
    }

   private:
    bool listo_ = false;
    QString estado_;
};

} // namespace ba::app::ptz
