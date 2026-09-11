// ============================================================================
// src/gateway/Webrtc.cpp — implementacion de las sesiones WebRTC (real + NULL)
//
// Proposito:
//   Implementa WebrtcSession y WebrtcHub. El backend se elige en compile-time
//   por BA_GATEWAY_HAVE_WEBRTC:
//     - 1 (Windows + SDK libwebrtc M123 presente): usa webrtc::PeerConnection-
//       Interface real. B es el answerer del offer del invitado.
//     - 0 (Linux / build offline): backend NULL: maquina de estados que completa
//       la senalizacion (offer->answer) para que el flujo sea testeable sin SDK.
//
// Modelo de threading:
//   - setRemoteOffer/addRemoteIce se llaman desde el thread de la conexion WS.
//   - En el backend real, los eventos de ICE/RTP/answer se despachan desde los
//     threads de libwebrtc (signaling/worker), por lo que los callbacks pueden
//     llamarse desde otro thread. El caller (servidor/hub) es thread-safe.
//   - pushLocalAudio/pushLocalVideo se llaman desde los threads del MediaPump.
//
// Estrategia de errores:
//   No lanza en el nucleo. En el backend real se captura cualquier excepcion en
//   el borde de los callbacks de libwebrtc (spec §14: "Ninguna excepcion C++
//   puede cruzar un callback C"). Los metodos de senalizacion devuelven bool.
// ============================================================================

#include "Webrtc.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

#include "Log.hpp"

#if BA_GATEWAY_HAVE_WEBRTC
// ----------------------------------------------------------------------------
// Backend REAL: libwebrtc M123 (binarios precompilados, spec §8.1, D4).
//
// IMPORTANTE: estos includes y llamadas solo se compilan cuando el SDK esta
// presente (BA_GATEWAY_HAVE_WEBRTC=1). En el build offline (BA_GATEWAY_HAVE_
// WEBRTC=0) todo este bloque se salta y se usa el backend NULL de abajo.
#include "api/audio/audio_frame.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/create_peerconnection_factory.h"
#include "api/jsep.h"
#include "api/jsep_ice_candidate.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/media_types.h"
#include "api/peer_connection_interface.h"
#include "api/rtp_transceiver_interface.h"
#include "api/scoped_refptr.h"
#include "api/set_local_description_observer_interface.h"
#include "api/set_remote_description_observer_interface.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"
#include "common_video/include/video_frame_buffer.h"
#include "media/base/adapted_video_track_source.h"
#include "modules/audio_processing/include/audio_processing.h"
#include "rtc_base/thread.h"
#endif // BA_GATEWAY_HAVE_WEBRTC

namespace ba::gateway {

// ============================================================================
// Backend NULL (compile-time cuando BA_GATEWAY_HAVE_WEBRTC=0)
// ============================================================================
namespace {

/// Backend NULL de una sesion: maquina de estados de senalizacion.
///
/// Completa el flujo offer->answer de inmediato (sin RTP real) para que la
/// senalizacion y el flujo de medios (pushLocal*) sean testeables sin el SDK.
/// onRemoteAudio/onRemoteVideo NUNCA se disparan (no hay RTP entrante).
struct NullSession {
    SessionCallbacks cb;
    int slot = 0;
    bool configured = false;
    bool offerReceived = false;
    bool connected = false;
    bool closed = false;
    std::atomic<uint64_t> localAudioBlocks{0};
    std::atomic<uint64_t> localVideoFrames{0};

    bool configure(const SessionCallbacks& c, int s) {
        cb = c;
        slot = s;
        configured = true;
        return true;
    }

    bool setRemoteOffer(const std::string& sdp) {
        if (!configured || closed) return false;
        offerReceived = true;
        connected = true;
        // Genera un answer SDP minimo (suficiente para que el navegador avance
        // en el flujo de senalizacion). En el backend real esto vendria de
        // libwebrtc; aqui es un placeholder coherente.
        const std::string answerSdp = "v=0\r\n"
                                      "o=- 0 0 IN IP4 127.0.0.1\r\n"
                                      "s=-\r\n"
                                      "t=0 0\r\n"
                                      "m=audio 9 UDP/TLS/RTP/SAVPF 0\r\n"
                                      "a=rtpmap:0 PCMU/8000\r\n"
                                      "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"
                                      "a=rtpmap:96 VP8/90000\r\n";
        (void)sdp;
        if (cb.onLocalAnswer) cb.onLocalAnswer(answerSdp);
        if (cb.onStateChange) cb.onStateChange(true);
        return true;
    }

