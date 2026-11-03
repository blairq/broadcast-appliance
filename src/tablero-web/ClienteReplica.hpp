// SPDX-License-Identifier: GPL-2.0-or-later
//
// ClienteReplica: el lado del navegador del protocolo de réplica (el de la app es
// src/app/control/ReplicaPublicador.hpp, que tiene el protocolo entero). Recibe los lotes,
// arma y actualiza los ObjetoReplica, junta lo que hay que mandar (abrir, llamar, poner,
// video) y lo manda cada kLoteMs en un solo mensaje (el gateway corta a quien manda más de
// 60 mensajes por segundo).
//
// No sabe de WebSocket ni de WebAssembly: `enviar` es una función (en el navegador va a
// puente.js; en las pruebas, derecho al publicador de la app).
//
// El video: cada VideoItem del tablero pide su clave (pedirVideo) y el cliente le pide a la
// app, por clave, el ancho más grande y los cuadros por segundo más altos. Cada cuadro llega
// en JPEG, se descomprime una vez y se avisa (cuadro).
// ============================================================================
#pragma once

#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <functional>

class QJSEngine;
class ObjetoReplica;

class ClienteReplica : public QObject {
    Q_OBJECT
    /// Llegó la raíz (se puede cargar la QML).
    Q_PROPERTY(bool listo READ listo NOTIFY listoChanged)
    /// Lo último que la app dijo que no se puede (p. ej. el perfil no alcanza).
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    /// Puede cambiar cosas (si no, solo mirar).
    Q_PROPERTY(bool escribe READ escribe NOTIFY listoChanged)
   public:
    static constexpr int kLoteMs = 30;
    explicit ClienteReplica(QObject* parent = nullptr);
    ~ClienteReplica() override;

    /// El motor de JavaScript de la vista (para las funciones de cada objeto).
    void setMotor(QJSEngine* motor);
    QJSEngine* motor() const { return motor_; }
    /// Lo que sale hacia la app (un JSON de texto con {type:"replica", …}).
    std::function<void(const QByteArray&)> enviar;

    /// Empezar (o empezar de nuevo, al reconectar).
    void hola();
    /// Un mensaje de la app ({type:"replica", op, …}).
    void recibir(const QJsonObject& m);
    void recibirTexto(const QByteArray& texto);

    ObjetoReplica* raiz() const;
    /// Una propiedad de un objeto replicado que en el navegador es OTRA cosa, local: p. ej.
    /// Studio.dashboard → la disposición propia de este aparato (nunca la de la app).
    void reemplazar(const QString& objeto, const QString& propiedad, QObject* local) { locales_.insert(objeto + QLatin1Char('.') + propiedad, local); }
    QObject* local(const QString& objeto, const QString& propiedad) const { return locales_.value(objeto + QLatin1Char('.') + propiedad); }
    ObjetoReplica* objeto(const QString& id) const { return objs_.value(id); }
    bool listo() const { return raiz() != nullptr && listo_; }
    QString error() const { return error_; }
    bool escribe() const { return escribe_; }
    /// Manda ya lo juntado.
    void vaciar();

    // --- Lo que usan los objetos ---
    void abrir(const QString& id);
    /// `conValor`: la función devuelve algo (un error no se avisa: la vista sigue con lo que tenía).
    int llamar(const QString& id, const QString& metodo, const QVariantList& args, bool conValor = true);
    /// La persona confirmó algo que toca el aire ({op:"confirmar"}): se manda de nuevo, confirmado.
    Q_INVOKABLE void confirmarLlamada(int id);
    /// «nombre([args en JSON])»: la clave de lo que devolvió una función.
    QString claveDe(const QString& metodo, const QVariantList& args) const;
    void poner(const QString& id, const QString& prop, const QVariant& v);
    QVariant aVariant(const QJsonValue& v) const;
    QJsonValue aJson(const QVariant& v) const;

    // --- El video ---
    void pedirVideo(QObject* quien, const QString& clave, int ancho, int fps);
    void soltarVideo(QObject* quien);
    /// Un cuadro ya descomprimido por el navegador (puente.js: createImageBitmap, fuera del
    /// hilo de la página): se avisa como si hubiera llegado en JPEG.
    void entregarCuadro(const QString& clave, const QImage& img, qint64 t, quint64 serial) { emit cuadro(clave, img, t, serial); }
    /// Pedir el enlace del programa por WebRTC (/cast/): vuelve en enlaceWebrtc.
    void pedirWebrtc();

   signals:
    void listoChanged();
    void errorChanged();
    /// Llegó un cuadro de esa clave (ya descomprimido). `t`: ms desde 1970 en que llegó a la app.
    void cuadro(const QString& clave, const QImage& imagen, qint64 t, quint64 serial);
    void enlaceWebrtc(const QString& url, const QString& error);
    /// Lo que la app le avisa al operador (Studio.notify), o por qué no se hizo un botón.
    void aviso(const QString& texto, const QString& nivel);
    /// Toca el aire y hay aire: preguntarle a la persona (confirmarLlamada(id) si dice que sí).
    void confirmar(int id, const QString& texto);
    /// La app pidió volver a empezar o se perdió el hilo: hay que reconectar.
    void reiniciar();

   private:
    void procesar(const QJsonObject& m);
    void encolar(const QJsonObject& m);
    void mandarVideo();
    ObjetoReplica* armar(const QJsonObject& esquema);

    QJSEngine* motor_ = nullptr;
    QHash<QString, ObjetoReplica*> objs_;
    QHash<QString, QObject*> locales_;
    QString raiz_;
    bool listo_ = false, escribe_ = false;
    QString error_;
    QJsonArray cola_;
    QTimer reloj_;
    int siguienteId_ = 1;
    QHash<int, QPair<QString, QString>> pendientes_; ///< id → objeto, clave de la llamada
    QHash<int, QJsonObject> enviados_;               ///< id → lo que se mandó (llamar/poner sin valor)
    struct PedidoVideo {
        QString clave;
        int ancho = 0, fps = 0;
    };
    QHash<QObject*, PedidoVideo> video_;
    QString ultimoVideo_;
    QTimer relojVideo_;
};
