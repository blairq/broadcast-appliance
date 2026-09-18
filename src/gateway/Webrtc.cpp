// ============================================================================
// src/gateway/Webrtc.cpp — Sesiones WebRTC (libwebrtc M123 real + backend NULL)
//
// Backend REAL (BA_GATEWAY_HAVE_WEBRTC=1):
//   - B es el ANSWERER. SetRemoteDescription(offer) crea los transceivers que
//     ofrece el navegador; recien DESPUES se agregan los tracks de retorno con
//     AddTrack, que se enganchan a esos transceivers (un answerer no puede
//     agregar m-lines propias: por eso nada de AddTransceiver).
//   - Cada offer (el primero y los re-offers al sumar/quitar la pantalla
//     compartida) se contesta: SRD -> CreateAnswer -> SLD -> onLocalAnswer.
//     Los candidatos ICE van por goteo (trickle) en ambos sentidos.
//   - Recepcion: OnTrack engancha sinks. El PRIMER track de video es la
//     camara (onRemoteVideo); cualquier otro, la pantalla (onRemoteScreen). El
//     audio llega como int16 al sink del track remoto y sale como float32
//     estereo (onRemoteAudio).
//   - Audio remoto: libwebrtc solo decodifica el audio recibido cuando alguien
//     "tira" de la reproduccion. B no tiene parlantes: PullAdm es un modulo de
//     audio sin dispositivo que tira del mix cada 10 ms en su propio thread.
//   - Envio: fuentes locales alimentadas por el MediaPump (BGRA -> I420 con
//     libyuv; audio float32 -> int16).
//
// Backend NULL (BA_GATEWAY_HAVE_WEBRTC=0): maquina de estados que completa la
// senalizacion sin RTP; sirve para testear el servidor sin el SDK.
//
// Threading: los callbacks de libwebrtc llegan desde SUS threads (signaling,
// worker); SessionCallbacks debe ser thread-safe. Ninguna excepcion cruza un
// callback de libwebrtc.
// ============================================================================

#include "Webrtc.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#if BA_GATEWAY_HAVE_WEBRTC
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/create_peerconnection_factory.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/notifier.h"
#include "api/peer_connection_interface.h"
#include "api/rtp_transceiver_interface.h"
#include "api/scoped_refptr.h"
#include "api/set_local_description_observer_interface.h"
#include "api/set_remote_description_observer_interface.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"
#include "api/video_codecs/video_decoder_factory_template.h"
#include "api/video_codecs/video_decoder_factory_template_dav1d_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_open_h264_adapter.h"
#include "api/video_codecs/video_encoder_factory_template.h"
#include "api/video_codecs/video_encoder_factory_template_libaom_av1_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_open_h264_adapter.h"
#include "media/base/adapted_video_track_source.h"
#include "modules/audio_device/include/audio_device.h"
#include "modules/audio_device/include/audio_device_default.h"
#include "rtc_base/logging.h"
#include "rtc_base/ssl_adapter.h"
#include "rtc_base/thread.h"
#include "rtc_base/time_utils.h"
#include "third_party/libyuv/include/libyuv/convert.h"
#include "third_party/libyuv/include/libyuv/convert_argb.h"
#endif

namespace ba::gateway {

using json = nlohmann::json;

#if !BA_GATEWAY_HAVE_WEBRTC
// ============================================================================
// Backend NULL
// ============================================================================
struct WebrtcSession::Impl {
    int slot = 0;
    SessionCallbacks cb;
    bool configured = false;
    bool connected = false;
    bool closed = false;
    std::atomic<uint64_t> localAudioBlocks{0};
    std::atomic<uint64_t> localVideoFrames{0};

