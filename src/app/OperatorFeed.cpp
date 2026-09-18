// ============================================================================
// src/app/OperatorFeed.cpp — ver OperatorFeed.hpp
// ============================================================================

#include "OperatorFeed.hpp"

#include <obs.h>

#include <cstring>

#include "BroadcastEngine.hpp"
#include "ipc/HealthBlock.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace ba::app {

namespace {

std::uint32_t selfPid() {
#ifdef _WIN32
    return static_cast<std::uint32_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint32_t>(getpid());
#endif
}

constexpr std::size_t kBlockFrames = 480; // 10 ms a 48 kHz (formato del ring)

} // namespace

OperatorFeed::~OperatorFeed() { shutdown(); }

void OperatorFeed::shutdown() {
    stop();
    for (obs_source_t* s : lobbyMonitors_) {
        if (s != nullptr) obs_source_release(s);
    }
    lobbyMonitors_.clear();
}

bool OperatorFeed::configure(BroadcastEngine& engine, const std::string& videoSource,
                             const std::string& audioSource) {
    bool ok = true;

    // --- Video ------------------------------------------------------------------
    if (videoSource != videoName_ || (video_ == nullptr && !videoSource.empty())) {
        stopVideo();
        videoName_ = videoSource;
        if (!videoSource.empty()) {
            obs_source_t* src = engine.sourceHandle(videoSource);
            obs_video_info ovi{};
            if (src == nullptr || !obs_get_video_info(&ovi)) {
                ok = false;
            } else {
                // Escena PRIVADA con la fuente ajustada al lienzo: la vista la
                // dibuja al tamaño base y libobs la escala a 640x360 en GPU.
                scene_ = obs_scene_create_private("ba-operador");
                obs_sceneitem_t* item = obs_scene_add(scene_, src);
                vec2 bounds{};
                bounds.x = static_cast<float>(ovi.base_width);
                bounds.y = static_cast<float>(ovi.base_height);
                obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
                obs_sceneitem_set_bounds(item, &bounds);
                obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
                view_ = obs_view_create();
                obs_view_set_source(view_, 0, obs_scene_get_source(scene_));
                ovi.output_width = kWidth;
                ovi.output_height = kHeight;
                ovi.output_format = VIDEO_FORMAT_BGRA;
                video_ = obs_view_add2(view_, &ovi);
                if (video_ != nullptr &&
                    sender_.publish(L"BA.Operator", ba::ipc::SlotConfig{kWidth, kHeight})) {
                    video_output_connect(video_, nullptr, &OperatorFeed::onVideo, this);
                } else {
                    stopVideo();
                    ok = false;
                }
            }
        }
    }

    // --- Audio ------------------------------------------------------------------
    if (audioSource != audioName_ || (audioSrc_ == nullptr && !audioSource.empty())) {
        stopAudio();
        audioName_ = audioSource;
        if (!audioSource.empty()) {
            obs_source_t* src = engine.sourceHandle(audioSource);
            if (src == nullptr) {
                ok = false;
            } else {
                if (!ring_.header()) {
                    ba::ipc::SharedAudioRing::init(L"BA.Audio.Operator", 2,
                                                   ba::ipc::audio_ring::kDefaultCapacityBlocks, selfPid(), 1,
                                                   ring_);
                }
                audioSrc_ = obs_source_get_ref(src);
                obs_source_add_audio_capture_callback(audioSrc_, &OperatorFeed::onAudio, this);
            }
        }
    }
    return ok;
}

void OperatorFeed::stop() {
    stopVideo();
    stopAudio();
    videoName_.clear();
    audioName_.clear();
}

void OperatorFeed::stopVideo() {
    if (video_ != nullptr) {
        video_output_disconnect(video_, &OperatorFeed::onVideo, this);
        video_ = nullptr;
    }
    if (view_ != nullptr) {
        obs_view_remove(view_);
        obs_view_set_source(view_, 0, nullptr);
        obs_view_destroy(view_);
        view_ = nullptr;
    }
    if (scene_ != nullptr) {
        obs_scene_release(scene_);
        scene_ = nullptr;
    }
    sender_ = ba::ipc::SpoutSender{}; // B ve el canal caido y pone la placa
}

