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
//     libyuv UNA vez por cuadro, compartido por las sesiones: CuadroSaliente;
//     audio float32 -> int16).
//   - Espectadores (relay): cada receptor de video de un invitado lleva un
//     TapTransformer entre el depacketizador y el decodificador: copia los
//     bytes CODIFICADOS del cuadro al Feed de esa sesion y deja pasar el
//     original al decodificador (A sigue recibiendo pixeles). Cada espectador
//     es una PeerConnection propia cuyo sender tiene un ViewerTransformer en
//     lugar de encoder: el Feed le inyecta los cuadros reenviados (mismos
//     bytes, payload type del espectador). La fuente de ese sender no emite
//     nunca, asi que B no codifica nada para los espectadores.
//
// Backend NULL (BA_GATEWAY_HAVE_WEBRTC=0): maquina de estados que completa la
// senalizacion sin RTP; sirve para testear el servidor sin el SDK.
//
// Threading: los callbacks de libwebrtc llegan desde SUS threads (signaling,
// worker); SessionCallbacks debe ser thread-safe. Ninguna excepcion cruza un
// callback de libwebrtc.
// ============================================================================

#include "Webrtc.hpp"
#include "ajustes/Ajustes.hpp"
#include "platform/Hilo.hpp"
#include "platform/TiempoReal.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
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
#include "api/frame_transformer_interface.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/notifier.h"
#include "api/peer_connection_interface.h"
#include "api/rtp_transceiver_interface.h"
#include "api/scoped_refptr.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtcstats_objects.h"
#include "api/set_local_description_observer_interface.h"
#include "api/set_remote_description_observer_interface.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_metadata.h"
#include "api/video/video_sink_interface.h"
#include "api/video_codecs/video_decoder_factory_template.h"
#include "api/video_codecs/video_decoder_factory_template_dav1d_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_open_h264_adapter.h"
#include "api/video_codecs/video_encoder_factory_template.h"
#ifdef __APPLE__
#include "CodecsMac.hpp"
#endif
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
    void pushLocalVideo() {
        if (connected) localVideoFrames.fetch_add(1, std::memory_order_relaxed);
    }
    void close() {
        if (closed) return;
        closed = true;
        const bool was = connected;
        connected = false;
        if (was && cb.onStateChange) cb.onStateChange(false);
    }
    std::string estadisticas() { return "{}"; }
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
        thread_ = std::thread([this]() {
            ba::platform::nombrarHilo("webrtc-playout");
            ba::platform::tiempoRealAudio(10); // tira del audio cada 10 ms: con plazo
            loop();
        });
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