    bool configure(const SessionCallbacks& c) {
        cb = c;
        configured = true;
        // configure() se llama de nuevo en cada reconexion: se limpia el estado
        // para que la sesion no quede muerta tras el primer cierre.
        closed = false;
        connected = false;
        localAudioBlocks.store(0);
        localVideoFrames.store(0);
        return true;
    }
    bool setRemoteOffer(const std::string&) {
        if (!configured || closed) return false;
        const bool first = !connected;
        connected = true;
        // Answer minimo coherente (el backend real lo genera libwebrtc).
        if (cb.onLocalAnswer) {
            cb.onLocalAnswer("v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n"
                             "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\na=rtpmap:111 opus/48000/2\r\n"
                             "m=video 9 UDP/TLS/RTP/SAVPF 96\r\na=rtpmap:96 VP8/90000\r\n");
        }
        if (first && cb.onStateChange) cb.onStateChange(true);
        return true;
    }
    bool addRemoteIce(const std::string&) { return configured && !closed; }
    void pushLocalAudio(const float*, size_t n, int) {
        if (n > 0 && connected) localAudioBlocks.fetch_add(1, std::memory_order_relaxed);
    }
    void pushLocalVideo(const uint8_t*, size_t bytes, int, int) {
        if (bytes > 0 && connected) localVideoFrames.fetch_add(1, std::memory_order_relaxed);
    }
    void close() {
        if (closed) return;
        closed = true;
        const bool was = connected;
        connected = false;
        if (was && cb.onStateChange) cb.onStateChange(false);
    }
};

#else
// ============================================================================
// Backend REAL (libwebrtc)
// ============================================================================
namespace {

constexpr int kAudioRate = 48000;
constexpr int kAudioChannels = 2;
constexpr size_t kFramesPer10Ms = kAudioRate / 100;

/// Modulo de audio SIN dispositivo que tira de la reproduccion cada 10 ms.
///
/// Sin esto libwebrtc nunca decodifica el audio remoto (NetEq se consume desde
/// la reproduccion del ADM) y los sinks de los tracks remotos no reciben nada.
/// No captura: el audio que B envia sale de fuentes propias (LocalAudioSource).
class PullAdm : public webrtc::webrtc_impl::AudioDeviceModuleDefault<webrtc::AudioDeviceModule> {
   public:
    ~PullAdm() override { StopPlayout(); }

    int32_t RegisterAudioCallback(webrtc::AudioTransport* t) override {
        std::lock_guard<std::mutex> lock(m_);
        transport_ = t;
        return 0;
    }
    int32_t PlayoutIsAvailable(bool* available) override {
        *available = true;
        return 0;
    }
    int32_t StereoPlayoutIsAvailable(bool* available) const override {
        *available = true;
        return 0;
    }
    int32_t StereoPlayout(bool* enabled) const override {
        *enabled = true;
        return 0;
    }
    int32_t RecordingIsAvailable(bool* available) override {
        *available = false;
        return 0;
    }
    int32_t StartPlayout() override {
        if (playing_.exchange(true)) return 0;
        thread_ = std::thread([this]() { loop(); });
        return 0;
    }
    int32_t StopPlayout() override {
        if (!playing_.exchange(false)) return 0;
        if (thread_.joinable()) thread_.join();
        return 0;
    }
    bool Playing() const override { return playing_.load(); }

   private:
    void loop() {
        std::vector<int16_t> buf(kFramesPer10Ms * kAudioChannels);
        auto next = std::chrono::steady_clock::now();
        while (playing_.load()) {
            next += std::chrono::milliseconds(10);
            {
                std::lock_guard<std::mutex> lock(m_);
                if (transport_ != nullptr) {
                    size_t out = 0;
                    int64_t elapsed = 0;
                    int64_t ntp = 0;
                    transport_->NeedMorePlayData(kFramesPer10Ms, sizeof(int16_t) * kAudioChannels,
                                                 kAudioChannels, kAudioRate, buf.data(), out, &elapsed, &ntp);
                }
            }
            std::this_thread::sleep_until(next);
        }
    }