    bool addRemoteIce(const std::string& candidate) {
        (void)candidate;
        return configured && !closed;
    }

    void pushLocalAudio(const float* samples, size_t n, int rate) {
        (void)samples;
        (void)rate;
        if (n > 0 && connected) localAudioBlocks.fetch_add(1, std::memory_order_relaxed);
    }

    void pushLocalVideo(const uint8_t* pixels, size_t bytes, int w, int h) {
        (void)pixels;
        (void)w;
        (void)h;
        if (bytes > 0 && connected) localVideoFrames.fetch_add(1, std::memory_order_relaxed);
    }

    bool connected_() const { return connected; }

    void close() {
        if (closed) return;
        closed = true;
        connected = false;
        if (cb.onStateChange) cb.onStateChange(false);
    }
};

} // namespace

// ============================================================================
// Backend REAL (libwebrtc) — solo cuando BA_GATEWAY_HAVE_WEBRTC=1
// ============================================================================
#if BA_GATEWAY_HAVE_WEBRTC

namespace {

// ----------------------------------------------------------------------------
// Fuentes de medios locales (retorno a los invitados, spec §8.5).
//
// El MediaPump inyecta frames via pushLocalAudio/pushLocalVideo. Estas fuentes
// son los "capturadores" que libwebrtc necesita para que el RtpSender tenga
// algo que codificar y enviar. Ambas son ref-counted (via make_ref_counted)
// porque CreateVideoTrack/la cadena de VideoTrackSourceInterface exige
// RefCountInterface.
// ----------------------------------------------------------------------------

/// Fuente de video local: convierte BGRA→I420 y despacha a los sinks.
///
/// Subclase de rtc::AdaptedVideoTrackSource, que ya implementa la parte de
/// VideoSourceInterface (AddOrUpdateSink/RemoveSink) y el broadcaster. Solo
/// implementamos los virtuales puros de MediaSourceInterface y
/// VideoTrackSourceInterface que AdaptedVideoTrackSource no cubre, y el punto
/// de entrada AddFrame() que el MediaPump llama. No es ref-counted por si
/// misma (AdaptedVideoTrackSource deja AddRef/Release puros), por lo que se
/// instancia con rtc::make_ref_counted, que la envuelve en RefCountedObject.
class LocalVideoSource : public rtc::AdaptedVideoTrackSource {
   public:
    /// Inyecta un frame BGRA en la fuente: lo convierte a I420 y lo despacha
    /// a los sinks via el broadcaster de AdaptedVideoTrackSource.
    ///
    /// :param bgra: buffer BGRA (4 bytes/pixel).
    /// :param w/h: resolucion del frame.
    void AddFrame(const uint8_t* bgra, int w, int h) {
        if (!bgra || w <= 0 || h <= 0) return;
        // Convierte BGRA → I420 (Y/U/V planar).
        const int cw = (w + 1) / 2; // chroma width
        const int ch = (h + 1) / 2; // chroma height
        std::vector<uint8_t> yPlane(static_cast<size_t>(w) * h);
        std::vector<uint8_t> uPlane(static_cast<size_t>(cw) * ch);
        std::vector<uint8_t> vPlane(static_cast<size_t>(cw) * ch);
        for (int row = 0; row < h; ++row) {
            for (int col = 0; col < w; ++col) {
                const uint8_t* px = bgra + (static_cast<size_t>(row) * w + col) * 4;
                const int b = px[0], g = px[1], r = px[2];
                yPlane[static_cast<size_t>(row) * w + col] =
                    static_cast<uint8_t>((66 * r + 129 * g + 25 * b + 128) >> 8);
                if ((row & 1) == 0 && (col & 1) == 0) {
                    const int uIdx = ((row >> 1) * cw) + (col >> 1);
                    uPlane[uIdx] = static_cast<uint8_t>((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                    vPlane[uIdx] = static_cast<uint8_t>((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
                }
            }
        }
        // Wrappa el buffer I420 (los planos viven en los vectors capturados)
        // y construye el VideoFrame.
        rtc::scoped_refptr<webrtc::I420BufferInterface> i420 = webrtc::WrapI420Buffer(
            w, h, yPlane.data(), w, uPlane.data(), cw, vPlane.data(), cw, [yPlane, uPlane, vPlane]() {});
        webrtc::VideoFrame frame =
            webrtc::VideoFrame::Builder().set_video_frame_buffer(i420).set_timestamp_ms(0).build();
        // Despacha a los sinks via el broadcaster de AdaptedVideoTrackSource.
        OnFrame(frame);
    }

    // --- Virtuales puros de MediaSourceInterface ---
    webrtc::MediaSourceInterface::SourceState state() const override {
        return webrtc::MediaSourceInterface::kLive;
    }
    bool remote() const override { return false; }

    // --- Virtuales puros de VideoTrackSourceInterface ---
    bool is_screencast() const override { return false; }
    absl::optional<bool> needs_denoising() const override { return absl::nullopt; }
    bool GetStats(Stats* stats) override {
        (void)stats;
        return false;
    }
};

/// Fuente de audio local: recibe float32 intercalado y lo entrega a los sinks.
///
/// Subclase de webrtc::Notifier<webrtc::AudioSourceInterface>, que implementa
/// RegisterObserver/UnregisterObserver. Implementamos los virtuales puros de
/// MediaSourceInterface (state/remote) y AddSink/RemoveSink para rastrear los
/// sinks y poder empujarles audio. No es ref-counted por si misma, por lo que
/// se instancia con rtc::make_ref_counted.
class LocalAudioSource : public webrtc::Notifier<webrtc::AudioSourceInterface> {
   public:
    LocalAudioSource() = default;
    ~LocalAudioSource() override = default;

    /// Inyecta un bloque de audio float32 intercalado: lo convierte a int16 y
    /// lo entrega a cada sink registrado.
    ///
    /// :param samples: float32 intercalado [-1, 1].
    /// :param n: cantidad de floats (total, no por canal).
    /// :param rate: sample rate.
    /// :param channels: numero de canales.
    void AddFrame(const float* samples, size_t n, int rate, int channels) {
        if (!samples || n == 0 || rate <= 0 || channels <= 0) return;
        const size_t frames = n / static_cast<size_t>(channels);
        if (frames == 0) return;
        // Convierte float32 → int16_t.
        std::vector<int16_t> pcm(n);
        for (size_t i = 0; i < n; ++i) {
            float v = samples[i];
            if (v > 1.0f) v = 1.0f;
            if (v < -1.0f) v = -1.0f;
            pcm[i] = static_cast<int16_t>(v * 32767.0f);
        }
        // Entrega a cada sink registrado como int16.
        for (auto* sink : sinks_) {
            if (sink) {
                sink->OnData(pcm.data(), 16, rate, static_cast<size_t>(channels), frames);
            }
        }
    }

    // --- Virtuales puros de MediaSourceInterface ---
    webrtc::MediaSourceInterface::SourceState state() const override {
        return webrtc::MediaSourceInterface::kLive;
    }
    bool remote() const override { return false; }

    // --- Rastreo de sinks (para poder empujar audio) ---
    void AddSink(webrtc::AudioTrackSinkInterface* sink) override {
        if (sink && std::find(sinks_.begin(), sinks_.end(), sink) == sinks_.end()) {
            sinks_.push_back(sink);
        }
    }
    void RemoveSink(webrtc::AudioTrackSinkInterface* sink) override {
        auto it = std::find(sinks_.begin(), sinks_.end(), sink);
        if (it != sinks_.end()) sinks_.erase(it);
    }

   private:
    std::vector<webrtc::AudioTrackSinkInterface*> sinks_;
};

// ----------------------------------------------------------------------------
// Observadores de senalizacion (callbacks de libwebrtc).
//
// Todos los observadores de descripcion son ref-counted (RefCountInterface)
// salvo el PeerConnectionObserver (clase simple, owned por la sesion).
// ----------------------------------------------------------------------------

/// Observador de PeerConnection: despacha eventos de ICE/estado a los
/// callbacks de alto nivel. Captura cualquier excepcion en el borde.
class SessionPeerObserver : public webrtc::PeerConnectionObserver {
   public:
    SessionPeerObserver(NullSession* null, webrtc::PeerConnectionInterface* pc) : null_(null), pc_(pc) {}

    /// Reasigna la PC una vez creada. El observer se pasa a
    /// PeerConnectionDependencies ANTES de que la PC exista (deps la necesita
    /// para crearla), por lo que pc_ se completa a posteriori con SetPc.
    void SetPc(webrtc::PeerConnectionInterface* pc) { pc_ = pc; }

    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState) override {}

    void OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface>) override {}

    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState s) override {
        // Cuando el gathering termina, el localDescription contiene todos los
        // candidatos; se serializa el answer (con ICE integrado) y se envia.
        if (!null_ || !pc_ || s != webrtc::PeerConnectionInterface::kIceGatheringComplete) return;
        try {
            if (null_->cb.onLocalAnswer) {
                const webrtc::SessionDescriptionInterface* desc = pc_->local_description();
                if (desc) {
                    std::string sdp;
                    if (desc->ToString(&sdp)) {
                        null_->cb.onLocalAnswer(sdp);
                    }
                }
            }
        } catch (...) {
            // No cruzar excepcion por el callback de libwebrtc.
        }
    }

    void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override {
        if (!null_ || !candidate) return;
        try {
            if (null_->cb.onLocalIce) {
                std::string sdp;
                if (candidate->ToString(&sdp)) {
                    null_->cb.onLocalIce(sdp);
                }
            }
        } catch (...) {
        }
    }

    void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState s) override {
        if (!null_) return;
        const bool up = (s == webrtc::PeerConnectionInterface::kIceConnectionConnected) ||
                        (s == webrtc::PeerConnectionInterface::kIceConnectionCompleted);
        try {
            if (null_->cb.onStateChange) null_->cb.onStateChange(up);
        } catch (...) {
        }
    }