/// Fuente de video de retorno: un cuadro ya en I420 (CuadroSaliente, el mismo buffer para
/// todas las sesiones) -> sinks.
class LocalVideoSource : public rtc::AdaptedVideoTrackSource {
   public:
    void AddFrame(const rtc::scoped_refptr<webrtc::VideoFrameBuffer>& i420) {
        if (!i420) return;
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

int64_t steadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string upper(std::string s) {
    for (char& ch : s)
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return s;
}

// --- Relay de video codificado (espectadores) --------------------------------

/// Cuadro reenviado a un espectador: los bytes tal como llegaron del invitado
/// (compartidos entre todos los espectadores de ese cuadro) y sus metadatos.
/// Se presenta como cuadro "recibido": el sender lo empaqueta a partir de los
/// metadatos (RTPVideoHeader::FromMetadata), sin pasar por ningun encoder. El
/// payload type es el que negocio ESE espectador para el codec.
class ForwardFrame : public webrtc::TransformableVideoFrameInterface {
   public:
    ForwardFrame(std::shared_ptr<const std::vector<uint8_t>> data, webrtc::VideoFrameMetadata meta,
                 uint8_t pt, uint32_t rtpTs, uint32_t ssrc, bool key, std::string mime)
        : data_(std::move(data)), meta_(std::move(meta)), pt_(pt), ts_(rtpTs), ssrc_(ssrc), key_(key),
          mime_(std::move(mime)) {}
    rtc::ArrayView<const uint8_t> GetData() const override { return {data_->data(), data_->size()}; }
    void SetData(rtc::ArrayView<const uint8_t> d) override {
        data_ = std::make_shared<const std::vector<uint8_t>>(d.begin(), d.end());
    }
    uint8_t GetPayloadType() const override { return pt_; }
    uint32_t GetSsrc() const override { return ssrc_; }
    uint32_t GetTimestamp() const override { return ts_; }
    void SetRTPTimestamp(uint32_t t) override { ts_ = t; }
    Direction GetDirection() const override { return Direction::kReceiver; }
    std::string GetMimeType() const override { return mime_; }
    bool IsKeyFrame() const override { return key_; }
    webrtc::VideoFrameMetadata Metadata() const override { return meta_; }
    void SetMetadata(const webrtc::VideoFrameMetadata& m) override { meta_ = m; }

   private:
    std::shared_ptr<const std::vector<uint8_t>> data_;
    webrtc::VideoFrameMetadata meta_;
    uint8_t pt_;
    uint32_t ts_;
    uint32_t ssrc_;
    bool key_;
    std::string mime_;
};

/// En el sender de un espectador: no hay encoder que transformar (su fuente
/// no emite); solo guarda el callback del empaquetador para inyectarle los
/// cuadros reenviados.
class ViewerTransformer : public webrtc::FrameTransformerInterface {
   public:
    void Transform(std::unique_ptr<webrtc::TransformableFrameInterface>) override {}
    void RegisterTransformedFrameCallback(rtc::scoped_refptr<webrtc::TransformedFrameCallback> cb) override {
        std::lock_guard<std::mutex> lock(m_);
        cb_ = std::move(cb);
    }
    void RegisterTransformedFrameSinkCallback(rtc::scoped_refptr<webrtc::TransformedFrameCallback> cb,
                                              uint32_t) override {
        std::lock_guard<std::mutex> lock(m_);
        cb_ = std::move(cb);
    }
    void UnregisterTransformedFrameCallback() override {
        std::lock_guard<std::mutex> lock(m_);
        cb_ = nullptr;
    }
    void UnregisterTransformedFrameSinkCallback(uint32_t) override {
        std::lock_guard<std::mutex> lock(m_);
        cb_ = nullptr;
    }
    /// :returns: false si el sender todavia no se engancho (sin SSRC aun).
    bool push(std::unique_ptr<webrtc::TransformableFrameInterface> f) {
        std::lock_guard<std::mutex> lock(m_);
        if (!cb_) return false;
        cb_->OnTransformedFrame(std::move(f)); // encola en el thread del sender
        return true;
    }

   private:
    std::mutex m_;
    rtc::scoped_refptr<webrtc::TransformedFrameCallback> cb_;
};

/// Video de una sesion (camara o pantalla) y sus espectadores.
struct Feed {
    struct Out {
        int viewer = 0;
        rtc::scoped_refptr<ViewerTransformer> tx;
        std::vector<std::pair<std::string, int>> pts; // "VIDEO/VP8" -> payload type del espectador
        bool needKey = true;                          // arranca en un keyframe
        bool ready = false; // su PeerConnection ya conecto (antes, lo enviado se pierde)
        uint64_t frames = 0;
    };
    std::mutex m;
    std::vector<Out> outs;
    // Bytes de video que pasaron por aca. Sirven para saber cuanta red esta
    // usando la sala de verdad, que es lo unico que dice si el limite es el
    // ancho de banda o la maquina. Son atomicos y van FUERA del lock: se
    // escriben en el hilo de red, en cada cuadro.
    std::atomic<uint64_t> bytesEntrada{0};                        ///< lo que mandan los invitados
    std::atomic<uint64_t> bytesSalida{0};                         ///< lo reenviado a los espectadores
    rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> source; // receptor actual (pedir keyframes)
    rtc::Thread* worker = nullptr;
    int64_t lastKeyReqMs = 0;

    /// Pide un keyframe al invitado (PLI/FIR por su receptor). Limitado.
    void requestKeyLocked(bool force = false) {
        const int64_t now = steadyMs();
        if (!source || worker == nullptr || (!force && now - lastKeyReqMs < 500)) return;
        lastKeyReqMs = now;
        rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> src = source;
        worker->PostTask([src]() { src->GenerateKeyFrame(); });
    }
    void requestKey() {
        std::lock_guard<std::mutex> lock(m);
        requestKeyLocked();
    }
    void setSource(rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> s, rtc::Thread* w) {
        std::lock_guard<std::mutex> lock(m);
        source = std::move(s);
        worker = w;
        // PeerConnection nueva del invitado (reconexion): cada espectador
        // vuelve a esperar un keyframe de la nueva.
        for (Out& o : outs)
            o.needKey = true;
        if (!outs.empty()) requestKeyLocked();
    }
    /// La PeerConnection del invitado se cerro: se suelta su fuente (es un
    /// proxy de libwebrtc que debe morir mientras sus threads viven).
    void clearSource() {
        rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> old;
        {
            std::lock_guard<std::mutex> lock(m);
            old = std::move(source);
            source = nullptr;
        }
    }
    void add(int viewer, rtc::scoped_refptr<ViewerTransformer> tx,
             std::vector<std::pair<std::string, int>> pts, bool connected) {
        std::lock_guard<std::mutex> lock(m);
        Out o;
        o.viewer = viewer;
        o.tx = std::move(tx);
        o.pts = std::move(pts);
        o.ready = connected;
        outs.push_back(std::move(o));
        if (connected) requestKeyLocked(true);
    }
    /// La PeerConnection del espectador conecto: desde aca lo enviado llega.
    /// Arranca en un keyframe nuevo (uno anterior pudo perderse antes de DTLS).
    void viewerConnected(int viewer) {
        std::lock_guard<std::mutex> lock(m);
        for (Out& o : outs) {
            if (o.viewer != viewer) continue;
            o.ready = true;
            o.needKey = true;
            requestKeyLocked(true);
        }
    }
    void remove(int viewer) {
        std::lock_guard<std::mutex> lock(m);
        outs.erase(
            std::remove_if(outs.begin(), outs.end(), [viewer](const Out& o) { return o.viewer == viewer; }),
            outs.end());
    }
    /// Un cuadro codificado del invitado: se copia UNA vez y se reparte.
    void forward(webrtc::TransformableVideoFrameInterface& f) {
        // Se cuenta SIEMPRE, aunque no haya ningun espectador: el invitado
        // esta mandando ese video igual y esa red se esta usando.
        const std::size_t tam = f.GetData().size();
        bytesEntrada.fetch_add(tam, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(m);
        if (outs.empty()) return;
        const bool key = f.IsKeyFrame();
        const std::string mime = f.GetMimeType();
        const std::string mimeU = upper(mime);
        std::shared_ptr<const std::vector<uint8_t>> data;
        webrtc::VideoFrameMetadata meta;
        bool waiting = false;
        for (Out& o : outs) {
            if (!o.ready) continue;
            if (o.needKey && !key) {
                waiting = true;
                continue;
            }
            int pt = -1;
            for (const auto& [name, p] : o.pts)
                if (name == mimeU) pt = p;
            if (pt < 0) continue; // el espectador no negocio este codec
            if (!data) {
                const auto d = f.GetData();
                data = std::make_shared<const std::vector<uint8_t>>(d.begin(), d.end());
                meta = f.Metadata();
            }
            if (o.tx->push(std::make_unique<ForwardFrame>(data, meta, static_cast<uint8_t>(pt),
                                                          f.GetTimestamp(), f.GetSsrc(), key, mime))) {
                o.needKey = false;
                ++o.frames;
                bytesSalida.fetch_add(tam, std::memory_order_relaxed);
            }
        }
        if (waiting) requestKeyLocked();
    }
};

/// Entre el depacketizador y el decodificador del video de un invitado: copia
/// al Feed y deja pasar el cuadro original (A lo sigue recibiendo decodificado).
class TapTransformer : public webrtc::FrameTransformerInterface {
   public:
    explicit TapTransformer(std::shared_ptr<Feed> feed) : feed_(std::move(feed)) {}
    void Transform(std::unique_ptr<webrtc::TransformableFrameInterface> f) override {
        if (!f) return;
        if (f->GetDirection() == webrtc::TransformableFrameInterface::Direction::kReceiver) {
            feed_->forward(*static_cast<webrtc::TransformableVideoFrameInterface*>(f.get()));
        }
        rtc::scoped_refptr<webrtc::TransformedFrameCallback> cb;
        {
            std::lock_guard<std::mutex> lock(m_);
            const auto it = sinks_.find(f->GetSsrc());
            if (it != sinks_.end()) {
                cb = it->second;
            } else if (any_) {
                cb = any_;
            } else if (sinks_.size() == 1) {
                cb = sinks_.begin()->second;
            }
        }
        if (cb) cb->OnTransformedFrame(std::move(f));
    }
    void RegisterTransformedFrameCallback(rtc::scoped_refptr<webrtc::TransformedFrameCallback> cb) override {
        std::lock_guard<std::mutex> lock(m_);
        any_ = std::move(cb);
    }
    void RegisterTransformedFrameSinkCallback(rtc::scoped_refptr<webrtc::TransformedFrameCallback> cb,
                                              uint32_t ssrc) override {
        std::lock_guard<std::mutex> lock(m_);
        sinks_[ssrc] = std::move(cb);
    }
    void UnregisterTransformedFrameCallback() override {
        std::lock_guard<std::mutex> lock(m_);
        any_ = nullptr;
    }
    void UnregisterTransformedFrameSinkCallback(uint32_t ssrc) override {
        std::lock_guard<std::mutex> lock(m_);
        sinks_.erase(ssrc);
    }

   private:
    std::shared_ptr<Feed> feed_;
    std::mutex m_;
    rtc::scoped_refptr<webrtc::TransformedFrameCallback> any_;
    std::map<uint32_t, rtc::scoped_refptr<webrtc::TransformedFrameCallback>> sinks_;
};

/// Fuente del track de un espectador: nunca emite (lo que se envia son los
/// cuadros reenviados). Existe solo para que el sender tenga un track.
class SilentVideoSource : public rtc::AdaptedVideoTrackSource {
   public:
    SourceState state() const override { return kLive; }
    bool remote() const override { return false; }
    bool is_screencast() const override { return false; }
    absl::optional<bool> needs_denoising() const override { return false; }
};

/// PeerConnection de un espectador (B es el answerer de su offer recvonly).
struct ViewerPc : public webrtc::PeerConnectionObserver {
    std::atomic<int> id{0};
    int feedKey = 0;
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc;
    rtc::scoped_refptr<ViewerTransformer> tx;
    std::mutex m;
    std::condition_variable cv;
    bool gathered = false;
    int described = 0;         // 0 pendiente, 1 ok, -1 error
    std::atomic<int> state{0}; // 0 nuevo, 1 conectado, 2 caido
    std::atomic<int64_t> downSinceMs{0};
    int64_t createdMs = 0;
    std::function<void()> onConnected; // lo fija openViewer

    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState) override {}
    void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface>) override {}
    void OnIceCandidate(const webrtc::IceCandidateInterface*) override {}
    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState s) override {
        if (s != webrtc::PeerConnectionInterface::kIceGatheringComplete) return;
        std::lock_guard<std::mutex> lock(m);
        gathered = true;
        cv.notify_all();
    }
    void OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState s) override {
        using S = webrtc::PeerConnectionInterface::PeerConnectionState;
        if (s == S::kConnected) {
            const bool first = state.exchange(1) != 1;
            downSinceMs = 0;
            if (first && onConnected) onConnected();
        } else if (s == S::kFailed || s == S::kDisconnected || s == S::kClosed) {
            if (state.exchange(2) != 2) downSinceMs = steadyMs();
        }
    }
};

