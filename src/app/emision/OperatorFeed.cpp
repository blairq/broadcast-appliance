// ============================================================================
// src/app/emision/OperatorFeed.cpp — ver OperatorFeed.hpp
// ============================================================================

#include "emision/OperatorFeed.hpp"

#include <cstring>

#include "emision/BroadcastEngine.hpp"
#include "emision/FuentesApp.hpp"
#include "Motor.hpp"
#include "ipc/HealthBlock.hpp"

#include <nlohmann/json.hpp>

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

motor::Motor* motorAndando() {
    BroadcastEngine* e = fuentes::motorApp();
    return e != nullptr ? e->motor() : nullptr;
}

} // namespace

OperatorFeed::~OperatorFeed() { shutdown(); }

void OperatorFeed::shutdown() {
    stop();
    if (motor::Motor* m = motorAndando())
        for (const auto& n : lobbyMonitors_) m->quitarEntrada(n);
    lobbyMonitors_.clear();
}

bool OperatorFeed::configure(BroadcastEngine& engine, const std::string& videoSource,
                             const std::string& audioSource) {
    bool ok = true;
    motor::Motor* m = engine.motor();

    // --- Video ------------------------------------------------------------------
    if (videoSource != videoName_ || (vista_.empty() && !videoSource.empty())) {
        stopVideo();
        videoName_ = videoSource;
        if (!videoSource.empty()) {
            if (m == nullptr || !engine.sourceExists(videoSource) ||
                !sender_.publish(L"BA.Operator", ba::ipc::SlotConfig{kWidth, kHeight})) {
                ok = false;
            } else {
                // Una vista propia de la entrada, a 640x360 con bandas (no toca el programa).
                vista_ = "entrada:" + videoSource + "@operador";
                if (!m->verPedir(vista_, kWidth, kHeight).empty()) {
                    vista_.clear();
                    ok = false;
                } else {
                    m->verAvisar(vista_, [this](const motor::Cuadro& c) {
                        std::lock_guard<std::mutex> l(videoLock_);
                        if (!sender_.published() || c.ancho != kWidth || c.alto != kHeight) return;
                        const std::size_t row = static_cast<std::size_t>(kWidth) * 4u;
                        const std::uint8_t* px = c.bgra.data();
                        if (static_cast<std::size_t>(c.stride) != row) {
                            packed_.resize(row * kHeight);
                            for (int y = 0; y < kHeight; ++y)
                                std::memcpy(packed_.data() + y * row, c.bgra.data() + static_cast<std::size_t>(y) * c.stride, row);
                            px = packed_.data();
                        }
                        ba::ipc::FrameMeta meta;
                        meta.frameIndex = ++frameIndex_;
                        meta.qpc = ba::ipc::HealthBlock::nowQpc();
                        sender_.send(px, row * kHeight, meta);
                    });
                }
            }
        }
    }

    // --- Audio ------------------------------------------------------------------
    if (audioSource != audioName_ || (tomaAudio_ == 0 && !audioSource.empty())) {
        stopAudio();
        audioName_ = audioSource;
        if (!audioSource.empty()) {
            if (m == nullptr || !engine.sourceExists(audioSource)) {
                ok = false;
            } else {
                if (!ring_.header()) {
                    ba::ipc::SharedAudioRing::init(L"BA.Audio.Operator", 2,
                                                   ba::ipc::audio_ring::kDefaultCapacityBlocks, selfPid(), 1,
                                                   ring_);
                }
                frecuencia_ = 48000;
                // Su entrada después de su cadena (silenciada en la app = ceros).
                tomaAudio_ = m->escucharEntrada(audioSource, [this](const float* d, std::size_t n) { onAudio(d, n); });
                if (tomaAudio_ == 0) ok = false;
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
    if (!vista_.empty()) {
        if (motor::Motor* m = motorAndando()) {
            m->verAvisar(vista_, nullptr);
            m->verSoltar(vista_);
        }
        vista_.clear();
    }
    std::lock_guard<std::mutex> l(videoLock_);
    sender_ = ba::ipc::SpoutSender{}; // B ve el canal caido y pone la placa
}

void OperatorFeed::stopAudio() {
    if (tomaAudio_ != 0) {
        if (motor::Motor* m = motorAndando()) m->dejarDeEscucharEntrada(tomaAudio_);
        tomaAudio_ = 0;
    }
    std::lock_guard<std::mutex> lock(audioLock_);
    audioBuf_.clear();
}

void OperatorFeed::setDictado(Dictado d) {
    std::lock_guard<std::mutex> lock(dictadoLock_);
    dictado_ = std::move(d);
}

void OperatorFeed::onAudio(const float* d, std::size_t frames) {
    if (d == nullptr || frames == 0) return;
    // Push-to-talk: la voz va al dictado (el canal izquierdo: un micrófono es
    // mono) y al lobby le llega silencio.
    const bool ptt = ptt_;
    if (ptt) {
        thread_local std::vector<float> mono;
        mono.resize(frames);
        for (std::size_t i = 0; i < frames; ++i) mono[i] = d[2 * i];
        std::lock_guard<std::mutex> lock(dictadoLock_);
        if (dictado_) dictado_(mono.data(), frames, frecuencia_);
    }
    if (!ring_.header()) return;
    std::lock_guard<std::mutex> lock(audioLock_);
    auto& buf = audioBuf_;
    const std::size_t base = buf.size();
    buf.resize(base + frames * 2);
    for (std::size_t i = 0; i < frames * 2; ++i)
        // Hablándole al asistente = silencio para el lobby (el silenciado de la
        // app ya llega en ceros desde la toma).
        buf[base + i] = ptt ? 0.0f : d[i];
    std::size_t pos = 0;
    while (buf.size() - pos >= kBlockFrames * 2) {
        ring_.push(buf.data() + pos, ba::ipc::HealthBlock::nowQpc());
        pos += kBlockFrames * 2;
    }
    buf.erase(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(pos));
}

void OperatorFeed::startLobbyMonitor(int count) {
    if (!lobbyMonitors_.empty()) return;
    motor::Motor* m = motorAndando();
    if (m == nullptr) return;
    for (int k = 1; k <= count && k <= kMaxLobby; ++k) {
        const std::string name = "ba-lobby-monitor-" + std::to_string(k);
        const nlohmann::json aj{{"pipeline", ""},
                                {"pipeline_audio", "baanillo canal=BA.Audio.Lobby." + std::to_string(k)},
                                {"audio", true},
                                {"estudio", false},
                                {"nombre", name}};
        // Interna (fuera del multiview) y en ninguna pista: solo a los auriculares.
        if (m->agregarEntrada(name, aj, nlohmann::json{{"interna", true}, {"global", false}, {"pistas", nlohmann::json::array()}})
                .empty())
            lobbyMonitors_.push_back(name);
    }
}

void OperatorFeed::setLobbyMonitor(int slot, bool on) {
    if (slot < 1 || slot > static_cast<int>(lobbyMonitors_.size())) return;
    if (motor::Motor* m = motorAndando()) m->ponerEscucha(lobbyMonitors_[static_cast<std::size_t>(slot - 1)], on);
}

} // namespace ba::app