void OperatorFeed::stopAudio() {
    if (audioSrc_ != nullptr) {
        obs_source_remove_audio_capture_callback(audioSrc_, &OperatorFeed::onAudio, this);
        obs_source_release(audioSrc_);
        audioSrc_ = nullptr;
    }
    std::lock_guard<std::mutex> lock(audioLock_);
    audioBuf_.clear();
}

void OperatorFeed::onVideo(void* param, video_data* frame) {
    auto* self = static_cast<OperatorFeed*>(param);
    if (!self->sender_.published() || frame->data[0] == nullptr) return;
    const std::size_t row = static_cast<std::size_t>(kWidth) * 4u;
    const std::uint8_t* px = frame->data[0];
    if (static_cast<std::size_t>(frame->linesize[0]) != row) {
        self->packed_.resize(row * kHeight);
        for (int y = 0; y < kHeight; ++y) {
            std::memcpy(self->packed_.data() + y * row,
                        frame->data[0] + static_cast<std::size_t>(y) * frame->linesize[0], row);
        }
        px = self->packed_.data();
    }
    ba::ipc::FrameMeta meta;
    meta.frameIndex = ++self->frameIndex_;
    meta.qpc = ba::ipc::HealthBlock::nowQpc();
    self->sender_.send(px, row * kHeight, meta);
}

void OperatorFeed::onAudio(void* param, obs_source_t*, const audio_data* data, bool muted) {
    auto* self = static_cast<OperatorFeed*>(param);
    if (data == nullptr || data->frames == 0 || !self->ring_.header()) return;
    // libobs entrega float planar (un plano por canal); el ring es estereo
    // intercalado a 48 kHz en bloques de 10 ms.
    const auto* l = reinterpret_cast<const float*>(data->data[0]);
    const auto* r = data->data[1] != nullptr ? reinterpret_cast<const float*>(data->data[1]) : l;
    std::lock_guard<std::mutex> lock(self->audioLock_);
    auto& buf = self->audioBuf_;
    const std::size_t base = buf.size();
    buf.resize(base + static_cast<std::size_t>(data->frames) * 2);
    for (std::uint32_t i = 0; i < data->frames; ++i) {
        // Microfono silenciado en la app = silencio tambien para el lobby.
        buf[base + i * 2] = (muted || l == nullptr) ? 0.0f : l[i];
        buf[base + i * 2 + 1] = (muted || r == nullptr) ? 0.0f : r[i];
    }
    std::size_t pos = 0;
    while (buf.size() - pos >= kBlockFrames * 2) {
        self->ring_.push(buf.data() + pos, ba::ipc::HealthBlock::nowQpc());
        pos += kBlockFrames * 2;
    }
    buf.erase(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(pos));
}

void OperatorFeed::startLobbyMonitor(int count) {
    if (!lobbyMonitors_.empty()) return;
    for (int k = 1; k <= count && k <= kMaxLobby; ++k) {
        obs_data_t* s = obs_data_create();
        obs_data_set_int(s, "slot", k);
        obs_data_set_string(s, "channel", "lobby");
        const std::string name = "ba-lobby-monitor-" + std::to_string(k);
        obs_source_t* src = obs_source_create_private("ba_guest_audio", name.c_str(), s);
        obs_data_release(s);
        if (src != nullptr) {
            // Solo a los auriculares del operador: jamas a la mezcla.
            obs_source_set_monitoring_type(src, OBS_MONITORING_TYPE_MONITOR_ONLY);
            obs_source_set_muted(src, true);
        }
        lobbyMonitors_.push_back(src);
    }
}

void OperatorFeed::setLobbyMonitor(int slot, bool on) {
    if (slot < 1 || slot > static_cast<int>(lobbyMonitors_.size())) return;
    if (obs_source_t* s = lobbyMonitors_[static_cast<std::size_t>(slot - 1)]) obs_source_set_muted(s, !on);
}

} // namespace ba::app