/// Payload types de video del answer: "VIDEO/<CODEC>" -> pt (el primero de
/// cada codec; sin rtx/red/fec).
std::vector<std::pair<std::string, int>> videoPayloadTypes(const std::string& sdp) {
    std::vector<std::pair<std::string, int>> out;
    bool inVideo = false;
    size_t pos = 0;
    while (pos < sdp.size()) {
        size_t eol = sdp.find('\n', pos);
        if (eol == std::string::npos) eol = sdp.size();
        std::string line = sdp.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("m=", 0) == 0) inVideo = line.rfind("m=video", 0) == 0;
        if (!inVideo || line.rfind("a=rtpmap:", 0) != 0) continue;
        const size_t sp = line.find(' ');
        const size_t slash = line.find('/', sp);
        if (sp == std::string::npos || slash == std::string::npos) continue;
        const int pt = std::atoi(line.c_str() + 9);
        const std::string name = upper(line.substr(sp + 1, slash - sp - 1));
        if (name == "RTX" || name == "RED" || name == "ULPFEC" || name == "FLEXFEC-03") continue;
        const std::string key = "VIDEO/" + name;
        if (std::none_of(out.begin(), out.end(), [&](const auto& e) { return e.first == key; }))
            out.emplace_back(key, pt);
    }
    return out;
}