   private:
    NullSession* null_;
    webrtc::PeerConnectionInterface* pc_;
};

/// Observador de SetLocalDescription (ref-counted, nueva API). Se declara
/// ANTES que AnswerObserver porque este ultimo lo instancia en OnSuccess.
class SetLocalDescObserver : public rtc::RefCountedObject<webrtc::SetLocalDescriptionObserverInterface> {
   public:
    void OnSetLocalDescriptionComplete(webrtc::RTCError error) override {
        if (!error.ok()) {
            // Loguea; la sesion queda coherente.
        }
    }
};

/// Observador de CreateAnswer (ref-counted). En OnSuccess toma la ownership de
/// la descripcion y la aplica como local via SetLocalDescription (nueva API).
class AnswerObserver : public rtc::RefCountedObject<webrtc::CreateSessionDescriptionObserver> {
   public:
    explicit AnswerObserver(webrtc::PeerConnectionInterface* pc) : pc_(pc) {}

    void OnSuccess(webrtc::SessionDescriptionInterface* desc) override {
        // desc es transferida a nosotros. La aplicamos como local con la nueva
        // sobrecarga que toma unique_ptr + observer ref-counted.
        if (!desc || !pc_) {
            delete desc;
            return;
        }
        try {
            std::unique_ptr<webrtc::SessionDescriptionInterface> owned(desc);
            rtc::scoped_refptr<webrtc::SetLocalDescriptionObserverInterface> obs(new SetLocalDescObserver());
            pc_->SetLocalDescription(std::move(owned), obs);
        } catch (...) {
            // La ownership ya paso al unique_ptr; si SetLocalDescription no la
            // tomo, se libera al salir del scope.
        }
    }

