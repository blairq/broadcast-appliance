// ============================================================================
// src/app/youtube/YouTubeClient.hpp — Cuenta de YouTube (OAuth + Data API v3)
//
// Capa de SERVICIO: sin vista. La usan la pagina YouTube de la app, el
// servidor MCP y la fuente "Chat en vivo".
//
// Login (OAuth 2.0 para apps de escritorio, RFC 8252 + PKCE):
//   1. Se abre el navegador en la pantalla de Google para elegir la cuenta
//      (prompt=select_account) y dar permiso (scope youtube).
//   2. Google vuelve a http://127.0.0.1:<puerto> (un servidor efimero de esta
//      app) con un codigo; se canjea por tokens con el verificador PKCE.
//   3. Se guarda SOLO el refresh token (Credenciales/youtube-token.cred, cifrado);
//      el access token vive en memoria y se renueva solo (y ante un 401).
//   El cliente OAuth (client_id/secret de la consola de Google Cloud, tipo
//   "App de escritorio") esta en Credenciales/youtube-client.cred (cifrado).
//
// Red: net::HttpsClient (mbedtls + raices de Mozilla del bundle), cada pedido
// en un thread de trabajo; los resultados vuelven al thread de la app.
//
// Errores: `Done(ok, datos, error)`; `error` es texto para una persona (se
// traducen los motivos de Google: cuota, vivo no habilitado, etc.).
// ============================================================================
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

#include <functional>
#include <memory>
#include <vector>

class QTcpServer;

namespace ba::app {

class YouTubeClient : public QObject {
    Q_OBJECT
   public:
    struct Endpoints {
        QString auth = "https://accounts.google.com/o/oauth2/v2/auth";
        QString token = "https://oauth2.googleapis.com/token";
        QString revoke = "https://oauth2.googleapis.com/revoke";
        QString api = "https://www.googleapis.com/youtube/v3";
        QString upload = "https://www.googleapis.com/upload/youtube/v3";
    };
    using Done = std::function<void(bool ok, const QJsonObject& data, const QString& error)>;

    explicit YouTubeClient(QString configDir, QObject* parent = nullptr);
    ~YouTubeClient() override;

    /// Tests: servidores falsos y "navegador" propio.
    void setEndpoints(const Endpoints& e) { ep_ = e; }
    void setBrowserOpener(std::function<void(const QUrl&)> fn) { openBrowser_ = std::move(fn); }

    // --- Cliente OAuth (credencial de la app en Google Cloud) --------------------
    bool hasClient() const { return !clientId_.isEmpty() && !clientSecret_.isEmpty(); }
    QString clientId() const { return clientId_; }
    /// Acepta el JSON que descarga la consola ({"installed": {...}}) o, si
    /// `secret` no esta vacio, `jsonOrId` es el client_id.
    bool saveClient(const QString& jsonOrId, const QString& secret, QString& error);
    void clearClient();

    // --- Sesion -------------------------------------------------------------------
    bool loggedIn() const { return !refreshToken_.isEmpty(); }
    bool loginPending() const { return loginServer_ != nullptr; }
    /// Canal conectado: {id, title, thumbnail, uploadsPlaylist, customUrl}.
    QJsonObject account() const { return account_; }
    /// Abre el navegador para elegir cuenta; `done` al terminar (o a los 5 min).
    void login(Done done);
    void cancelLogin();
    void logout(Done done);

    // --- API ---------------------------------------------------------------------
    /// Pedido autenticado a la Data API (`path` relativo, p. ej. "/videos").
    void api(const QString& method, const QString& path, const QUrlQuery& query, const QJsonObject& body,
             Done done);

    void loadChannel(Done done);
    /// Videos subidos (mas nuevos primero): {items: [{id, title, description,
    /// privacy, publishedAt, thumbnail, views, likes, comments, duration, url}]}.
    void listVideos(int max, Done done);
    /// Cambios: title, description, privacy (public|unlisted|private), tags[].
    void updateVideo(const QString& id, const QJsonObject& changes, Done done);
    void deleteVideo(const QString& id, Done done);
    /// Subida reanudable (streaming, sin cargar el archivo en memoria).
    /// meta: title, description, privacy, tags[].
    void uploadVideo(const QString& path, const QJsonObject& meta, Done done);
    void setThumbnail(const QString& videoId, const QString& imagePath, Done done);

    /// Eventos en vivo del canal: {items: [{id, title, description, status
    /// (lifeCycle), privacy, scheduledStart, actualStart, actualEnd, liveChatId,
    /// boundStreamId, url, studioUrl}]}.
    void listBroadcasts(Done done);
    /// Crea el evento + su stream de ingesta y los vincula.
    /// p: title, description, privacy (private por defecto), scheduledStart
    /// (ISO 8601; por defecto ahora), latency (normal|low|ultraLow),
    /// autoStart/autoStop (true), dvr (true), madeForKids (false).
    /// -> {broadcast, ingestion: {url, key}, url, studioUrl}
    void createBroadcast(const QJsonObject& p, Done done);
    /// Servidor y clave de ingesta del evento (crea y vincula un stream si no
    /// tiene). -> {url, key, streamId, streamStatus, health}
    void ingestion(const QString& broadcastId, Done done);
    /// testing | live | complete
    void transition(const QString& broadcastId, const QString& status, Done done);
    /// Cambios: title, description, privacy, scheduledStart.
    void updateBroadcast(const QString& id, const QJsonObject& changes, Done done);
    void deleteBroadcast(const QString& id, Done done);
    /// Chat: {items: [{id, author, text, publishedAt, owner, moderator,
    /// member}], nextPageToken, pollingIntervalMillis}
    void chatMessages(const QString& liveChatId, const QString& pageToken, Done done);
    void chatSend(const QString& liveChatId, const QString& text, Done done);

    /// Descarga publica sin credenciales (miniaturas de i.ytimg.com).
    void download(const QString& url, std::function<void(bool ok, const QByteArray& body)> done);

    /// Texto legible de un error de Google (tambien lo usan los tests).
    static QString explain(int status, const QByteArray& body);

   signals:
    void stateChanged();
    void uploadProgress(const QString& path, qint64 sent, qint64 total);

   private:
    struct Req; // net::HttpRequest sin exponer el header
    void run(std::shared_ptr<Req> req,
             std::function<void(int status, QByteArray body, QString location, QString error)> cb);
    void ensureToken(std::function<void(QString token, QString error)> cb);
    void exchangeCode(const QString& code, Done done);
    void finishLogin(bool ok, const QString& error);
    void persistToken();
    void loadFiles();
    QString redirectUri() const;

    QString configDir_;
    Endpoints ep_;
    std::function<void(const QUrl&)> openBrowser_;
    QString clientId_;
    QString clientSecret_;
    QString refreshToken_;
    QString accessToken_;
    qint64 accessExpiresMs_ = 0;
    QJsonObject account_;
    std::vector<std::function<void(QString, QString)>> tokenWaiters_;
    bool refreshing_ = false;
    // Login en curso
    QTcpServer* loginServer_ = nullptr;
    QString loginState_;
    QString loginVerifier_;
    Done loginDone_;
};

} // namespace ba::app
