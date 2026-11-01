// ============================================================================
// src/app/ui/TwitchViewModel.hpp — Pagina Twitch (ViewModel)
//
// Adapta TwitchClient a la vista y al MCP (lo común, en PlataformaViewModel): app de
// Twitch (Client ID), login
// por codigo (se muestra el codigo y se abre twitch.tv/activate), canal
// (titulo, categoria), transmitir (clave solo en memoria), estado del vivo,
// VODs y chat (IRC) para la pagina y la fuente "Chat en vivo".
// ============================================================================
#pragma once

#include <QJsonArray>
#include <QTimer>
#include <QVariantMap>

#include <memory>

#include "twitch/TwitchClient.hpp"
#include "ui/PlataformaViewModel.hpp"

namespace ba::app {

class AppRuntime;
class VentanillaMotor;
class JsonListModel;

class TwitchViewModel : public PlataformaViewModel {
    Q_OBJECT
    // Cada grupo avisa solo si cambió lo suyo (el estado del vivo se mira cada 30 s y casi
    // nunca cambia; la cuenta, al conectar).
    /// La app de Twitch (Client ID).
    Q_PROPERTY(bool configured READ configured NOTIFY appChanged)
    Q_PROPERTY(QString clientId READ clientId NOTIFY appChanged)
    /// La cuenta: conectada, el código para conectarla, quién es.
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY cuentaChanged)
    Q_PROPERTY(bool loginPending READ loginPending NOTIFY cuentaChanged)
    Q_PROPERTY(QString userCode READ userCode NOTIFY cuentaChanged)
    Q_PROPERTY(QString verificationUri READ verificationUri NOTIFY cuentaChanged)
    Q_PROPERTY(QVariantMap user READ user NOTIFY cuentaChanged)
    Q_PROPERTY(QString avatar READ avatar NOTIFY avatarChanged)
    Q_PROPERTY(QVariantMap channel READ channel NOTIFY channelChanged)
    Q_PROPERTY(QVariantMap stream READ stream NOTIFY streamChanged)
    Q_PROPERTY(bool streamingHere READ streamingHere NOTIFY streamingHereChanged)
    Q_PROPERTY(QObject* videos READ videos CONSTANT)
    Q_PROPERTY(QObject* chat READ chat CONSTANT)
    Q_PROPERTY(QObject* categories READ categories CONSTANT)
    Q_PROPERTY(bool chatOn READ chatOn NOTIFY chatChanged)
   public:
    /// `ventanilla`: transmitir y cortar se le piden al motor; si transmite, de su foto (F3-03).
    TwitchViewModel(AppRuntime& rt, VentanillaMotor& ventanilla, QObject* parent = nullptr);
    ~TwitchViewModel() override;

    // --- La plataforma (PlataformaViewModel) -----------------------------------------------
    QString plataforma() const override { return QStringLiteral("twitch"); }
    QString nombre() const override { return QStringLiteral("Twitch"); }
    bool configured() const override { return client_->hasClient(); }
    bool loggedIn() const override { return client_->loggedIn(); }
    bool loginPending() const override { return client_->loginPending(); }
    QString cuentaNombre() const override { return client_->user().value("display_name").toString(); }
    QJsonObject estadoCuenta() const override;
    QJsonObject estado() const override;
    QStringList permisos() const override { return TwitchClient::permisos(); }
    QString guardarCredencial(const QVariantMap& campos) override;
    using PlataformaViewModel::login;
    void login(Done listo) override;
    bool loginConCodigo() const override { return true; }
    QString codigo() const override { return userCode(); }
    QString codigoUri() const override { return verificationUri(); }
    void listarVideos(int max, Done done) override { client_->videos(max, std::move(done)); }
    /// Twitch no tiene vivos con id: corta la transmisión de la app si iba a Twitch.
    void terminarVivo(const QString&, Done done) override { stopLive(std::move(done)); }
    const QJsonArray& chatMessages() const override { return client_->chatMessages(); }

    QString userCode() const { return client_->userCode(); }
    QString verificationUri() const { return client_->verificationUri(); }
    QString clientId() const { return client_->clientId(); }
    QVariantMap user() const { return client_->user().toVariantMap(); }
    QString avatar() const { return avatar_; }
    QVariantMap channel() const { return channel_.toVariantMap(); }
    QVariantMap stream() const { return stream_.toVariantMap(); }
    bool streamingHere() const { return streamingHere_; }
    QObject* videos() const;
    QObject* chat() const;
    QObject* categories() const;
    bool chatOn() const { return client_->chatRunning(); }

    Q_INVOKABLE QString saveClient(const QString& id, const QString& secret = {});
    void clearClient() override;
    void cancelLogin() override;
    void logout() override;
    void refresh() override;
    Q_INVOKABLE void updateChannel(const QString& title, const QString& category);
    Q_INVOKABLE void searchCategories(const QString& text);
    Q_INVOKABLE void goLive();
    Q_INVOKABLE void stopLive();
    Q_INVOKABLE void deleteVideo(const QString& id);
    Q_INVOKABLE void setChatOn(bool on);
    Q_INVOKABLE void sendChat(const QString& text);

    // Para el MCP (lo que ve la página se pone al día)
    void goLive(TwitchClient::Done done);
    void stopLive(TwitchClient::Done done);
    /// El canal (y la página lo muestra) / cambiarlo (la página se relee).
    void leerCanal(Done done);
    void cambiarCanal(const QJsonObject& cambios, Done done);
    /// Categorías por nombre: {items: [{id, name, box_art}]} (no toca la lista de la página).
    void buscarCategorias(const QString& texto, Done done);
    /// Lo que ve Twitch del vivo (y la página lo muestra).
    void estadoDelVivo(Done done);
    void borrarVideos(const QStringList& ids, Done done);
    /// ¿La cuenta puede leer la clave de transmisión? (account_check; la clave no sale).
    void probarClave(Done done);

   signals:
    // stateChanged (en PlataformaViewModel): cambió algo de la cuenta o de lo que sale (la
    // app, la cuenta, transmitir desde acá), para quien junta todo; no para enlazar.
    void appChanged();
    void cuentaChanged();
    void avatarChanged();
    void streamingHereChanged();
    void channelChanged();
    void streamChanged();

   private:
    /// El motor contestó si salió al aire (`error` vacío = sí): sigue goLive.
    void alSalir(const QJsonObject& ing, const std::string& error, const TwitchClient::Done& done);
    void pollStream();
    /// El cliente avisó: emite lo que cambió de la app y de la cuenta.
    void revisarCuenta();
    void setStreamingHere(bool si);
    void setChannel(const QJsonObject& c);
    void setStream(const QJsonObject& s);

    AppRuntime& rt_;
    VentanillaMotor& ventanilla_;
    std::unique_ptr<TwitchClient> client_;
    std::unique_ptr<JsonListModel> videos_;
    std::unique_ptr<JsonListModel> chat_;
    std::unique_ptr<JsonListModel> categories_;
    QJsonObject channel_;
    QJsonObject stream_;
    QString avatar_;
    bool streamingHere_ = false;
    QVariantMap appAntes_, cuentaAntes_; ///< para avisar solo lo que cambió
    QTimer streamTimer_;
};

} // namespace ba::app