    void OnFailure(webrtc::RTCError error) override { (void)error; }

   private:
    webrtc::PeerConnectionInterface* pc_;
};

/// Observador de SetRemoteDescription (ref-counted, nueva API).
///
/// libwebrtc aplica el remote description de forma ASINCRONA: el answer solo
/// puede generarse DESPUES de que SetRemoteDescription se haya completado, no
/// inline. Por eso CreateAnswer se dispara desde este callback (la doc del
/// header garantiza que al llegar aqui el estado de la PC ya refleja el remote
/// desc aplicado). En el caso de error, la sesion queda coherente y no se
/// genera answer.
class SetRemoteDescObserver : public rtc::RefCountedObject<webrtc::SetRemoteDescriptionObserverInterface> {
   public:
    explicit SetRemoteDescObserver(webrtc::PeerConnectionInterface* pc) : pc_(pc) {}

    void OnSetRemoteDescriptionComplete(webrtc::RTCError error) override {
        if (!error.ok() || !pc_) {
            // Loguea el error; la sesion queda en estado coherente (sin answer).
            return;
        }
        try {
            // El remote desc ya esta aplicado: ahora si se puede crear el answer.
            // El AnswerObserver serializa el local description y dispara
            // cb.onLocalAnswer.
            rtc::scoped_refptr<webrtc::CreateSessionDescriptionObserver> answerObs(new AnswerObserver(pc_));
            webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
            options.ice_restart = false;
            pc_->CreateAnswer(answerObs.get(), options);
        } catch (...) {
            // No cruzar excepcion por el callback de libwebrtc.
        }
    }

