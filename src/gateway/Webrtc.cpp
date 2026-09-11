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

#include <atomic>

#include "Log.hpp"

#if BA_GATEWAY_HAVE_WEBRTC
// ----------------------------------------------------------------------------
// Backend REAL: libwebrtc M123 (binarios precompilados, spec §8.1, D4).
//
// IMPORTANTE: estos includes y llamadas solo se compilan cuando el SDK esta
// presente (BA_GATEWAY_HAVE_WEBRTC=1). En el build offline (BA_GATEWAY_HAVE_
// WEBRTC=0) todo este bloque se salta y se usa el backend NULL de abajo.
//
// VERIFY: los includes exactos del webrtc-build de Shiguredo M123
//   (peer_connection_interface.h, sdp_adapter.h, rtc_base/task_queue,
//    media/engine/webrtc_media_engine, api/rtc_param_types.h). El layout de
//   headers del webrtc-build puede variar; ajustar si el tag lo requiere.
#    include "api/media_stream_interface.h"
#    include "api/peer_connection_interface.h"
#    include "api/rtp_parameters.h"
#    include "media/engine/webrtc_media_engine.h"
#    include "p2p/basic_port_allocator.h"
#    include "p2p/basic_session_description.h"
#    include "p2p/ice_transport_interface.h"
#    include "p2p/port_allocator.h"
#    include "p2p/session_description.h"
#    include "p2p/stun.h"
#    include "rtc_base/asyncinvoker.h"
#    include "rtc_base/event.h"
#    include "rtc_base/logging.h"
#    include "rtc_base/location.h"
#    include "rtc_base/messagehandler.h"
#    include "rtc_base/task_queue.h"
#    include "system_wrappers/include/clock.h"
#    include "system_wrappers/include/gcc.h"
#    include "system_wrappers/include/rwlock.h"
#    include "system_wrappers/include/sleep.h"
#    include "system_wrappers/include/stats.h"
#    include "system_wrappers/include/timeutil.h"
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
        const std::string answerSdp =
            "v=0\r\n"
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

/// Callback de PeerConnection de libwebrtc para una sesion.
///
/// Despacha los eventos de ICE/estado a los callbacks de alto nivel. Captura
/// cualquier excepcion en el borde (spec §14: no cruzar excepciones por un
/// callback C de libwebrtc).
class SessionPeerObserver : public webrtc::PeerConnectionObserver {
public:
    SessionPeerObserver(NullSession* null, WebrtcSession* self)
        : null_(null), self_(self) {}

    void OnSignalingStateChange(webrtc::PeerConnectionState) override {
        // VERIFY: la transicion a Stable indica que el answer se envio.
    }

    void OnIceConnectionChange(webrtc::IceConnectionState s) override {
        if (!null_) return;
        const bool up = (s == webrtc::IceConnectionState::kConnected) ||
                        (s == webrtc::IceConnectionState::kCompleted);
        try {
            if (null_->cb.onStateChange) null_->cb.onStateChange(up);
        } catch (...) {
            // No cruzar excepcion por el callback de libwebrtc.
        }
    }

    void OnIceGatheringChange(webrtc::IceGatheringState s) override {
        // Cuando el gathering termina, el localDescription contiene todos los
        // candidatos; se envia el answer (con ICE integrado) al invitado.
        if (!null_ || s != webrtc::IceGatheringState::kComplete) return;
        try {
            // VERIFY: obtener el localDescription completo tras el gathering.
            if (null_->cb.onLocalAnswer) null_->cb.onLocalAnswer("");
        } catch (...) {
        }
    }

    void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override {
        if (!null_ || !candidate) return;
        try {
            if (null_->cb.onLocalIce) {
                // VERIFY: serializar el candidato a JSON (sdp + sdpMid).
                null_->cb.onLocalIce(candidate->sdp());
            }
        } catch (...) {
        }
    }

    void OnDataChannel(webrtc::DataChannelInterface*) override {}
    void OnRenegotiationNeeded() override {}
    void OnTrack(webrtc::RtpTransceiverInterface*) override {
        // VERIFY: aqui se engancharian los callbacks onRemoteAudio/onRemoteVideo
        // (lectura de frames RTP de los tracks remotos). En el backend real el
        // decode + entrega de frames vive en el motor de medios (spec §8.5/§8.6).
    }

private:
    NullSession* null_;
    WebrtcSession* self_;
};

} // namespace
#endif // BA_GATEWAY_HAVE_WEBRTC

// ============================================================================
// WebrtcSession::Impl — unifica el backend real y el NULL
// ============================================================================
struct WebrtcSession::Impl {
    NullSession null; // siempre presente: guarda callbacks + estadisticas
#if BA_GATEWAY_HAVE_WEBRTC
    // Estado del backend real (solo se usa cuando BA_GATEWAY_HAVE_WEBRTC=1).
    webrtc::PeerConnectionInterface* pc = nullptr;
    webrtc::PeerConnectionObserver* observer = nullptr;
    webrtc::PeerConnectionFactory* factory = nullptr;
    bool realConfigured = false;
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
            factory = static_cast<webrtc::PeerConnectionFactory*>(factory);
            // VERIFY: crear la PeerConnection con una RTCConfiguration local
            // (ICE servers vacios: el medio no pasa por el tunel, spec §8.4;
            // STUN/TURN se agregan si el sitio los tiene).
            webrtc::PeerConnectionInterface::RTCConfiguration config;
            webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
            options.ice_restart = false;
            options vox = 0; // VERIFY: campo correcto de VOIP
            pc = factory->CreatePeerConnection(config, &observer).release();
            if (!pc) return false;
            realConfigured = true;
            // VERIFY: agregar los tracks LOCALES de retorno (audio + video) con
            // las factories de audio/video de libwebrtc. El MediaPump los
            // alimenta via pushLocalAudio/pushLocalVideo.
        }
