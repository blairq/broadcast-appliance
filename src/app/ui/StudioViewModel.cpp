// ============================================================================
// src/app/ui/StudioViewModel.cpp — ver StudioViewModel.hpp
// ============================================================================

#include "ui/StudioViewModel.hpp"

#include <QHostAddress>
#include <QNetworkInterface>

#include <util/platform.h>

#include <algorithm>
#include <cstdlib>

#include "AppRuntime.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/DashboardModel.hpp"
#include "ui/FrameService.hpp"
#include "ui/ListModels.hpp"
#include "ui/PropertiesModel.hpp"
#include "ui/SettingsModel.hpp"

namespace ba::app {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

QString hms(std::chrono::steady_clock::time_point since) {
    const auto s =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - since).count();
    return QString("%1:%2:%3")
        .arg(s / 3600, 2, 10, QChar('0'))
        .arg((s / 60) % 60, 2, 10, QChar('0'))
        .arg(s % 60, 2, 10, QChar('0'));
}

} // namespace

StudioViewModel::StudioViewModel(AppRuntime& rt, QObject* parent) : QObject(parent), rt_(rt) {
    scenes_ = std::make_unique<ScenesModel>(rt);
    sources_ = std::make_unique<SourcesModel>(rt);
    sources_->setScene(currentScene());
    mixer_ = std::make_unique<MixerModel>(rt);
    types_ = std::make_unique<SourceTypesModel>(rt);
    settings_ = std::make_unique<SettingsModel>(rt);
    dashboard_ = std::make_unique<DashboardModel>(rt.dataRoot() / "Config" / "ui.json");
    frames_ = std::make_unique<FrameService>(rt.engine(), rt.config().guests);
    frames_->setLobbySlots(rt.gateway().lobbySlots);
    props_ = std::make_unique<PropertiesModel>(rt);
    conference_ = std::make_unique<ConferenceViewModel>(rt);
    connect(conference_.get(), &ConferenceViewModel::notify, this, &StudioViewModel::notify);
    // Las etiquetas de video ("Invitado 2") pasan a ser el nombre real.
    connect(conference_.get(), &ConferenceViewModel::roomChanged, this,
            [this]() { frames_->setGuestNames(conference_->slotNames()); });
    connect(conference_.get(), &ConferenceViewModel::sourceAdded, this, [this]() {
        sources_->reload();
        mixer_->reload();
    });
    cpuInfo_ = os_cpu_usage_info_start();

    connect(sources_.get(), &SourcesModel::error, this,
            [this](const QString& m) { emit notify(m, "error"); });
    connect(settings_.get(), &SettingsModel::applied, this, [this](const QString& summary) {
        emit notify(summary, summary.contains("ERROR") ? "error" : "ok");
        emit canvasChanged();
        emit themeChanged();
        emit sceneChanged();
        frames_->setGuests(rt_.config().guests);
    });

    connect(&tick_, &QTimer::timeout, this, &StudioViewModel::onTick);
    tick_.start(100);
    connect(&frameTick_, &QTimer::timeout, frames_.get(), &FrameService::poll);
    frameTick_.start(16); // hasta 60 Hz; FrameService solo avisa si hay frame nuevo
}

StudioViewModel::~StudioViewModel() {
    if (cpuInfo_ != nullptr) {
        os_cpu_usage_info_destroy(static_cast<os_cpu_usage_info_t*>(cpuInfo_));
    }
}

void StudioViewModel::onTick() {
    rt_.tick();
    if (AppRuntime::stopRequested()) {
        emit quitRequested();
        return;
    }
    const EngineStatus st = rt_.engine().status();
    if (st.recording && !recording_) {
        recSince_ = std::chrono::steady_clock::now();
    }
    if (st.streaming && !streaming_) {
        streamSince_ = std::chrono::steady_clock::now();
        lastBytes_ = 0;
        lastBytesAt_ = streamSince_;
    }
    if (recording_ && !st.recording) {
        emit notify("Grabacion detenida", "info");
    }
    if (streaming_ && !st.streaming) {
        emit notify("Transmision detenida", "warn");
    }
    recording_ = st.recording;
    streaming_ = st.streaming;
    fps_ = st.fps;
    encoder_ = qs(st.videoEncoder);
    dropped_ = st.droppedFrames;
    total_ = st.totalFrames;
    // Bitrate real de la transmision (promedio de ~1 s).
    const auto now = std::chrono::steady_clock::now();
    const double dt = std::chrono::duration<double>(now - lastBytesAt_).count();
    if (st.streaming && dt >= 1.0) {
        kbps_ = static_cast<int>((st.bytesSent - lastBytes_) * 8.0 / 1000.0 / dt);
        lastBytes_ = st.bytesSent;
        lastBytesAt_ = now;
    } else if (!st.streaming) {
        kbps_ = 0;
    }
    cpu_ = os_cpu_usage_info_query(static_cast<os_cpu_usage_info_t*>(cpuInfo_));
    gatewayAlive_ = rt_.gatewayAlive();
    float l = -100;
    float r = -100;
    rt_.engine().programPeaksDb(l, r);
    peakL_ = std::max<double>(l, peakL_ - 1.5);
    peakR_ = std::max<double>(r, peakR_ - 1.5);
    mixer_->refreshLevels();
    emit statusChanged();
}

