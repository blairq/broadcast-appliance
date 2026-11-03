// ============================================================================
// src/app/ui/FacebookViewModel.hpp — Pagina Facebook (ViewModel)
//
// Adapta FacebookClient a la vista y al MCP (lo común, en PlataformaViewModel): app de Meta (ID, clave, puerto de
// redireccion), login, eleccion de Pagina, vivos (crear en prueba o en vivo,
// transmitir con RTMPS, publicar, terminar), videos (editar, borrar, subir
// grabaciones) y comentarios del vivo (pagina y fuente "Chat en vivo").
// ============================================================================
#pragma once

#include <QJsonArray>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

#include "facebook/FacebookClient.hpp"
#include "ui/PlataformaViewModel.hpp"

namespace ba::app {

class AppRuntime;
class VentanillaMotor;
class JsonListModel;
class GrabacionesModel;

class FacebookViewModel : public PlataformaViewModel {
    Q_OBJECT
    // Cada grupo avisa solo si cambió lo suyo.
    /// La app de Meta (ID, puerto de redirección).
    Q_PROPERTY(bool configured READ configured NOTIFY appChanged)
    Q_PROPERTY(QString appId READ appId NOTIFY appChanged)
    Q_PROPERTY(int port READ port NOTIFY appChanged)
    Q_PROPERTY(QString redirectUri READ redirectUri NOTIFY appChanged)
    /// La cuenta.
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY cuentaChanged)
    Q_PROPERTY(bool loginPending READ loginPending NOTIFY cuentaChanged)
    Q_PROPERTY(QVariantMap user READ user NOTIFY cuentaChanged)
    /// Las Páginas y la elegida.
    Q_PROPERTY(QVariantList pages READ pages NOTIFY paginaChanged)
    Q_PROPERTY(QString pageId READ pageId NOTIFY paginaChanged)
    Q_PROPERTY(QVariantMap page READ page NOTIFY paginaChanged)
    Q_PROPERTY(QObject* lives READ lives CONSTANT)
    Q_PROPERTY(QObject* videos READ videos CONSTANT)
    Q_PROPERTY(QObject* recordings READ recordings CONSTANT)
    Q_PROPERTY(QObject* chat READ chat CONSTANT)
    Q_PROPERTY(QString liveId READ liveId NOTIFY liveIdChanged)
    Q_PROPERTY(QString chatLiveId READ chatLiveId NOTIFY chatChanged)
    Q_PROPERTY(QVariantMap upload READ upload NOTIFY uploadChanged)
   public:
    /// `ventanilla`: transmitir y cortar se le piden al motor; si transmite, de su foto (F3-03).
    /// `grabaciones`: la lista de grabaciones para subir (compartida con las otras plataformas).
    FacebookViewModel(AppRuntime& rt, VentanillaMotor& ventanilla, GrabacionesModel& grabaciones, QObject* parent = nullptr);
    ~FacebookViewModel() override;

    // --- La plataforma (PlataformaViewModel) -----------------------------------------------
    QString plataforma() const override { return QStringLiteral("facebook"); }
    QString nombre() const override { return QStringLiteral("Facebook"); }
    bool configured() const override { return client_->hasClient(); }
    bool loggedIn() const override { return client_->loggedIn(); }
    bool loginPending() const override { return client_->loginPending(); }
    QString cuentaNombre() const override { return client_->user().value("name").toString(); }
    /// Además de la cuenta, la Página donde transmitir.
    Falta falta() const override;
    QJsonObject estadoCuenta() const override;
    QJsonObject estado() const override;
    QStringList permisos() const override { return FacebookClient::permisos(); }
    bool guardarCredencial(const QVariantMap& campos) override;
    using PlataformaViewModel::login;
    void login(Done listo) override;
    QJsonObject alConectar() const override { return {{"pages", client_->pages()}}; }
    void listarVideos(int max, Done done) override { client_->videos(max, std::move(done)); }
    void terminarVivo(const QString& id, Done done) override { endLive(id, std::move(done)); }
    const QJsonArray& chatMessages() const override { return chatItems_; }