   private:
    webrtc::PeerConnectionInterface* pc_;
};

} // namespace

// Cierra el bloque #if BA_GATEWAY_HAVE_WEBRTC que abrio la seccion de backend
// real. El codigo comun (WebrtcSession::Impl, public API, WebrtcHub) sigue con
// sus propios #if anidados para el estado del backend real.
#endif // BA_GATEWAY_HAVE_WEBRTC

// ============================================================================
// WebrtcSession::Impl — unifica el backend real y el NULL
// ============================================================================
struct WebrtcSession::Impl {
    NullSession null; // siempre presente: guarda callbacks + estadisticas
#if BA_GATEWAY_HAVE_WEBRTC
    // Estado del backend real (solo se usa cuando BA_GATEWAY_HAVE_WEBRTC=1).
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc;
    webrtc::PeerConnectionObserver* observer = nullptr;
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory;
    bool realConfigured = false;

    // true mientras hay un offer recibido cuya respuesta (answer) esta en
    // vuela: SetRemoteDescription ya se despacho y el answer se generara de
    // forma asincrona desde el callback del observer. Sirve para manejar de
    // forma coherente un offer tardio/segundo: la PC serializa las operaciones
    // en su cadena, y este flag solo registra que el flujo de answer esta
    // abierto (se resetea en close()).
    bool answerPending = false;

    // Fuentes de medios locales (retorno a los invitados). Ref-counted.
    rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> videoSource;
    rtc::scoped_refptr<webrtc::AudioSourceInterface> audioSource;
    rtc::scoped_refptr<webrtc::VideoTrackInterface> videoTrack;
    rtc::scoped_refptr<webrtc::AudioTrackInterface> audioTrack;
#endif

    int slot = 0;
    int videoW = 640, videoH = 360, audioRate = 48000, audioChannels = 2;

    Impl(int slot, int vw, int vh, int ar, int ac)
        : slot(slot), videoW(vw), videoH(vh), audioRate(ar), audioChannels(ac) {}

    bool configure(const SessionCallbacks& cb, void* factory) {
        null.cb = cb;
        null.slot = slot;
        (void)factory; // solo se usa en el backend real (HAVE_WEBRTC=1)
#if BA_GATEWAY_HAVE_WEBRTC
        if (factory) {
            // El factory lo aporta el hub (void* = PeerConnectionFactoryInterface*).
            // Se guarda en el miembro scoped_refptr (no shadowear el param void*).
            auto* fac = static_cast<webrtc::PeerConnectionFactoryInterface*>(factory);
            this->factory = fac;

            // Crea la PeerConnection con una RTCConfiguration local.
            // ICE servers vacios: el medio no pasa por el tunel (spec §8.4).
            webrtc::PeerConnectionInterface::RTCConfiguration config;
            config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;

            // Observer de la PC (clase simple, owned por esta sesion). Se crea
            // con pc_=nullptr porque deps lo necesita para crear la PC; se
            // completa con SetPc una vez la PC existe. Un solo observer, sin leak.
            auto* obs = new SessionPeerObserver(&null, nullptr);
            observer = obs;

            webrtc::PeerConnectionDependencies deps(observer);
            auto pcOrErr = fac->CreatePeerConnectionOrError(config, std::move(deps));
            if (!pcOrErr.ok()) {
                delete observer;
                observer = nullptr;
                this->factory = nullptr;
                return false;
            }
            pc = pcOrErr.MoveValue();
            // observer es el tipo base (PeerConnectionObserver*); SetPc vive en
            // la clase concreta SessionPeerObserver.
            static_cast<SessionPeerObserver*>(observer)->SetPc(pc.get());
            realConfigured = true;

            // Crea las fuentes de medios locales (retorno a los invitados).
            // Video: fuente (wrapped en RefCountedObject, ya que LocalVideoSource
            // es abstracta por AddRef/Release) + track + transceiver.
            videoSource = rtc::scoped_refptr<webrtc::VideoTrackSourceInterface>(
                new rtc::RefCountedObject<LocalVideoSource>());
            videoTrack = fac->CreateVideoTrack(
                rtc::scoped_refptr<webrtc::VideoTrackSourceInterface>(videoSource), "ba-return-video");
            if (videoTrack) {
                webrtc::RtpTransceiverInit init;
                init.direction = webrtc::RtpTransceiverDirection::kSendOnly;
                (void)pc->AddTransceiver(videoTrack, init);
            }
            // Audio: fuente + track + transceiver.
            audioSource = rtc::scoped_refptr<webrtc::AudioSourceInterface>(
                new rtc::RefCountedObject<LocalAudioSource>());
            audioTrack = fac->CreateAudioTrack("ba-return-audio", audioSource.get());
            if (audioTrack) {
                webrtc::RtpTransceiverInit init;
                init.direction = webrtc::RtpTransceiverDirection::kSendOnly;
                (void)pc->AddTransceiver(audioTrack, init);
            }
        }
#endif
        return null.configure(cb, slot);
    }