    std::mutex m_;
    webrtc::AudioTransport* transport_ = nullptr;
    std::atomic<bool> playing_{false};
    std::thread thread_;
};

/// Fuente de video de retorno: BGRA -> I420 (libyuv) -> sinks.
class LocalVideoSource : public rtc::AdaptedVideoTrackSource {
   public:
    void AddFrame(const uint8_t* bgra, int w, int h) {
        if (bgra == nullptr || w <= 0 || h <= 0) return;
        rtc::scoped_refptr<webrtc::I420Buffer> i420 = webrtc::I420Buffer::Create(w, h);
        // libyuv "ARGB" es B,G,R,A en memoria: justamente BGRA.
        libyuv::ARGBToI420(bgra, w * 4, i420->MutableDataY(), i420->StrideY(), i420->MutableDataU(),
                           i420->StrideU(), i420->MutableDataV(), i420->StrideV(), w, h);
        OnFrame(webrtc::VideoFrame::Builder()
                    .set_video_frame_buffer(i420)
                    .set_timestamp_us(rtc::TimeMicros())
                    .set_rotation(webrtc::kVideoRotation_0)
                    .build());
    }
    SourceState state() const override { return kLive; }
    bool remote() const override { return false; }
    bool is_screencast() const override { return false; }
    absl::optional<bool> needs_denoising() const override { return false; }
};

/// Fuente de audio de retorno: float32 intercalado -> int16 -> sinks.
class LocalAudioSource : public webrtc::Notifier<webrtc::AudioSourceInterface> {
   public:
    void AddFrame(const float* samples, size_t n, int rate, int channels) {
        if (samples == nullptr || n == 0 || rate <= 0 || channels <= 0) return;
        const size_t frames = n / static_cast<size_t>(channels);
        pcm_.resize(n);
        for (size_t i = 0; i < n; ++i) {
            const float v = samples[i] > 1.0f ? 1.0f : (samples[i] < -1.0f ? -1.0f : samples[i]);
            pcm_[i] = static_cast<int16_t>(v * 32767.0f);
        }
        std::lock_guard<std::mutex> lock(m_);
        for (auto* s : sinks_)
            s->OnData(pcm_.data(), 16, rate, static_cast<size_t>(channels), frames);
    }
    SourceState state() const override { return kLive; }
    bool remote() const override { return false; }
    void AddSink(webrtc::AudioTrackSinkInterface* s) override {
        std::lock_guard<std::mutex> lock(m_);
        if (std::find(sinks_.begin(), sinks_.end(), s) == sinks_.end()) sinks_.push_back(s);
    }
    void RemoveSink(webrtc::AudioTrackSinkInterface* s) override {
        std::lock_guard<std::mutex> lock(m_);
        sinks_.erase(std::remove(sinks_.begin(), sinks_.end(), s), sinks_.end());
    }

   private:
    std::mutex m_;
    std::vector<webrtc::AudioTrackSinkInterface*> sinks_;
    std::vector<int16_t> pcm_; // solo lo usa el thread del MediaPump
};

/// Sink de video remoto: I420 (rotado si hace falta) -> BGRA -> callback.
class RemoteVideoSink : public rtc::VideoSinkInterface<webrtc::VideoFrame> {
   public:
    using Fn = std::function<void(const uint8_t*, size_t, int, int)>;
    explicit RemoteVideoSink(Fn fn) : fn_(std::move(fn)) {}
    void OnFrame(const webrtc::VideoFrame& frame) override {
        if (!fn_) return;
        rtc::scoped_refptr<webrtc::I420BufferInterface> i420 = frame.video_frame_buffer()->ToI420();
        if (!i420) return;
        if (frame.rotation() != webrtc::kVideoRotation_0) {
            i420 = webrtc::I420Buffer::Rotate(*i420, frame.rotation());
        }
        const int w = i420->width();
        const int h = i420->height();
        bgra_.resize(static_cast<size_t>(w) * h * 4);
        libyuv::I420ToARGB(i420->DataY(), i420->StrideY(), i420->DataU(), i420->StrideU(), i420->DataV(),
                           i420->StrideV(), bgra_.data(), w * 4, w, h);
        fn_(bgra_.data(), bgra_.size(), w, h);
    }

   private:
    Fn fn_;
    std::vector<uint8_t> bgra_;
};

/// Sink de audio remoto: int16 (mono o estereo) -> float32 estereo -> callback.
class RemoteAudioSink : public webrtc::AudioTrackSinkInterface {
   public:
    using Fn = std::function<void(const float*, size_t, int)>;
    explicit RemoteAudioSink(Fn fn) : fn_(std::move(fn)) {}
    void OnData(const void* data, int bits, int rate, size_t channels, size_t frames) override {
        if (!fn_ || data == nullptr || bits != 16 || channels == 0) return;
        const auto* in = static_cast<const int16_t*>(data);
        out_.resize(frames * 2);
        for (size_t f = 0; f < frames; ++f) {
            const float l = in[f * channels] / 32768.0f;
            const float r = channels > 1 ? in[f * channels + 1] / 32768.0f : l;
            out_[f * 2] = l;
            out_[f * 2 + 1] = r;
        }
        fn_(out_.data(), out_.size(), rate);
    }

