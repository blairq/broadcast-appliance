// src/app/ptz/PtzPanasonic.hpp — Panasonic AW/UE por HTTP
//
// Órdenes de texto por GET a /cgi-bin/aw_ptz (AW-HE40/42/50/60/70/130,
// AW-UE70/100/150/160, y las que lo copian). La cámara pide al menos 130 ms
// entre órdenes: lo del joystick se junta y sale lo último; parar sale
// primero que nada. Usuario y clave (si la cámara los pide) van por HTTP
// básico.

#pragma once

#include "ptz/PtzBackend.hpp"

#include <QStringList>
#include <QTimer>

class QNetworkAccessManager;

namespace ba::app::ptz {

class PtzPanasonic : public PtzBackend {
    Q_OBJECT
   public:
    explicit PtzPanasonic(QObject* parent = nullptr);
    ~PtzPanasonic() override;

    QString tipo() const override { return QStringLiteral("panasonic"); }
    Capacidades capacidades() const override;
    void configurar(const QJsonObject& conf) override;

    void mover(double pan, double tilt, double zoom) override;
    void parar() override;
    void enfocar(double velocidad) override;
    void autoFoco(bool automatico) override;
    void irAPreset(int numero) override;
    void guardarPreset(int numero) override;
    void home() override;
    void borrarPosicion(int n, const QString& token) override;

   private:
    /// Pone una orden en la cola; `reemplaza` = la última del mismo tipo vale.
    void encolar(const QString& orden, bool reemplaza = true, bool primero = false);
    void siguiente();

    QNetworkAccessManager* red_ = nullptr;
    QString host_, usuario_, clave_;
    int puerto_ = 80;
    QStringList cola_;
    bool enVuelo_ = false;
    QTimer espacio_; ///< los 130 ms entre órdenes
};

} // namespace ba::app::ptz
