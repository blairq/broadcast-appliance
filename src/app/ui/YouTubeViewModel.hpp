// ============================================================================
// src/app/ui/YouTubeViewModel.hpp — Pagina YouTube (ViewModel)
//
// Adapta YouTubeClient a la vista y al servidor MCP (lo común, en PlataformaViewModel):
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
#include <QTimer>
#include <QVariantMap>

#include <functional>
#include <memory>

#include "ui/PlataformaViewModel.hpp"
#include "youtube/YouTubeClient.hpp"

namespace ba::app {

class AppRuntime;
class VentanillaMotor;
class JsonListModel;
class GrabacionesModel;

class YouTubeViewModel : public PlataformaViewModel {
    Q_OBJECT
    Q_PROPERTY(bool configured READ configured NOTIFY stateChanged)
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY stateChanged)
    Q_PROPERTY(bool loginPending READ loginPending NOTIFY stateChanged)
    Q_PROPERTY(QString clientId READ clientId NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap account READ account NOTIFY stateChanged)
    /// Avatar del canal como data: URL (vacio mientras se descarga).
    Q_PROPERTY(QString accountThumb READ accountThumb NOTIFY stateChanged)
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
    /// `grabaciones`: la lista de grabaciones para subir (compartida con las otras plataformas).
    YouTubeViewModel(AppRuntime& rt, VentanillaMotor& ventanilla, GrabacionesModel& grabaciones, QObject* parent = nullptr);
    ~YouTubeViewModel() override;

    // --- La plataforma (PlataformaViewModel) -----------------------------------------------
    QString plataforma() const override { return QStringLiteral("youtube"); }
    QString nombre() const override { return QStringLiteral("YouTube"); }
    bool configured() const override { return client_->hasClient(); }
    bool loggedIn() const override { return client_->loggedIn(); }
    bool loginPending() const override { return client_->loginPending(); }
    QString cuentaNombre() const override { return client_->account().value("title").toString(); }
    QJsonObject estado() const override;
    QStringList permisos() const override { return YouTubeClient::permisos(); }
    bool guardarCredencial(const QVariantMap& campos) override;
    using PlataformaViewModel::login;
    void login(Done listo) override;
    void listarVideos(int max, Done done) override;
    void terminarVivo(const QString& id, Done done) override { endBroadcast(id, std::move(done)); }
    /// Mensajes de chat en memoria (YouTube), del mas viejo al mas nuevo.
    const QJsonArray& chatMessages() const override { return chatItems_; }

    QString clientId() const { return client_->clientId(); }
    QVariantMap account() const { return client_->account().toVariantMap(); }
    QString accountThumb() const { return thumbs_.value(client_->account().value("thumbnail").toString()); }
    QObject* videos() const;
    QObject* broadcasts() const;
    QObject* recordings() const;
    QObject* chat() const;
    QVariantMap upload() const { return upload_; }
    QString chatBroadcastId() const { return chatBroadcastId_; }
    QString liveBroadcastId() const { return liveBroadcastId_; }

    // --- Configuracion y cuenta -------------------------------------------------------
    /// Pega el JSON descargado (o el ID si `secret` no esta vacio). El error, por avisarError.
    Q_INVOKABLE bool saveClient(const QString& jsonOrId, const QString& secret = {});
    void clearClient() override;
    void cancelLogin() override;
    void logout() override;
    void refresh() override;
    /// Vuelve a leer las grabaciones (aparte: la lista cambia cuando termina).
    Q_INVOKABLE void refreshRecordings();

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
    void goLive(const QString& broadcastId, YouTubeClient::Done done);
    void endBroadcast(const QString& broadcastId, YouTubeClient::Done done);
    void uploadRecording(const QString& path, const QJsonObject& meta, YouTubeClient::Done done);
    /// Cambiar o borrar un video, crear, cambiar, pasar de estado o borrar un evento: lo
    /// mismo que hace la página (la lista se pone al día al terminar), con el resultado. Sin
    /// esto el MCP le hablaba al cliente directo y la página quedaba vieja.
    void updateVideo(const QString& id, const QJsonObject& cambios, YouTubeClient::Done done);
    void deleteVideo(const QString& id, YouTubeClient::Done done);
    void createBroadcast(const QJsonObject& datos, YouTubeClient::Done done);
    void updateBroadcast(const QString& id, const QJsonObject& cambios, YouTubeClient::Done done);
    void transition(const QString& id, const QString& estado, YouTubeClient::Done done);
    void deleteBroadcast(const QString& broadcastId, YouTubeClient::Done done);
    /// Lecturas para el MCP (no cambian la página): los eventos, el canal, la ingesta de
    /// un evento (sin la clave) y la miniatura de un video (esa sí relee la página).
    void listarEventos(Done done);
    void cargarCanal(Done done);
    void ingesta(const QString& broadcastId, Done done);
    void ponerMiniatura(const QString& videoId, const QString& imagen, Done done);
    QString recordingsDir() const;

   signals:
    void uploadChanged();

   private:
    /// El motor contestó si salió al aire (`error` vacío = sí): sigue goLive.
    void alSalir(const QString& broadcastId, const QString& url, const std::string& error, const YouTubeClient::Done& done);
    void fetchThumbs(const QJsonArray& items, JsonListModel* model);
    void pollChat();

    AppRuntime& rt_;
    VentanillaMotor& ventanilla_;
    std::unique_ptr<YouTubeClient> client_;
    std::unique_ptr<JsonListModel> videos_;
    std::unique_ptr<JsonListModel> broadcasts_;
    GrabacionesModel& grabaciones_; ///< compartida (ui/GrabacionesModel)
    std::unique_ptr<JsonListModel> chat_;
    QHash<QString, QString> thumbs_; // url -> data: URL
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