   private:
    Fn fn_;
    std::vector<float> out_;
};

/// Estado compartido de la factory (lo guarda el hub como void*).
struct RtcHubState {
    uint16_t minPort = 0;
    uint16_t maxPort = 0;
    std::unique_ptr<rtc::Thread> network;
    std::unique_ptr<rtc::Thread> worker;
    std::unique_ptr<rtc::Thread> signaling;
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory;
};

template <typename Fn> class SrdObserver : public webrtc::SetRemoteDescriptionObserverInterface {
   public:
    explicit SrdObserver(Fn fn) : fn_(std::move(fn)) {}
    void OnSetRemoteDescriptionComplete(webrtc::RTCError e) override { fn_(e); }

   private:
    Fn fn_;
};

template <typename Fn> class SldObserver : public webrtc::SetLocalDescriptionObserverInterface {
   public:
    explicit SldObserver(Fn fn) : fn_(std::move(fn)) {}
    void OnSetLocalDescriptionComplete(webrtc::RTCError e) override { fn_(e); }

   private:
    Fn fn_;
};

} // namespace

struct WebrtcSession::Impl : public webrtc::PeerConnectionObserver {
    int slot = 0;
    SessionCallbacks cb;
    std::mutex m;
    RtcHubState* hub = nullptr;
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc;
    rtc::scoped_refptr<LocalVideoSource> videoSource;
    rtc::scoped_refptr<LocalAudioSource> audioSource;
    bool tracksAdded = false;
    std::string cameraMid;
    std::vector<std::unique_ptr<RemoteVideoSink>> videoSinks;
    std::vector<std::unique_ptr<RemoteAudioSink>> audioSinks;
    std::vector<std::pair<rtc::scoped_refptr<webrtc::VideoTrackInterface>, RemoteVideoSink*>> videoAttached;
    std::vector<std::pair<rtc::scoped_refptr<webrtc::AudioTrackInterface>, RemoteAudioSink*>> audioAttached;
    std::atomic<bool> connected{false};
    std::atomic<uint64_t> localAudioBlocks{0};
    std::atomic<uint64_t> localVideoFrames{0};

    bool configure(const SessionCallbacks& c, void* factory) {
        close();
        std::lock_guard<std::mutex> lock(m);
        cb = c;
        hub = static_cast<RtcHubState*>(factory);
        if (hub == nullptr || !hub->factory) return false;
        webrtc::PeerConnectionInterface::RTCConfiguration config;
        config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
        config.bundle_policy = webrtc::PeerConnectionInterface::kBundlePolicyMaxBundle;
        config.rtcp_mux_policy = webrtc::PeerConnectionInterface::kRtcpMuxPolicyRequire;
        if (hub->minPort != 0) {
            config.port_allocator_config.min_port = hub->minPort;
            config.port_allocator_config.max_port = hub->maxPort;
        }
        // Sin STUN/TURN: B atiende en la LAN (candidatos host) y, con el tunel,
        // en su IP publica. El medio no pasa por Cloudflare (spec §8.4).
        webrtc::PeerConnectionDependencies deps(this);
        auto res = hub->factory->CreatePeerConnectionOrError(config, std::move(deps));
        if (!res.ok()) return false;
        pc = res.MoveValue();
        videoSource = rtc::make_ref_counted<LocalVideoSource>();
        audioSource = rtc::make_ref_counted<LocalAudioSource>();
        tracksAdded = false;
        cameraMid.clear();
        return true;
    }

    /// Tracks de retorno: DESPUES de aplicar el offer, para engancharse a sus
    /// transceivers (el primero de audio y el primero de video).
    void addReturnTracks() {
        if (tracksAdded || !pc) return;
        tracksAdded = true;
        auto v = hub->factory->CreateVideoTrack(videoSource, "ba-return-video");
        auto a = hub->factory->CreateAudioTrack("ba-return-audio", audioSource.get());
        pc->AddTrack(a, {"ba-return"});
        pc->AddTrack(v, {"ba-return"});
    }