    bool setRemoteOffer(const std::string& sdp) {
#if BA_GATEWAY_HAVE_WEBRTC
        if (realConfigured && pc) {
            try {
                // Crea la SessionDescription del offer.
                auto offerDesc = webrtc::CreateSessionDescription(webrtc::SdpType::kOffer, sdp);
                if (!offerDesc) return false;

                // SetRemoteDescription con el observer nuevo (ref-counted).
                // IMPORTANTE: el remote desc se aplica de forma ASINCRONA en
                // libwebrtc, por lo que el answer NO puede generarse inline. El
                // observer dispara CreateAnswer una vez el remote desc esta
                // aplicado (ver SetRemoteDescObserver).
                rtc::scoped_refptr<webrtc::SetRemoteDescriptionObserverInterface> obs(
                    new SetRemoteDescObserver(pc.get()));
                pc->SetRemoteDescription(std::move(offerDesc), obs);

                // Marca que hay un answer en vuela (se resetea en close()).
                answerPending = true;
                return true;
            } catch (...) {
                return false;
            }
        }
#endif
        return null.setRemoteOffer(sdp);
    }

    bool addRemoteIce(const std::string& candidate) {
#if BA_GATEWAY_HAVE_WEBRTC
        if (realConfigured && pc) {
            try {
                // El candidato llega como JSON con campos "candidate",
                // "sdpMid", "sdpMLineIndex". Extraemos los tres; si no es JSON,
                // lo tomamos como el SDP del candidato con mid="" y mline=0.
                std::string sdpMid;
                int mlineIndex = 0;
                std::string sdpCandidate = candidate;

                const auto jsonStart = candidate.find('{');
                if (jsonStart != std::string::npos) {
                    auto extractStr = [&](const std::string& field) -> std::string {
                        auto f = candidate.find(field);
                        if (f == std::string::npos) return std::string();
                        auto colon = candidate.find(':', f);
                        if (colon == std::string::npos) return std::string();
                        auto q1 = candidate.find('"', colon + 1);
                        if (q1 == std::string::npos) return std::string();
                        auto q2 = candidate.find('"', q1 + 1);
                        if (q2 == std::string::npos) return std::string();
                        return candidate.substr(q1 + 1, q2 - q1 - 1);
                    };
                    sdpCandidate = extractStr("\"candidate\"");
                    sdpMid = extractStr("\"sdpMid\"");
                    auto mf = candidate.find("\"sdpMLineIndex\"");
                    if (mf != std::string::npos) {
                        auto colon = candidate.find(':', mf);
                        if (colon != std::string::npos) {
                            auto numStart = candidate.find_first_of("0123456789", colon + 1);
                            if (numStart != std::string::npos) {
                                mlineIndex = std::atoi(candidate.c_str() + numStart);
                            }
                        }
                    }
                    if (sdpCandidate.empty()) sdpCandidate = candidate;
                }

                webrtc::SdpParseError parseErr;
                webrtc::IceCandidateInterface* iceCand =
                    webrtc::CreateIceCandidate(sdpMid, mlineIndex, sdpCandidate, &parseErr);
                if (!iceCand) return false;
                const bool ok = pc->AddIceCandidate(iceCand);
                delete iceCand; // AddIceCandidate copia; el caller libera.
                return ok;
            } catch (...) {
                return false;
            }
        }
#endif
        return null.addRemoteIce(candidate);
    }

