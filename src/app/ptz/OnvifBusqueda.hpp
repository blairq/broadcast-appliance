// src/app/ptz/OnvifBusqueda.hpp — encontrar cámaras ONVIF en la red
//
// WS-Discovery: un Probe por multicast (239.255.255.250:3702) desde cada
// placa de red con IPv4, y durante unos segundos se juntan las respuestas
// (ProbeMatch) con la dirección del device_service, el nombre y el modelo.

#pragma once

#include <QList>
#include <QObject>
#include <QTimer>
#include <QVariantList>

#include "ptz/PtzProtocolos.hpp"

class QUdpSocket;

namespace ba::app::ptz {

class OnvifBusqueda : public QObject {
    Q_OBJECT
   public:
    explicit OnvifBusqueda(QObject* parent = nullptr);

    /// Busca durante `ms`; al terminar emite `terminada`.
    void buscar(int ms = 3000);
    bool buscando() const { return reloj_.isActive(); }
    /// [{direccion, host, nombre, modelo}]
    QVariantList encontradas() const;

    /// A dónde se manda el Probe (los tests apuntan a un falso en 127.0.0.1).
    void setDestino(const QString& host, quint16 puerto) {
        destino_ = host;
        puerto_ = puerto;
    }

   signals:
    void cambio();
    void terminada();

   private:
    void leer(QUdpSocket* s);

    QList<QUdpSocket*> sockets_;
    QList<onvif::Encontrada> encontradas_;
    QTimer reloj_;
    QString destino_ = QStringLiteral("239.255.255.250");
    quint16 puerto_ = 3702;
};

} // namespace ba::app::ptz
