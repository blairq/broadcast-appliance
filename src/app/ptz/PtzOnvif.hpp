// src/app/ptz/PtzOnvif.hpp — ONVIF (Profile S: PTZ + Media + Imaging)
//
// SOAP 1.2 a mano sobre QNetworkAccessManager: no hace falta gSOAP ni nada
// del sistema. Al configurar:
//   1. GetSystemDateAndTime (sin clave): la hora de la cámara. El digest de
//      WS-Security lleva la hora, y si la cámara está corrida la rechaza: se
//      firma con NUESTRA hora + el desfase.
//   2. GetCapabilities: dónde están los servicios PTZ, Media e Imaging.
//   3. GetProfiles: el perfil con PTZ (o el que se eligió en «perfil»).
//   4. GetPresets: los presets que ya tiene la cámara (el 1 es el primero).
// Los movimientos son ContinuousMove (lo del joystick se junta cada 100 ms) y
// Stop. El foco va por Imaging (Move continuo), si la cámara lo ofrece.
// Autenticación: WS-Security UsernameToken (digest) y, si la cámara pide
// HTTP Digest/Basic en su lugar, también.

#pragma once

#include "ptz/PtzBackend.hpp"
#include "ptz/PtzProtocolos.hpp"

#include <QTimer>

#include <functional>

class QNetworkAccessManager;

namespace ba::app::ptz {

class PtzOnvif : public PtzBackend {
    Q_OBJECT
   public:
    explicit PtzOnvif(QObject* parent = nullptr);
    ~PtzOnvif() override;

    QString tipo() const override { return QStringLiteral("onvif"); }
    Capacidades capacidades() const override;
    void configurar(const QJsonObject& conf) override;

    void mover(double pan, double tilt, double zoom) override;
    void parar() override;
    void enfocar(double velocidad) override;
    void irAPreset(int numero) override;
    void guardarPreset(int numero) override;
    void home() override;

    bool usaTokens() const override { return true; }
    int memorias() const override { return capacidades().panTilt ? 1 : 0; }
    void grabarPosicion(int n, const QString& nombre, std::function<void(bool, QString)> listo) override;
    void irAPosicion(int n, const QString& token) override;
    void irAPosicionCon(int n, const QString& token, double velocidad) override;
    void borrarPosicion(int n, const QString& token) override;
    QVariantMap motor() const override { return motor_; }
    QVariantMap funciones() const override { return funciones_; }
    bool apuntar(double u, double v, double ancho, double alto) override;

    /// El device_service de un host ("192.168.1.20" o una URL completa).
    static QString urlDispositivo(const QString& host, int puerto);

   private:
    using Respuesta = std::function<void(bool ok, const QByteArray& xml)>;
    void soap(const QString& url, const QString& cuerpo, Respuesta alTerminar, bool conClave = true);
    void paso(const QString& que);
    void leerPresets();
    void leerMotor();
    /// Le pregunta a la cámara qué ofrece (nodos, configuración PTZ, imagen) y
    /// arma `funciones_`; después sigue con los presets.
    void mapearFunciones();
    void resumirFunciones();
    QVariantMap crudo_;     ///< las respuestas aplanadas
    QVariantMap funciones_; ///< el resumen: qué eje se mueve cómo
    QString fabricante_;    ///< de GetDeviceInformation (para lo propio de cada marca)
    // Los movimientos: UNO en vuelo por vez; mientras tanto se guarda solo el
    // último pedido (los de en medio ya no importan). Encolados, la cámara los
    // ejecutaba en fila y el «parar» llegaba tarde: respondía tarde y se pasaba.
    bool enVuelo_ = false;
    bool hayPendiente_ = false;
    double pPan_ = 0, pTilt_ = 0, pZoom_ = 0; ///< el pendiente
    double ePan_ = 9, eTilt_ = 9, eZoom_ = 9; ///< el último mandado (9: nada)
    void enviarMovimiento(double pan, double tilt, double zoom);
    void mandar(double pan, double tilt, double zoom);
    bool esAxis() const { return fabricante_.contains("AXIS", Qt::CaseInsensitive); }
    void vapix(const QString& consulta, std::function<void(bool)> alTerminar);
    void vaciar();

    QNetworkAccessManager* red_ = nullptr;
    QString dispositivo_, usuario_, clave_, perfilPedido_;
    qint64 desfase_ = 0;
    onvif::Servicios servicios_;
    onvif::Perfil perfil_;
    QList<onvif::Preset> presets_;
    QVariantMap motor_;
    int generacion_ = 0; ///< una reconfiguración invalida respuestas viejas

    bool hayMovimiento_ = false;
    double pan_ = 0, tilt_ = 0, zoom_ = 0;
    QTimer junta_;
};

} // namespace ba::app::ptz
