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

#include <functional>

namespace ba::app::ptz {

struct Capacidades {
    bool panTilt = false;
    bool zoom = false;
    bool foco = false;     ///< foco manual continuo
    bool autoFoco = false; ///< prender/apagar el automático
    bool home = false;
    int presets = 0; ///< cuántos (0 = no tiene)

    QVariantMap mapa() const {
        return {{"panTilt", panTilt},   {"zoom", zoom}, {"foco", foco},
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

    // --- Memorias para las posiciones con nombre (ptz/Posiciones) -----------
    /// Hasta qué número de memoria tiene (0 = no tiene memorias propias).
    virtual int memorias() const { return capacidades().presets; }
    /// Apuntar EXACTO a un punto (u, v en 0..1 de la imagen) y, con `ancho`/`alto` > 0, acercar
    /// hasta que ese cuadro llene la imagen, si la cámara lo sabe hacer sola (Axis: center y
    /// areazoom). false: no sabe (ControlCamaras usa pulsos aproximados).
    virtual bool apuntar(double u, double v, double ancho, double alto) {
        Q_UNUSED(u);
        Q_UNUSED(v);
        Q_UNUSED(ancho);
        Q_UNUSED(alto);
        return false;
    }
    /// Dónde está el motor, si el protocolo lo dice: {pan, tilt, zoom (0..1), moviendo}.
    virtual QVariantMap motor() const { return {}; }
    /// Lo que la cámara dice que puede (por protocolo), con cómo se maneja cada eje.
    virtual QVariantMap funciones() const { return {}; }
    /// La cámara identifica sus memorias con un token que elige ella (ONVIF).
    virtual bool usaTokens() const { return false; }
    /// Graba donde está ahora en la memoria `n` (o, con tokens, en una nueva
    /// llamada `nombre`). `listo(ok, token)` al terminar.
    virtual void grabarPosicion(int n, const QString& nombre, std::function<void(bool, QString)> listo) {
        Q_UNUSED(nombre);
        guardarPreset(n);
        listo(true, {});
    }
    virtual void irAPosicion(int n, const QString& token) {
        Q_UNUSED(token);
        irAPreset(n);
    }
    virtual void borrarPosicion(int n, const QString& token) {
        Q_UNUSED(n);
        Q_UNUSED(token);
    }

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
