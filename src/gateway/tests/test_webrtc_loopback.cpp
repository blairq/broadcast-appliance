// test_webrtc_loopback.cpp
//
// El backend WebRTC REAL de punta a punta, en un solo proceso: una
// PeerConnection hace de NAVEGADOR del invitado (offerer) y del otro lado esta
// WebrtcSession, exactamente como la usa el gateway. Todo el medio viaja por
// RTP/SRTP real sobre UDP (candidatos host), codificado y decodificado por
// libwebrtc.
//
// Verifica:
//   1. Senalizacion: offer -> answer y candidatos ICE por goteo en ambos
//      sentidos; la sesion informa "conectada".
//   2. Uplink: la CAMARA del invitado llega por onRemoteVideo (BGRA, tamaño y
//      color correctos) y su MICROFONO por onRemoteAudio (estereo, con señal).
//   3. Renegociacion: el invitado suma una PANTALLA compartida (segundo track
//      de video); el re-offer se contesta y llega por onRemoteScreen, separada
//      de la camara.
//   4. Retorno: lo que B empuja con pushLocalVideo/pushLocalAudio llega al
//      invitado (su track remoto recibe frames y audio).
//   5. Espectadores (relay): una PeerConnection "espectadora" (recvonly) ve la
//      CAMARA del invitado (rojo, 640x360: no es el retorno gris de B) y otra
//      su PANTALLA (1280 de ancho), con los cuadros reenviados codificados
//      (keyframe pedido aguas arriba al abrir). Cerrar el espectador lo saca.
//   6. La camara en DOS capas (Capas.hpp): dos transceptores (alta 640x360, baja
//      320x180). El navegador manda en la baja un clon de la pista de la cámara
//      (06-canal-web.md §7); acá los dos llevan la misma pista porque lo que se
//      prueba es lo que recibe B, no el ritmo de la fuente; B elige cual sale y le pide al navegador que
//      prenda o pause cada una (onCapas). Al pasar de la baja a la alta no hay
//      corte (la baja sale hasta que llega la alta) y despues la baja se pausa.
//   7. Por que dos transceptores: ante una oferta con simulcast (a=rid +
//      a=simulcast) libwebrtc contesta SIN a=simulcast (no lo recibe).
//   8. Retorno compartido (RetornoCompartido.hpp): el video que B manda sale de un
//      FlujoRetorno, codificado UNA vez por capa. El invitado pasa del codificador
//      propio al compartido sin corte; sube a la capa de 720p cuando su red lo deja
//      (el control de congestión sondea); con el tope del operador baja a 180p y al
//      sacarlo vuelve; un segundo invitado recibe un cuadro clave enseguida y los dos
//      reciben la MISMA codificación (la capa codifica un cuadro por emisión, no uno
//      por invitado).
//
// Necesita al menos una interfaz de red (se permite loopback). Sin conexion en
// el plazo, SKIP (77): en un CI sin red no se puede probar RTP.

#include <numbers>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/create_peerconnection_factory.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/notifier.h"
#include "api/peer_connection_interface.h"
#include "api/rtp_transceiver_interface.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "api/video/i420_buffer.h"
#include "api/video_codecs/video_decoder_factory_template.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_encoder_factory_template.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp8_adapter.h"
#include "media/base/adapted_video_track_source.h"
#include "modules/audio_device/include/audio_device.h"
#include "rtc_base/thread.h"
#include "rtc_base/time_utils.h"

#include "Webrtc.hpp"

using json = nlohmann::json;
using ba::gateway::SessionCallbacks;
using ba::gateway::WebrtcHub;

namespace {

int g_failures = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_failures;                                                                                    \
        } else {                                                                                             \
            std::printf("ok: %s\n", msg);                                                                    \
        }                                                                                                    \
    } while (0)

/// Fuente de video sintetica del "navegador": un color liso.
class ColorSource : public rtc::AdaptedVideoTrackSource {
   public:
    ColorSource(uint8_t y, uint8_t u, uint8_t v) : y_(y), u_(u), v_(v) {}
    void push(int w, int h) {
        auto b = webrtc::I420Buffer::Create(w, h);
        webrtc::I420Buffer::SetBlack(b.get());
        std::memset(b->MutableDataY(), y_, static_cast<size_t>(b->StrideY()) * h);
        std::memset(b->MutableDataU(), u_, static_cast<size_t>(b->StrideU()) * ((h + 1) / 2));
        std::memset(b->MutableDataV(), v_, static_cast<size_t>(b->StrideV()) * ((h + 1) / 2));
        OnFrame(webrtc::VideoFrame::Builder()
                    .set_video_frame_buffer(b)
                    .set_timestamp_us(rtc::TimeMicros())
                    .build());
    }
    SourceState state() const override { return kLive; }
    bool remote() const override { return false; }
    bool is_screencast() const override { return false; }
    absl::optional<bool> needs_denoising() const override { return false; }

   private:
    uint8_t y_, u_, v_;
};

