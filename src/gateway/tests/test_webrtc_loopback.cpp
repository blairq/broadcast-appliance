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
//
// Necesita al menos una interfaz de red (se permite loopback). Sin conexion en
// el plazo, SKIP (77): en un CI sin red no se puede probar RTP.

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
            phase_ += 2 * M_PI * 440.0 / 48000.0;
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

/// Cuenta frames que recibe el "navegador" del retorno.
class CountingSink : public rtc::VideoSinkInterface<webrtc::VideoFrame> {
   public:
    void OnFrame(const webrtc::VideoFrame&) override { frames++; }
    std::atomic<int> frames{0};
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
                    .dump());
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
    ::setenv("BA_WEBRTC_ALLOW_LOOPBACK", "1", 1);

    // --- Lado B: el hub y una sesion, como en el gateway ------------------------
    WebrtcHub hub(1, 640, 360, 48000, 2);
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
    std::thread feeder([&]() {
        int tick = 0;
        std::vector<uint8_t> ret(640 * 360 * 4, 0x80);
        std::vector<float> retAudio(960, 0.1f);
        while (!stop) {
            mic->push10ms();
            sess->pushLocalAudio(retAudio.data(), retAudio.size(), 48000);
            if (tick % 3 == 0) {
                cam->push(640, 360);
                if (screen) screen->push(1280, 720);
                sess->pushLocalVideo(ret.data(), ret.size(), 640, 360);
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