/// Estado compartido de la factory (lo guarda el hub como void*).
struct RtcHubState {
    uint16_t minPort = 0;
    uint16_t maxPort = 0;
    std::unique_ptr<rtc::Thread> network;
    std::unique_ptr<rtc::Thread> worker;
    std::unique_ptr<rtc::Thread> signaling;
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory;
    // Relay: un Feed por (sesion, camara/pantalla) y los espectadores.
    std::mutex feedsM;
    std::map<int, std::shared_ptr<Feed>> feeds; // clave: sesion * 2 + (pantalla ? 1 : 0)
    std::mutex viewersM;
    std::map<int, std::unique_ptr<ViewerPc>> viewers;
    int nextViewer = 1;

    std::shared_ptr<Feed> feed(int key) {
        std::lock_guard<std::mutex> lock(feedsM);
        auto& f = feeds[key];
        if (!f) f = std::make_shared<Feed>();
        return f;
    }
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
    // Estadisticas de la conexion (las arma el callback de GetStats).
    std::mutex statsMu;
    std::string statsJson = "{}";
    uint64_t prevIn = 0, prevOut = 0, prevPkts = 0;
    int64_t prevLost = 0;
    int64_t prevMs = 0;

    class StatsCb : public webrtc::RTCStatsCollectorCallback {
       public:
        explicit StatsCb(Impl* i) : impl_(i) {}
        void OnStatsDelivered(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) override {
            uint64_t in = 0, out = 0, pkts = 0;
            int64_t lost = 0;
            double jitterMax = 0, rtt = -1;
            uint32_t perdidos = 0, alto = 0, ancho = 0;
            nlohmann::json salida;
            for (const webrtc::RTCStats& st : *report) {
                if (st.type() == webrtc::RTCInboundRtpStreamStats::kType) {
                    const auto& r = st.cast_to<webrtc::RTCInboundRtpStreamStats>();
                    in += r.bytes_received.value_or(0);
                    pkts += r.packets_received.value_or(0);
                    lost += r.packets_lost.value_or(0);
                    jitterMax = std::max(jitterMax, r.jitter.value_or(0.0));
                    if (r.kind.value_or("") == "video") {
                        perdidos += r.frames_dropped.value_or(0);
                        alto = std::max(alto, r.frame_height.value_or(0));
                        ancho = std::max(ancho, r.frame_width.value_or(0));
                    }
                } else if (st.type() == webrtc::RTCOutboundRtpStreamStats::kType) {
                    const auto& o = st.cast_to<webrtc::RTCOutboundRtpStreamStats>();
                    out += o.bytes_sent.value_or(0);
                    // Lo que SALE de video: con qué codificador, en qué capas y qué lo
                    // limita (para saber por qué una pantalla se ve como se ve).
                    if (o.kind.value_or("") == "video" && o.frame_height.has_value()) {
                        salida = {{"codificador", o.encoder_implementation.value_or("?")},
                                  {"capas", o.scalability_mode.value_or("")},
                                  {"alto", o.frame_height.value_or(0)},
                                  {"fps", std::lround(o.frames_per_second.value_or(0.0))},
                                  {"limita", o.quality_limitation_reason.value_or("")}};
                    }
                } else if (st.type() == webrtc::RTCIceCandidatePairStats::kType) {
                    const auto& c = st.cast_to<webrtc::RTCIceCandidatePairStats>();
                    if (c.nominated.value_or(false) && c.current_round_trip_time.has_value())
                        rtt = *c.current_round_trip_time;
                }
            }
            const int64_t ahora = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::steady_clock::now().time_since_epoch())
                                      .count();
            std::lock_guard<std::mutex> l(impl_->statsMu);
            const double dt = impl_->prevMs > 0 ? (ahora - impl_->prevMs) / 1000.0 : 0.0;
            const double kin = dt > 0 && in >= impl_->prevIn ? (in - impl_->prevIn) * 8.0 / 1000.0 / dt : 0.0;
            const double kout = dt > 0 && out >= impl_->prevOut ? (out - impl_->prevOut) * 8.0 / 1000.0 / dt : 0.0;
            const double dp = pkts >= impl_->prevPkts ? static_cast<double>(pkts - impl_->prevPkts) : 0.0;
            const double dl = static_cast<double>(std::max<int64_t>(0, lost - impl_->prevLost));
            const double perdida = dp + dl > 0 ? 100.0 * dl / (dp + dl) : 0.0;
            impl_->prevIn = in;
            impl_->prevOut = out;
            impl_->prevPkts = pkts;
            impl_->prevLost = lost;
            impl_->prevMs = ahora;
            const double jit = jitterMax * 1000.0, rttMs = rtt >= 0 ? rtt * 1000.0 : -1;
            // El semáforo de red del invitado (Configuración → avanzado de la sala).
            namespace aj = ba::ajustes;
            const char* sem = (perdida > aj::numero("room.rojoPerdidaPct", 5) || rttMs > aj::numero("room.rojoRttMs", 400) ||
                               jit > aj::numero("room.rojoJitterMs", 50))
                                  ? "rojo"
                              : (perdida > aj::numero("room.ambarPerdidaPct", 2) || rttMs > aj::numero("room.ambarRttMs", 200) ||
                                 jit > aj::numero("room.ambarJitterMs", 30))
                                  ? "ambar"
                                  : "verde";
            nlohmann::json j{{"kbps_in", std::lround(kin)},  {"kbps_out", std::lround(kout)},
                             {"perdida_pct", std::round(perdida * 10) / 10},
                             {"jitter_ms", std::lround(jit)}, {"rtt_ms", std::lround(rttMs)},
                             {"cuadros_perdidos", perdidos},   {"alto", alto},
                             {"ancho", ancho},                 {"semaforo", sem}};
            if (!salida.is_null()) j["salida"] = salida;
            impl_->statsJson = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }

       private:
        Impl* impl_;
    };

    std::string estadisticas() {
        rtc::scoped_refptr<webrtc::PeerConnectionInterface> p;
        {
            std::lock_guard<std::mutex> lock(m);
            p = pc;
        }
        if (p && connected) p->GetStats(rtc::make_ref_counted<StatsCb>(this).get());
        std::lock_guard<std::mutex> l(statsMu);
        return statsJson;
    }

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

    /// SEGUIR EL RELOJ aunque se pierdan cuadros. En VP8 común cada cuadro depende del
    /// anterior: un paquete perdido en el wifi y el que mira se queda clavado hasta
    /// recuperarlo (o hasta un cuadro completo), y después va atrás. Con capas
    /// temporales (L1T3) dos de cada tres cuadros no son referencia de nadie: si se
    /// pierde uno de esos, se saltea y la imagen sigue en hora; solo una pérdida en la
    /// capa base hace esperar. Va DESPUÉS de negociar (el H.264 de VideoToolbox no
    /// tiene capas: queda como está).
    static void seguirElReloj(const rtc::scoped_refptr<webrtc::PeerConnectionInterface>& p) {
        for (const auto& sender : p->GetSenders()) {
            if (sender->media_type() != cricket::MEDIA_TYPE_VIDEO) continue;
            webrtc::RtpParameters par = sender->GetParameters();
            // Sin mirar el codec: a esta altura GetParameters todavía no lo informa
            // (codecs vacío). Si el codec no tiene capas (H.264 de VideoToolbox),
            // SetParameters lo rechaza y queda como estaba.
            if (par.encodings.empty()) continue;
            par.encodings.front().scalability_mode = "L1T3";
            // Ante poco ancho de banda, menos cuadros antes que menos nitidez: lo que se
            // ve en una pantalla de sala es texto y gráficos.
            par.degradation_preference = webrtc::DegradationPreference::BALANCED;
            sender->SetParameters(par);
        }
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
            seguirElReloj(p);
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

    void pushLocalVideo(const rtc::scoped_refptr<webrtc::VideoFrameBuffer>& i420) {
        if (!connected.load(std::memory_order_relaxed) || !videoSource) return;
        videoSource->AddFrame(i420);
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
        if (hub != nullptr) {
            hub->feed(slot * 2)->clearSource();
            hub->feed(slot * 2 + 1)->clearSource();
        }
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
                .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
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
            // Relay a espectadores: los cuadros codificados pasan por el Feed de
            // esta sesion antes del decodificador.
            if (hub != nullptr) {
                std::shared_ptr<Feed> feed = hub->feed(slot * 2 + (camera ? 0 : 1));
                feed->setSource(rtc::scoped_refptr<webrtc::VideoTrackSourceInterface>(video->GetSource()),
                                hub->worker.get());
                t->receiver()->SetDepacketizerToDecoderFrameTransformer(
                    rtc::make_ref_counted<TapTransformer>(feed));
            }
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
// CuadroSaliente (F2-13: una conversión por cuadro, no una por sesión)
// ============================================================================

namespace {
std::atomic<uint64_t> g_conversiones{0};
std::atomic<uint64_t> g_bytesConvertidos{0};
} // namespace

struct CuadroSaliente::Impl {
    int ancho = 0, alto = 0;
#if BA_GATEWAY_HAVE_WEBRTC
    rtc::scoped_refptr<webrtc::I420Buffer> i420;
#endif
};

CuadroSaliente CuadroSaliente::desdeBgra(const uint8_t* bgra, int ancho, int alto, int stride) {
    CuadroSaliente c;
    if (bgra == nullptr || ancho <= 0 || alto <= 0) return c;
    auto impl = std::make_shared<Impl>();
    impl->ancho = ancho;
    impl->alto = alto;
#if BA_GATEWAY_HAVE_WEBRTC
    impl->i420 = webrtc::I420Buffer::Create(ancho, alto);
    // libyuv "ARGB" es B,G,R,A en memoria: justamente BGRA.
    libyuv::ARGBToI420(bgra, stride > 0 ? stride : ancho * 4, impl->i420->MutableDataY(), impl->i420->StrideY(),
                       impl->i420->MutableDataU(), impl->i420->StrideU(), impl->i420->MutableDataV(),
                       impl->i420->StrideV(), ancho, alto);
#else
    (void)stride;
#endif
    g_conversiones.fetch_add(1, std::memory_order_relaxed);
    const uint64_t croma = static_cast<uint64_t>((ancho + 1) / 2) * static_cast<uint64_t>((alto + 1) / 2);
    g_bytesConvertidos.fetch_add(static_cast<uint64_t>(ancho) * static_cast<uint64_t>(alto) + 2 * croma,
                                 std::memory_order_relaxed);
    c.impl_ = std::move(impl);
    return c;
}

int CuadroSaliente::ancho() const { return impl_ ? impl_->ancho : 0; }
int CuadroSaliente::alto() const { return impl_ ? impl_->alto : 0; }
uint64_t CuadroSaliente::conversiones() { return g_conversiones.load(std::memory_order_relaxed); }
uint64_t CuadroSaliente::bytesConvertidos() { return g_bytesConvertidos.load(std::memory_order_relaxed); }

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
    if (impl_ == nullptr || p == nullptr || w <= 0 || h <= 0 || bytes < static_cast<size_t>(w) * h * 4u) return;
    if (!impl_->connected) return; // sin nadie del otro lado no se convierte nada
    pushLocalVideo(CuadroSaliente::desdeBgra(p, w, h));
}
void WebrtcSession::pushLocalVideo(const CuadroSaliente& c) {
    if (impl_ == nullptr || c.vacio()) return;
#if BA_GATEWAY_HAVE_WEBRTC
    impl_->pushLocalVideo(c.impl_->i420);
#else
    impl_->pushLocalVideo();
#endif
}
bool WebrtcSession::connected() const { return impl_ != nullptr && impl_->connected; }
void WebrtcSession::close() {
    if (impl_ != nullptr) impl_->close();
}
int WebrtcSession::slot() const { return impl_ != nullptr ? impl_->slot : 0; }
std::string WebrtcSession::estadisticas() { return impl_ != nullptr ? impl_->estadisticas() : std::string("{}"); }
uint64_t WebrtcSession::localAudioBlocks() const {
    return impl_ != nullptr ? impl_->localAudioBlocks.load() : 0;
}
uint64_t WebrtcSession::localVideoFrames() const {
    return impl_ != nullptr ? impl_->localVideoFrames.load() : 0;
}

// ============================================================================
// WebrtcHub
// ============================================================================

WebrtcHub::WebrtcHub(int guests, int videoW, int videoH, int audioRate, int audioChannels) : guests_(guests) {
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
#ifdef __APPLE__
        // H.264 por VideoToolbox además de VP8/VP9/AV1 (ver CodecsMac.mm).
        codificadoresMac(), decodificadoresMac(),
#else
        std::make_unique<webrtc::VideoEncoderFactoryTemplate<
            webrtc::LibvpxVp8EncoderTemplateAdapter, webrtc::LibvpxVp9EncoderTemplateAdapter,
            webrtc::OpenH264EncoderTemplateAdapter, webrtc::LibaomAv1EncoderTemplateAdapter>>(),
        std::make_unique<webrtc::VideoDecoderFactoryTemplate<
            webrtc::LibvpxVp8DecoderTemplateAdapter, webrtc::LibvpxVp9DecoderTemplateAdapter,
            webrtc::OpenH264DecoderTemplateAdapter, webrtc::Dav1dDecoderTemplateAdapter>>(),
#endif
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
    if (auto* st = static_cast<RtcHubState*>(rtcFactory_)) {
        std::vector<int> ids;
        {
            std::lock_guard<std::mutex> lock(st->viewersM);
            for (const auto& [id, v] : st->viewers)
                ids.push_back(id);
        }
        for (int id : ids)
            closeViewer(id);
    }
    sessions_.clear(); // antes que la factory: las PC la referencian
    if (auto* st = static_cast<RtcHubState*>(rtcFactory_)) {
        {
            std::lock_guard<std::mutex> lock(st->feedsM);
            st->feeds.clear(); // sueltan proxies de libwebrtc: con los threads vivos
        }
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

// ============================================================================
// Espectadores (relay de video codificado)
// ============================================================================

#if BA_GATEWAY_HAVE_WEBRTC
int WebrtcHub::openViewer(int session, bool screen, const std::string& offerSdp, std::string& answerSdp) {
    auto* st = static_cast<RtcHubState*>(rtcFactory_);
    if (st == nullptr || session < 1 || session > guests_) return 0;
    reapViewers();
    if (viewerCount() >= kMaxViewers()) return 0;
    webrtc::SdpParseError err;
    std::unique_ptr<webrtc::SessionDescriptionInterface> offer =
        webrtc::CreateSessionDescription(webrtc::SdpType::kOffer, offerSdp, &err);
    if (!offer) return 0;

    auto v = std::make_unique<ViewerPc>();
    v->feedKey = session * 2 + (screen ? 1 : 0);
    v->createdMs = steadyMs();
    v->tx = rtc::make_ref_counted<ViewerTransformer>();
    {
        // El id se asigna al final: el callback lo lee de v (vive mientras la PC).
        ViewerPc* raw = v.get();
        std::shared_ptr<Feed> fd = st->feed(v->feedKey);
        v->onConnected = [raw, fd]() { fd->viewerConnected(raw->id); };
    }
    webrtc::PeerConnectionInterface::RTCConfiguration config;
    config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
    config.bundle_policy = webrtc::PeerConnectionInterface::kBundlePolicyMaxBundle;
    config.rtcp_mux_policy = webrtc::PeerConnectionInterface::kRtcpMuxPolicyRequire;
    if (st->minPort != 0) {
        config.port_allocator_config.min_port = st->minPort;
        config.port_allocator_config.max_port = st->maxPort;
    }
    webrtc::PeerConnectionDependencies deps(v.get());
    auto res = st->factory->CreatePeerConnectionOrError(config, std::move(deps));
    if (!res.ok()) return 0;
    v->pc = res.MoveValue();

    // SRD -> track mudo con el ViewerTransformer en su sender -> SLD (answer).
    ViewerPc* vp = v.get();
    auto p = v->pc;
    auto finish = [vp](int result) {
        std::lock_guard<std::mutex> lock(vp->m);
        vp->described = result;
        vp->cv.notify_all();
    };
    auto onSld = [finish](webrtc::RTCError e) { finish(e.ok() ? 1 : -1); };
    auto onSrd = [st, p, vp, finish, onSld](webrtc::RTCError e) {
        if (!e.ok()) {
            finish(-1);
            return;
        }
        auto track = st->factory->CreateVideoTrack(rtc::make_ref_counted<SilentVideoSource>(), "ba-relay");
        auto sender = p->AddTrack(track, {"ba-relay"});
        if (!sender.ok()) {
            finish(-1);
            return;
        }
        sender.value()->SetEncoderToPacketizerFrameTransformer(vp->tx);
        p->SetLocalDescription(rtc::make_ref_counted<SldObserver<decltype(onSld)>>(onSld));
    };
    p->SetRemoteDescription(std::move(offer), rtc::make_ref_counted<SrdObserver<decltype(onSrd)>>(onSrd));
    {
        std::unique_lock<std::mutex> lock(vp->m);
        // Answer aplicado y candidatos reunidos (host: instantaneo). Sin goteo:
        // el answer viaja completo en una sola respuesta HTTP.
        vp->cv.wait_for(lock, std::chrono::seconds(3), [vp]() { return vp->described != 0; });
        if (vp->described == 1)
            vp->cv.wait_for(lock, std::chrono::seconds(2), [vp]() { return vp->gathered; });
        if (vp->described != 1) {
            lock.unlock();
            p->Close();
            return 0;
        }
    }
    if (p->local_description() == nullptr) {
        p->Close();
        return 0;
    }
    p->local_description()->ToString(&answerSdp);
    const auto pts = videoPayloadTypes(answerSdp);
    int id = 0;
    {
        std::lock_guard<std::mutex> lock(st->viewersM);
        id = st->nextViewer++;
        v->id = id;
        st->viewers[id] = std::move(v);
    }
    st->feed(vp->feedKey)->add(id, vp->tx, pts, vp->state.load() == 1);
    return id;
}

void WebrtcHub::closeViewer(int viewerId) {
    auto* st = static_cast<RtcHubState*>(rtcFactory_);
    if (st == nullptr) return;
    std::unique_ptr<ViewerPc> v;
    {
        std::lock_guard<std::mutex> lock(st->viewersM);
        const auto it = st->viewers.find(viewerId);
        if (it == st->viewers.end()) return;
        v = std::move(it->second);
        st->viewers.erase(it);
    }
    st->feed(v->feedKey)->remove(viewerId);
    if (v->pc) v->pc->Close(); // sincronico: despues no llegan mas callbacks
}

void WebrtcHub::closeViewersOf(int session) {
    auto* st = static_cast<RtcHubState*>(rtcFactory_);
    if (st == nullptr) return;
    std::vector<int> ids;
    {
        std::lock_guard<std::mutex> lock(st->viewersM);
        for (const auto& [id, v] : st->viewers)
            if (v->feedKey / 2 == session) ids.push_back(id);
    }
    for (int id : ids)
        closeViewer(id);
}

void WebrtcHub::requestKeyFrame(int viewerId) {
    auto* st = static_cast<RtcHubState*>(rtcFactory_);
    if (st == nullptr) return;
    int key = -1;
    {
        std::lock_guard<std::mutex> lock(st->viewersM);
        const auto it = st->viewers.find(viewerId);
        if (it != st->viewers.end()) key = it->second->feedKey;
    }
    if (key >= 0) st->feed(key)->requestKey();
}

void WebrtcHub::reapViewers() {
    auto* st = static_cast<RtcHubState*>(rtcFactory_);
    if (st == nullptr) return;
    const int64_t now = steadyMs();
    std::vector<int> ids;
    {
        std::lock_guard<std::mutex> lock(st->viewersM);
        for (const auto& [id, v] : st->viewers) {
            const int s = v->state.load();
            if ((s == 2 && now - v->downSinceMs.load() > ba::ajustes::sAMs("room.visorCaidoS", 10)) ||
                (s == 0 && now - v->createdMs > ba::ajustes::sAMs("room.visorNoConectaS", 20)))
                ids.push_back(id);
        }
    }
    for (int id : ids)
        closeViewer(id);
}

WebrtcHub::Trafico WebrtcHub::trafico() const {
    Trafico out;
    auto* st = static_cast<RtcHubState*>(rtcFactory_);
    if (st == nullptr) return out;
    std::lock_guard<std::mutex> lock(st->feedsM);
    for (const auto& [clave, f] : st->feeds) {
        if (!f) continue;
        out.entrada += f->bytesEntrada.load(std::memory_order_relaxed);
        out.salida += f->bytesSalida.load(std::memory_order_relaxed);
    }
    return out;
}

int WebrtcHub::viewerCount() const {
    auto* st = static_cast<RtcHubState*>(rtcFactory_);
    if (st == nullptr) return 0;
    std::lock_guard<std::mutex> lock(st->viewersM);
    return static_cast<int>(st->viewers.size());
}
#else
// Backend NULL: sin RTP no hay nada que reenviar.
int WebrtcHub::openViewer(int, bool, const std::string&, std::string&) { return 0; }
void WebrtcHub::closeViewer(int) {}
void WebrtcHub::closeViewersOf(int) {}
void WebrtcHub::requestKeyFrame(int) {}
void WebrtcHub::reapViewers() {}
WebrtcHub::Trafico WebrtcHub::trafico() const { return {}; }

int WebrtcHub::viewerCount() const { return 0; }
#endif

} // namespace ba::gateway
