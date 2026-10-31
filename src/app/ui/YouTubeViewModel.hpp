// ============================================================================
// src/app/ui/YouTubeViewModel.hpp — Pagina YouTube (ViewModel)
//
// Adapta YouTubeClient a la vista y al servidor MCP:
//   - Configuracion: cliente OAuth (pegar el JSON de Google Cloud), login con
//     el navegador (elegir cuenta), desconectar.
//   - Videos subidos (miniaturas descargadas por el HTTPS propio y servidas a
//     QML como data: URL), editar titulo/descripcion/privacidad, borrar.
//   - Eventos en vivo: crear, transmitir desde la app (servidor y clave de
//     ingesta SOLO en memoria), terminar, borrar.
//   - Grabaciones locales: subirlas a YouTube con progreso.
//   - Chat del vivo: lectura periodica del evento elegido (lo usa la fuente
//     "Chat en vivo" y la pagina) y envio.
// ============================================================================
#pragma once

#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

#include <functional>
#include <memory>

#include "youtube/YouTubeClient.hpp"

namespace ba::app {

class AppRuntime;
class VentanillaMotor;
class JsonListModel;

class YouTubeViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool configured READ configured NOTIFY stateChanged)
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY stateChanged)
    Q_PROPERTY(bool loginPending READ loginPending NOTIFY stateChanged)
    Q_PROPERTY(QString clientId READ clientId NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap account READ account NOTIFY stateChanged)
    /// Avatar del canal como data: URL (vacio mientras se descarga).
    Q_PROPERTY(QString accountThumb READ accountThumb NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(QObject* videos READ videos CONSTANT)
    Q_PROPERTY(QObject* broadcasts READ broadcasts CONSTANT)
    Q_PROPERTY(QObject* recordings READ recordings CONSTANT)
    Q_PROPERTY(QObject* chat READ chat CONSTANT)
    /// Subida en curso: {path, title, sent, total} (vacio = ninguna).
    Q_PROPERTY(QVariantMap upload READ upload NOTIFY uploadChanged)
    /// Evento del que se lee el chat (y al que se esta transmitiendo, si hay).
    Q_PROPERTY(QString chatBroadcastId READ chatBroadcastId NOTIFY chatChanged)
    Q_PROPERTY(QString liveBroadcastId READ liveBroadcastId NOTIFY stateChanged)
   public:
    /// `ventanilla`: transmitir y cortar se le piden al motor; si transmite, de su foto (F3-03).
    YouTubeViewModel(AppRuntime& rt, VentanillaMotor& ventanilla, QObject* parent = nullptr);
    ~YouTubeViewModel() override;

    bool configured() const { return client_->hasClient(); }
    bool loggedIn() const { return client_->loggedIn(); }
    bool loginPending() const { return client_->loginPending(); }
    QString clientId() const { return client_->clientId(); }
    QVariantMap account() const { return client_->account().toVariantMap(); }
    QString accountThumb() const { return thumbs_.value(client_->account().value("thumbnail").toString()); }
    bool busy() const { return busy_ > 0; }
    QString lastError() const { return lastError_; }
    QObject* videos() const;
    QObject* broadcasts() const;
    QObject* recordings() const;
    QObject* chat() const;
    QVariantMap upload() const { return upload_; }
    QString chatBroadcastId() const { return chatBroadcastId_; }
    QString liveBroadcastId() const { return liveBroadcastId_; }

    // --- Configuracion y cuenta -------------------------------------------------------
    /// Pega el JSON descargado (o el ID si `secret` no esta vacio). "" = ok.
    Q_INVOKABLE QString saveClient(const QString& jsonOrId, const QString& secret = {});
    Q_INVOKABLE void clearClient();
    Q_INVOKABLE void login();
    Q_INVOKABLE void cancelLogin();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshRecordings();
    Q_INVOKABLE void openUrl(const QString& url);
    Q_INVOKABLE void copy(const QString& text);

    // --- Videos ----------------------------------------------------------------------
    Q_INVOKABLE void updateVideo(const QString& id, const QString& title, const QString& description,
                                 const QString& privacy);
    Q_INVOKABLE void deleteVideo(const QString& id);
    Q_INVOKABLE void uploadRecording(const QString& path, const QString& title, const QString& description,
                                     const QString& privacy);

    // --- En vivo -------------------------------------------------------------------------
    Q_INVOKABLE void createBroadcast(const QString& title, const QString& description, const QString& privacy,
                                     const QString& latency);
    /// Transmite desde la app a ese evento (ingesta solo en memoria).
    Q_INVOKABLE void goLive(const QString& broadcastId);
    /// Termina el evento en YouTube y corta la transmision de la app.
    Q_INVOKABLE void endBroadcast(const QString& broadcastId);
    Q_INVOKABLE void deleteBroadcast(const QString& broadcastId);
    /// De que evento se lee el chat ("" = ninguno).
    Q_INVOKABLE void setChatBroadcast(const QString& broadcastId);
    Q_INVOKABLE void sendChat(const QString& text);

    // --- Para el MCP (mismo comportamiento, con resultado) ------------------------------
    YouTubeClient& client() { return *client_; }
    void goLive(const QString& broadcastId, YouTubeClient::Done done);
    void endBroadcast(const QString& broadcastId, YouTubeClient::Done done);
    void uploadRecording(const QString& path, const QJsonObject& meta, YouTubeClient::Done done);
    /// Mensajes de chat en memoria (YouTube), del mas viejo al mas nuevo.
    const QJsonArray& chatMessages() const { return chatItems_; }
    QString recordingsDir() const;

   signals:
    void stateChanged();
    void busyChanged();
    void errorChanged();
    void uploadChanged();
    void chatChanged();
    /// Aviso para el operador (mismo formato que el resto de la app).
    void notify(const QString& message, const QString& level);
    /// Arrancó un vivo desde acá: {plataforma, url, id, estudio?} (sin la clave).
    /// Va al bus como «salida/vivo» y queda en el histórico del evento.
    void vivo(const QVariantMap& datos);

   private:
    /// El motor contestó si salió al aire (`error` vacío = sí): sigue goLive.
    void alSalir(const QString& broadcastId, const QString& url, const std::string& error, const YouTubeClient::Done& done);
    void setError(const QString& e);
    void track(bool start);
    void fetchThumbs(const QJsonArray& items, JsonListModel* model);
    void pollChat();

    AppRuntime& rt_;
    VentanillaMotor& ventanilla_;
    std::unique_ptr<YouTubeClient> client_;
    std::unique_ptr<JsonListModel> videos_;
    std::unique_ptr<JsonListModel> broadcasts_;
    std::unique_ptr<JsonListModel> recordings_;
    std::unique_ptr<JsonListModel> chat_;
    QHash<QString, QString> thumbs_; // url -> data: URL
    int busy_ = 0;
    QString lastError_;
    QVariantMap upload_;
    // Chat
    QString chatBroadcastId_;
    QString liveChatId_;
    QString chatPageToken_;
    QJsonArray chatItems_;
    QTimer chatTimer_;
    bool chatInFlight_ = false;
    QString liveBroadcastId_;
};

} // namespace ba::app
