// test_webrtc_capas.cpp — las capas de la red de WebRTC y el TURN en memoria (F1/F2 de
// docs/planes/PLAN-GATEWAY-FRAGMENTOS.md), con libwebrtc de verdad
//
//   1. Varias fábricas comparten hilos (M123): con gateway.hilosRed = 2 y hilosTrabajo = 2 el
//      hub arma 4 fábricas sobre 2 + 2 hilos; 4 «navegadores» se conectan directo (loopback)
//      y a todos les llega el audio (un solo reloj de reproducción para las 4 fábricas).
//   2. El reparto: las 4 sesiones quedan 2 y 2 en cada capa; cerrar una la suelta.
//   3. El TURN en los hilos de red con la entrega en memoria: 2 navegadores más entran SOLO
//      por relay (uno por UDP y otro por TCP) y conectan; el TURN cuenta paquetes en memoria
//      en los dos sentidos y el conmutador los ve.
//
// Sin conexión en el plazo (sin red), SKIP (77).
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <numbers>
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
#include "api/video_codecs/video_decoder_factory_template.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_encoder_factory_template.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp8_adapter.h"
#include "modules/audio_device/include/audio_device.h"
#include "rtc_base/thread.h"

#include "Webrtc.hpp"
#include "ajustes/Ajustes.hpp"
#include "rtc/Conmutador.hpp"
#include "turn/Ice.hpp"
#include "turn/Servidor.hpp"
#include "net/NetUtil.hpp"

using json = nlohmann::json;
namespace gw = ba::gateway;

namespace {

int g_fallas = 0;
#define CHECK(c, msg)                                                                                                  \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::printf("FALLA (línea %d): %s\n", __LINE__, msg);                                                      \
            ++g_fallas;                                                                                                \
        } else {                                                                                                       \
            std::printf("ok: %s\n", msg);                                                                              \
        }                                                                                                              \
    } while (0)