/// Fuente de audio sintetica del "navegador": un seno de 440 Hz, mono.
class ToneSource : public webrtc::Notifier<webrtc::AudioSourceInterface> {
   public:
    void push10ms() {
        int16_t pcm[480];
        for (int i = 0; i < 480; ++i) {
            pcm[i] = static_cast<int16_t>(12000 * std::sin(phase_));
            phase_ += 2 * std::numbers::pi * 440.0 / 48000.0;
        }
        std::lock_guard<std::mutex> lock(m_);
        for (auto* s : sinks_)
            s->OnData(pcm, 16, 48000, 1, 480);
    }
    SourceState state() const override { return kLive; }
    bool remote() const override { return false; }
    void AddSink(webrtc::AudioTrackSinkInterface* s) override {
        std::lock_guard<std::mutex> lock(m_);
        sinks_.push_back(s);
    }
    void RemoveSink(webrtc::AudioTrackSinkInterface* s) override {
        std::lock_guard<std::mutex> lock(m_);
        sinks_.erase(std::remove(sinks_.begin(), sinks_.end(), s), sinks_.end());
    }

   private:
    std::mutex m_;
    std::vector<webrtc::AudioTrackSinkInterface*> sinks_;
    double phase_ = 0;
};

/// Cuenta frames que recibe el "navegador" del retorno (decodificados) y su tamaño.
class CountingSink : public rtc::VideoSinkInterface<webrtc::VideoFrame> {
   public:
    void OnFrame(const webrtc::VideoFrame& f) override {
        w = f.width();
        h = f.height();
        if (frames.load() == 0) primeroMs = rtc::TimeMillis();
        frames++;
    }
    std::atomic<int> frames{0}, w{0}, h{0};
    std::atomic<int64_t> primeroMs{0};
};

/// Sonda de un espectador: cuenta cuadros y guarda tamaño y color central.
class ProbeSink : public rtc::VideoSinkInterface<webrtc::VideoFrame> {
   public:
    void OnFrame(const webrtc::VideoFrame& f) override {
        auto i420 = f.video_frame_buffer()->ToI420();
        w = i420->width();
        h = i420->height();
        v = i420->DataV()[(i420->height() / 4) * i420->StrideV() + i420->width() / 4];
        frames++;
    }
    std::atomic<int> frames{0}, w{0}, h{0}, v{0};
};

/// Espectador: PeerConnection recvonly (como la vista web).
class Viewer : public webrtc::PeerConnectionObserver {
   public:
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc;
    ProbeSink sink;
    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState) override {}
    void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface>) override {}
    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState) override {}
    void OnIceCandidate(const webrtc::IceCandidateInterface*) override {}
    void OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> t) override {
        auto track = t->receiver()->track();
        if (track && track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind)
            static_cast<webrtc::VideoTrackInterface*>(track.get())
                ->AddOrUpdateSink(&sink, rtc::VideoSinkWants());
    }
};

/// Lado navegador: PeerConnection offerer + su observador.
class Browser : public webrtc::PeerConnectionObserver {
   public:
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc;
    std::function<void(const std::string&)> sendIce;
    CountingSink returnSink;
    std::atomic<bool> gotReturnVideo{false};

    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState) override {}
    void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface>) override {}
    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState) override {}
    void OnIceCandidate(const webrtc::IceCandidateInterface* c) override {
        std::string line;
        c->ToString(&line);
        if (sendIce)
            sendIce(
                json{{"candidate", line}, {"sdpMid", c->sdp_mid()}, {"sdpMLineIndex", c->sdp_mline_index()}}
                    .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    }
    void OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> t) override {
        auto track = t->receiver()->track();
        if (track && track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind &&
            !gotReturnVideo.exchange(true)) {
            static_cast<webrtc::VideoTrackInterface*>(track.get())
                ->AddOrUpdateSink(&returnSink, rtc::VideoSinkWants());
        }
    }
};

template <typename Fn> struct CsdObs : webrtc::CreateSessionDescriptionObserver {
    explicit CsdObs(Fn f) : fn(std::move(f)) {}
    void OnSuccess(webrtc::SessionDescriptionInterface* d) override { fn(d); }
    void OnFailure(webrtc::RTCError) override {}
    Fn fn;
};
struct SetObs : webrtc::SetSessionDescriptionObserver {
    void OnSuccess() override {}
    void OnFailure(webrtc::RTCError e) override { std::printf("set desc fallo: %s\n", e.message()); }
};

bool waitFor(const std::function<bool()>& cond, int ms) {
    for (int i = 0; i < ms / 20; ++i) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return cond();
}

} // namespace

