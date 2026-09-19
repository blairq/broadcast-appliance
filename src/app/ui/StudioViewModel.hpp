// ============================================================================
// src/app/ui/StudioViewModel.hpp — Fachada de la interfaz (capa ViewModel)
//
// Unico punto de entrada de la vista (QML): estado observable, submodelos y
// comandos del operador. No sabe nada de QML ni de como se dibuja; la vista no
// sabe nada del motor ni de la configuracion en disco.
//
//   Modelo (C++ puro)          ViewModel (Qt, sin QML)         Vista (QML)
//   AppRuntime / Engine  <-->  StudioViewModel + submodelos <-> tarjetas
//   AppSettings (JSON)         FrameService (servicio)          VideoItem
// ============================================================================

#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include <chrono>
#include <cstdint>
#include <memory>

namespace ba::app::agent {
class AlertEngine;
class AgentViewModel;
} // namespace ba::app::agent

namespace ba::app::audio {
class RackManager;
class AudioViewModel;
} // namespace ba::app::audio

namespace ba::app {

class AppRuntime;
class ConferenceViewModel;
class YouTubeViewModel;
class TwitchViewModel;
class FacebookViewModel;
class SystemViewModel;
class OperatorMessages;
class InputsViewModel;
class ChatOverlayRenderer;
class DashboardModel;
class FrameService;
class MixerModel;
class PropertiesModel;
class ScenesModel;
class SettingsModel;
class SourcesModel;
class SourceTypesModel;

class StudioViewModel : public QObject {
    Q_OBJECT
    // --- Estado ---------------------------------------------------------------
    Q_PROPERTY(bool recording READ recording NOTIFY statusChanged)
    Q_PROPERTY(bool streaming READ streaming NOTIFY statusChanged)
    Q_PROPERTY(QString recordTime READ recordTime NOTIFY statusChanged)
    Q_PROPERTY(QString streamTime READ streamTime NOTIFY statusChanged)
    Q_PROPERTY(double fps READ fps NOTIFY statusChanged)
    Q_PROPERTY(QString encoder READ encoder NOTIFY statusChanged)
    Q_PROPERTY(int droppedFrames READ droppedFrames NOTIFY statusChanged)
    Q_PROPERTY(int totalFrames READ totalFrames NOTIFY statusChanged)
    Q_PROPERTY(int streamKbps READ streamKbps NOTIFY statusChanged)
    Q_PROPERTY(double cpuPercent READ cpuPercent NOTIFY statusChanged)
    Q_PROPERTY(bool gatewayAlive READ gatewayAlive NOTIFY statusChanged)
    Q_PROPERTY(double programPeakL READ programPeakL NOTIFY statusChanged)
    Q_PROPERTY(double programPeakR READ programPeakR NOTIFY statusChanged)
    Q_PROPERTY(QString currentScene READ currentScene NOTIFY sceneChanged)
    Q_PROPERTY(QString transitionType READ transitionType NOTIFY sceneChanged)
    Q_PROPERTY(double canvasAspect READ canvasAspect NOTIFY canvasChanged)
    Q_PROPERTY(QString canvasLabel READ canvasLabel NOTIFY canvasChanged)
    Q_PROPERTY(QString streamServer READ streamServer NOTIFY statusChanged)
    Q_PROPERTY(bool streamKeyAvailable READ streamKeyAvailable NOTIFY statusChanged)
    Q_PROPERTY(QString room READ room NOTIFY statusChanged)
    Q_PROPERTY(QString joinUrl READ joinUrl NOTIFY statusChanged)
    Q_PROPERTY(int guests READ guests NOTIFY statusChanged)
    Q_PROPERTY(QString theme READ theme NOTIFY themeChanged)
    // --- Submodelos -------------------------------------------------------------
    Q_PROPERTY(QObject* scenes READ scenes CONSTANT)
    Q_PROPERTY(QObject* sources READ sources CONSTANT)
    Q_PROPERTY(QObject* mixer READ mixer CONSTANT)
    Q_PROPERTY(QObject* sourceTypes READ sourceTypes CONSTANT)
    Q_PROPERTY(QObject* settings READ settings CONSTANT)
    Q_PROPERTY(QObject* dashboard READ dashboard CONSTANT)
    Q_PROPERTY(QObject* frames READ frames CONSTANT)
    Q_PROPERTY(QObject* properties READ properties CONSTANT)
    Q_PROPERTY(QObject* conference READ conference CONSTANT)
    Q_PROPERTY(QObject* youtube READ youtube CONSTANT)
    Q_PROPERTY(QObject* twitch READ twitch CONSTANT)
    Q_PROPERTY(QObject* facebook READ facebook CONSTANT)
    Q_PROPERTY(QObject* system READ system CONSTANT)
    Q_PROPERTY(QObject* agent READ agent CONSTANT)
    Q_PROPERTY(QObject* messages READ messages CONSTANT)
    Q_PROPERTY(QObject* inputs READ inputs CONSTANT)
    /// Pestaña Audio (patchbay de modulos LSP por fuente); null sin CLAP.
    Q_PROPERTY(QObject* audio READ audio CONSTANT)
   public:
    explicit StudioViewModel(AppRuntime& rt, QObject* parent = nullptr);
    ~StudioViewModel() override;

