// ============================================================================
// src/app/ui/ConferenceClient.hpp — Cliente de la API de control de la sala
//
// Capa de SERVICIO (modelo): habla con la API del operador que expone B en
// 127.0.0.1 (ver src/gateway/AdminApi.cpp) y no sabe nada de la vista.
//   - Descubre puerto y token en Runtime/gateway-admin.json (dentro del bundle) (lo escribe B
//     con permisos solo-usuario). Si B se reinicia el token cambia: ante un
//     401 o un corte se vuelve a leer el archivo.
//   - Estado: flujo SSE /api/v1/events. "room" trae el snapshot completo;
//     "chat", un mensaje. Reconexion automatica con espera creciente y un
//     vigia que corta si el servidor deja de latir (B manda ": ping" cada 15 s).
//   - Acciones: POST/DELETE con Bearer.
//
// La consola web consume EXACTAMENTE la misma API: ninguna de las dos vistas
// tiene logica de sala propia.
// ============================================================================
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
#include <QUrl>

#include <functional>

class QNetworkReply;

namespace ba::app {

class ConferenceClient : public QObject {
    Q_OBJECT
   public:
    using Done = std::function<void(bool ok, const QJsonObject& reply)>;

    explicit ConferenceClient(QObject* parent = nullptr);
    ~ConferenceClient() override;

    bool online() const { return online_; }
    const QJsonObject& snapshot() const { return snap_; }

    /// POST a la API (`path` absoluto, p. ej. "/api/v1/room").
    void post(const QString& path, const QJsonObject& body = {}, Done done = {}, int timeoutMs = 5000);
    void del(const QString& path, Done done = {});
    void get(const QString& path, Done done);
    /// GET de bytes crudos (fotos de las tarjetas). No avisa `failed`: una
    /// foto que falta no es un error para el operador.
    void getRaw(const QString& path, std::function<void(bool ok, const QByteArray& body)> done);

   signals:
    void onlineChanged(bool online);
    void snapshotChanged();
    void chatReceived(const QJsonObject& msg);
    /// La botonera (superficie de control): solicitud, entrada o conexión de una tablet.
    void botoneraEvento(const QJsonObject& ev);
    /// Lo que mandó el corrector de subtítulos desde /correccion/ (T44): {id, texto?, descartar}.
    void correccionEvento(const QJsonObject& ev);
    void sesionEvento(const QJsonObject& ev);
    /// Una accion fallo; `error` es el codigo de la API ("full", "not_found"...).
    void failed(const QString& error);

   private:
    bool loadAdminFile();
    void connectEvents();
    void onEventsData();
    void onEventsClosed();
    void dispatch(const QByteArray& event, const QByteArray& data);
    void setOnline(bool on);
    void scheduleReconnect();
    void send(const QByteArray& verb, const QString& path, const QByteArray& body, Done done, int timeoutMs = 5000);

    QNetworkAccessManager nam_;
    QNetworkReply* sse_ = nullptr;
    QByteArray buf_;
    QString token_;
    int port_ = 0;
    bool online_ = false;
    QJsonObject snap_;
    QTimer retry_;
    QTimer watchdog_;
    int backoffMs_ = 1000;
};

} // namespace ba::app