    bool setRemoteOffer(const std::string& sdp) {
        rtc::scoped_refptr<webrtc::PeerConnectionInterface> p;
        {
            std::lock_guard<std::mutex> lock(m);
            p = pc;
        }
        if (!p) return false;
        webrtc::SdpParseError err;
        std::unique_ptr<webrtc::SessionDescriptionInterface> offer =
            webrtc::CreateSessionDescription(webrtc::SdpType::kOffer, sdp, &err);
        if (!offer) return false;
        auto self = this;
        auto onSld = [self, p](webrtc::RTCError e) {
            if (!e.ok() || p->local_description() == nullptr) return;
            {
                // Una PC ya reemplazada (reconexion) no contesta por la nueva.
                std::lock_guard<std::mutex> lock(self->m);
                if (self->pc != p) return;
            }
            std::string answer;
            p->local_description()->ToString(&answer);
            if (self->cb.onLocalAnswer) self->cb.onLocalAnswer(answer);
        };
        auto onSrd = [self, p, onSld](webrtc::RTCError e) {
            if (!e.ok()) return;
            {
                std::lock_guard<std::mutex> lock(self->m);
                if (self->pc != p) return;
                self->addReturnTracks();
            }
            // SLD sin descripcion = crea y aplica el answer implicito.
            p->SetLocalDescription(rtc::make_ref_counted<SldObserver<decltype(onSld)>>(onSld));
        };
        p->SetRemoteDescription(std::move(offer), rtc::make_ref_counted<SrdObserver<decltype(onSrd)>>(onSrd));
        return true;
    }

    bool addRemoteIce(const std::string& cand) {
        rtc::scoped_refptr<webrtc::PeerConnectionInterface> p;
        {
            std::lock_guard<std::mutex> lock(m);
            p = pc;
        }
        if (!p) return false;
        std::string line = cand;
        std::string mid;
        int index = 0;
        const json j = json::parse(cand, nullptr, false);
        if (j.is_object()) {
            line = j.value("candidate", "");
            if (j.contains("sdpMid") && j["sdpMid"].is_string()) mid = j["sdpMid"].get<std::string>();
            if (j.contains("sdpMLineIndex") && j["sdpMLineIndex"].is_number())
                index = j["sdpMLineIndex"].get<int>();
        }
        if (line.empty()) return true; // fin de candidatos
        webrtc::SdpParseError err;
        std::unique_ptr<webrtc::IceCandidateInterface> c(webrtc::CreateIceCandidate(mid, index, line, &err));
        if (!c) return false;
        p->AddIceCandidate(std::move(c), [](webrtc::RTCError) {});
        return true;
    }

    void pushLocalAudio(const float* s, size_t n, int rate) {
        if (!connected.load(std::memory_order_relaxed) || !audioSource) return;
        audioSource->AddFrame(s, n, rate, kAudioChannels);
        localAudioBlocks.fetch_add(1, std::memory_order_relaxed);
    }

    void pushLocalVideo(const uint8_t* px, size_t, int w, int h) {
        if (!connected.load(std::memory_order_relaxed) || !videoSource) return;
        videoSource->AddFrame(px, w, h);
        localVideoFrames.fetch_add(1, std::memory_order_relaxed);
    }

    void close() {
        rtc::scoped_refptr<webrtc::PeerConnectionInterface> p;
        {
            std::lock_guard<std::mutex> lock(m);
            p = std::move(pc);
            pc = nullptr;
        }
        if (!p) return;
        for (auto& [t, s] : videoAttached)
            t->RemoveSink(s);
        for (auto& [t, s] : audioAttached)
            t->RemoveSink(s);
        videoAttached.clear();
        audioAttached.clear();
        p->Close(); // sincronico: despues no llegan mas callbacks de esta PC
        videoSinks.clear();
        audioSinks.clear();
        if (connected.exchange(false) && cb.onStateChange) cb.onStateChange(false);
    }