    /// El cliente de la cuenta principal (vivos/PlataformasVivo: la principal de la
    /// plataforma es la de esta página; las demás tienen el suyo).
    FacebookClient& cliente() { return *client_; }
    QString appId() const { return client_->appId(); }
    int port() const { return client_->port(); }
    QString redirectUri() const { return client_->redirectUri(); }
    QVariantMap user() const { return client_->user().toVariantMap(); }
    QVariantList pages() const { return client_->pages().toVariantList(); }
    QString pageId() const { return client_->pageId(); }
    QVariantMap page() const { return client_->page().toVariantMap(); }
    QObject* lives() const;
    QObject* videos() const;
    QObject* recordings() const;
    QObject* chat() const;
    QString liveId() const { return liveId_; }
    QString chatLiveId() const { return chatLiveId_; }
    QVariantMap upload() const { return upload_; }

    Q_INVOKABLE bool saveClient(const QString& appId, const QString& secret, int port);
    void clearClient() override;
    void cancelLogin() override;
    void logout() override;
    Q_INVOKABLE void selectPage(const QString& id);
    void refresh() override;
    /// Vuelve a leer las grabaciones (aparte: la lista cambia cuando termina).
    Q_INVOKABLE void refreshRecordings();
    /// Crea y transmite. `test` = sin publicar (solo lo ve la Pagina en Live Producer).
    Q_INVOKABLE void goLive(const QString& title, const QString& description, bool test);
    Q_INVOKABLE void publishLive(const QString& id);
    Q_INVOKABLE void endLive(const QString& id);
    Q_INVOKABLE void deleteItem(const QString& id);
    Q_INVOKABLE void updateVideo(const QString& id, const QString& title, const QString& description);
    Q_INVOKABLE void uploadRecording(const QString& path, const QString& title, const QString& description);
    Q_INVOKABLE void setChatLive(const QString& id);
    Q_INVOKABLE void sendChat(const QString& text);

    // Para el MCP (lo que ve la página se pone al día)
    void goLive(const QJsonObject& p, FacebookClient::Done done);
    void endLive(const QString& id, FacebookClient::Done done);
    void uploadRecording(const QString& path, const QJsonObject& meta, FacebookClient::Done done);
    QString recordingsDir() const;
    /// Las Páginas de la cuenta: [{id, name, category, picture}] (sin tokens).
    QJsonArray paginas() const { return client_->pages(); }
    /// Los vivos de la Página / el estado de uno (lecturas: no cambian la página).
    void vivos(Done done);
    void estadoDeVivo(const QString& id, Done done);
    /// Publicar un vivo en prueba, cambiar un video o borrarlo (la página se relee).
    void publicarVivo(const QString& id, Done done);
    void cambiarVideo(const QString& id, const QJsonObject& cambios, Done done);
    void borrar(const QString& id, Done done);
    /// Los permisos que la cuenta otorgó: {data: [{permission, status}]} (account_check).
    void permisosOtorgados(Done done);

   signals:
    // stateChanged (en PlataformaViewModel): cambió algo de la cuenta o del vivo (la app,
    // la cuenta, la Página, el vivo de acá), para quien junta todo; no para enlazar.
    void appChanged();
    void cuentaChanged();
    void paginaChanged();
    void liveIdChanged();
    void uploadChanged();

   private:
    /// El motor contestó si salió al aire (`error` vacío = sí): sigue goLive.
    void alSalir(const QJsonObject& d, const QJsonObject& ing, const std::string& error, const FacebookClient::Done& done);
    void pollChat();
    /// El cliente avisó: emite lo que cambió de la app, la cuenta y la Página.
    void revisarCuenta();
    void setLiveId(const QString& id);

    AppRuntime& rt_;
    VentanillaMotor& ventanilla_;
    std::unique_ptr<FacebookClient> client_;
    std::unique_ptr<JsonListModel> lives_;
    std::unique_ptr<JsonListModel> videos_;
    GrabacionesModel& grabaciones_; ///< compartida (ui/GrabacionesModel)
    std::unique_ptr<JsonListModel> chat_;
    QString liveId_;
    QVariantMap appAntes_, cuentaAntes_, paginaAntes_; ///< para avisar solo lo que cambió
    QString chatLiveId_;
    QJsonArray chatItems_;
    QTimer chatTimer_;
    QVariantMap upload_;
};

} // namespace ba::app