    void pushLocalAudio(const float* samples, size_t n, int rate) {
#if BA_GATEWAY_HAVE_WEBRTC
        if (realConfigured && audioSource) {
            try {
                auto* src = static_cast<LocalAudioSource*>(audioSource.get());
                src->AddFrame(samples, n, rate, audioChannels);
            } catch (...) {
            }
        }
#endif
        null.pushLocalAudio(samples, n, rate);
    }

    void pushLocalVideo(const uint8_t* pixels, size_t bytes, int w, int h) {
#if BA_GATEWAY_HAVE_WEBRTC
        if (realConfigured && videoSource) {
            try {
                (void)bytes;
                auto* src = static_cast<LocalVideoSource*>(videoSource.get());
                src->AddFrame(pixels, w, h);
            } catch (...) {
            }
        }
#endif
        null.pushLocalVideo(pixels, bytes, w, h);
    }

    bool connected() const { return null.connected_(); }

    void close() {
#if BA_GATEWAY_HAVE_WEBRTC
        if (realConfigured && pc) {
            try {
                pc->Close();
            } catch (...) {
            }
            // La PC es ref-counted; el scoped_refptr la libera al salir.
            pc = nullptr;
            // El observer se puede liberar despues de Close().
            if (observer) {
                delete observer;
                observer = nullptr;
            }
            // Las fuentes y tracks se liberan por el scoped_refptr.
            videoSource = nullptr;
            audioSource = nullptr;
            videoTrack = nullptr;
            audioTrack = nullptr;
            realConfigured = false;
            answerPending = false;
        }
        // El factory es compartido (lo aporta el hub); no se libera aqui.
        factory = nullptr;
#endif
        null.close();
    }

