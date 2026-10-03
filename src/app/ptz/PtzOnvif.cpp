// src/app/ptz/PtzOnvif.cpp — ver PtzOnvif.hpp

#include "ptz/PtzOnvif.hpp"

#include <algorithm>
#include <memory>
#include <functional>
#include <QPointer>

#include <QAuthenticator>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace ba::app::ptz {

PtzOnvif::PtzOnvif(QObject* parent) : PtzBackend(parent), red_(new QNetworkAccessManager(this)) {
    // Hay cámaras que en vez de (o además de) WS-Security piden HTTP Digest.
    connect(red_, &QNetworkAccessManager::authenticationRequired, this,
            [this](QNetworkReply*, QAuthenticator* a) {
                if (usuario_.isEmpty()) return;
                a->setUser(usuario_);
                a->setPassword(clave_);
            });
    junta_.setInterval(100);
    connect(&junta_, &QTimer::timeout, this, &PtzOnvif::vaciar);
}

PtzOnvif::~PtzOnvif() = default;

QString PtzOnvif::urlDispositivo(const QString& host, int puerto) {
    const QString h = host.trimmed();
    if (h.contains("://")) return h;
    const QString conPuerto = puerto > 0 && puerto != 80 ? h + ":" + QString::number(puerto) : h;
    return "http://" + conPuerto + "/onvif/device_service";
}

Capacidades PtzOnvif::capacidades() const {
    Capacidades c;
    const bool ptz = !servicios_.ptz.isEmpty() && !perfil_.token.isEmpty();
    c.panTilt = c.zoom = c.home = ptz;
    c.presets = ptz ? 32 : 0;
    c.foco = !servicios_.imaging.isEmpty() && !perfil_.fuenteVideo.isEmpty();
    // Con lo que la cámara dijo que puede (mapearFunciones), cada control de verdad.
    if (ptz && funciones_.value("nodosOk").toBool()) {
        const QVariantMap pt = funciones_.value("panTilt").toMap(), zm = funciones_.value("zoom").toMap(),
                          fo = funciones_.value("foco").toMap();
        c.panTilt = pt.value("continuo").toBool() || pt.value("relativo").toBool() || pt.value("absoluto").toBool();
        c.zoom = !zm.value("bloqueado").toBool() &&
                 (zm.value("continuo").toBool() || zm.value("absoluto").toBool() || zm.value("relativo").toBool());
        c.home = funciones_.value("home").toBool();
        if (funciones_.value("presetsMax").toInt() > 0) c.presets = std::min(funciones_.value("presetsMax").toInt(), 100);
        if (funciones_.value("imagenOk").toBool()) {
            c.foco = fo.value("continuo").toBool();
            c.autoFoco = fo.value("auto").toBool();
        }
    }
    return c;
}

void PtzOnvif::soap(const QString& url, const QString& cuerpo, Respuesta alTerminar, bool conClave) {
    QNetworkRequest pedido{QUrl(url)};
    pedido.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/soap+xml; charset=utf-8"));
    pedido.setTransferTimeout(4000);
    const QByteArray datos = conClave ? onvif::sobre(cuerpo, usuario_, clave_, desfase_) : onvif::sobre(cuerpo);
    QNetworkReply* r = red_->post(pedido, datos);
    const int gen = generacion_;
    connect(r, &QNetworkReply::finished, this, [this, r, gen, alTerminar = std::move(alTerminar)]() {
        r->deleteLater();
        if (gen != generacion_) return; // de una configuración anterior
        const QByteArray xml = r->readAll();
        const QString falla = onvif::leerFalla(xml);
        if (r->error() != QNetworkReply::NoError && xml.isEmpty()) {
            setEstado(false, QStringLiteral("Sin respuesta de la cámara (%1)").arg(r->errorString()));
            alTerminar(false, xml);
            return;
        }
        if (!falla.isEmpty()) {
            const bool clave = falla.contains("auth", Qt::CaseInsensitive) || falla.contains("Sender not Authorized") ||
                               r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 401;
            setEstado(listo(), clave ? QStringLiteral("La cámara no acepta el usuario o la clave")
                                     : QStringLiteral("La cámara contestó: %1").arg(falla));
            alTerminar(false, xml);
            return;
        }
        alTerminar(true, xml);
    });
}

void PtzOnvif::configurar(const QJsonObject& conf) {
    ++generacion_;
    junta_.stop();
    dispositivo_ = urlDispositivo(conf.value("host").toString(), conf.value("puerto").toInt(80));
    usuario_ = conf.value("usuario").toString();
    clave_ = conf.value("clave").toString();
    perfilPedido_ = conf.value("perfil").toString();
    servicios_ = {};
    perfil_ = {};
    presets_.clear();
    desfase_ = 0;
    if (conf.value("host").toString().trimmed().isEmpty()) {
        setEstado(false, QStringLiteral("Falta la dirección de la cámara"));
        return;
    }
    paso("hora");
}

