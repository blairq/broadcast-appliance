// src/app/ptz/PtzPanasonic.cpp — ver PtzPanasonic.hpp

#include "ptz/PtzPanasonic.hpp"
#include "ajustes/Ajustes.hpp"

#include "ptz/PtzProtocolos.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <algorithm>

namespace ba::app::ptz {

PtzPanasonic::PtzPanasonic(QObject* parent) : PtzBackend(parent), red_(new QNetworkAccessManager(this)) {
    espacio_.setSingleShot(true);
    espacio_.setInterval(130);
    connect(&espacio_, &QTimer::timeout, this, &PtzPanasonic::siguiente);
}

PtzPanasonic::~PtzPanasonic() = default;

Capacidades PtzPanasonic::capacidades() const {
    Capacidades c;
    c.panTilt = c.zoom = c.foco = c.autoFoco = c.home = true;
    c.presets = 100;
    return c;
}

void PtzPanasonic::configurar(const QJsonObject& conf) {
    host_ = conf.value("host").toString().trimmed();
    puerto_ = conf.value("puerto").toInt(80);
    if (puerto_ <= 0) puerto_ = 80;
    usuario_ = conf.value("usuario").toString();
    clave_ = conf.value("clave").toString();
    cola_.clear();
    if (host_.isEmpty()) {
        setEstado(false, QStringLiteral("Falta la dirección de la cámara"));
        return;
    }
    setEstado(false, QStringLiteral("Probando %1…").arg(host_));
    // Una orden inocua para ver si contesta: pan/tilt quieto.
    encolar(panasonic::panTilt(0, 0));
}

void PtzPanasonic::encolar(const QString& orden, bool reemplaza, bool primero) {
    if (reemplaza) {
        // Misma familia (#PTS, #Z, #F): queda la última.
        const QString pref = orden.left(orden.startsWith("#PTS") ? 4 : 2);
        cola_.erase(
            std::remove_if(cola_.begin(), cola_.end(), [&](const QString& o) { return o.startsWith(pref); }),
            cola_.end());
    }
    if (primero)
        cola_.prepend(orden);
    else
        cola_.append(orden);
    if (!enVuelo_ && !espacio_.isActive()) siguiente();
}

void PtzPanasonic::siguiente() {
    if (cola_.isEmpty() || enVuelo_ || host_.isEmpty()) return;
    const QString orden = cola_.takeFirst();
    QUrl url(panasonic::url(host_, puerto_, orden));
    if (!usuario_.isEmpty()) {
        url.setUserName(usuario_);
        url.setPassword(clave_);
    }
    QNetworkRequest pedido(url);
    pedido.setTransferTimeout(ba::ajustes::entero("ptz.timeoutMs", 4000) / 2);
    enVuelo_ = true;
    QNetworkReply* r = red_->get(pedido);
    connect(r, &QNetworkReply::finished, this, [this, r, orden]() {
        r->deleteLater();
        enVuelo_ = false;
        if (r->error() != QNetworkReply::NoError) {
            setEstado(false, QStringLiteral("Sin respuesta de %1 (%2)").arg(host_, r->errorString()));
        } else {
            const QByteArray cuerpo = r->readAll();
            if (panasonic::respuestaOk(orden, cuerpo))
                setEstado(true, QStringLiteral("Conectada (Panasonic %1)").arg(host_));
            else
                setEstado(true, QStringLiteral("La cámara rechazó %1 (%2)")
                                    .arg(orden, QString::fromLatin1(cuerpo).trimmed()));
        }
        espacio_.start();
    });
}

void PtzPanasonic::mover(double pan, double tilt, double zoom) {
    encolar(panasonic::panTilt(pan, tilt));
    encolar(panasonic::zoom(zoom));
}

void PtzPanasonic::parar() {
    encolar(panasonic::panTilt(0, 0), true, true);
    encolar(panasonic::zoom(0), true, true);
}

void PtzPanasonic::enfocar(double v) { encolar(panasonic::foco(v)); }
void PtzPanasonic::autoFoco(bool automatico) { encolar(panasonic::focoAuto(automatico), false); }

void PtzPanasonic::irAPreset(int n) {
    encolar(panasonic::preset(false, n), false);
    setEstado(listo(), QStringLiteral("Yendo al preset %1").arg(n));
}

void PtzPanasonic::guardarPreset(int n) {
    encolar(panasonic::preset(true, n), false);
    setEstado(listo(), QStringLiteral("Preset %1 guardado en la cámara").arg(n));
}

void PtzPanasonic::home() { encolar(panasonic::home(), false); }

void PtzPanasonic::borrarPosicion(int n, const QString&) {
    if (n > 0) encolar(panasonic::borrarPreset(n), false);
}

} // namespace ba::app::ptz
