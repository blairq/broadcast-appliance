// src/app/ptz/OnvifBusqueda.cpp — ver OnvifBusqueda.hpp

#include "ptz/OnvifBusqueda.hpp"

#include <QHostAddress>
#include <QNetworkInterface>
#include <QUdpSocket>
#include <QUrl>
#include <QUuid>

namespace ba::app::ptz {

OnvifBusqueda::OnvifBusqueda(QObject* parent) : QObject(parent) {
    reloj_.setSingleShot(true);
    connect(&reloj_, &QTimer::timeout, this, [this]() {
        for (QUdpSocket* s : sockets_) s->deleteLater();
        sockets_.clear();
        emit cambio();
        emit terminada();
    });
}

void OnvifBusqueda::buscar(int ms) {
    if (reloj_.isActive()) return;
    encontradas_.clear();
    const QByteArray sondeo = onvif::sondeo(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QHostAddress destino(destino_);
    // Una vez por cada placa con IPv4 (si no, el multicast sale por una sola
    // y no se ven las cámaras de la red de producción).
    QList<QHostAddress> locales;
    if (destino.isMulticast()) {
        for (const QNetworkInterface& i : QNetworkInterface::allInterfaces()) {
            if (!(i.flags() & QNetworkInterface::IsUp) || !(i.flags() & QNetworkInterface::CanMulticast)) continue;
            for (const QNetworkAddressEntry& e : i.addressEntries())
                if (e.ip().protocol() == QAbstractSocket::IPv4Protocol && !e.ip().isLoopback()) locales << e.ip();
        }
    }
    if (locales.isEmpty()) locales << QHostAddress(QHostAddress::AnyIPv4);
    for (const QHostAddress& local : locales) {
        auto* s = new QUdpSocket(this);
        if (!s->bind(local, 0)) {
            delete s;
            continue;
        }
        connect(s, &QUdpSocket::readyRead, this, [this, s]() { leer(s); });
        s->writeDatagram(sondeo, destino, puerto_);
        sockets_ << s;
    }
    reloj_.start(ms);
    emit cambio();
}

void OnvifBusqueda::leer(QUdpSocket* s) {
    while (s->hasPendingDatagrams()) {
        QByteArray d;
        d.resize(static_cast<int>(s->pendingDatagramSize()));
        QHostAddress desde;
        s->readDatagram(d.data(), d.size(), &desde);
        onvif::Encontrada e = onvif::leerEncontrada(d);
        e.origen = QHostAddress(desde.toIPv4Address()).toString();
        // La dirección a usar: la que coincide con quien contestó (no una
        // 169.254 autoasignada que la cámara anuncia primero).
        for (const QString& x : e.todas)
            if (QUrl(x).host() == e.origen) e.direccion = x;
        if (e.direccion.isEmpty()) continue;
        bool repetida = false;
        for (const auto& x : encontradas_) repetida = repetida || x.direccion == e.direccion;
        if (!repetida) {
            encontradas_ << e;
            emit cambio();
        }
    }
}

QVariantList OnvifBusqueda::encontradas() const {
    QVariantList out;
    for (const auto& e : encontradas_) {
        const QUrl u(e.direccion);
        out << QVariantMap{{"direccion", e.direccion},
                           {"host", u.port() > 0 && u.port() != 80 ? u.host() + ":" + QString::number(u.port()) : u.host()},
                           {"nombre", e.nombre},
                           {"modelo", e.modelo},
                           {"origen", e.origen},
                           {"todas", e.todas}};
    }
    return out;
}

} // namespace ba::app::ptz