void PtzOnvif::paso(const QString& que) {
    if (que == "hora") {
        setEstado(false, QStringLiteral("Conectando con %1…").arg(dispositivo_));
        soap(dispositivo_, onvif::getSystemDateAndTime(), [this](bool ok, const QByteArray& xml) {
            if (ok) {
                const QDateTime hora = onvif::leerHora(xml);
                if (hora.isValid()) desfase_ = QDateTime::currentDateTimeUtc().secsTo(hora);
            }
            // Sin hora se sigue igual (desfase 0): muchas cámaras andan.
            paso("capacidades");
        }, false);
    } else if (que == "capacidades") {
        soap(dispositivo_, onvif::getCapabilities(), [this](bool ok, const QByteArray& xml) {
            if (!ok) return;
            servicios_ = onvif::leerCapacidades(xml);
            if (servicios_.media.isEmpty()) servicios_.media = dispositivo_;
            paso("perfiles");
        });
    } else if (que == "perfiles") {
        soap(servicios_.media, onvif::getProfiles(), [this](bool ok, const QByteArray& xml) {
            if (!ok) return;
            const auto perfiles = onvif::leerPerfiles(xml);
            for (const auto& p : perfiles)
                if (!perfilPedido_.isEmpty() && (p.token == perfilPedido_ || p.nombre == perfilPedido_)) perfil_ = p;
            if (perfil_.token.isEmpty())
                for (const auto& p : perfiles)
                    if (p.tienePtz) {
                        perfil_ = p;
                        break;
                    }
            if (perfil_.token.isEmpty() && !perfiles.isEmpty()) perfil_ = perfiles.first();
            if (servicios_.ptz.isEmpty() || perfil_.token.isEmpty()) {
                setEstado(true, QStringLiteral("Conectada, pero la cámara no tiene PTZ por ONVIF (queda el digital)"));
                emit estadoCambio();
                return;
            }
            mapearFunciones();
        });
    }
}

void PtzOnvif::leerPresets() {
    soap(servicios_.ptz, onvif::getPresets(perfil_.token), [this](bool ok, const QByteArray& xml) {
        if (ok) presets_ = onvif::leerPresets(xml);
        leerMotor();
        setEstado(true, QStringLiteral("Conectada (ONVIF, perfil %1, %2 presets)")
                            .arg(perfil_.nombre.isEmpty() ? perfil_.token : perfil_.nombre)
                            .arg(presets_.size()));
        emit estadoCambio(); // las capacidades cambiaron
    });
}

void PtzOnvif::mover(double pan, double tilt, double zoom) {
    if (servicios_.ptz.isEmpty()) return;
    if (pan == 0 && tilt == 0 && zoom == 0) {
        parar();
        return;
    }
    pan_ = pan;
    tilt_ = tilt;
    zoom_ = zoom;
    hayMovimiento_ = true;
    if (!junta_.isActive()) {
        vaciar();
        junta_.start();
    }
}

void PtzOnvif::vaciar() {
    if (!hayMovimiento_) {
        junta_.stop();
        return;
    }
    hayMovimiento_ = false;
    soap(servicios_.ptz, onvif::continuousMove(perfil_.token, pan_, tilt_, zoom_), [](bool, const QByteArray&) {});
}

void PtzOnvif::parar() {
    pan_ = tilt_ = zoom_ = 0;
    junta_.stop();
    hayMovimiento_ = false;
    if (servicios_.ptz.isEmpty()) return;
    soap(servicios_.ptz, onvif::stop(perfil_.token), [](bool, const QByteArray&) {});
    // Dónde quedó (para mostrarlo y para saber si el zoom se movió de verdad).
    QTimer::singleShot(500, this, [this]() { leerMotor(); });
}

void PtzOnvif::leerMotor() {
    if (servicios_.ptz.isEmpty() || perfil_.token.isEmpty()) return;
    soap(servicios_.ptz, onvif::getStatus(perfil_.token), [this](bool ok, const QByteArray& xml) {
        if (!ok) return;
        const onvif::EstadoPtz e = onvif::leerEstadoPtz(xml);
        if (!e.ok) return;
        motor_ = {{"pan", e.pan}, {"tilt", e.tilt}, {"zoom", e.hayZoom ? QVariant(e.zoom) : QVariant()},
                  {"moviendo", e.movPanTilt == "MOVING" || e.movZoom == "MOVING"}};
        emit estadoCambio();
    });
}

void PtzOnvif::enfocar(double v) {
    if (servicios_.imaging.isEmpty() || perfil_.fuenteVideo.isEmpty()) return;
    soap(servicios_.imaging,
         v == 0 ? onvif::focoParar(perfil_.fuenteVideo) : onvif::focoMover(perfil_.fuenteVideo, v),
         [](bool, const QByteArray&) {});
}