QString StudioViewModel::recordTime() const { return recording_ ? hms(recSince_) : QString("00:00:00"); }
QString StudioViewModel::streamTime() const { return streaming_ ? hms(streamSince_) : QString("00:00:00"); }
QString StudioViewModel::currentScene() const { return qs(rt_.engine().currentScene()); }
QString StudioViewModel::transitionType() const { return qs(rt_.config().transition.type); }
double StudioViewModel::canvasAspect() const {
    const VideoConfig& v = rt_.config().video;
    return v.baseHeight > 0 ? double(v.baseWidth) / v.baseHeight : 16.0 / 9.0;
}
QString StudioViewModel::canvasLabel() const {
    const VideoConfig& v = rt_.config().video;
    return QString("%1x%2 · %3 fps").arg(v.outputWidth).arg(v.outputHeight).arg(v.fpsNum);
}
QString StudioViewModel::streamServer() const { return qs(rt_.config().streamCfg.server); }
bool StudioViewModel::streamKeyAvailable() const {
    const char* k = std::getenv(rt_.config().streamKeyEnv.c_str());
    return k != nullptr && *k != '\0';
}
QString StudioViewModel::room() const { return qs(const_cast<AppRuntime&>(rt_).gateway().room); }
QString StudioViewModel::joinUrl() const {
    const GatewaySettings& g = const_cast<AppRuntime&>(rt_).gateway();
    QString host = "127.0.0.1";
    if (g.bindAddress != "127.0.0.1") {
        // Red local: la primera IPv4 no loopback.
        for (const QHostAddress& a : QNetworkInterface::allAddresses()) {
            if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback()) {
                host = a.toString();
                break;
            }
        }
    }
    return QString("http://%1:%2").arg(host).arg(g.port);
}
int StudioViewModel::guests() const { return rt_.config().guests; }
QString StudioViewModel::theme() const { return qs(rt_.config().theme); }

QObject* StudioViewModel::scenes() const { return scenes_.get(); }
QObject* StudioViewModel::sources() const { return sources_.get(); }
QObject* StudioViewModel::mixer() const { return mixer_.get(); }
QObject* StudioViewModel::sourceTypes() const { return types_.get(); }
QObject* StudioViewModel::settings() const { return settings_.get(); }
QObject* StudioViewModel::dashboard() const { return dashboard_.get(); }
QObject* StudioViewModel::frames() const { return frames_.get(); }
QObject* StudioViewModel::properties() const { return props_.get(); }
QObject* StudioViewModel::conference() const { return conference_.get(); }

void StudioViewModel::toggleRecording() {
    if (recording_) {
        rt_.stopRecording();
    } else if (rt_.startRecording()) {
        emit notify("Grabando en " + qs(rt_.config().rec.directory), "ok");
    } else {
        emit notify("No se pudo grabar: " + qs(rt_.lastError()), "error");
    }
    onTick();
}

void StudioViewModel::startStreaming(const QString& server, const QString& key) {
    if (rt_.startStreaming(server.toStdString(), key.toStdString())) {
        emit notify("Al aire", "ok");
    } else {
        emit notify("No se pudo transmitir: " + qs(rt_.lastError()), "error");
    }
    onTick();
}

void StudioViewModel::stopStreaming() {
    rt_.stopStreaming();
    onTick();
}

void StudioViewModel::takeScene(const QString& name) {
    if (!rt_.setScene(name.toStdString())) {
        emit notify(qs(rt_.lastError()), "error");
        return;
    }
    scenes_->liveChanged();
    sources_->setScene(name); // el editor de fuentes sigue a la escena al aire
    emit sceneChanged();
    rt_.save();
}

void StudioViewModel::addScene(const QString& name) {
    if (!rt_.addScene(name.trimmed().toStdString())) {
        emit notify(qs(rt_.lastError()), "error");
        return;
    }
    scenes_->reload();
    rt_.save();
}

void StudioViewModel::removeScene(const QString& name) {
    if (!rt_.removeScene(name.toStdString())) {
        emit notify(qs(rt_.lastError()), "error");
        return;
    }
    scenes_->reload();
    if (sources_->scene() == name) {
        sources_->setScene(currentScene());
    }
    emit sceneChanged();
    rt_.save();
}

void StudioViewModel::renameScene(const QString& from, const QString& to) {
    if (!rt_.renameScene(from.toStdString(), to.trimmed().toStdString())) {
        emit notify(qs(rt_.lastError()), "error");
        return;
    }
    scenes_->reload();
    if (sources_->scene() == from) {
        sources_->setScene(to.trimmed());
    }
    emit sceneChanged();
    rt_.save();
}

void StudioViewModel::save() {
    if (rt_.save()) {
        emit notify("Guardado", "ok");
    } else {
        emit notify("No se pudo guardar: " + qs(rt_.lastError()), "error");
    }
}

} // namespace ba::app