    uint64_t localAudioBlocks() const { return null.localAudioBlocks.load(std::memory_order_relaxed); }
    uint64_t localVideoFrames() const { return null.localVideoFrames.load(std::memory_order_relaxed); }
};

// ============================================================================
// WebrtcSession (public API)
// ============================================================================
WebrtcSession::WebrtcSession(int slot, int videoW, int videoH, int audioRate, int audioChannels)
    : impl_(new Impl(slot, videoW, videoH, audioRate, audioChannels)) {}

/// Ctor por defecto: sesion vacía en el slot 0 (reservado). Solo existe para
/// que `std::vector<WebrtcSession>::resize` pueda default-construct los
/// elementos; el hub los sobreescribe con el ctor de 5 args vía move-assign.
WebrtcSession::WebrtcSession() : impl_(new Impl(0, 640, 360, 48000, 2)) {}

WebrtcSession::~WebrtcSession() { delete impl_; }

WebrtcSession::WebrtcSession(WebrtcSession&& other) noexcept : impl_(other.impl_) { other.impl_ = nullptr; }

WebrtcSession& WebrtcSession::operator=(WebrtcSession&& other) noexcept {
    if (this != &other) {
        delete impl_;
        impl_ = other.impl_;
        other.impl_ = nullptr;
    }
    return *this;
}

bool WebrtcSession::configure(const SessionCallbacks& cb, void* factory) {
    return impl_ && impl_->configure(cb, factory);
}

bool WebrtcSession::setRemoteOffer(const std::string& sdp) { return impl_ && impl_->setRemoteOffer(sdp); }

bool WebrtcSession::addRemoteIce(const std::string& candidate) {
    return impl_ && impl_->addRemoteIce(candidate);
}

void WebrtcSession::pushLocalAudio(const float* samples, size_t n, int rate) {
    if (impl_) impl_->pushLocalAudio(samples, n, rate);
}

void WebrtcSession::pushLocalVideo(const uint8_t* pixels, size_t bytes, int w, int h) {
    if (impl_) impl_->pushLocalVideo(pixels, bytes, w, h);
}

bool WebrtcSession::connected() const { return impl_ && impl_->connected(); }

void WebrtcSession::close() {
    if (impl_) impl_->close();
}

int WebrtcSession::slot() const { return impl_ ? impl_->slot : 0; }

uint64_t WebrtcSession::localAudioBlocks() const { return impl_ ? impl_->localAudioBlocks() : 0; }

uint64_t WebrtcSession::localVideoFrames() const { return impl_ ? impl_->localVideoFrames() : 0; }

// ============================================================================
// WebrtcHub
// ============================================================================
#if BA_GATEWAY_HAVE_WEBRTC
namespace {
/// Estado compartido del backend real del hub: la factory y los tres threads
/// de libwebrtc. Vive en un heap allocation punteado por rtcFactory_ (void*)
/// y se libera en el dtor. Un solo hub por proceso (appliance).
struct RtcHubState {
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory;
    std::unique_ptr<rtc::Thread> networkThread;
    std::unique_ptr<rtc::Thread> workerThread;
    std::unique_ptr<rtc::Thread> signalingThread;
};
} // namespace
#endif

WebrtcHub::WebrtcHub(int guests, int videoW, int videoH, int audioRate, int audioChannels)
    : guests_(guests), videoW_(videoW), videoH_(videoH), audioRate_(audioRate),
      audioChannels_(audioChannels) {
    // Indice 0 reservado; las sesiones viven en 1..guests.
    sessions_.resize(static_cast<size_t>(guests) + 1);
    for (int s = 1; s <= guests; ++s) {
        sessions_[static_cast<size_t>(s)] = WebrtcSession(s, videoW, videoH, audioRate, audioChannels);
    }
#if BA_GATEWAY_HAVE_WEBRTC
    // Crea la webrtc::PeerConnectionFactory compartida.
    // Threads: network, worker, signaling.
    auto networkThread = rtc::Thread::Create();
    auto workerThread = rtc::Thread::Create();
    auto signalingThread = rtc::Thread::Create();
    if (networkThread) networkThread->SetName("ba-webrtc-network", nullptr);
    if (workerThread) workerThread->SetName("ba-webrtc-worker", nullptr);
    if (signalingThread) signalingThread->SetName("ba-webrtc-signaling", nullptr);
    if (networkThread) networkThread->Start();
    if (workerThread) workerThread->Start();
    if (signalingThread) signalingThread->Start();

    // Factories de codecs.
    rtc::scoped_refptr<webrtc::AudioEncoderFactory> audioEncFactory =
        webrtc::CreateBuiltinAudioEncoderFactory();
    rtc::scoped_refptr<webrtc::AudioDecoderFactory> audioDecFactory =
        webrtc::CreateBuiltinAudioDecoderFactory();
    std::unique_ptr<webrtc::VideoEncoderFactory> videoEncFactory = webrtc::CreateBuiltinVideoEncoderFactory();
    std::unique_ptr<webrtc::VideoDecoderFactory> videoDecFactory = webrtc::CreateBuiltinVideoDecoderFactory();

    // AudioProcessing.
    rtc::scoped_refptr<webrtc::AudioProcessing> ap = webrtc::AudioProcessingBuilder().Create();

    // Crea la factory (adm=nullptr, mixer=nullptr: sin captacion local).
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory = webrtc::CreatePeerConnectionFactory(
        networkThread.get(), workerThread.get(), signalingThread.get(),
        nullptr, // adm
        audioEncFactory, audioDecFactory, std::move(videoEncFactory), std::move(videoDecFactory),
        nullptr, // mixer
        ap);

    if (factory) {
        auto* state = new RtcHubState();
        state->factory = factory;
        state->networkThread = std::move(networkThread);
        state->workerThread = std::move(workerThread);
        state->signalingThread = std::move(signalingThread);
        rtcFactory_ = static_cast<void*>(state);
    } else {
        // La factory no se creo; liberamos los threads.
        if (signalingThread) signalingThread->Stop();
        if (workerThread) workerThread->Stop();
        if (networkThread) networkThread->Stop();
        rtcFactory_ = nullptr;
    }
#endif
}

WebrtcHub::~WebrtcHub() {
    closeAll();
#if BA_GATEWAY_HAVE_WEBRTC
    // Libera la factory y los threads.
    if (rtcFactory_) {
        auto* state = static_cast<RtcHubState*>(rtcFactory_);
        // Detiene los threads antes de liberar la factory (que los referencia).
        if (state->signalingThread) state->signalingThread->Stop();
        if (state->workerThread) state->workerThread->Stop();
        if (state->networkThread) state->networkThread->Stop();
        state->factory = nullptr;
        delete state;
        rtcFactory_ = nullptr;
    }
#endif
}

WebrtcSession* WebrtcHub::session(int slot) {
    if (slot < 1 || slot > guests_) return nullptr;
    return &sessions_[static_cast<size_t>(slot)];
}

void WebrtcHub::closeAll() {
    for (auto& s : sessions_)
        s.close();
}

} // namespace ba::gateway