void PtzOnvif::irAPreset(int n) {
    if (n < 1 || n > presets_.size()) {
        setEstado(listo(), QStringLiteral("La cámara no tiene el preset %1 (tiene %2)").arg(n).arg(presets_.size()));
        return;
    }
    soap(servicios_.ptz, onvif::gotoPreset(perfil_.token, presets_[n - 1].token), [this, n](bool ok, const QByteArray&) {
        if (ok) setEstado(true, QStringLiteral("Yendo al preset %1").arg(n));
    });
}

void PtzOnvif::guardarPreset(int n) {
    if (servicios_.ptz.isEmpty() || n < 1) return;
    // Pisa el n si existe; si no, crea uno nuevo (la cámara elige el token).
    const QString token = n <= presets_.size() ? presets_[n - 1].token : QString();
    soap(servicios_.ptz, onvif::setPreset(perfil_.token, QStringLiteral("Preset %1").arg(n), token),
         [this, n](bool ok, const QByteArray&) {
             if (!ok) return;
             setEstado(true, QStringLiteral("Preset %1 guardado en la cámara").arg(n));
             leerPresets();
         });
}

void PtzOnvif::grabarPosicion(int, const QString& nombre, std::function<void(bool, QString)> listo) {
    if (servicios_.ptz.isEmpty()) {
        listo(false, {});
        return;
    }
    // Sin token: la cámara crea una memoria nueva y contesta su token.
    soap(servicios_.ptz, onvif::setPreset(perfil_.token, nombre), [this, listo, nombre](bool ok, const QByteArray& xml) {
        const QString token = ok ? onvif::leerTokenPreset(xml) : QString();
        listo(ok && !token.isEmpty(), token);
        if (ok) {
            setEstado(true, QStringLiteral("«%1» grabada en la cámara").arg(nombre));
            leerPresets();
        }
    });
}

void PtzOnvif::irAPosicion(int, const QString& token) {
    if (servicios_.ptz.isEmpty() || token.isEmpty()) return;
    soap(servicios_.ptz, onvif::gotoPreset(perfil_.token, token), [](bool, const QByteArray&) {});
}

void PtzOnvif::borrarPosicion(int, const QString& token) {
    if (servicios_.ptz.isEmpty() || token.isEmpty()) return;
    soap(servicios_.ptz, onvif::removePreset(perfil_.token, token), [this](bool ok, const QByteArray&) {
        if (ok) leerPresets();
    });
}

void PtzOnvif::home() {
    if (servicios_.ptz.isEmpty()) return;
    soap(servicios_.ptz, onvif::gotoHomePosition(perfil_.token), [](bool, const QByteArray&) {});
}

void PtzOnvif::mapearFunciones() {
    crudo_.clear();
    struct Pedido {
        QString clave, url, cuerpo;
    };
    auto pedidos = std::make_shared<QList<Pedido>>(QList<Pedido>{
        {"nodos", servicios_.ptz, onvif::getNodes()},
        {"configuraciones", servicios_.ptz, onvif::getConfigurations()},
    });
    if (!perfil_.configPtz.isEmpty()) pedidos->append({"opciones", servicios_.ptz, onvif::getConfigurationOptions(perfil_.configPtz)});
    if (!servicios_.imaging.isEmpty() && !perfil_.fuenteVideo.isEmpty()) {
        pedidos->append({"imagen", servicios_.imaging, onvif::imagingGetOptions(perfil_.fuenteVideo)});
        pedidos->append({"imagen_mover", servicios_.imaging, onvif::imagingGetMoveOptions(perfil_.fuenteVideo)});
    }
    // De a uno (hay cámaras que se atoran con pedidos en paralelo).
    auto siguiente = std::make_shared<std::function<void()>>();
    QPointer<PtzOnvif> yo(this);
    *siguiente = [yo, pedidos, siguiente]() {
        if (!yo) return;
        if (pedidos->isEmpty()) {
            yo->resumirFunciones();
            // Se suelta la recursión fuera de esta llamada.
            QTimer::singleShot(0, yo, [siguiente]() { *siguiente = nullptr; });
            yo->leerPresets();
            return;
        }
        const Pedido p = pedidos->takeFirst();
        yo->soap(p.url, p.cuerpo, [yo, p, siguiente](bool ok, const QByteArray& xml) {
            if (!yo) return;
            if (ok) yo->crudo_[p.clave] = onvif::aplanar(xml);
            else yo->crudo_[p.clave] = QVariantMap{{"error", onvif::leerFalla(xml)}};
            if (*siguiente) (*siguiente)();
        });
    };
    (*siguiente)();
}

void PtzOnvif::resumirFunciones() {
    funciones_ = onvif::resumirFunciones(crudo_);
    emit estadoCambio();
}

} // namespace ba::app::ptz
