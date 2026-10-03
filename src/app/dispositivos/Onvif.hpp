// ============================================================================
// src/app/dispositivos/Onvif.hpp — hablar ONVIF con cualquier cámara de la red
//
// Un cliente chico y genérico: prepara la conexión (la hora de la cámara para
// WS-Security, sus servicios, sus perfiles) y llama CUALQUIER operación de
// cualquier servicio (device, media, ptz, imaging) con el usuario y la clave
// guardados. Arriba de eso:
//   - funciones(): lo que la cámara dice que puede (nodos PTZ, configuración
//     y opciones PTZ, imagen y foco), resumido como en el PTZ;
//   - llamar(): una operación cruda, para explorar un equipo raro.
// Lo usan las acciones de los equipos de la red (dispositivos/Registro.cpp):
// el asistente y la interfaz llegan a lo mismo por ahí.
// ============================================================================
#pragma once

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

#include <functional>

class QNetworkAccessManager;

namespace ba::app::dispositivos {

class ClienteOnvif : public QObject {
    Q_OBJECT
   public:
    using Listo = std::function<void(QVariantMap resultado)>;

    ClienteOnvif(const QString& host, const QString& usuario, const QString& clave, QObject* parent = nullptr);

    /// Hora, servicios y perfiles. `listo` con {servicios, perfiles} o {error}.
    void preparar(Listo listo);
    /// Lo que la cámara ofrece (onvif::resumirFunciones) del perfil con PTZ (o el primero).
    void funciones(Listo listo);
    /// Una operación: `servicio` device|media|ptz|imaging; `cuerpo` el XML del Body
    /// (p. ej. <tptz:GetStatus><tptz:ProfileToken>p</tptz:ProfileToken></tptz:GetStatus>;
    /// `{perfil}`, `{config_ptz}` y `{fuente}` se reemplazan). {ok, respuesta (aplanada), falla, xml}.
    void llamar(const QString& servicio, const QString& cuerpo, Listo listo);

   private:
    void soap(const QString& url, const QString& cuerpo, bool conClave, std::function<void(bool, QByteArray, QString)> cb);
    QString url(const QString& servicio) const;
    QString completar(QString cuerpo) const;

    QString host_, usuario_, clave_, dispositivo_;
    qint64 desfase_ = 0;
    bool preparado_ = false;
    QVariantMap servicios_;
    QString perfil_, configPtz_, fuente_;
    QVariantList perfiles_;
    QNetworkAccessManager* red_;
};

} // namespace ba::app::dispositivos
