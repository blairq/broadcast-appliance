// src/app/ptz/PtzVisca.hpp — VISCA por IP
//
// Tres transportes (conf «transporte»):
//   sony  UDP 52381 con el encabezado de 8 bytes de Sony (BRC, SRG, FR7 y
//         las que lo copian: BirdDog, Avonic, Marshall, AVer, Lumens).
//   udp   VISCA «crudo» por UDP (sin encabezado; PTZOptics usa 1259).
//   tcp   VISCA crudo por TCP (PTZOptics y muchas chinas: 5678).
//
// El joystick manda muchas órdenes por segundo: se juntan y sale la última
// cada 40 ms (una cámara VISCA se atraganta si le llegan de a cientos). Parar
// sale en el acto. Si no contesta nada en 1,5 s, queda «sin respuesta».

#pragma once

#include "ptz/PtzBackend.hpp"

#include <QByteArray>
#include <QHostAddress>
#include <QPointer>
#include <QTimer>

#include <cstdint>

class QUdpSocket;
class QTcpSocket;

namespace ba::app::ptz {

class PtzVisca : public PtzBackend {
    Q_OBJECT
   public:
    explicit PtzVisca(QObject* parent = nullptr);
    ~PtzVisca() override;

    QString tipo() const override { return QStringLiteral("visca"); }
    Capacidades capacidades() const override;
    void configurar(const QJsonObject& conf) override;

    void mover(double pan, double tilt, double zoom) override;
    void parar() override;
    void enfocar(double velocidad) override;
    void autoFoco(bool automatico) override;
    void irAPreset(int numero) override;
    void guardarPreset(int numero) override;
    void home() override;

    static int puertoPorDefecto(const QString& transporte);

   private:
    void enviar(const QByteArray& visca);
    void vaciar();
    void leer();
    void conectarTcp();

    QString transporte_ = QStringLiteral("sony");
    QString host_;
    QHostAddress dir_;
    int puerto_ = 52381;
    int direccion_ = 1;
    std::uint32_t secuencia_ = 0;

    QUdpSocket* udp_ = nullptr;
    QTcpSocket* tcp_ = nullptr;
    QByteArray pendienteTcp_; ///< lo que espera a que conecte el TCP
    QByteArray restoTcp_;     ///< una respuesta TCP que llegó partida

    // Lo que pidió el joystick y todavía no salió (y lo último que salió).
    QByteArray panTiltPend_, zoomPend_, focoPend_;
    QByteArray panTiltEnviado_, zoomEnviado_;
    QTimer junta_;      ///< sale lo juntado cada 40 ms
    QTimer silencio_;   ///< 1,5 s sin respuesta
    QTimer reconexion_; ///< TCP caído: reintenta
};

} // namespace ba::app::ptz