    // --- PeerConnectionObserver ------------------------------------------------
    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState) override {}
    void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface>) override {}
    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState) override {}

    void OnIceCandidate(const webrtc::IceCandidateInterface* c) override {
        if (c == nullptr || !cb.onLocalIce) return;
        std::string line;
        if (!c->ToString(&line)) return;
        cb.onLocalIce(
            json{{"candidate", line}, {"sdpMid", c->sdp_mid()}, {"sdpMLineIndex", c->sdp_mline_index()}}
                .dump());
    }

    void OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState s) override {
        const bool up = s == webrtc::PeerConnectionInterface::PeerConnectionState::kConnected;
        const bool down = s == webrtc::PeerConnectionInterface::PeerConnectionState::kFailed ||
                          s == webrtc::PeerConnectionInterface::PeerConnectionState::kClosed ||
                          s == webrtc::PeerConnectionInterface::PeerConnectionState::kDisconnected;
        if (up && !connected.exchange(true) && cb.onStateChange) cb.onStateChange(true);
        if (down && connected.exchange(false) && cb.onStateChange) cb.onStateChange(false);
    }

    void OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> t) override {
        auto track = t->receiver()->track();
        if (!track) return;
        if (track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
            const std::string mid = t->mid().value_or("");
            if (cameraMid.empty()) cameraMid = mid;
            const bool camera = mid == cameraMid;
            auto sink = std::make_unique<RemoteVideoSink>(camera ? cb.onRemoteVideo : cb.onRemoteScreen);
            auto* video = static_cast<webrtc::VideoTrackInterface*>(track.get());
            video->AddOrUpdateSink(sink.get(), rtc::VideoSinkWants());
            videoAttached.emplace_back(rtc::scoped_refptr<webrtc::VideoTrackInterface>(video), sink.get());
            videoSinks.push_back(std::move(sink));
        } else if (track->kind() == webrtc::MediaStreamTrackInterface::kAudioKind) {
            auto sink = std::make_unique<RemoteAudioSink>(cb.onRemoteAudio);
            auto* audio = static_cast<webrtc::AudioTrackInterface*>(track.get());
            audio->AddSink(sink.get());
            audioAttached.emplace_back(rtc::scoped_refptr<webrtc::AudioTrackInterface>(audio), sink.get());
            audioSinks.push_back(std::move(sink));
        }
    }
};
#endif // BA_GATEWAY_HAVE_WEBRTC

// ============================================================================
// WebrtcSession (API comun)
// ============================================================================

WebrtcSession::WebrtcSession(int slot, int, int, int, int) : impl_(new Impl()) { impl_->slot = slot; }
WebrtcSession::WebrtcSession() : impl_(new Impl()) {}
WebrtcSession::~WebrtcSession() {
    if (impl_ != nullptr) {
        impl_->close();
        delete impl_;
    }
}
WebrtcSession::WebrtcSession(WebrtcSession&& o) noexcept : impl_(o.impl_) { o.impl_ = nullptr; }
WebrtcSession& WebrtcSession::operator=(WebrtcSession&& o) noexcept {
    if (this != &o) {
        if (impl_ != nullptr) {
            impl_->close();
            delete impl_;
        }
        impl_ = o.impl_;
        o.impl_ = nullptr;
    }
    return *this;
}

bool WebrtcSession::configure(const SessionCallbacks& cb, void* factory) {
#if BA_GATEWAY_HAVE_WEBRTC
    return impl_ != nullptr && impl_->configure(cb, factory);
#else
    (void)factory;
    return impl_ != nullptr && impl_->configure(cb);
#endif
}
bool WebrtcSession::setRemoteOffer(const std::string& sdp) {
    return impl_ != nullptr && impl_->setRemoteOffer(sdp);
}
bool WebrtcSession::addRemoteIce(const std::string& c) { return impl_ != nullptr && impl_->addRemoteIce(c); }
void WebrtcSession::pushLocalAudio(const float* s, size_t n, int rate) {
    if (impl_ != nullptr) impl_->pushLocalAudio(s, n, rate);
}
void WebrtcSession::pushLocalVideo(const uint8_t* p, size_t bytes, int w, int h) {
    if (impl_ != nullptr) impl_->pushLocalVideo(p, bytes, w, h);
}
bool WebrtcSession::connected() const { return impl_ != nullptr && impl_->connected; }
void WebrtcSession::close() {
    if (impl_ != nullptr) impl_->close();
}
int WebrtcSession::slot() const { return impl_ != nullptr ? impl_->slot : 0; }
uint64_t WebrtcSession::localAudioBlocks() const {
    return impl_ != nullptr ? impl_->localAudioBlocks.load() : 0;
}
uint64_t WebrtcSession::localVideoFrames() const {
    return impl_ != nullptr ? impl_->localVideoFrames.load() : 0;
}

// ============================================================================
// WebrtcHub
// ============================================================================

