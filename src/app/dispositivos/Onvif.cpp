// src/app/dispositivos/Onvif.cpp — ver Onvif.hpp

#include "dispositivos/Onvif.hpp"
#include "ajustes/Ajustes.hpp"

#include "ptz/PtzProtocolos.hpp"

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <memory>

namespace ba::app::dispositivos {

namespace onvif = ba::app::ptz::onvif;

ClienteOnvif::ClienteOnvif(const QString& host, const QString& usuario, const QString& clave, QObject* parent)
    : QObject(parent), host_(host), usuario_(usuario), clave_(clave), red_(new QNetworkAccessManager(this)) {
    dispositivo_ = host.contains("://") ? host : "http://" + host + "/onvif/device_service";
}

void ClienteOnvif::soap(const QString& u, const QString& cuerpo, bool conClave,
                        std::function<void(bool, QByteArray, QString)> cb) {
    QNetworkRequest pedido{QUrl(u)};
    pedido.setHeader(QNetworkRequest::ContentTypeHeader,
                     QStringLiteral("application/soap+xml; charset=utf-8"));
    pedido.setTransferTimeout(ba::ajustes::entero("ptz.timeoutMs", 4000) * 3 / 2);
    const QByteArray datos = conClave && !usuario_.isEmpty()
                                 ? onvif::sobre(cuerpo, usuario_, clave_, desfase_)
                                 : onvif::sobre(cuerpo);
    QNetworkReply* r = red_->post(pedido, datos);
    connect(r, &QNetworkReply::finished, this, [r, cb]() {
        r->deleteLater();
        const QByteArray xml =
            r->isOpen() ? r->readAll() : QByteArray(); // cortada por tiempo: ya está cerrada
        QString falla = onvif::leerFalla(xml);
        if (falla.isEmpty() && r->error() != QNetworkReply::NoError && xml.isEmpty())
            falla = tr("sin respuesta: %1").arg(r->errorString());
        cb(falla.isEmpty(), xml, falla);
    });
}

QString ClienteOnvif::url(const QString& servicio) const {
    const QString u = servicios_.value(servicio).toString();
    if (!u.isEmpty()) {
        // La dirección que anuncia puede ser de otra red (una 169.254): se usa
        // el host con el que se habla, con su puerto y su ruta.
        QUrl q(u);
        q.setHost(QUrl(dispositivo_).host());
        return q.toString();
    }
    return dispositivo_;
}

QString ClienteOnvif::completar(QString cuerpo) const {
    return cuerpo.replace("{perfil}", perfil_)
        .replace("{config_ptz}", configPtz_)
        .replace("{fuente}", fuente_);
}

void ClienteOnvif::preparar(Listo listo) {
    if (preparado_) return listo({{"servicios", servicios_}, {"perfiles", perfiles_}});
    QPointer<ClienteOnvif> yo(this);
    soap(dispositivo_, onvif::getSystemDateAndTime(), false, [yo, listo](bool, QByteArray xml, QString) {
        if (!yo) return;
        const QDateTime hora = onvif::leerHora(xml);
        if (hora.isValid()) yo->desfase_ = QDateTime::currentDateTimeUtc().secsTo(hora);
        yo->soap(yo->dispositivo_, onvif::getCapabilities(), true,
                 [yo, listo](bool ok, QByteArray xml, QString falla) {
                     if (!yo) return;
                     if (!ok) return listo({{"error", "GetCapabilities: " + falla}});
                     const onvif::Servicios s = onvif::leerCapacidades(xml);
                     yo->servicios_ = {{"device", yo->dispositivo_},
                                       {"media", s.media},
                                       {"ptz", s.ptz},
                                       {"imaging", s.imaging}};
                     yo->soap(yo->url("media"), onvif::getProfiles(), true,
                              [yo, listo](bool ok, QByteArray xml, QString falla) {
                                  if (!yo) return;
                                  if (!ok) return listo({{"error", "GetProfiles: " + falla}});
                                  yo->perfiles_.clear();
                                  for (const auto& p : onvif::leerPerfiles(xml)) {
                                      yo->perfiles_ << QVariantMap{{"token", p.token},
                                                                   {"nombre", p.nombre},
                                                                   {"ptz", p.tienePtz},
                                                                   {"config_ptz", p.configPtz},
                                                                   {"fuente", p.fuenteVideo}};
                                      if (yo->perfil_.isEmpty() || (p.tienePtz && yo->configPtz_.isEmpty())) {
                                          yo->perfil_ = p.token;
                                          yo->configPtz_ = p.configPtz;
                                          yo->fuente_ = p.fuenteVideo;
                                      }
                                  }
                                  yo->preparado_ = true;
                                  listo({{"servicios", yo->servicios_},
                                         {"perfiles", yo->perfiles_},
                                         {"perfil", yo->perfil_}});
                              });
                 });
    });
}

void ClienteOnvif::llamar(const QString& servicio, const QString& cuerpo, Listo listo) {
    QPointer<ClienteOnvif> yo(this);
    preparar([yo, servicio, cuerpo, listo](QVariantMap p) {
        if (!yo) return;
        if (p.contains("error")) return listo(p);
        yo->soap(yo->url(servicio), yo->completar(cuerpo), true,
                 [listo](bool ok, QByteArray xml, QString falla) {
                     listo({{"ok", ok},
                            {"falla", falla},
                            {"respuesta", onvif::aplanar(xml)},
                            {"xml", QString::fromUtf8(xml.left(6000))}});
                 });
    });
}

void ClienteOnvif::funciones(Listo listo) {
    QPointer<ClienteOnvif> yo(this);
    preparar([yo, listo](QVariantMap p) {
        if (!yo) return;
        if (p.contains("error")) return listo(p);
        struct Pedido {
            QString clave, servicio, cuerpo;
        };
        auto pedidos = std::make_shared<QList<Pedido>>(
            QList<Pedido>{{"nodos", "ptz", onvif::getNodes()},
                          {"configuraciones", "ptz", onvif::getConfigurations()},
                          {"opciones", "ptz", onvif::getConfigurationOptions("{config_ptz}")},
                          {"imagen", "imaging", onvif::imagingGetOptions("{fuente}")},
                          {"imagen_mover", "imaging", onvif::imagingGetMoveOptions("{fuente}")},
                          {"estado", "ptz", onvif::getStatus("{perfil}")}});
        auto crudo = std::make_shared<QVariantMap>();
        auto paso = std::make_shared<std::function<void()>>();
        *paso = [yo, pedidos, crudo, paso, listo, p]() {
            if (!yo) return;
            if (pedidos->isEmpty()) {
                QVariantMap r = onvif::resumirFunciones(*crudo);
                r["servicios"] = p.value("servicios");
                r["perfiles"] = p.value("perfiles");
                r["perfil"] = p.value("perfil");
                QMetaObject::invokeMethod(yo, [paso]() { *paso = nullptr; }, Qt::QueuedConnection);
                return listo(r);
            }
            const Pedido x = pedidos->takeFirst();
            if (yo->servicios_.value(x.servicio).toString().isEmpty()) {
                (*crudo)[x.clave] = QVariantMap{{"error", tr("la cámara no tiene el servicio %1").arg(x.servicio)}};
                return (*paso)();
            }
            yo->soap(yo->url(x.servicio), yo->completar(x.cuerpo), true,
                     [crudo, x, paso](bool ok, QByteArray xml, QString falla) {
                         (*crudo)[x.clave] = ok ? onvif::aplanar(xml) : QVariantMap{{"error", falla}};
                         if (*paso) (*paso)();
                     });
        };
        (*paso)();
    });
}

} // namespace ba::app::dispositivos
