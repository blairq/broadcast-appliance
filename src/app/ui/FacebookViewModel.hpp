// ============================================================================
// src/app/ui/FacebookViewModel.hpp — Pagina Facebook (ViewModel)
//
// Adapta FacebookClient a la vista y al MCP: app de Meta (ID, clave, puerto de
// redireccion), login, eleccion de Pagina, vivos (crear en prueba o en vivo,
// transmitir con RTMPS, publicar, terminar), videos (editar, borrar, subir
// grabaciones) y comentarios del vivo (pagina y fuente "Chat en vivo").
// ============================================================================
#pragma once

#include <QJsonArray>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

#include "facebook/FacebookClient.hpp"

namespace ba::app {

class AppRuntime;
class VentanillaMotor;
class JsonListModel;
class GrabacionesModel;

class FacebookViewModel : public QObject {
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
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
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

    bool configured() const { return client_->hasClient(); }
    bool loggedIn() const { return client_->loggedIn(); }
    bool loginPending() const { return client_->loginPending(); }
    QString appId() const { return client_->appId(); }
    int port() const { return client_->port(); }
    QString redirectUri() const { return client_->redirectUri(); }
    QVariantMap user() const { return client_->user().toVariantMap(); }
    QVariantList pages() const { return client_->pages().toVariantList(); }
    QString pageId() const { return client_->pageId(); }
    QVariantMap page() const { return client_->page().toVariantMap(); }
    bool busy() const { return busy_ > 0; }
    QString lastError() const { return lastError_; }
    QObject* lives() const;
    QObject* videos() const;
    QObject* recordings() const;
    QObject* chat() const;
    QString liveId() const { return liveId_; }
    QString chatLiveId() const { return chatLiveId_; }
    QVariantMap upload() const { return upload_; }

    Q_INVOKABLE QString saveClient(const QString& appId, const QString& secret, int port);
    Q_INVOKABLE void clearClient();
    Q_INVOKABLE void login();
    /// El mismo login, con la respuesta (el MCP con `wait`): conectada (o el error), la
    /// cuenta ya releída. Lo que ve la página es lo mismo que con login().
    void login(FacebookClient::Done listo);
    /// Para una escritura hecha con el cliente (el MCP): si sale bien, la página se
    /// relee (refresh) antes de contestar. Una sola regla para todas las escrituras.
    FacebookClient::Done conRefresco(FacebookClient::Done listo);
    Q_INVOKABLE void cancelLogin();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void selectPage(const QString& id);
    Q_INVOKABLE void refresh();
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
    Q_INVOKABLE void openUrl(const QString& url);
    Q_INVOKABLE void copy(const QString& text);

    // Para el MCP
    FacebookClient& client() { return *client_; }
    void goLive(const QJsonObject& p, FacebookClient::Done done);
    void endLive(const QString& id, FacebookClient::Done done);
    void uploadRecording(const QString& path, const QJsonObject& meta, FacebookClient::Done done);
    const QJsonArray& chatMessages() const { return chatItems_; }
    QString recordingsDir() const;

   signals:
    /// Cambió algo de la cuenta o del vivo (la app, la cuenta, la Página, el vivo de acá):
    /// para quien junta todo (la web del evento), no para enlazar propiedades.
    void stateChanged();
    void appChanged();
    void cuentaChanged();
    void paginaChanged();
    void liveIdChanged();
    void busyChanged();
    void errorChanged();
    void chatChanged();
    void uploadChanged();
    void notify(const QString& message, const QString& level);
    /// Arrancó un vivo desde acá: {plataforma, url, id, estudio?} (sin la clave).
    /// Va al bus como «salida/vivo» y queda en el histórico del evento.
    void vivo(const QVariantMap& datos);

   private:
    /// El motor contestó si salió al aire (`error` vacío = sí): sigue goLive.
    void alSalir(const QJsonObject& d, const QJsonObject& ing, const std::string& error, const FacebookClient::Done& done);
    void setError(const QString& e);
    void track(bool start);
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
    int busy_ = 0;
    QString lastError_;
    QString liveId_;
    QVariantMap appAntes_, cuentaAntes_, paginaAntes_; ///< para avisar solo lo que cambió
    QString chatLiveId_;
    QJsonArray chatItems_;
    QTimer chatTimer_;
    QVariantMap upload_;
};

} // namespace ba::app