#endif
        return null.configure(cb, slot);
    }

    bool setRemoteOffer(const std::string& sdp) {
#if BA_GATEWAY_HAVE_WEBRTC
        if (realConfigured && pc) {
            try {
                // VERIFY: BasicSessionDescription::Create + SetRemoteDescription,
                // luego CreateAnswer con un callback que envia el answer.
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
                // VERIFY: IceCandidateInterface::Create + AddIceCandidate.
                return true;
            } catch (...) {
                return false;
            }
        }
#endif
        return null.addRemoteIce(candidate);
    }

    void pushLocalAudio(const float* samples, size_t n, int rate) {
#if BA_GATEWAY_HAVE_WEBRTC
        if (realConfigured) {
            // VERIFY: entregar los samples al track local de audio
            // (AudioTrack->AddFrame o el ADM de libwebrtc).
        }
#endif
        null.pushLocalAudio(samples, n, rate);
    }

    void pushLocalVideo(const uint8_t* pixels, size_t bytes, int w, int h) {
#if BA_GATEWAY_HAVE_WEBRTC
        if (realConfigured) {
            // VERIFY: entregar el frame al track local de video
            // (VideoTrack->AddFrame con webrtc::VideoFrame).
        }
#endif
        null.pushLocalVideo(pixels, bytes, w, h);
    }

    bool connected() const { return null.connected_(); }

    void close() {
#if BA_GATEWAY_HAVE_WEBRTC
        if (realConfigured && pc) {
            // VERIFY: pc->Close() / DeletePeerConnection + liberar observer.
            pc = nullptr;
            realConfigured = false;
        }
#endif
        null.close();
    }

    uint64_t localAudioBlocks() const {
        return null.localAudioBlocks.load(std::memory_order_relaxed);
    }
    uint64_t localVideoFrames() const {
        return null.localVideoFrames.load(std::memory_order_relaxed);
    }
};

// ============================================================================
// WebrtcSession (public API)
// ============================================================================
WebrtcSession::WebrtcSession(int slot, int videoW, int videoH, int audioRate,
                             int audioChannels)
    : impl_(new Impl(slot, videoW, videoH, audioRate, audioChannels)) {}

/// Ctor por defecto: sesion vacía en el slot 0 (reservado). Solo existe para
/// que `std::vector<WebrtcSession>::resize` pueda default-construct los
/// elementos; el hub los sobreescribe con el ctor de 5 args vía move-assign.
WebrtcSession::WebrtcSession()
    : impl_(new Impl(0, 640, 360, 48000, 2)) {}

WebrtcSession::~WebrtcSession() { delete impl_; }

WebrtcSession::WebrtcSession(WebrtcSession&& other) noexcept
    : impl_(other.impl_) {
    other.impl_ = nullptr;
}

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

bool WebrtcSession::setRemoteOffer(const std::string& sdp) {
    return impl_ && impl_->setRemoteOffer(sdp);
}

bool WebrtcSession::addRemoteIce(const std::string& candidate) {
    return impl_ && impl_->addRemoteIce(candidate);
}

void WebrtcSession::pushLocalAudio(const float* samples, size_t n, int rate) {
    if (impl_) impl_->pushLocalAudio(samples, n, rate);
}

void WebrtcSession::pushLocalVideo(const uint8_t* pixels, size_t bytes, int w,
                                   int h) {
    if (impl_) impl_->pushLocalVideo(pixels, bytes, w, h);
}

bool WebrtcSession::connected() const { return impl_ && impl_->connected(); }

void WebrtcSession::close() {
    if (impl_) impl_->close();
}

int WebrtcSession::slot() const { return impl_ ? impl_->slot : 0; }

uint64_t WebrtcSession::localAudioBlocks() const {
    return impl_ ? impl_->localAudioBlocks() : 0;
}

uint64_t WebrtcSession::localVideoFrames() const {
    return impl_ ? impl_->localVideoFrames() : 0;
}

// ============================================================================
// WebrtcHub
// ============================================================================
WebrtcHub::WebrtcHub(int guests, int videoW, int videoH, int audioRate,
                     int audioChannels)
    : guests_(guests),
      videoW_(videoW),
      videoH_(videoH),
      audioRate_(audioRate),
      audioChannels_(audioChannels) {
    // Indice 0 reservado; las sesiones viven en 1..guests.
    sessions_.resize(static_cast<size_t>(guests) + 1);
    for (int s = 1; s <= guests; ++s) {
        sessions_[static_cast<size_t>(s)] =
            WebrtcSession(s, videoW, videoH, audioRate, audioChannels);
    }
#if BA_GATEWAY_HAVE_WEBRTC
    // VERIFY: crear la webrtc::PeerConnectionFactory compartida (con las
    // task queues de libwebrtc y el WebRTCMediaEngine). Se libera en el
    // destructor.
    rtcFactory_ = nullptr;
#endif
}

WebrtcHub::~WebrtcHub() {
    closeAll();
#if BA_GATEWAY_HAVE_WEBRTC
    // VERIFY: liberar la factory (DeletePeerConnectionFactory).
    rtcFactory_ = nullptr;
#endif
}

WebrtcSession* WebrtcHub::session(int slot) {
    if (slot < 1 || slot > guests_) return nullptr;
    return &sessions_[static_cast<size_t>(slot)];
}

void WebrtcHub::closeAll() {
    for (auto& s : sessions_) s.close();
}

} // namespace ba::gateway
