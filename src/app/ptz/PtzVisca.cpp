// src/app/ptz/PtzVisca.cpp — ver PtzVisca.hpp

#include "ptz/PtzVisca.hpp"

#include "ptz/PtzProtocolos.hpp"

#include <QHostInfo>
#include <QTcpSocket>
#include <QUdpSocket>

namespace ba::app::ptz {

PtzVisca::PtzVisca(QObject* parent) : PtzBackend(parent) {
    junta_.setInterval(40);
    connect(&junta_, &QTimer::timeout, this, &PtzVisca::vaciar);
    silencio_.setSingleShot(true);
    silencio_.setInterval(1500);
    connect(&silencio_, &QTimer::timeout, this,
            [this]() { setEstado(false, QStringLiteral("Sin respuesta de %1:%2").arg(host_).arg(puerto_)); });
    reconexion_.setSingleShot(true);
    reconexion_.setInterval(3000);
    connect(&reconexion_, &QTimer::timeout, this, &PtzVisca::conectarTcp);
}

PtzVisca::~PtzVisca() = default;

int PtzVisca::puertoPorDefecto(const QString& transporte) {
    if (transporte == "tcp") return 5678;
    if (transporte == "udp") return 1259;
    return 52381;
}

Capacidades PtzVisca::capacidades() const {
    Capacidades c;
    c.panTilt = c.zoom = c.foco = c.autoFoco = c.home = true;
    c.presets = 128;
    return c;
}

void PtzVisca::configurar(const QJsonObject& conf) {
    transporte_ = conf.value("transporte").toString("sony");
    host_ = conf.value("host").toString().trimmed();
    puerto_ = conf.value("puerto").toInt(0);
    if (puerto_ <= 0) puerto_ = puertoPorDefecto(transporte_);
    direccion_ = qBound(1, conf.value("direccion").toInt(1), 7);
    secuencia_ = 0;
    dir_.clear();
    restoTcp_.clear();
    pendienteTcp_.clear();
    delete udp_;
    udp_ = nullptr;
    delete tcp_;
    tcp_ = nullptr;
    reconexion_.stop();
    silencio_.stop();
    if (host_.isEmpty()) {
        setEstado(false, QStringLiteral("Falta la dirección de la cámara"));
        return;
    }
    if (transporte_ == "tcp") {
        tcp_ = new QTcpSocket(this);
        connect(tcp_, &QTcpSocket::readyRead, this, &PtzVisca::leer);
        connect(tcp_, &QTcpSocket::connected, this, [this]() {
            setEstado(true, QStringLiteral("Conectada (VISCA TCP %1:%2)").arg(host_).arg(puerto_));
            if (!pendienteTcp_.isEmpty()) tcp_->write(pendienteTcp_);
            pendienteTcp_.clear();
        });
        connect(tcp_, &QTcpSocket::errorOccurred, this, [this]() {
            setEstado(
                false,
                QStringLiteral("No conecta con %1:%2 (%3)").arg(host_).arg(puerto_).arg(tcp_->errorString()));
            reconexion_.start();
        });
        conectarTcp();
        return;
    }
    udp_ = new QUdpSocket(this);
    udp_->bind(QHostAddress::AnyIPv4, 0);
    connect(udp_, &QUdpSocket::readyRead, this, &PtzVisca::leer);
    setEstado(false, QStringLiteral("Buscando %1…").arg(host_));
    QHostInfo::lookupHost(host_, this, [this, h = host_](const QHostInfo& info) {
        if (h != host_) return; // se reconfiguró mientras tanto
        if (info.addresses().isEmpty()) {
            setEstado(false, QStringLiteral("No encuentro «%1» en la red").arg(host_));
            return;
        }
        for (const QHostAddress& a : info.addresses())
            if (a.protocol() == QAbstractSocket::IPv4Protocol) dir_ = a;
        if (dir_.isNull()) dir_ = info.addresses().first();
        if (transporte_ == "sony") {
            // Empieza la secuencia de cero (si la cámara venía de otra sesión).
            udp_->writeDatagram(visca::reiniciarSecuencia(), dir_, static_cast<quint16>(puerto_));
            silencio_.start();
        }
        // Un pedido inocuo para saber si contesta: «zoom quieto».
        enviar(visca::zoom(0, direccion_));
    });
}

void PtzVisca::conectarTcp() {
    if (tcp_ == nullptr) return;
    tcp_->abort();
    tcp_->connectToHost(host_, static_cast<quint16>(puerto_));
}

void PtzVisca::enviar(const QByteArray& m) {
    if (tcp_ != nullptr) {
        if (tcp_->state() == QAbstractSocket::ConnectedState)
            tcp_->write(m);
        else
            pendienteTcp_ = m; // la última; al conectar sale
    } else if (udp_ != nullptr && !dir_.isNull()) {
        const QByteArray d = transporte_ == "sony" ? visca::conEncabezado(0x0100, m, ++secuencia_) : m;
        udp_->writeDatagram(d, dir_, static_cast<quint16>(puerto_));
    } else {
        return;
    }
    if (!silencio_.isActive()) silencio_.start();
}

void PtzVisca::leer() {
    silencio_.stop();
    QList<QByteArray> respuestas;
    if (tcp_ != nullptr) {
        // Por TCP pueden venir varias pegadas (o una partida): FF cierra cada una.
        restoTcp_ += tcp_->readAll();
        int fin;
        while ((fin = restoTcp_.indexOf(char(0xFF))) >= 0) {
            respuestas << restoTcp_.left(fin + 1);
            restoTcp_.remove(0, fin + 1);
        }
    } else if (udp_ != nullptr) {
        while (udp_->hasPendingDatagrams()) {
            QByteArray d;
            d.resize(static_cast<int>(udp_->pendingDatagramSize()));
            udp_->readDatagram(d.data(), d.size());
            respuestas << d;
        }
    }
    for (const QByteArray& m : respuestas) {
        const auto r = visca::leer(m);
        if (r.tipo == visca::Respuesta::Error) {
            setEstado(true, QStringLiteral("La cámara contestó: %1").arg(visca::textoError(r.codigo)));
            return;
        }
    }
    setEstado(true, QStringLiteral("Conectada (VISCA %1 %2:%3)").arg(transporte_, host_).arg(puerto_));
}

void PtzVisca::mover(double pan, double tilt, double zoom) {
    panTiltPend_ = visca::panTilt(pan, tilt, direccion_);
    zoomPend_ = visca::zoom(zoom, direccion_);
    if (pan == 0 && tilt == 0 && zoom == 0) {
        parar();
        return;
    }
    if (!junta_.isActive()) {
        vaciar(); // la primera sale ya: no se siente demora al tocar
        junta_.start();
    }
}

void PtzVisca::vaciar() {
    bool algo = false;
    if (!panTiltPend_.isEmpty() && panTiltPend_ != panTiltEnviado_) {
        enviar(panTiltPend_);
        panTiltEnviado_ = panTiltPend_;
        algo = true;
    }
    if (!zoomPend_.isEmpty() && zoomPend_ != zoomEnviado_) {
        enviar(zoomPend_);
        zoomEnviado_ = zoomPend_;
        algo = true;
    }
    if (!focoPend_.isEmpty()) {
        enviar(focoPend_);
        focoPend_.clear();
        algo = true;
    }
    if (!algo) junta_.stop();
}

void PtzVisca::parar() {
    junta_.stop();
    panTiltPend_ = panTiltEnviado_ = visca::panTilt(0, 0, direccion_);
    zoomPend_ = zoomEnviado_ = visca::zoom(0, direccion_);
    enviar(panTiltPend_);
    enviar(zoomPend_);
}

void PtzVisca::enfocar(double v) {
    focoPend_ = visca::foco(v, direccion_);
    if (v == 0) {
        enviar(focoPend_);
        focoPend_.clear();
    } else if (!junta_.isActive()) {
        vaciar();
        junta_.start();
    }
}

void PtzVisca::autoFoco(bool automatico) { enviar(visca::focoAuto(automatico, direccion_)); }

void PtzVisca::irAPreset(int n) {
    enviar(visca::preset(false, n, direccion_));
    setEstado(listo(), QStringLiteral("Yendo al preset %1").arg(n));
}

void PtzVisca::guardarPreset(int n) {
    enviar(visca::preset(true, n, direccion_));
    setEstado(listo(), QStringLiteral("Preset %1 guardado en la cámara").arg(n));
}

void PtzVisca::home() { enviar(visca::home(direccion_)); }

void PtzVisca::borrarPosicion(int n, const QString&) {
    if (n > 0) enviar(visca::borrarPreset(n, direccion_));
}

} // namespace ba::app::ptz