int main() {
    std::printf("== test_webrtc_loopback (libwebrtc real) ==\n");
#ifdef _WIN32
    _putenv_s("BA_WEBRTC_ALLOW_LOOPBACK", "1");
#else
    ::setenv("BA_WEBRTC_ALLOW_LOOPBACK", "1", 1);
#endif

    // --- Lado B: el hub y una sesion, como en el gateway ------------------------
    WebrtcHub hub(2, 640, 360, 48000, 2);
    CHECK(hub.rtcFactory() != nullptr, "factory de libwebrtc creada");
    auto* sess = hub.session(1);

    std::mutex m;
    std::string answerSdp;
    std::vector<std::string> bIce;
    std::atomic<bool> connected{false};
    std::atomic<int> camFrames{0}, screenFrames{0}, audioBlocks{0};
    std::atomic<int> camW{0}, camH{0}, camB{0}, camG{0}, camR{0};
    std::atomic<int> screenW{0};
    std::atomic<double> audioPeak{0};
    std::atomic<int> answers{0};

    SessionCallbacks cb;
    cb.onLocalAnswer = [&](const std::string& s) {
        std::lock_guard<std::mutex> l(m);
        answerSdp = s;
        answers++;
    };
    cb.onLocalIce = [&](const std::string& c) {
        std::lock_guard<std::mutex> l(m);
        bIce.push_back(c);
    };
    cb.onStateChange = [&](bool up) { connected = up; };
    cb.onRemoteVideo = [&](const uint8_t* px, size_t, int w, int h) {
        camW = w;
        camH = h;
        const uint8_t* c = px + (static_cast<size_t>(h / 2) * w + w / 2) * 4;
        camB = c[0];
        camG = c[1];
        camR = c[2];
        camFrames++;
    };
    cb.onRemoteScreen = [&](const uint8_t*, size_t, int w, int) {
        screenW = w;
        screenFrames++;
    };
    cb.onRemoteAudio = [&](const float* s, size_t n, int) {
        double peak = 0;
        for (size_t i = 0; i < n; ++i)
            peak = std::max(peak, static_cast<double>(std::fabs(s[i])));
        if (peak > audioPeak) audioPeak = peak;
        audioBlocks++;
    };
    CHECK(sess->configure(cb, hub.rtcFactory()), "sesion configurada con la factory real");

    // --- Lado navegador ----------------------------------------------------------
    auto net = rtc::Thread::CreateWithSocketServer();
    auto work = rtc::Thread::Create();
    auto sig = rtc::Thread::Create();
    net->Start();
    work->Start();
    sig->Start();
    auto tqf = webrtc::CreateDefaultTaskQueueFactory();
    auto adm = work->BlockingCall([&]() {
        return webrtc::AudioDeviceModule::Create(webrtc::AudioDeviceModule::kDummyAudio, tqf.get());
    });
    auto bf = webrtc::CreatePeerConnectionFactory(
        net.get(), work.get(), sig.get(), adm, webrtc::CreateBuiltinAudioEncoderFactory(),
        webrtc::CreateBuiltinAudioDecoderFactory(),
        std::make_unique<webrtc::VideoEncoderFactoryTemplate<webrtc::LibvpxVp8EncoderTemplateAdapter>>(),
        std::make_unique<webrtc::VideoDecoderFactoryTemplate<webrtc::LibvpxVp8DecoderTemplateAdapter>>(),
        nullptr, nullptr);
    webrtc::PeerConnectionFactoryInterface::Options opts;
    opts.network_ignore_mask = 0; // permitir loopback en el test
    bf->SetOptions(opts);

    Browser browser;
    browser.sendIce = [&](const std::string& c) { sess->addRemoteIce(c); };
    webrtc::PeerConnectionInterface::RTCConfiguration cfg;
    cfg.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
    auto pcRes = bf->CreatePeerConnectionOrError(cfg, webrtc::PeerConnectionDependencies(&browser));
    CHECK(pcRes.ok(), "PeerConnection del navegador");
    browser.pc = pcRes.MoveValue();

    // Camara: Y=81,U=90,V=240 es rojo (BT.601) -> BGRA con R alto.
    auto cam = rtc::make_ref_counted<ColorSource>(81, 90, 240);
    auto mic = rtc::make_ref_counted<ToneSource>();
    browser.pc->AddTrack(bf->CreateVideoTrack(cam, "cam"), {"guest"});
    browser.pc->AddTrack(bf->CreateAudioTrack("mic", mic.get()), {"guest"});

    std::atomic<bool> stop{false};
    rtc::scoped_refptr<ColorSource> screen;
    // 8. El retorno compartido: con `compartido`, el mismo cuadro 720p emitido UNA vez a las dos sesiones.
    const ba::gateway::FlujoRetorno flujo = hub.flujoRetorno("prueba");
    std::atomic<bool> compartido{false};
    std::thread feeder([&]() {
        int tick = 0;
        std::vector<uint8_t> ret720(1280 * 720 * 4, 0x60);
        std::vector<uint8_t> ret(640 * 360 * 4, 0x80);
        std::vector<float> retAudio(960, 0.1f);
        while (!stop) {
            mic->push10ms();
            sess->pushLocalAudio(retAudio.data(), retAudio.size(), 48000);
            if (tick % 3 == 0) {
                cam->push(640, 360);
                if (screen) screen->push(1280, 720);
                if (compartido) {
                    // Algo que se mueve (una franja que baja): con una imagen quieta el codificador
                    // casi no manda nada y el retorno no se parece a un programa.
                    const size_t fila = static_cast<size_t>((tick / 3) % 704) * 1280 * 4;
                    std::memset(ret720.data() + fila, (tick * 7) & 0xff, 1280 * 4 * 16);
                    const auto c = flujo.emitir(ba::gateway::CuadroSaliente::desdeBgra(ret720.data(), 1280, 720));
                    sess->pushLocalVideo(c);
                    hub.session(2)->pushLocalVideo(c); // sin conectar no hace nada
                } else {
                    sess->pushLocalVideo(ret.data(), ret.size(), 640, 360);
                }
            }
            ++tick;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    auto negotiate = [&](int expectAnswers) {
        std::string offerSdp;
        std::atomic<bool> done{false};
        auto pc = browser.pc;
        pc->CreateOffer(
            rtc::make_ref_counted<CsdObs<std::function<void(webrtc::SessionDescriptionInterface*)>>>(
                [&, pc](webrtc::SessionDescriptionInterface* d) {
                    d->ToString(&offerSdp);
                    pc->SetLocalDescription(rtc::make_ref_counted<SetObs>().get(), d);
                    done = true;
                })
                .get(),
            {});
        waitFor([&]() { return done.load(); }, 5000);
        CHECK(sess->setRemoteOffer(offerSdp), "B acepta el offer");
        CHECK(waitFor([&]() { return answers.load() >= expectAnswers; }, 5000), "B contesta con un answer");
        std::string a;
        {
            std::lock_guard<std::mutex> l(m);
            a = answerSdp;
        }
        webrtc::SdpParseError err;
        auto ans = webrtc::CreateSessionDescription(webrtc::SdpType::kAnswer, a, &err);
        CHECK(ans != nullptr, "el answer es SDP valido");
        pc->SetRemoteDescription(rtc::make_ref_counted<SetObs>().get(), ans.release());
    };

    negotiate(1);
    // ICE de B -> navegador (goteo).
    std::thread iceFwd([&]() {
        size_t sent = 0;
        while (!stop) {
            std::vector<std::string> pending;
            {
                std::lock_guard<std::mutex> l(m);
                pending.assign(bIce.begin() + static_cast<long>(sent), bIce.end());
                sent = bIce.size();
            }
            for (const auto& c : pending) {
                const json j = json::parse(c);
                webrtc::SdpParseError err;
                std::unique_ptr<webrtc::IceCandidateInterface> ic(
                    webrtc::CreateIceCandidate(j["sdpMid"].get<std::string>(), j["sdpMLineIndex"].get<int>(),
                                               j["candidate"].get<std::string>(), &err));
                if (ic) browser.pc->AddIceCandidate(std::move(ic), [](webrtc::RTCError) {});
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });

    if (!waitFor([&]() { return connected.load(); }, 15000)) {
        std::printf("SKIP: sin conexion ICE en 15 s (¿no hay red?)\n");
        stop = true;
        feeder.join();
        iceFwd.join();
        browser.pc->Close();
        sess->close();
        return 77;
    }
    CHECK(connected.load(), "ICE + DTLS conectados");

    CHECK(waitFor([&]() { return camFrames.load() > 10; }, 10000),
          "la camara del invitado llega a B (onRemoteVideo)");
    CHECK(camW == 640 && camH == 360, "con su resolucion (640x360)");
    std::printf("   color central BGRA = %d,%d,%d\n", camB.load(), camG.load(), camR.load());
    CHECK(camR > 180 && camG < 90 && camB < 90, "y su color (rojo) tras VP8 + I420->BGRA");
    CHECK(waitFor([&]() { return audioBlocks.load() > 50 && audioPeak.load() > 0.1; }, 10000),
          "el microfono llega a B (onRemoteAudio con señal)");
    CHECK(screenFrames.load() == 0, "sin pantalla compartida todavia");
    CHECK(waitFor([&]() { return browser.returnSink.frames.load() > 10; }, 10000),
          "el retorno de video de B llega al invitado");

    // --- Pantalla compartida: segundo track de video + re-offer ------------------
    screen = rtc::make_ref_counted<ColorSource>(145, 54, 34); // verde
    browser.pc->AddTrack(bf->CreateVideoTrack(screen, "screen"), {"screen"});
    negotiate(2);
    CHECK(waitFor([&]() { return screenFrames.load() > 10; }, 10000),
          "la pantalla llega aparte (onRemoteScreen)");
    CHECK(screenW == 1280, "con su propia resolucion (1280 de ancho)");
    const int camBefore = camFrames.load();
    CHECK(waitFor([&]() { return camFrames.load() > camBefore + 10; }, 5000),
          "la camara sigue llegando despues del re-offer");

    // --- Espectadores: relay de lo codificado (sin re-codificar) -----------------
    auto openViewer = [&](Viewer& vw, bool wantScreen) -> int {
        auto r = bf->CreatePeerConnectionOrError(cfg, webrtc::PeerConnectionDependencies(&vw));
        if (!r.ok()) return 0;
        vw.pc = r.MoveValue();
        webrtc::RtpTransceiverInit init;
        init.direction = webrtc::RtpTransceiverDirection::kRecvOnly;
        vw.pc->AddTransceiver(cricket::MEDIA_TYPE_VIDEO, init);
        std::atomic<bool> set{false};
        auto vpc = vw.pc;
        vpc->CreateOffer(
            rtc::make_ref_counted<CsdObs<std::function<void(webrtc::SessionDescriptionInterface*)>>>(
                [&, vpc](webrtc::SessionDescriptionInterface* d) {
                    vpc->SetLocalDescription(rtc::make_ref_counted<SetObs>().get(), d);
                    set = true;
                })
                .get(),
            {});
        waitFor([&]() { return set.load(); }, 5000);
        // Sin goteo (como la vista web): el offer va con sus candidatos.
        waitFor(
            [&]() {
                return vpc->ice_gathering_state() == webrtc::PeerConnectionInterface::kIceGatheringComplete;
            },
            5000);
        std::string offer, answer;
        vpc->local_description()->ToString(&offer);
        const int id = hub.openViewer(1, wantScreen, offer, answer);
        if (id <= 0) return 0;
        webrtc::SdpParseError err;
        auto ans = webrtc::CreateSessionDescription(webrtc::SdpType::kAnswer, answer, &err);
        if (!ans) return 0;
        vpc->SetRemoteDescription(rtc::make_ref_counted<SetObs>().get(), ans.release());
        return id;
    };
    Viewer camView, screenView;
    const int camId = openViewer(camView, false);
    CHECK(camId > 0, "B abre un espectador de la camara (answer completo, sin goteo)");
    CHECK(waitFor([&]() { return camView.sink.frames.load() > 10; }, 10000),
          "el espectador recibe la camara reenviada");
    std::printf("   espectador: %dx%d V=%d\n", camView.sink.w.load(), camView.sink.h.load(),
                camView.sink.v.load());
    CHECK(camView.sink.w == 640 && camView.sink.h == 360 && camView.sink.v > 180,
          "es la camara del invitado (roja), no el retorno de B");
    const int screenId = openViewer(screenView, true);
    CHECK(screenId > 0 && waitFor([&]() { return screenView.sink.frames.load() > 10; }, 10000),
          "otro espectador ve la pantalla compartida");
    CHECK(screenView.sink.w == 1280, "con la resolucion de la pantalla");
    CHECK(hub.viewerCount() == 2, "dos espectadores abiertos");
    hub.requestKeyFrame(camId); // no debe romper nada
    hub.closeViewer(camId);
    CHECK(hub.viewerCount() == 1, "cerrar un espectador lo saca");
    const int before = screenView.sink.frames.load();
    CHECK(waitFor([&]() { return screenView.sink.frames.load() > before + 5; }, 5000),
          "el otro sigue recibiendo");
    hub.closeViewersOf(1);
    CHECK(hub.viewerCount() == 0, "al irse el participante se cierran sus espectadores");
    camView.pc->Close();
    screenView.pc->Close();

    // --- Dos capas de la camara (Capas.hpp) ---------------------------------------
    {
        auto* s2 = hub.session(2);
        std::mutex m2;
        std::string answer2;
        std::vector<std::string> ice2;
        std::atomic<bool> up2{false};
        std::atomic<int> frames2{0}, w2{0};
        std::atomic<int64_t> primeraAltaMs{0};
        std::vector<std::pair<bool, bool>> pedidos;
        SessionCallbacks cb2;
        cb2.onLocalAnswer = [&](const std::string& a) {
            std::lock_guard<std::mutex> l(m2);
            answer2 = a;
        };
        cb2.onLocalIce = [&](const std::string& c) {
            std::lock_guard<std::mutex> l(m2);
            ice2.push_back(c);
        };
        cb2.onStateChange = [&](bool up) { up2 = up; };
        cb2.onRemoteVideo = [&](const uint8_t*, size_t, int w, int) {
            if (w == 640 && w2.load() != 640 && primeraAltaMs.load() == 0) primeraAltaMs = rtc::TimeMillis();
            w2 = w;
            frames2++;
        };
        Browser b2;
        rtc::scoped_refptr<webrtc::RtpSenderInterface> senderAlta, senderBaja;
        // El navegador obedece: prende o pausa cada capa (encodings[0].active).
        const auto obedecer = [&](bool alta, bool baja) {
            for (auto [snd, on] : {std::make_pair(senderAlta, alta), std::make_pair(senderBaja, baja)}) {
                if (!snd) continue;
                webrtc::RtpParameters par = snd->GetParameters();
                if (par.encodings.empty()) continue;
                par.encodings[0].active = on;
                snd->SetParameters(par);
            }
        };
        cb2.onCapas = [&](bool alta, bool baja) {
            {
                std::lock_guard<std::mutex> l(m2);
                pedidos.emplace_back(alta, baja);
            }
            obedecer(alta, baja);
        };
        CHECK(s2->configure(cb2, hub.rtcFactory()), "dos capas: sesion configurada");
        b2.sendIce = [&](const std::string& c) { s2->addRemoteIce(c); };
        auto r2 = bf->CreatePeerConnectionOrError(cfg, webrtc::PeerConnectionDependencies(&b2));
        b2.pc = r2.MoveValue();
        auto cam2 = rtc::make_ref_counted<ColorSource>(81, 90, 240);
        auto pista = bf->CreateVideoTrack(cam2, "cam2");
        b2.pc->AddTrack(bf->CreateAudioTrack("mic2", mic.get()), {"guest2"});
        webrtc::RtpTransceiverInit alta;
        alta.direction = webrtc::RtpTransceiverDirection::kSendOnly;
        alta.stream_ids = {"guest2"};
        webrtc::RtpEncodingParameters ea;
        ea.max_bitrate_bps = 1500000;
        alta.send_encodings = {ea};
        webrtc::RtpTransceiverInit baja = alta;
        webrtc::RtpEncodingParameters eb;
        eb.max_bitrate_bps = 150000;
        eb.scale_resolution_down_by = 2.0;
        eb.max_framerate = 15;
        baja.send_encodings = {eb};
        auto ta = b2.pc->AddTransceiver(pista, alta);
        auto tb = b2.pc->AddTransceiver(pista, baja); // la misma pista (el navegador usa un clon: §7)
        CHECK(ta.ok() && tb.ok(), "la misma pista en dos transceptores");
        senderAlta = ta.value()->sender();
        senderBaja = tb.value()->sender();
        std::string oferta;
        std::atomic<bool> lista{false};
        auto pc2 = b2.pc;
        pc2->CreateOffer(rtc::make_ref_counted<CsdObs<std::function<void(webrtc::SessionDescriptionInterface*)>>>(
                             [&, pc2](webrtc::SessionDescriptionInterface* d) {
                                 d->ToString(&oferta);
                                 pc2->SetLocalDescription(rtc::make_ref_counted<SetObs>().get(), d);
                                 lista = true;
                             })
                             .get(),
                         {});
        waitFor([&]() { return lista.load(); }, 5000);
        waitFor([&]() { return ta.value()->mid().has_value() && tb.value()->mid().has_value(); }, 2000);
        // Lo que el navegador manda en {type:"offer", sdp, capas:{alta, baja}}.
        s2->setCapas(ta.value()->mid().value_or(""), tb.value()->mid().value_or(""));
        CHECK(s2->conCapas(), "B sabe cual es cada capa");
        CHECK(s2->setRemoteOffer(oferta), "B acepta la oferta de dos capas");
        CHECK(waitFor([&]() {
                  std::lock_guard<std::mutex> l(m2);
                  return !answer2.empty();
              }, 5000),
              "y contesta");
        {
            std::lock_guard<std::mutex> l(m2);
            webrtc::SdpParseError err;
            auto ans = webrtc::CreateSessionDescription(webrtc::SdpType::kAnswer, answer2, &err);
            pc2->SetRemoteDescription(rtc::make_ref_counted<SetObs>().get(), ans.release());
        }
        s2->pedirCapa(ba::gateway::Capa::Baja); // en una miniatura
        std::atomic<bool> fin2{false};
        std::thread aux([&]() {
            size_t enviados = 0;
            while (!fin2) {
                cam2->push(640, 360);
                std::vector<std::string> pend;
                {
                    std::lock_guard<std::mutex> l(m2);
                    pend.assign(ice2.begin() + static_cast<long>(enviados), ice2.end());
                    enviados = ice2.size();
                }
                for (const auto& c : pend) {
                    const json j = json::parse(c);
                    webrtc::SdpParseError err;
                    std::unique_ptr<webrtc::IceCandidateInterface> ic(webrtc::CreateIceCandidate(
                        j["sdpMid"].get<std::string>(), j["sdpMLineIndex"].get<int>(), j["candidate"].get<std::string>(), &err));
                    if (ic) pc2->AddIceCandidate(std::move(ic), [](webrtc::RTCError) {});
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(33));
            }
        });
        CHECK(waitFor([&]() { return up2.load(); }, 15000), "dos capas: conectado");
        CHECK(waitFor([&]() { return frames2.load() > 10; }, 10000), "llega la camara");
        CHECK(w2 == 320, "en una miniatura sale la baja (320 de ancho)");
        {
            std::lock_guard<std::mutex> l(m2);
            CHECK(!pedidos.empty() && pedidos.front() == std::make_pair(false, true), "y al navegador se le pidio solo la baja");
        }
        CHECK(s2->capaSale() == ba::gateway::Capa::Baja, "sale la baja");
        // Pasa al aire: alta, sin negro en el medio.
        const int64_t t0 = rtc::TimeMillis();
        s2->pedirCapa(ba::gateway::Capa::Alta);
        CHECK(waitFor([&]() { return w2.load() == 640; }, 5000), "al aire sale la alta (640 de ancho)");
        const int64_t demora = primeraAltaMs.load() > 0 ? primeraAltaMs.load() - t0 : -1;
        std::printf("   de la baja a la alta: %lld ms\n", static_cast<long long>(demora));
        CHECK(demora >= 0 && demora < 1000, "la alta llega en menos de 1 s");
        {
            std::lock_guard<std::mutex> l(m2);
            CHECK(pedidos.size() >= 3 && pedidos[1] == std::make_pair(true, true),
                  "mientras tanto, las dos prendidas (la baja sigue saliendo)");
            CHECK(pedidos.back() == std::make_pair(true, false), "llegada la alta, la baja se pausa");
        }
        CHECK(s2->capaSale() == ba::gateway::Capa::Alta, "sale la alta");
        const int antes = frames2.load();
        CHECK(waitFor([&]() { return frames2.load() > antes + 10; }, 5000) && w2 == 640, "y sigue llegando, solo la alta");
        s2->pedirCapa(ba::gateway::Capa::Nada);
        {
            std::lock_guard<std::mutex> l(m2);
            CHECK(pedidos.back() == std::make_pair(false, false), "nadie lo mira: las dos pausadas");
        }
        fin2 = true;
        aux.join();
        pc2->Close();
        s2->close();

        // Simulcast con rid: libwebrtc (B) no lo recibe. El answer sale sin a=simulcast y
        // el navegador mandaria solo la primera capa: por eso dos transceptores.
        CHECK(s2->configure(cb2, hub.rtcFactory()), "sesion nueva para la prueba de simulcast");
        Browser b3;
        auto r3 = bf->CreatePeerConnectionOrError(cfg, webrtc::PeerConnectionDependencies(&b3));
        b3.pc = r3.MoveValue();
        webrtc::RtpTransceiverInit sim = alta;
        webrtc::RtpEncodingParameters l1, l2;
        l1.rid = "b";
        l1.scale_resolution_down_by = 2.0;
        l2.rid = "a";
        sim.send_encodings = {l1, l2};
        b3.pc->AddTransceiver(bf->CreateVideoTrack(cam2, "cam3"), sim);
        std::string ofertaSim;
        std::atomic<bool> lista3{false};
        auto pc3 = b3.pc;
        pc3->CreateOffer(rtc::make_ref_counted<CsdObs<std::function<void(webrtc::SessionDescriptionInterface*)>>>(
                             [&, pc3](webrtc::SessionDescriptionInterface* d) {
                                 d->ToString(&ofertaSim);
                                 pc3->SetLocalDescription(rtc::make_ref_counted<SetObs>().get(), d);
                                 lista3 = true;
                             })
                             .get(),
                         {});
        waitFor([&]() { return lista3.load(); }, 5000);
        CHECK(ofertaSim.find("a=simulcast:send") != std::string::npos, "la oferta pide simulcast");
        {
            std::lock_guard<std::mutex> l(m2);
            answer2.clear();
        }
        s2->setRemoteOffer(ofertaSim);
        waitFor([&]() {
            std::lock_guard<std::mutex> l(m2);
            return !answer2.empty();
        }, 5000);
        {
            std::lock_guard<std::mutex> l(m2);
            CHECK(!answer2.empty() && answer2.find("a=simulcast") == std::string::npos,
                  "B contesta sin a=simulcast: no recibe simulcast (por eso dos transceptores)");
        }
        pc3->Close();
        s2->close();
    }

    // --- 8. Retorno compartido ----------------------------------------------------
    {
        const auto capaDe = [&](int alto) {
            // {cuadros, en_uso} de la capa de `alto` del flujo «prueba».
            std::pair<long long, bool> out{-1, false};
            for (const auto& f : hub.retornoCompartido())
                if (f.value("nombre", "") == "prueba")
                    for (const auto& c : f["capas"])
                        if (c.value("alto", 0) == alto) out = {c.value("cuadros", 0LL), c.value("en_uso", false)};
            return out;
        };
        const auto emisiones = [&]() {
            for (const auto& f : hub.retornoCompartido())
                if (f.value("nombre", "") == "prueba") return f.value("emisiones", 0LL);
            return 0LL;
        };
        const int antes = browser.returnSink.frames.load();
        compartido = true;
        CHECK(waitFor([&]() { return browser.returnSink.frames.load() > antes + 10 && browser.returnSink.w.load() != 640; },
                      10000),
              "compartido: el invitado pasa al retorno compartido sin corte (cuadros del flujo)");
        const json est = json::parse(sess->estadisticas(), nullptr, false);
        std::printf("   estadisticas: %s\n", est.dump().c_str());
        CHECK(est.contains("retorno") && est["retorno"].value("fuente", "") == "compartido", "y lo informa");
        CHECK(waitFor([&]() { return browser.returnSink.h.load() == 720; }, 25000),
              "su red aguanta: sube a la capa de 720p (control de congestión + sondeo)");
        std::printf("   capa: %dx%d\n", browser.returnSink.w.load(), browser.returnSink.h.load());

        // El tope del operador (calidad-*): a 180p ya, y al sacarlo vuelve.
        sess->setTopeRetorno(180);
        CHECK(waitFor([&]() { return browser.returnSink.h.load() == 180; }, 5000), "tope 180: baja a la capa de 180p");
        const int a180 = browser.returnSink.frames.load();
        CHECK(waitFor([&]() { return browser.returnSink.frames.load() > a180 + 20; }, 5000) &&
                  browser.returnSink.h.load() == 180,
              "y sigue llegando en 180p");
        sess->setTopeRetorno(0);
        CHECK(waitFor([&]() { return browser.returnSink.h.load() == 720; }, 15000), "sin tope: vuelve a 720p");

        // Un segundo invitado: cuadro clave enseguida, la misma codificación.
        auto* s4 = hub.session(2);
        std::mutex m4;
        std::string answer4;
        std::vector<std::string> ice4;
        std::atomic<bool> up4{false};
        std::atomic<int64_t> up4Ms{0};
        SessionCallbacks cb4;
        cb4.onLocalAnswer = [&](const std::string& a) {
            std::lock_guard<std::mutex> l(m4);
            answer4 = a;
        };
        cb4.onLocalIce = [&](const std::string& c) {
            std::lock_guard<std::mutex> l(m4);
            ice4.push_back(c);
        };
        cb4.onStateChange = [&](bool up) {
            if (up && up4Ms.load() == 0) up4Ms = rtc::TimeMillis();
            up4 = up;
        };
        CHECK(s4->configure(cb4, hub.rtcFactory()), "compartido: segunda sesion");
        Browser b4;
        b4.sendIce = [&](const std::string& c) { s4->addRemoteIce(c); };
        auto r4 = bf->CreatePeerConnectionOrError(cfg, webrtc::PeerConnectionDependencies(&b4));
        b4.pc = r4.MoveValue();
        auto cam4 = rtc::make_ref_counted<ColorSource>(81, 90, 240);
        b4.pc->AddTrack(bf->CreateVideoTrack(cam4, "cam4"), {"guest4"});
        std::string oferta4;
        std::atomic<bool> lista4{false};
        auto pc4 = b4.pc;
        pc4->CreateOffer(rtc::make_ref_counted<CsdObs<std::function<void(webrtc::SessionDescriptionInterface*)>>>(
                             [&, pc4](webrtc::SessionDescriptionInterface* d) {
                                 d->ToString(&oferta4);
                                 pc4->SetLocalDescription(rtc::make_ref_counted<SetObs>().get(), d);
                                 lista4 = true;
                             })
                             .get(),
                         {});
        waitFor([&]() { return lista4.load(); }, 5000);
        CHECK(s4->setRemoteOffer(oferta4), "B acepta la oferta del segundo");
        CHECK(waitFor([&]() {
                  std::lock_guard<std::mutex> l(m4);
                  return !answer4.empty();
              }, 5000),
              "y contesta");
        {
            std::lock_guard<std::mutex> l(m4);
            webrtc::SdpParseError err;
            auto ans = webrtc::CreateSessionDescription(webrtc::SdpType::kAnswer, answer4, &err);
            pc4->SetRemoteDescription(rtc::make_ref_counted<SetObs>().get(), ans.release());
        }
        std::atomic<bool> fin4{false};
        std::thread aux4([&]() {
            size_t enviados = 0;
            while (!fin4) {
                cam4->push(320, 180);
                std::vector<std::string> pend;
                {
                    std::lock_guard<std::mutex> l(m4);
                    pend.assign(ice4.begin() + static_cast<long>(enviados), ice4.end());
                    enviados = ice4.size();
                }
                for (const auto& c : pend) {
                    const json j = json::parse(c);
                    webrtc::SdpParseError err;
                    std::unique_ptr<webrtc::IceCandidateInterface> ic(webrtc::CreateIceCandidate(
                        j["sdpMid"].get<std::string>(), j["sdpMLineIndex"].get<int>(), j["candidate"].get<std::string>(), &err));
                    if (ic) pc4->AddIceCandidate(std::move(ic), [](webrtc::RTCError) {});
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(33));
            }
        });
        CHECK(waitFor([&]() { return up4.load(); }, 15000), "compartido: el segundo conecta");
        CHECK(waitFor([&]() { return b4.returnSink.frames.load() > 10; }, 10000), "y recibe el retorno (decodificado)");
        const long long alPrimero = b4.returnSink.primeroMs.load() - up4Ms.load();
        std::printf("   segundo invitado: primer cuadro a %lld ms de conectar\n", alPrimero);
        CHECK(alPrimero >= 0 && alPrimero < 1500, "con un cuadro clave enseguida (< 1,5 s)");
        CHECK(waitFor([&]() { return b4.returnSink.h.load() == 720; }, 25000), "el segundo tambien llega a 720p");

        // Los dos en 720p: la capa codifica UN cuadro por emisión, no uno por invitado.
        const auto c0 = capaDe(720);
        const long long e0 = emisiones();
        const int f1 = browser.returnSink.frames.load(), f2 = b4.returnSink.frames.load();
        std::this_thread::sleep_for(std::chrono::seconds(3));
        const auto c1 = capaDe(720);
        const long long e1 = emisiones();
        const int g1 = browser.returnSink.frames.load() - f1, g2 = b4.returnSink.frames.load() - f2;
        std::printf("   3 s: %lld emisiones, %lld cuadros codificados en 720p, recibidos %d y %d\n", e1 - e0,
                    c1.first - c0.first, g1, g2);
        CHECK(c1.second && c1.first - c0.first > 0 && c1.first - c0.first <= e1 - e0,
              "una codificación por emisión para los dos");
        CHECK(g1 > 30 && g2 > 30, "y los dos la reciben");

        fin4 = true;
        aux4.join();
        pc4->Close();
        s4->close();
        compartido = false;
    }

    stop = true;
    feeder.join();
    iceFwd.join();
    browser.pc->Close();
    sess->close();
    CHECK(!connected.load(), "cerrar la sesion informa desconexion");

    if (g_failures == 0) {
        std::printf("test_webrtc_loopback: OK\n");
        return 0;
    }
    std::printf("test_webrtc_loopback: %d fallo(s)\n", g_failures);
    return 1;
}
