// ============================================================================
// src/app/twitch/TwitchClient.hpp — Cuenta de Twitch (OAuth + Helix + chat)
//
// Capa de SERVICIO (sin vista): la usan la pagina Twitch, el servidor MCP y
// la fuente "Chat en vivo".
//
// Login: "Device Code Grant" de Twitch (el mismo de las consolas y TVs): se
// pide un codigo, se abre el navegador en twitch.tv/activate con el codigo ya
// cargado, la persona elige la cuenta y acepta, y la app recibe los tokens.
// No hace falta registrar redirecciones: la app de Twitch se crea como
// "Publica" (sin secreto). Si es "Confidencial", se guarda tambien el secreto.
//
// Guardado (DENTRO del bundle portable, solo-dueño):
//   Credenciales/twitch-client.cred  {client_id, client_secret?}
//   Credenciales/twitch-token.cred   {access_token, refresh_token, expires_at, user}
//   (cifrados: security/CredentialStore; una actualizacion no los toca)
//
// Chat: IRC de Twitch sobre TLS (irc.chat.twitch.tv:6697) en un thread
// propio: lee mensajes (con nombre, color e insignias) y manda los propios.
//
// Transmitir: la clave del canal (streams/key) va SOLO a memoria y la app
// transmite a rtmp://live.twitch.tv/app.
// ============================================================================
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrlQuery>

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace ba::app {

class TwitchClient : public QObject {
    Q_OBJECT
   public:
    struct Endpoints {
        QString id = "https://id.twitch.tv/oauth2";
        QString api = "https://api.twitch.tv/helix";
        QString ircHost = "irc.chat.twitch.tv";
        QString ircPort = "6697";
        bool ircTls = true;
        QString ingest = "rtmp://live.twitch.tv/app";
    };
    using Done = std::function<void(bool ok, const QJsonObject& data, const QString& error)>;

    explicit TwitchClient(QString configDir, QObject* parent = nullptr);
    ~TwitchClient() override;

    void setEndpoints(const Endpoints& e) { ep_ = e; }
    void setBrowserOpener(std::function<void(const QString& url)> fn) { openBrowser_ = std::move(fn); }
    const Endpoints& endpoints() const { return ep_; }

    // --- App de Twitch (dev.twitch.tv/console) -------------------------------------
    bool hasClient() const { return !clientId_.isEmpty(); }
    QString clientId() const { return clientId_; }
    /// `secret` vacio = app Publica (lo recomendado).
    bool saveClient(const QString& id, const QString& secret, QString& error);
    void clearClient();

    // --- Sesion ---------------------------------------------------------------------
    bool loggedIn() const { return !refreshToken_.isEmpty(); }
    bool loginPending() const { return !deviceCode_.isEmpty(); }
    QString userCode() const { return userCode_; }
    QString verificationUri() const { return verificationUri_; }
    /// {id, login, display_name, profile_image_url}
    QJsonObject user() const { return user_; }
    void login(Done done);
    void cancelLogin();
    void logout(Done done);

    // --- Helix ------------------------------------------------------------------------
    void api(const QString& method, const QString& path, const QUrlQuery& query, const QJsonObject& body,
             Done done);
    /// {title, game_id, game_name, tags[], broadcaster_language}
    void channel(Done done);
    /// Cambios: title, game (nombre: se busca la categoria), game_id, tags[], language.
    void updateChannel(const QJsonObject& changes, Done done);
    /// {live, viewers, started_at, title, game_name, thumbnail}
    void streamStatus(Done done);
    /// {url, key} de ingesta (la clave, solo para la app).
    void ingest(Done done);
    /// VODs: {items: [{id, title, url, thumbnail, views, created_at, duration, type}]}
    void videos(int max, Done done);
    void deleteVideos(const QStringList& ids, Done done);
    /// Categorias: {items: [{id, name, box_art}]}
    void searchCategories(const QString& query, Done done);

    // --- Chat (IRC) -------------------------------------------------------------------
    void startChat();
    void stopChat();
    bool chatRunning() const { return chatThread_.joinable(); }
    void sendChat(const QString& text);
    /// Mensajes recibidos (del mas viejo al mas nuevo): {id, author, color,
    /// text, atMs, broadcaster, moderator, subscriber}
    const QJsonArray& chatMessages() const { return chat_; }

    static QString explain(int status, const QByteArray& body);

   signals:
    void stateChanged();
    void chatChanged();
    void chatStatus(const QString& text);

   private:
    void ensureToken(std::function<void(QString token, QString err)> cb);
    void pollDevice(Done done);
    void persistToken();
    void loadFiles();
    void loadUser(Done done);
    void chatLoop(QString token, QString login);
    void onChatLine(const QJsonObject& msg);

    QString configDir_;
    Endpoints ep_;
    std::function<void(const QString&)> openBrowser_;
    QString clientId_;
    QString clientSecret_;
    QString accessToken_;
    QString refreshToken_;
    qint64 expiresAtMs_ = 0;
    QJsonObject user_;
    std::vector<std::function<void(QString, QString)>> tokenWaiters_;
    bool refreshing_ = false;
    // Login por codigo
    QString deviceCode_;
    QString userCode_;
    QString verificationUri_;
    int pollIntervalMs_ = 5000;
    qint64 deviceExpiresMs_ = 0;
    Done loginDone_;
    // Chat
    std::thread chatThread_;
    std::atomic<bool> chatStop_{false};
    std::mutex outLock_;
    std::deque<std::string> outgoing_;
    QJsonArray chat_;
};

} // namespace ba::app