bool esperar(const std::function<bool()>& c, int ms) {
    for (int i = 0; i < ms / 20; ++i) {
        if (c()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return c();
}

/// El micrófono del «navegador»: un seno de 440 Hz.
class Tono : public webrtc::Notifier<webrtc::AudioSourceInterface> {
   public:
    void push10ms() {
        int16_t pcm[480];
        for (int i = 0; i < 480; ++i) {
            pcm[i] = static_cast<int16_t>(12000 * std::sin(fase_));
            fase_ += 2 * std::numbers::pi * 440.0 / 48000.0;
        }
        std::lock_guard<std::mutex> l(m_);
        for (auto* s : sinks_) s->OnData(pcm, 16, 48000, 1, 480);
    }
    SourceState state() const override { return kLive; }
    bool remote() const override { return false; }
    void AddSink(webrtc::AudioTrackSinkInterface* s) override {
        std::lock_guard<std::mutex> l(m_);
        sinks_.push_back(s);
    }
    void RemoveSink(webrtc::AudioTrackSinkInterface* s) override {
        std::lock_guard<std::mutex> l(m_);
        sinks_.erase(std::remove(sinks_.begin(), sinks_.end(), s), sinks_.end());
    }

   private:
    std::mutex m_;
    std::vector<webrtc::AudioTrackSinkInterface*> sinks_;
    double fase_ = 0;
};

template <typename Fn> struct CsdObs : webrtc::CreateSessionDescriptionObserver {
    explicit CsdObs(Fn f) : fn(std::move(f)) {}
    void OnSuccess(webrtc::SessionDescriptionInterface* d) override { fn(d); }
    void OnFailure(webrtc::RTCError) override {}
    Fn fn;
};
struct SetObs : webrtc::SetSessionDescriptionObserver {
    void OnSuccess() override {}
    void OnFailure(webrtc::RTCError e) override { std::printf("set desc falló: %s\n", e.message()); }
};

/// Un navegador conectado a una sesión del hub.
struct Invitado : public webrtc::PeerConnectionObserver {
    gw::WebrtcSession* sess = nullptr;
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc;
    std::mutex m;
    std::string answer;
    std::vector<std::string> iceDeB;
    std::atomic<bool> conectada{false};
    std::atomic<int> bloques{0};
    std::atomic<double> pico{0};

    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState) override {}
    void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface>) override {}
    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState) override {}
    void OnIceCandidate(const webrtc::IceCandidateInterface* c) override {
        std::string line;
        c->ToString(&line);
        sess->addRemoteIce(json{{"candidate", line}, {"sdpMid", c->sdp_mid()}, {"sdpMLineIndex", c->sdp_mline_index()}}
                               .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    }

    gw::SessionCallbacks callbacks() {
        gw::SessionCallbacks cb;
        cb.onLocalAnswer = [this](const std::string& s) {
            std::lock_guard<std::mutex> l(m);
            answer = s;
        };
        cb.onLocalIce = [this](const std::string& c) {
            std::lock_guard<std::mutex> l(m);
            iceDeB.push_back(c);
        };
        cb.onStateChange = [this](bool up) { conectada = up; };
        cb.onRemoteAudio = [this](const float* s, size_t n, int) {
            double p = 0;
            for (size_t i = 0; i < n; ++i) p = std::max(p, static_cast<double>(std::fabs(s[i])));
            if (p > pico) pico = p;
            bloques++;
        };
        return cb;
    }

    /// offer → answer; después los candidatos de B van al navegador (llamar seguido a pasarIce).
    bool negociar() {
        std::string oferta;
        std::atomic<bool> lista{false};
        auto p = pc;
        p->CreateOffer(rtc::make_ref_counted<CsdObs<std::function<void(webrtc::SessionDescriptionInterface*)>>>(
                           [&, p](webrtc::SessionDescriptionInterface* d) {
                               d->ToString(&oferta);
                               p->SetLocalDescription(rtc::make_ref_counted<SetObs>().get(), d);
                               lista = true;
                           })
                           .get(),
                       {});
        if (!esperar([&] { return lista.load(); }, 5000)) return false;
        if (!sess->setRemoteOffer(oferta)) return false;
        if (!esperar(
                [&] {
                    std::lock_guard<std::mutex> l(m);
                    return !answer.empty();
                },
                5000))
            return false;
        std::string a;
        {
            std::lock_guard<std::mutex> l(m);
            a = answer;
        }
        webrtc::SdpParseError err;
        auto ans = webrtc::CreateSessionDescription(webrtc::SdpType::kAnswer, a, &err);
        if (!ans) return false;
        p->SetRemoteDescription(rtc::make_ref_counted<SetObs>().get(), ans.release());
        return true;
    }
    size_t pasados = 0;
    void pasarIce() {
        // Antes del answer los candidatos se pierden («can't be added without any remote
        // session description»): con política relay, sin los candidatos de B el navegador
        // nunca pide permiso en el TURN y nada de lo que B le manda al relay pasa.
        if (!pc || pc->remote_description() == nullptr) return;
        std::vector<std::string> pend;
        {
            std::lock_guard<std::mutex> l(m);
            pend.assign(iceDeB.begin() + static_cast<long>(pasados), iceDeB.end());
            pasados = iceDeB.size();
        }
        for (const auto& c : pend) {
            const json j = json::parse(c);
            webrtc::SdpParseError err;
            std::unique_ptr<webrtc::IceCandidateInterface> ic(webrtc::CreateIceCandidate(
                j["sdpMid"].get<std::string>(), j["sdpMLineIndex"].get<int>(), j["candidate"].get<std::string>(), &err));
            if (ic) pc->AddIceCandidate(std::move(ic), [](webrtc::RTCError) {});
        }
    }
};

} // namespace

