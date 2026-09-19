// ============================================================================
// src/app/facebook/FacebookClient.hpp — Facebook (Paginas): vivos, videos, chat
//
// Capa de SERVICIO (sin vista): la usan la pagina Facebook, el MCP y la
// fuente "Chat en vivo".
//
// Alcance: la API de Facebook solo deja transmitir y administrar videos en
// PAGINAS (no en el perfil personal). Se conecta la cuenta, se listan sus
// Paginas y se elige una.
//
// Login (OAuth de Facebook con una app propia, en modo desarrollo alcanza):
//   1. Se abre el navegador en el dialogo de Facebook (permisos de Paginas y
//      video) que vuelve a http://localhost:<puerto>/ (puerto fijo, 8791 por
//      defecto: hay que cargarlo en "URI de redireccionamiento de OAuth
//      validos" de la app).
//   2. Codigo -> token corto -> token largo (~60 dias) -> tokens de cada
//      Pagina (derivados del largo: no vencen mientras no cambie la clave).
//   Guardado DENTRO del bundle portable, solo-dueño:
//     Credenciales/facebook-client.cred  {app_id, app_secret, port}
//     Credenciales/facebook-token.cred   {user_token, expires_at, user, pages[], page_id}
//     (cifrados: security/CredentialStore; una actualizacion no los toca)
//
// Transmitir: POST /{pagina}/live_videos -> secure_stream_url (RTMPS); el
// servidor y la clave van SOLO a memoria. Chat = comentarios del vivo.
// ============================================================================
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrlQuery>

#include <functional>

class QTcpServer;

namespace ba::app {

class FacebookClient : public QObject {
    Q_OBJECT
   public:
    struct Endpoints {
        QString dialog = "https://www.facebook.com/v21.0/dialog/oauth";
        QString graph = "https://graph.facebook.com/v21.0";
        QString video = "https://graph-video.facebook.com/v21.0";
    };
    using Done = std::function<void(bool ok, const QJsonObject& data, const QString& error)>;

    explicit FacebookClient(QString configDir, QObject* parent = nullptr);
    ~FacebookClient() override;

    void setEndpoints(const Endpoints& e) { ep_ = e; }
    void setBrowserOpener(std::function<void(const QString&)> fn) { openBrowser_ = std::move(fn); }

    // --- App de Meta --------------------------------------------------------------------
    bool hasClient() const { return !appId_.isEmpty() && !appSecret_.isEmpty(); }
    QString appId() const { return appId_; }
    int port() const { return port_; }
    QString redirectUri() const { return QString("http://localhost:%1/").arg(port_); }
    bool saveClient(const QString& appId, const QString& appSecret, int port, QString& error);
    void clearClient();

    // --- Sesion ------------------------------------------------------------------------
    bool loggedIn() const { return !userToken_.isEmpty(); }
    bool loginPending() const { return loginServer_ != nullptr; }
    QJsonObject user() const { return user_; }
    /// [{id, name, category, picture}] (sin tokens)
    QJsonArray pages() const;
    QString pageId() const { return pageId_; }
    QJsonObject page() const;
    bool selectPage(const QString& id);
    void login(Done done);
    void cancelLogin();
    void logout(Done done);

    // --- API (con el token de la Pagina elegida) ------------------------------------------
    void graph(const QString& method, const QString& path, const QUrlQuery& params, Done done);
    /// Vivos de la Pagina: {items: [{id, title, description, status, url, views, created}]}
    void liveVideos(Done done);
    /// p: title, description, status (LIVE_NOW | UNPUBLISHED: prueba, no publicado)
    /// -> {id, url, ingestion: {url, key}}
    void createLive(const QJsonObject& p, Done done);
    void liveStatus(const QString& id, Done done);
    /// Publica un vivo que estaba en prueba (UNPUBLISHED -> LIVE_NOW).
    void publishLive(const QString& id, Done done);
    void endLive(const QString& id, Done done);
    void deleteObject(const QString& id, Done done);
    /// Videos de la Pagina: {items: [{id, title, description, url, picture, created, length}]}
    void videos(int max, Done done);
    void updateVideo(const QString& id, const QJsonObject& changes, Done done);
    /// Subida reanudable por partes (start/transfer/finish). meta: title, description.
    void uploadVideo(const QString& path, const QJsonObject& meta, Done done);
    /// Comentarios de un vivo: {items: [{id, author, text, atMs}]}
    void comments(const QString& liveId, Done done);
    void comment(const QString& liveId, const QString& text, Done done);

    /// Separa la URL RTMPS de Facebook en servidor + clave (libobs las pide aparte).
    static bool splitIngest(const QString& secureUrl, QString& server, QString& key);
    static QString explain(int status, const QByteArray& body);

   signals:
    void stateChanged();
    void uploadProgress(const QString& path, qint64 sent, qint64 total);

   private:
    void finishLogin(bool ok, const QString& error);
    void exchange(const QString& code, Done done);
    void loadPages(Done done);
    void persist();
    void loadFiles();
    QString pageToken() const;
    void uploadChunk(const QString& path, const QString& session, qint64 start, qint64 end, qint64 total,
                     const QJsonObject& meta, const QString& videoId, Done done);

    QString configDir_;
    Endpoints ep_;
    std::function<void(const QString&)> openBrowser_;
    QString appId_;
    QString appSecret_;
    int port_ = 8791;
    QString userToken_;
    qint64 expiresAtMs_ = 0;
    QJsonObject user_;
    QJsonArray pages_; // con access_token (solo en memoria y en el archivo privado)
    QString pageId_;
    QTcpServer* loginServer_ = nullptr;
    QString loginState_;
    Done loginDone_;
};

} // namespace ba::app