    bool recording() const { return recording_; }
    bool streaming() const { return streaming_; }
    QString recordTime() const;
    QString streamTime() const;
    double fps() const { return fps_; }
    QString encoder() const { return encoder_; }
    int droppedFrames() const { return dropped_; }
    int totalFrames() const { return total_; }
    int streamKbps() const { return kbps_; }
    double cpuPercent() const { return cpu_; }
    bool gatewayAlive() const { return gatewayAlive_; }
    double programPeakL() const { return peakL_; }
    double programPeakR() const { return peakR_; }
    QString currentScene() const;
    QString transitionType() const;
    double canvasAspect() const;
    QString canvasLabel() const;
    QString streamServer() const;
    bool streamKeyAvailable() const;
    QString room() const;
    QString joinUrl() const;
    int guests() const;
    QString theme() const;

    QObject* scenes() const;
    QObject* sources() const;
    QObject* mixer() const;
    QObject* sourceTypes() const;
    QObject* settings() const;
    QObject* dashboard() const;
    QObject* frames() const;
    QObject* properties() const;
    QObject* conference() const;
    QObject* youtube() const;
    QObject* twitch() const;
    QObject* facebook() const;
    QObject* system() const;
    QObject* agent() const;
    QObject* messages() const;
    QObject* inputs() const;
    QObject* audio() const;
#if defined(BA_HAVE_CLAP)
    /// Racks de audio (modulos LSP) de las fuentes. nullptr si no hay libobs/CLAP.
    audio::RackManager* racks() const { return racks_.get(); }
#endif
    agent::AlertEngine& alerts() { return *alerts_; }

    // --- Comandos ---------------------------------------------------------------
    /// Pasos para conectar una plataforma (misma fuente que el MCP: AccountGuides).
    Q_INVOKABLE QVariantMap accountGuide(const QString& platform) const;
    /// Guarda la credencial de una plataforma desde el formulario seguro: campos
    /// del formulario, {"importFile": ruta} o {"fromDownloads": true}. "" o el error.
    Q_INVOKABLE QString saveAccountCredential(const QString& platform, const QVariantMap& values);
    Q_INVOKABLE void toggleRecording();
    Q_INVOKABLE void startStreaming(const QString& server, const QString& key);
    Q_INVOKABLE void stopStreaming();
    Q_INVOKABLE void takeScene(const QString& name);
    Q_INVOKABLE void addScene(const QString& name);
    Q_INVOKABLE void removeScene(const QString& name);
    Q_INVOKABLE void renameScene(const QString& from, const QString& to);
    Q_INVOKABLE void save();
    /// Relee escenas, fuentes y mezclador del motor (algo los cambio por
    /// fuera de la vista, p. ej. el servidor MCP).
    void refreshModels();

   signals:
    void statusChanged();
    void sceneChanged();
    void canvasChanged();
    void themeChanged();
    /// Aviso para el operador: level = "info" | "ok" | "warn" | "error".
    void notify(const QString& message, const QString& level);
    /// El launcher (o una señal) pidio cerrar.
    void quitRequested();

   private slots:
    void onTick();

   private:
    AppRuntime& rt_;
    std::unique_ptr<ScenesModel> scenes_;
    std::unique_ptr<SourcesModel> sources_;
    std::unique_ptr<MixerModel> mixer_;
    std::unique_ptr<SourceTypesModel> types_;
    std::unique_ptr<SettingsModel> settings_;
    std::unique_ptr<DashboardModel> dashboard_;
    std::unique_ptr<FrameService> frames_;
    std::unique_ptr<PropertiesModel> props_;
    std::unique_ptr<ConferenceViewModel> conference_;
#if defined(BA_HAVE_CLAP)
    std::unique_ptr<audio::RackManager> racks_;
    std::unique_ptr<audio::AudioViewModel> audio_;
#endif
    std::unique_ptr<YouTubeViewModel> youtube_;
    std::unique_ptr<TwitchViewModel> twitch_;
    std::unique_ptr<FacebookViewModel> facebook_;
    std::unique_ptr<SystemViewModel> system_;
    std::unique_ptr<agent::AlertEngine> alerts_;
    std::unique_ptr<agent::AgentViewModel> agent_;
    std::unique_ptr<OperatorMessages> messages_;
    std::unique_ptr<InputsViewModel> inputs_;
    std::unique_ptr<ChatOverlayRenderer> chatOverlay_;
    QTimer tick_;
    QTimer frameTick_;

    bool recording_ = false;
    bool streaming_ = false;
    std::chrono::steady_clock::time_point recSince_{};
    std::chrono::steady_clock::time_point streamSince_{};
    double fps_ = 0;
    QString encoder_;
    int dropped_ = 0;
    int total_ = 0;
    int kbps_ = 0;
    std::uint64_t lastBytes_ = 0;
    std::chrono::steady_clock::time_point lastBytesAt_{};
    double cpu_ = 0;
    void* cpuInfo_ = nullptr; // os_cpu_usage_info_t*
    bool gatewayAlive_ = false;
    double peakL_ = -100;
    double peakR_ = -100;
};

} // namespace ba::app
