// ============================================================================
// src/app/ui/TwitchViewModel.hpp — Pagina Twitch (ViewModel)
//
// Adapta TwitchClient a la vista y al MCP: app de Twitch (Client ID), login
// por codigo (se muestra el codigo y se abre twitch.tv/activate), canal
// (titulo, categoria), transmitir (clave solo en memoria), estado del vivo,
// VODs y chat (IRC) para la pagina y la fuente "Chat en vivo".
// ============================================================================
#pragma once

#include <QJsonArray>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

#include <memory>

#include "twitch/TwitchClient.hpp"

namespace ba::app {

class AppRuntime;
class JsonListModel;

class TwitchViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool configured READ configured NOTIFY stateChanged)
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY stateChanged)
    Q_PROPERTY(bool loginPending READ loginPending NOTIFY stateChanged)
    Q_PROPERTY(QString userCode READ userCode NOTIFY stateChanged)
    Q_PROPERTY(QString verificationUri READ verificationUri NOTIFY stateChanged)
    Q_PROPERTY(QString clientId READ clientId NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap user READ user NOTIFY stateChanged)
    Q_PROPERTY(QString avatar READ avatar NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap channel READ channel NOTIFY channelChanged)
    Q_PROPERTY(QVariantMap stream READ stream NOTIFY channelChanged)
    Q_PROPERTY(bool streamingHere READ streamingHere NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(QObject* videos READ videos CONSTANT)
    Q_PROPERTY(QObject* chat READ chat CONSTANT)
    Q_PROPERTY(QObject* categories READ categories CONSTANT)
    Q_PROPERTY(bool chatOn READ chatOn NOTIFY chatChanged)
   public:
    TwitchViewModel(AppRuntime& rt, QObject* parent = nullptr);
    ~TwitchViewModel() override;

    bool configured() const { return client_->hasClient(); }
    bool loggedIn() const { return client_->loggedIn(); }
    bool loginPending() const { return client_->loginPending(); }
    QString userCode() const { return client_->userCode(); }
    QString verificationUri() const { return client_->verificationUri(); }
    QString clientId() const { return client_->clientId(); }
    QVariantMap user() const { return client_->user().toVariantMap(); }
    QString avatar() const { return avatar_; }
    QVariantMap channel() const { return channel_.toVariantMap(); }
    QVariantMap stream() const { return stream_.toVariantMap(); }
    bool streamingHere() const { return streamingHere_; }
    bool busy() const { return busy_ > 0; }
    QString lastError() const { return lastError_; }
    QObject* videos() const;
    QObject* chat() const;
    QObject* categories() const;
    bool chatOn() const { return client_->chatRunning(); }

    Q_INVOKABLE QString saveClient(const QString& id, const QString& secret = {});
    Q_INVOKABLE void clearClient();
    Q_INVOKABLE void login();
    Q_INVOKABLE void cancelLogin();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void updateChannel(const QString& title, const QString& category);
    Q_INVOKABLE void searchCategories(const QString& text);
    Q_INVOKABLE void goLive();
    Q_INVOKABLE void stopLive();
    Q_INVOKABLE void deleteVideo(const QString& id);
    Q_INVOKABLE void setChatOn(bool on);
    Q_INVOKABLE void sendChat(const QString& text);
    Q_INVOKABLE void openUrl(const QString& url);

    // Para el MCP
    TwitchClient& client() { return *client_; }
    void goLive(TwitchClient::Done done);
    void stopLive(TwitchClient::Done done);
    const QJsonArray& chatMessages() const { return client_->chatMessages(); }

   signals:
    void stateChanged();
    void channelChanged();
    void busyChanged();
    void errorChanged();
    void chatChanged();
    void notify(const QString& message, const QString& level);

   private:
    void setError(const QString& e);
    void track(bool start);
    void pollStream();

    AppRuntime& rt_;
    std::unique_ptr<TwitchClient> client_;
    std::unique_ptr<JsonListModel> videos_;
    std::unique_ptr<JsonListModel> chat_;
    std::unique_ptr<JsonListModel> categories_;
    QJsonObject channel_;
    QJsonObject stream_;
    QString avatar_;
    bool streamingHere_ = false;
    int busy_ = 0;
    QString lastError_;
    QTimer streamTimer_;
};

} // namespace ba::app