WebrtcHub::WebrtcHub(int guests, int videoW, int videoH, int audioRate, int audioChannels)
    : guests_(guests), videoW_(videoW), videoH_(videoH), audioRate_(audioRate),
      audioChannels_(audioChannels) {
    sessions_.resize(static_cast<size_t>(guests) + 1);
    for (int s = 1; s <= guests; ++s)
        sessions_[static_cast<size_t>(s)] = WebrtcSession(s, videoW, videoH, audioRate, audioChannels);
#if BA_GATEWAY_HAVE_WEBRTC
    rtc::LogMessage::LogToDebug(rtc::LS_ERROR);
    rtc::InitializeSSL();
    auto* st = new RtcHubState();
    st->network = rtc::Thread::CreateWithSocketServer();
    st->worker = rtc::Thread::Create();
    st->signaling = rtc::Thread::Create();
    st->network->SetName("ba-rtc-net", nullptr);
    st->worker->SetName("ba-rtc-work", nullptr);
    st->signaling->SetName("ba-rtc-sig", nullptr);
    st->network->Start();
    st->worker->Start();
    st->signaling->Start();
    // El ADM se crea en el worker (asi lo espera libwebrtc).
    rtc::scoped_refptr<webrtc::AudioDeviceModule> adm = st->worker->BlockingCall(
        []() { return rtc::scoped_refptr<webrtc::AudioDeviceModule>(rtc::make_ref_counted<PullAdm>()); });
    st->factory = webrtc::CreatePeerConnectionFactory(
        st->network.get(), st->worker.get(), st->signaling.get(), adm,
        webrtc::CreateBuiltinAudioEncoderFactory(), webrtc::CreateBuiltinAudioDecoderFactory(),
        std::make_unique<webrtc::VideoEncoderFactoryTemplate<
            webrtc::LibvpxVp8EncoderTemplateAdapter, webrtc::LibvpxVp9EncoderTemplateAdapter,
            webrtc::OpenH264EncoderTemplateAdapter, webrtc::LibaomAv1EncoderTemplateAdapter>>(),
        std::make_unique<webrtc::VideoDecoderFactoryTemplate<
            webrtc::LibvpxVp8DecoderTemplateAdapter, webrtc::LibvpxVp9DecoderTemplateAdapter,
            webrtc::OpenH264DecoderTemplateAdapter, webrtc::Dav1dDecoderTemplateAdapter>>(),
        nullptr /* mixer por defecto */, nullptr /* sin APM: B no captura audio */);
    if (st->factory) {
        // Loopback solo para pruebas en una sola maquina (tests): en produccion
        // un candidato 127.0.0.1 no le sirve a ningun invitado.
        if (const char* lb = std::getenv("BA_WEBRTC_ALLOW_LOOPBACK"); lb != nullptr && lb[0] == '1') {
            webrtc::PeerConnectionFactoryInterface::Options opts;
            opts.network_ignore_mask = 0;
            st->factory->SetOptions(opts);
        }
        rtcFactory_ = st;
    } else {
        st->signaling->Stop();
        st->worker->Stop();
        st->network->Stop();
        delete st;
    }
#endif
}

WebrtcHub::~WebrtcHub() {
    closeAll();
#if BA_GATEWAY_HAVE_WEBRTC
    sessions_.clear(); // antes que la factory: las PC la referencian
    if (auto* st = static_cast<RtcHubState*>(rtcFactory_)) {
        st->factory = nullptr;
        st->signaling->Stop();
        st->worker->Stop();
        st->network->Stop();
        delete st;
        rtcFactory_ = nullptr;
    }
    rtc::CleanupSSL();
#endif
}

void WebrtcHub::setNetwork(uint16_t minPort, uint16_t maxPort, bool allowLoopback) {
#if BA_GATEWAY_HAVE_WEBRTC
    auto* st = static_cast<RtcHubState*>(rtcFactory_);
    if (st == nullptr) return;
    st->minPort = minPort;
    st->maxPort = maxPort;
    if (allowLoopback) {
        webrtc::PeerConnectionFactoryInterface::Options opts;
        opts.network_ignore_mask = 0;
        st->factory->SetOptions(opts);
    }
#else
    (void)minPort;
    (void)maxPort;
    (void)allowLoopback;
#endif
}

WebrtcSession* WebrtcHub::session(int idx) {
    if (idx < 1 || idx > guests_) return nullptr;
    return &sessions_[static_cast<size_t>(idx)];
}

void WebrtcHub::closeAll() {
    for (auto& s : sessions_)
        s.close();
}

} // namespace ba::gateway