int main() {
    std::printf("== test_webrtc_capas (libwebrtc real) ==\n");
#ifdef _WIN32
    _putenv_s("BA_WEBRTC_ALLOW_LOOPBACK", "1");
#else
    ::setenv("BA_WEBRTC_ALLOW_LOOPBACK", "1", 1);
#endif
    ba::ajustes::poner("gateway.hilosRed", 2);
    ba::ajustes::poner("gateway.hilosTrabajo", 2);
    constexpr uint16_t kMin = 41000, kMax = 41999;

    gw::WebrtcHub hub(6, 640, 360, 48000, 2);
    CHECK(hub.rtcFactory() != nullptr, "hub con las capas");
    hub.setNetwork(kMin, kMax, true);
    json capas = hub.capasEstado();
    CHECK(capas.value("fabricas", 0) == 4, "4 fábricas (2 de red × 2 de trabajo) sobre 4 hilos");
    CHECK(capas["red"].size() == 2 && capas["trabajo"].size() == 2, "2 hilos de red y 2 de trabajo");
    CHECK(hub.buclesRed().size() == 2 && hub.conmutador() != nullptr, "los bucles de red y el conmutador para el TURN");

    // El TURN en los hilos de red, con la entrega en memoria.
    gw::turn::Servidor::Config tc;
    tc.puertoTcp = 0;
    tc.conUdp = true;
    tc.direccion = "127.0.0.1";
    tc.direccionRelay = "127.0.0.1";
    tc.bindRelay = "0.0.0.0";
    tc.secreto = gw::turn::secretoAleatorio();
    // Como en main.cpp: las IP de la máquina y 127.0.0.1. El hub junta candidatos en todas
    // sus redes; los sockets de la LAN también le hablan al relay y el TURN tiene que
    // dejarlos (sus permisos son por IP de origen).
    tc.peersPermitidos = gw::net::localIPv4s();
    tc.peersPermitidos.push_back("127.0.0.1");
    tc.rtcMin = kMin;
    tc.rtcMax = kMax;
    tc.bucles = hub.buclesRed();
    tc.memoria = hub.conmutador();
    gw::turn::Servidor turn(tc);
    std::string err;
    CHECK(turn.start(&err), "el TURN arranca en los hilos de red");
    CHECK(turn.estado()["fragmentos"].size() == 2, "un fragmento del TURN por hilo de red");

    // El «navegador»: su propia fábrica y sus hilos.
    auto red = rtc::Thread::CreateWithSocketServer();
    auto trabajo = rtc::Thread::Create();
    auto sen = rtc::Thread::Create();
    red->Start();
    trabajo->Start();
    sen->Start();
    auto tqf = webrtc::CreateDefaultTaskQueueFactory();
    auto adm = trabajo->BlockingCall(
        [&]() { return webrtc::AudioDeviceModule::Create(webrtc::AudioDeviceModule::kDummyAudio, tqf.get()); });
    auto bf = webrtc::CreatePeerConnectionFactory(
        red.get(), trabajo.get(), sen.get(), adm, webrtc::CreateBuiltinAudioEncoderFactory(),
        webrtc::CreateBuiltinAudioDecoderFactory(),
        std::make_unique<webrtc::VideoEncoderFactoryTemplate<webrtc::LibvpxVp8EncoderTemplateAdapter>>(),
        std::make_unique<webrtc::VideoDecoderFactoryTemplate<webrtc::LibvpxVp8DecoderTemplateAdapter>>(), nullptr,
        nullptr);
    webrtc::PeerConnectionFactoryInterface::Options opts;
    opts.network_ignore_mask = 0;
    bf->SetOptions(opts);
    auto mic = rtc::make_ref_counted<Tono>();

    // 4 directos (sesiones 1-4) y 2 por relay (5 por UDP, 6 por TCP).
    std::vector<std::unique_ptr<Invitado>> inv;
    const gw::turn::Credencial cred = gw::turn::credencialEfimera(tc.secreto, 600);
    for (int i = 1; i <= 6; ++i) {
        auto x = std::make_unique<Invitado>();
        x->sess = hub.session(i);
        CHECK(x->sess->configure(x->callbacks(), hub.rtcFactory()), "sesión configurada en su par de hilos");
        webrtc::PeerConnectionInterface::RTCConfiguration c;
        c.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
        if (i >= 5) {
            webrtc::PeerConnectionInterface::IceServer s;
            s.urls = {i == 5 ? "turn:127.0.0.1:" + std::to_string(turn.puertoUdp()) + "?transport=udp"
                             : "turn:127.0.0.1:" + std::to_string(turn.puertoTcp()) + "?transport=tcp"};
            s.username = cred.usuario;
            s.password = cred.clave;
            c.servers.push_back(s);
            c.type = webrtc::PeerConnectionInterface::kRelay; // como un navegador que viene por pinggy
        }
        auto r = bf->CreatePeerConnectionOrError(c, webrtc::PeerConnectionDependencies(x.get()));
        CHECK(r.ok(), "PeerConnection del navegador");
        if (!r.ok()) return 1;
        x->pc = r.MoveValue();
        x->pc->AddTrack(bf->CreateAudioTrack("mic", mic.get()), {"invitado"});
        inv.push_back(std::move(x));
    }
    capas = hub.capasEstado();
    CHECK(capas["red"][0].value("sesiones", 0) == 3 && capas["red"][1].value("sesiones", 0) == 3,
          "reparto: 3 y 3 sesiones por hilo de red");
    CHECK(capas["trabajo"][0].value("sesiones", 0) == 3 && capas["trabajo"][1].value("sesiones", 0) == 3,
          "reparto: 3 y 3 por hilo de trabajo");

    std::atomic<bool> parar{false};
    std::thread hilo([&] {
        while (!parar) {
            mic->push10ms();
            for (auto& x : inv) x->pasarIce();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });
    for (auto& x : inv) CHECK(x->negociar(), "offer → answer");

    const bool todos = esperar(
        [&] {
            for (auto& x : inv)
                if (!x->conectada.load()) return false;
            return true;
        },
        20000);
    if (!todos) {
        int n = 0;
        for (auto& x : inv) n += x->conectada ? 1 : 0;
        std::printf("conectadas %d de %d\n", n, int(inv.size()));
        if (n == 0) {
            std::printf("SKIP: sin conexión ICE (¿no hay red?)\n");
            parar = true;
            hilo.join();
            for (auto& x : inv) x->pc->Close();
            turn.stop();
            return 77;
        }
    }
    CHECK(todos, "las 6 conectan (4 directas en 4 fábricas, 2 por el TURN en memoria)");
    CHECK(esperar(
              [&] {
                  for (auto& x : inv)
                      if (x->bloques.load() < 50 || x->pico.load() < 0.1) return false;
                  return true;
              },
              8000),
          "a todas les llega el audio (un reloj para todas las fábricas)");
    {
        // El reloj del audio repartido (gateway.hilosPlayout = 0: uno por hilo de trabajo) y la
        // frontera #4 en lote (gateway.redYTrabajo = "lote" de fábrica).
        const json c = hub.capasEstado();
        std::printf("playout: %s\nred y trabajo: %s\n", c["playout"].dump().c_str(), c["redYTrabajo"].dump().c_str());
        const json& g = c["playout"]["grupos"];
        CHECK(c["playout"].value("hilos", 0) == 2 && g.size() == 2, "dos hilos de playout (uno por hilo de trabajo)");
        bool tiran = g.size() == 2;
        for (const auto& x : g) tiran = tiran && x.value("fabricas", 0) >= 1 && x.value("vueltas", uint64_t(0)) > 50;
        CHECK(tiran, "cada uno tira de las fábricas de su hilo de trabajo, al ritmo del mismo tick");
        if (g.size() == 2) {
            const auto v0 = g[0].value("vueltas", uint64_t(0)), v1 = g[1].value("vueltas", uint64_t(0));
            CHECK((v0 > v1 ? v0 - v1 : v1 - v0) <= 3, "los dos grupos van a la par (alineados)");
        }
        CHECK(c["redYTrabajo"].value("modo", std::string()) == "lote", "red → trabajo en lote de fábrica");
        uint64_t deRed = 0;
        for (const auto& t : c["redYTrabajo"]["trabajo"]) deRed += t.value("deRed", uint64_t(0));
        CHECK(deRed > 100, "#4: los paquetes pasan de la red al trabajo por el lote");
    }

    // Que corra un poco el medio por el relay (el micrófono de los dos, 50 paquetes/s cada uno).
    esperar(
        [&] {
            const json e = turn.estado(), m = hub.capasEstado()["memoria"];
            return e.value("paquetesEnMemoriaHacia", uint64_t(0)) > 150 && e.value("paquetesEnMemoriaDesde", uint64_t(0)) > 10 &&
                   m.value("directos", uint64_t(0)) + m.value("encolados", uint64_t(0)) + m.value("cruzados", uint64_t(0)) > 150;
        },
        6000);
    const json te = turn.estado();
    std::printf("TURN: %s\n", te.dump().c_str());
    CHECK(te.value("asignaciones", 0) >= 2, "las dos asignaciones de los que entran por relay");
    // Hacia B va el micrófono de los dos (50 paquetes/s cada uno); de B al navegador solo
    // ICE, DTLS y RTCP (el hub no manda audio en esta prueba).
    CHECK(te.value("paquetesEnMemoriaHacia", uint64_t(0)) > 40, "#3: el TURN entrega en memoria a la red de WebRTC");
    CHECK(te.value("paquetesEnMemoriaDesde", uint64_t(0)) > 10, "#14: la red de WebRTC le entrega en memoria al TURN");
    const json me = hub.capasEstado()["memoria"];
    std::printf("conmutador: %s\n", me.dump().c_str());
    CHECK(me.value("directos", uint64_t(0)) + me.value("encolados", uint64_t(0)) + me.value("cruzados", uint64_t(0)) > 100,
          "el conmutador lleva los paquetes");

    // Cerrar una sesión suelta su lugar.
    inv[0]->pc->Close();
    inv[0]->sess->close();
    capas = hub.capasEstado();
    int sesiones = 0;
    for (const auto& r : capas["red"]) sesiones += r.value("sesiones", 0);
    CHECK(sesiones == 5, "al cerrar una sesión, su capa la suelta");

    parar = true;
    hilo.join();
    for (auto& x : inv) x->pc->Close();
    turn.stop();
    for (auto& x : inv) x->sess->close();
    inv.clear();
    std::printf(g_fallas ? "test_webrtc_capas: %d fallas\n" : "test_webrtc_capas: todo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
