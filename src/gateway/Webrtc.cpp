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
//   - Recepcion: OnTrack engancha sinks. Con dos capas (Capas.hpp: el navegador
//     dice el mid de la alta y el de la baja al ofrecer), cada capa tiene su sink
//     y solo la que SALE (SelectorCapas) llega a onRemoteVideo; sin capas (un
//     navegador de antes), el PRIMER track de video es la camara. Cualquier otro
//     video es la pantalla (onRemoteScreen). El audio llega como int16 al sink
//     del track remoto y sale como float32 estereo (onRemoteAudio).
//   - Audio remoto: libwebrtc solo decodifica el audio recibido cuando alguien
//     "tira" de la reproduccion. B no tiene parlantes: PullAdm es un modulo de
//     audio sin dispositivo que tira del mix cada 10 ms en su propio thread.
//   - Envio: fuentes locales alimentadas por el MediaPump (BGRA -> I420 con
//     libyuv UNA vez por cuadro, compartido por las sesiones: CuadroSaliente;
//     audio float32 -> int16). El VIDEO se codifica una vez por flujo y capa,
//     no por sesión: el codificador de cada sesión es de paso y entrega lo que
//     codificó su flujo (RetornoCompartido.hpp). El audio es el mix-minus de cada
//     una, pero los que oyen lo mismo se codifican una vez (AudioCompartido.hpp).
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
#include "AudioCompartido.hpp"
#include "AudioCompartidoRtc.hpp"
#include "RetornoCompartido.hpp"
#include "RetornoRtc.hpp"
#include "ajustes/Ajustes.hpp"
#include "diag/Frontera.hpp"
#include "diag/Registro.hpp"
#include "platform/Hilo.hpp"
#include "platform/Nucleos.hpp"
#include "platform/TiempoReal.hpp"
#include "rtc/CapasHilos.hpp"
#include "rtc/Conmutador.hpp"
#include "turn/Bucle.hpp"

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
#include "RedesSesionRtc.hpp"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/create_peerconnection_factory.h"
#include "api/enable_media.h"
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
#include "api/rtc_event_log/rtc_event_log_factory.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "api/transport/field_trial_based_config.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_metadata.h"
#include "api/video/video_sink_interface.h"
#include "p2p/base/basic_packet_socket_factory.h"
#include "p2p/client/basic_port_allocator.h"
#include "rtc_base/network.h"
#if defined(WEBRTC_WIN)
#include "rtc_base/win32_socket_init.h"
#endif
#include "CodecsVideo.hpp"
#include "DestinoCamara.hpp"
#include "media/base/adapted_video_track_source.h"
#include "modules/audio_device/include/audio_device.h"
#include "modules/audio_device/include/audio_device_default.h"
#include "modules/audio_processing/include/audio_processing.h"
#include "rtc/ColasCompartidas.hpp"
#include "rtc/BucleRtc.hpp"
#include "rtc/HilosLote.hpp"
#include "rtc/LoteTrabajo.hpp"
#include "rtc/SocketsEnMemoria.hpp"
#if BA_HAVE_LWIP
#include "rtc/TunelEnMemoria.hpp"
#endif
#include "rtc_base/logging.h"
#include "rtc_base/ssl_adapter.h"
#include "rtc_base/thread.h"
#include "rtc_base/time_utils.h"
#include "system_wrappers/include/field_trial.h"
#include "third_party/libyuv/include/libyuv/convert.h"
#include "third_party/libyuv/include/libyuv/convert_argb.h"
#endif

namespace ba::gateway {

using json = nlohmann::json;

namespace {
/// Las dos capas de la camara de una sesion (Capas.hpp), igual en los dos backends. Los
/// cuadros llegan por los hilos de libwebrtc y los pedidos por el del servidor: candado.
struct EstadoCapas {
    mutable std::mutex mu;
    SelectorCapas sel;
    std::string midAlta, midBaja;

    bool conLocked() const { return !midAlta.empty() && !midBaja.empty(); }
    bool con() const {
        std::lock_guard<std::mutex> l(mu);
        return conLocked();
    }
    /// Conexion nueva (configure): el navegador arranca con las dos prendidas.
    void reiniciar() {
        std::lock_guard<std::mutex> l(mu);
        sel.reiniciar();
        midAlta.clear();
        midBaja.clear();
    }
    /// Los mids que declaró el navegador. Otros que los de antes (o los primeros): es otro
    /// juego de capas, que arranca con las dos prendidas.
    void ponerMids(const std::string& alta, const std::string& baja) {
        std::lock_guard<std::mutex> l(mu);
        if (alta == midAlta && baja == midBaja) return;
        midAlta = alta;
        midBaja = baja;
        sel.reiniciar();
    }
    /// La capa de un transceptor por su mid (Nada: no es una capa, o no hay capas).
    Capa deMid(const std::string& mid) const {
        std::lock_guard<std::mutex> l(mu);
        if (!conLocked() || mid.empty()) return Capa::Nada;
        return mid == midAlta ? Capa::Alta : mid == midBaja ? Capa::Baja : Capa::Nada;
    }
    /// true (con `a`) si hay que decirle al navegador qué capas prender.
    bool pedir(Capa c, CapasActivas& a) {
        std::lock_guard<std::mutex> l(mu);
        if (!conLocked()) return false; // una sola cámara: no hay nada que elegir
        const bool cambio = sel.pedir(c);
        a = sel.activas();
        return cambio;
    }
    bool llego(Capa c, bool& pasa, bool& anterior, bool& empieza, int& fundidoMs, CapasActivas& a) {
        std::lock_guard<std::mutex> l(mu);
        const auto ahora = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count();
        const bool cambio = sel.llego(c, pasa, anterior, empieza, ahora);
        fundidoMs = sel.fundidoMs();
        a = sel.activas();
        return cambio;
    }
    void setFundidoMs(int ms) {
        std::lock_guard<std::mutex> l(mu);
        sel.setFundidoMs(ms);
    }
    Capa sale() const {
        std::lock_guard<std::mutex> l(mu);
        return sel.sale();
    }
    Capa pedida() const {
        std::lock_guard<std::mutex> l(mu);
        return sel.pedida();
    }
};
} // namespace

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
    EstadoCapas capas;
    std::atomic<int> topeRetorno{0};

    bool configure(const SessionCallbacks& c) {
        cb = c;
        configured = true;
        capas.reiniciar();
        topeRetorno = 0;
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

/// DSCP por la prioridad de red de un sender (F4-06): libwebrtc pone EF al audio y AF41
/// al video con prioridad alta (con `enable_dscp`; si el sistema no deja marcar el
/// socket, se ignora). Una red sin QoS no lo mira; una con QoS los pasa primero.
void marcarPrioridad(const rtc::scoped_refptr<webrtc::RtpSenderInterface>& sender) {
    if (!sender || !ba::ajustes::siNo("red.dscp", true)) return;
    webrtc::RtpParameters par = sender->GetParameters();
    if (par.encodings.empty()) return;
    for (auto& e : par.encodings) e.network_priority = webrtc::Priority::kHigh;
    sender->SetParameters(par);
}

class PullAdm;

/// El reloj del audio de TODAS las fábricas (F2: una fábrica por par de hilos red/trabajo,
/// cada una con su módulo de audio): un solo hilo «webrtc-playout» que cada 10 ms tira de la
/// reproducción de cada una, no un hilo por fábrica (#10 del plan: una despertada por vuelta).
class RelojPlayout {
   public:
    ~RelojPlayout() { parar(); }
    void sumar(PullAdm* a);
    void sacar(PullAdm* a);
    void parar() {
        {
            std::lock_guard<std::mutex> l(m_);
            if (!corriendo_) return;
            corriendo_ = false;
        }
        if (hilo_.joinable()) hilo_.join();
    }

   private:
    void bucle();

    std::mutex m_; // la lista; se tiene tomado mientras se tira de cada una
    std::vector<PullAdm*> adms_;
    bool corriendo_ = false;
    std::thread hilo_;
};

/// Modulo de audio SIN dispositivo que tira de la reproduccion cada 10 ms.
///
/// Sin esto libwebrtc nunca decodifica el audio remoto (NetEq se consume desde
/// la reproduccion del ADM) y los sinks de los tracks remotos no reciben nada.
/// No captura: el audio que B envia sale de fuentes propias (LocalAudioSource).
/// El ritmo lo pone el RelojPlayout común.
class PullAdm : public webrtc::webrtc_impl::AudioDeviceModuleDefault<webrtc::AudioDeviceModule> {
   public:
    explicit PullAdm(std::shared_ptr<RelojPlayout> reloj) : reloj_(std::move(reloj)) {}
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
        reloj_->sumar(this);
        return 0;
    }
    int32_t StopPlayout() override {
        if (!playing_.exchange(false)) return 0;
        reloj_->sacar(this); // al volver, el reloj ya no tira de este
        return 0;
    }
    bool Playing() const override { return playing_.load(); }

    /// 10 ms de la reproducción de esta fábrica (en el hilo del reloj).
    void tirar(std::vector<int16_t>& buf) {
        std::lock_guard<std::mutex> lock(m_);
        if (transport_ == nullptr) return;
        size_t out = 0;
        int64_t elapsed = 0;
        int64_t ntp = 0;
        transport_->NeedMorePlayData(kFramesPer10Ms, sizeof(int16_t) * kAudioChannels, kAudioChannels, kAudioRate,
                                     buf.data(), out, &elapsed, &ntp);
    }

   private:
    std::shared_ptr<RelojPlayout> reloj_;
    std::mutex m_;
    webrtc::AudioTransport* transport_ = nullptr;
    std::atomic<bool> playing_{false};
};

void RelojPlayout::sumar(PullAdm* a) {
    std::lock_guard<std::mutex> l(m_);
    adms_.push_back(a);
    if (!corriendo_) {
        corriendo_ = true;
        if (hilo_.joinable()) hilo_.join(); // uno anterior que ya salió
        hilo_ = std::thread([this]() {
            ba::platform::nombrarHilo("webrtc-playout");
            ba::platform::nucleos::alGrupoGateway();
            ba::platform::tiempoRealAudio(10); // tira del audio cada 10 ms: con plazo
            bucle();
        });
    }
}

void RelojPlayout::sacar(PullAdm* a) {
    std::lock_guard<std::mutex> l(m_);
    adms_.erase(std::remove(adms_.begin(), adms_.end(), a), adms_.end());
}

void RelojPlayout::bucle() {
    std::vector<int16_t> buf(kFramesPer10Ms * kAudioChannels);
    auto next = std::chrono::steady_clock::now();
    // #10 del plan: sacar el audio de todos cada 10 ms (NetEq de cada invitado), de todas las fábricas.
    static ba::diag::Frontera& fPlayout =
        ba::diag::frontera("audio.playout", 10, "sacar el audio de todos cada 10 ms (webrtc-playout)");
    for (;;) {
        next += std::chrono::milliseconds(10);
        fPlayout.despertada();
        {
            ba::diag::MedirPase medir(fPlayout, buf.size() * sizeof(int16_t));
            std::lock_guard<std::mutex> lock(m_);
            if (!corriendo_) return;
            for (PullAdm* a : adms_) a->tirar(buf);
        }
        std::this_thread::sleep_until(next);
    }
}

/// Fuente de video de retorno: un BufferRetorno (el I420 de CuadroSaliente, el mismo para
/// todas las sesiones, y su emisión del flujo) -> sinks -> el codificador de paso.
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
    /// `grupo` != 0: el bloque es el `bloque` de ese grupo de audio compartido y lleva su
    /// marca (AudioCompartido.hpp) hasta el codificador de paso de la conexión.
    void AddFrame(const float* samples, size_t n, int rate, int channels, uint16_t grupo = 0, uint64_t bloque = 0) {
        if (samples == nullptr || n == 0 || rate <= 0 || channels <= 0) return;
        const size_t frames = n / static_cast<size_t>(channels);
        pcm_.resize(n);
        for (size_t i = 0; i < n; ++i) {
            const float v = samples[i] > 1.0f ? 1.0f : (samples[i] < -1.0f ? -1.0f : samples[i]);
            pcm_[i] = static_cast<int16_t>(v * 32767.0f);
        }
        if (grupo != 0) marcarBloque(pcm_.data(), frames, channels, grupo, bloque);
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
    static constexpr int kNo = 0, kSale = 1, kAnterior = 2;
    /// `pasa`: adónde va este cuadro: a `fn` (kSale), a `anterior` (kAnterior: la capa que
    /// se va durante el fundido) o a ningún lado (una capa que no sale se descarta ANTES de
    /// convertirla). Vacío = todos a `fn`.
    using FnYuv = std::function<void(const PlanosYuv&)>;
    /// `camara` (F3): la cámara que sale va derecho a su destino (celda de la grilla y ba-app)
    /// en este mismo hilo, sin el BGRA de acá.
    explicit RemoteVideoSink(Fn fn, std::function<int()> pasa = {}, Fn anterior = {}, FnYuv yuv = {},
                             std::shared_ptr<DestinoCamara> camara = {})
        : fn_(std::move(fn)), pasa_(std::move(pasa)), anterior_(std::move(anterior)), yuv_(std::move(yuv)),
          camara_(std::move(camara)) {}
    void OnFrame(const webrtc::VideoFrame& frame) override {
        if (!fn_ && !camara_) return;
        const int destino = pasa_ ? pasa_() : kSale;
        if (destino == kNo || (destino == kAnterior && !anterior_)) return;
        if (destino == kSale && camara_) {
            codecs::entregarCamara(frame, *camara_, bgra_); // la medición #8 va adentro
            return;
        }
        if (!fn_) return;
        const Fn& fn = destino == kSale ? fn_ : anterior_;
        // #5/#8 del plan: lo que sale del decodificador, en su hilo (BGRA, retrato de la grilla,
        // memoria compartida a ba-app). La espera es desde que llegó el primer paquete del
        // cuadro (red → armar → decodificar).
        static ba::diag::Frontera& fDecodificado =
            ba::diag::frontera("video.decodificado", 8, "cuadro decodificado → BGRA + grilla + ba-app (hilo del decodificador)");
        ba::diag::MedirPase medir(fDecodificado, static_cast<std::uint64_t>(frame.width()) * frame.height() * 3 / 2);
        if (!frame.packet_infos().empty()) {
            const std::int64_t llego = frame.packet_infos().begin()->receive_time().us();
            const std::int64_t esperaUs = rtc::TimeMicros() - llego;
            if (llego > 0 && esperaUs > 0) medir.espera(static_cast<std::uint64_t>(esperaUs) * 1000);
        }
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
        if (destino == kSale && yuv_)
            yuv_(PlanosYuv{i420->DataY(), i420->DataU(), i420->DataV(), i420->StrideY(), i420->StrideU(), i420->StrideV(), w, h,
                           bgra_.data()});
        fn(bgra_.data(), bgra_.size(), w, h);
    }

   private:
    Fn fn_;
    std::function<int()> pasa_;
    Fn anterior_;
    FnYuv yuv_;
    std::shared_ptr<DestinoCamara> camara_;
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
    // Su par de hilos (F2): se suelta cuando se va.
    capas::Reparto* reparto = nullptr;
    capas::Lugar lugar;
    ~ViewerPc() override {
        if (reparto != nullptr) reparto->soltar(lugar);
    }

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

/// Una capa de RED (F1/F2 de PLAN-GATEWAY-FRAGMENTOS.md): su hilo (con el socket server
/// donde esperan los sockets de WebRTC y los del TURN), el bucle que le presta al TURN y las
/// redes y sockets de las sesiones que viven en él.
struct CapaRed {
    std::unique_ptr<rtc::Thread> hilo;
    std::unique_ptr<rtcred::BucleRtc> bucle;
    // Las redes de las sesiones (setNetwork): la lista de interfaces de la máquina SIN las
    // que se ignoran, compartida por todas las PeerConnection de esta capa; cada una la ve por
    // su RedesDeSesion (RedesSesionRtc.hpp: sin eso, cada sesión que empieza hace volver a
    // juntar puertos a las que todavía no conectaron y una ráfaga agota el rango).
    // Las crea y las suelta el hilo de señalización (como las de la factory).
    std::unique_ptr<rtc::BasicNetworkManager> redes;
    /// Los sockets UDP en memoria con el TURN y el túnel (rtc/SocketsEnMemoria.hpp).
    std::unique_ptr<rtcred::FabricaEnMemoria> sockets;
};

/// Una fábrica de libwebrtc por par (red, trabajo): las fábricas comparten los hilos.
struct ParRtc {
    int red = 0, trabajo = 0;
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory;
};

/// Las colas de libwebrtc de TODAS las fábricas sobre los mismos grupos de hilos
/// (rtc/ColasCompartidas.hpp): cada fábrica recibe una de paso a la común.
class ColasDePaso final : public webrtc::TaskQueueFactory {
   public:
    explicit ColasDePaso(std::shared_ptr<webrtc::TaskQueueFactory> comun) : comun_(std::move(comun)) {}
    std::unique_ptr<webrtc::TaskQueueBase, webrtc::TaskQueueDeleter> CreateTaskQueue(absl::string_view nombre,
                                                                                      Priority prioridad) const override {
        return comun_->CreateTaskQueue(nombre, prioridad);
    }

   private:
    std::shared_ptr<webrtc::TaskQueueFactory> comun_;
};

/// Estado compartido de las fábricas (lo guarda el hub como void*).
struct RtcHubState {
#if defined(WEBRTC_WIN)
    /// Winsock arriba mientras viva el hub (el primero en nacer, el último en irse): los
    /// hilos de red crean sockets apenas arrancan y libwebrtc no llama a WSAStartup. En el
    /// gateway ya lo había hecho otro (HttpIo); un hub solo (los tests) no juntaba candidatos.
    rtc::WinsockInitializer winsock;
#endif
    /// Los codificadores de verdad (VP8; H.264 de VideoToolbox en la Mac): los usan los flujos
    /// del retorno compartido. Las PeerConnection ven una fábrica de PASO sobre esta.
    std::shared_ptr<webrtc::VideoEncoderFactory> codificadores;
    /// Los grupos del audio de retorno compartido (AudioCompartidoRtc.hpp).
    std::shared_ptr<audioret::Registro> audioGrupos;
    std::mutex flujosM;
    std::map<std::string, std::shared_ptr<retorno::Flujo>> flujos;
    uint16_t minPort = 0;
    uint16_t maxPort = 0;
    bool loopback = false; ///< también 127.0.0.1 (setNetwork: el túnel de WireGuard)

    // --- las capas (F2) -----------------------------------------------------------------
    std::shared_ptr<RelojPlayout> reloj = std::make_shared<RelojPlayout>();
    std::unique_ptr<capas::Reparto> reparto;
    std::vector<CapaRed> red;                        ///< R hilos de red
    /// T hilos de trabajo. Con `gateway.redYTrabajo` = "juntos" son los mismos de red (no son
    /// dueños); si no, los de `trabajoPropios`.
    std::vector<rtc::Thread*> trabajo;
    std::vector<std::unique_ptr<rtc::Thread>> trabajoPropios;
    /// La espera de cada hilo de trabajo propio (la frontera #4, rtc/LoteTrabajo.hpp).
    std::vector<std::shared_ptr<lote::Destino>> destinos;
    lote::Modo redYTrabajo = lote::Modo::Lote;
    std::int64_t loteNs = 0;
    std::unique_ptr<rtc::Thread> signaling;          ///< uno para toda la sala
    std::vector<ParRtc> pares;                       ///< [red * T + trabajo] (sin cruzar: solo red == trabajo)
    /// La entrega en memoria TURN ↔ WebRTC ↔ túnel (nullptr con `gateway.turnEnMemoria` apagado).
    std::unique_ptr<memoria::Conmutador> conmutador;
    std::shared_ptr<rtcred::EnlaceTunel> tunel = std::make_shared<rtcred::EnlaceTunel>();
    std::mutex tunelM;
    net::IpStack* pila = nullptr;

    int nTrabajo() const { return int(trabajo.size()); }
    ParRtc& par(const capas::Lugar& l) { return pares[size_t(l.red * nTrabajo() + l.trabajo)]; }
    /// Una fábrica cualquiera (para lo que no es de una sesión: capacidades de los códecs).
    webrtc::PeerConnectionFactoryInterface* algunaFabrica() const {
        for (const ParRtc& p : pares)
            if (p.factory) return p.factory.get();
        return nullptr;
    }

    /// La red de una PeerConnection nueva en la capa `r`: sin las de enlace local, y con el
    /// asignador de puertos propio (el rango va en él: la config de la PC no se aplica a uno
    /// inyectado). Y el video que llega sin suavizar.
    void configurarRed(webrtc::PeerConnectionInterface::RTCConfiguration& config, webrtc::PeerConnectionDependencies& deps,
                       int r) {
        // Sin «suavizado previo al render» (IncomingVideoStream): libwebrtc retiene cada
        // cuadro decodificado en una cola con su propio hilo hasta su hora de mostrarse. El
        // gateway no muestra nada: la grilla (cola de 4 cuadros por persona) y ba-app llevan
        // su propio ritmo. Medido en la .177: un hilo por stream y, con 10 en 720p, 107 % de
        // un núcleo solo en eso. Así el cuadro sale del decodificador derecho al sink.
        config.set_prerenderer_smoothing(false);
        config.disable_link_local_networks = true;
        if (minPort != 0) {
            config.port_allocator_config.min_port = minPort;
            config.port_allocator_config.max_port = maxPort;
        }
        if (r < 0 || r >= int(red.size()) || !red[size_t(r)].redes) return;
        CapaRed& c = red[size_t(r)];
        auto a = std::make_unique<AsignadorDeSesion>(c.redes.get(), c.sockets.get());
        if (minPort != 0) a->SetPortRange(minPort, maxPort);
        a->SetNetworkIgnoreMask(loopback ? 0 : rtc::kDefaultNetworkIgnoreMask);
        deps.allocator = std::move(a);
    }
    // Relay: un Feed por (sesion, camara/pantalla) y los espectadores.
    std::mutex feedsM;
    // clave: sesion * 2 + (pantalla ? 1 : 0); -sesion, la capa baja de su camara (solo cuenta)
    std::map<int, std::shared_ptr<Feed>> feeds;
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

void apagarCapas(RtcHubState* st);

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
    // Su par de hilos (F2): en cuál red y cuál trabajo vive esta conexión; no se muda.
    std::mutex lugarM;
    capas::Lugar lugar;
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> fab; ///< la fábrica de su par
    rtc::Thread* trabajo = nullptr;                                ///< su hilo de trabajo
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc;
    rtc::scoped_refptr<LocalVideoSource> videoSource;
    /// Lo que la sesión le dice a su codificador de paso (el tope) y lo que él cuenta.
    const std::shared_ptr<retorno::EstadoSesion> retornoEstado = std::make_shared<retorno::EstadoSesion>();
    rtc::scoped_refptr<LocalAudioSource> audioSource;
    bool tracksAdded = false;
    std::string cameraMid;
    EstadoCapas capas;
    std::vector<std::unique_ptr<RemoteVideoSink>> videoSinks;
    std::vector<std::unique_ptr<RemoteAudioSink>> audioSinks;
    std::vector<std::pair<rtc::scoped_refptr<webrtc::VideoTrackInterface>, RemoteVideoSink*>> videoAttached;
    std::vector<std::pair<rtc::scoped_refptr<webrtc::AudioTrackInterface>, RemoteAudioSink*>> audioAttached;
    std::atomic<bool> connected{false};
    std::atomic<int> candidatos{0}; ///< los locales que juntó la PC de ahora (0 al terminar: sin puerto)
    std::atomic<uint64_t> localAudioBlocks{0};
    std::atomic<uint64_t> localVideoFrames{0};
    // Estadisticas de la conexion (las arma el callback de GetStats).
    std::mutex statsMu;
    std::string statsJson = "{}";
    std::string codecsAnotados; ///< lo último que se anotó en el log (códec que sube/baja)
    uint64_t prevIn = 0, prevOut = 0, prevPkts = 0;
    int64_t prevLost = 0;
    int64_t prevMs = 0;
    // El búfer de NetEq del audio que llega (la suma de los flujos de audio de la conexión).
    double prevJbDemora = 0, prevJbObjetivo = 0;
    uint64_t prevJbEmitidos = 0;

    class StatsCb : public webrtc::RTCStatsCollectorCallback {
       public:
        explicit StatsCb(Impl* i) : impl_(i) {}
        void OnStatsDelivered(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) override {
            uint64_t in = 0, out = 0, pkts = 0;
            int64_t lost = 0;
            double jitterMax = 0, rtt = -1;
            uint32_t perdidos = 0, alto = 0, ancho = 0;
            double jbDemora = 0, jbObjetivo = 0;
            uint64_t jbEmitidos = 0, jbDescartados = 0;
            nlohmann::json salida;
            // El video que SUBE el invitado: en qué códec y quién lo decodifica (placa o no).
            std::string codecEntraId, decodificador, codecSaleId;
            bool decodificaHw = false;
            for (const webrtc::RTCStats& st : *report) {
                if (st.type() == webrtc::RTCInboundRtpStreamStats::kType) {
                    const auto& r = st.cast_to<webrtc::RTCInboundRtpStreamStats>();
                    in += r.bytes_received.value_or(0);
                    pkts += r.packets_received.value_or(0);
                    lost += r.packets_lost.value_or(0);
                    jitterMax = std::max(jitterMax, r.jitter.value_or(0.0));
                    if (r.kind.value_or("") == "audio") {
                        jbDemora += r.jitter_buffer_delay.value_or(0.0);
                        jbObjetivo += r.jitter_buffer_target_delay.value_or(0.0);
                        jbEmitidos += r.jitter_buffer_emitted_count.value_or(0);
                        jbDescartados += r.packets_discarded.value_or(0);
                    }
                    if (r.kind.value_or("") == "video") {
                        if (r.codec_id.has_value() && (codecEntraId.empty() || r.frames_decoded.value_or(0) > 0)) {
                            codecEntraId = *r.codec_id;
                            decodificador = r.decoder_implementation.value_or("");
                            decodificaHw = r.power_efficient_decoder.value_or(false);
                        }
                        perdidos += r.frames_dropped.value_or(0);
                        alto = std::max(alto, r.frame_height.value_or(0));
                        ancho = std::max(ancho, r.frame_width.value_or(0));
                    }
                } else if (st.type() == webrtc::RTCOutboundRtpStreamStats::kType) {
                    const auto& o = st.cast_to<webrtc::RTCOutboundRtpStreamStats>();
                    out += o.bytes_sent.value_or(0);
                    // Lo que SALE de video: con qué codificador, en qué capas y qué lo
                    // limita (para saber por qué una pantalla se ve como se ve).
                    if (o.kind.value_or("") == "video" && o.codec_id.has_value()) codecSaleId = *o.codec_id;
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
            // «video/H264» → «H264».
            const auto codecDe = [&](const std::string& id) -> std::string {
                const webrtc::RTCStats* c = id.empty() ? nullptr : report->Get(id);
                if (c == nullptr || c->type() != webrtc::RTCCodecStats::kType) return {};
                const std::string mime = c->cast_to<webrtc::RTCCodecStats>().mime_type.value_or("");
                const auto barra = mime.find('/');
                return barra == std::string::npos ? mime : mime.substr(barra + 1);
            };
            const std::string codecEntra = codecDe(codecEntraId), codecSale = codecDe(codecSaleId);
            const int64_t ahora = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::steady_clock::now().time_since_epoch())
                                      .count();
            std::lock_guard<std::mutex> l(impl_->statsMu);
            // Una línea en el log por sesión cuando se sabe (o cambia) qué códec va en cada sentido.
            const std::string resumen = codecEntra + "|" + decodificador + "|" + codecSale;
            if ((!codecEntra.empty() || !codecSale.empty()) && resumen != impl_->codecsAnotados) {
                impl_->codecsAnotados = resumen;
                ba::diag::registro().linea(
                    ba::diag::Nivel::Info, "gateway", 0,
                    "video: sesión " + std::to_string(impl_->slot) + ": sube " + (codecEntra.empty() ? "-" : codecEntra) +
                        (decodificador.empty() ? std::string()
                                               : " (decodifica " + decodificador + (decodificaHw ? ", hardware)" : ", software)")) +
                        ", baja " + (codecSale.empty() ? "-" : codecSale) + " (retorno compartido)");
            }
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
            // Cuánto espera el audio que llega en el búfer de NetEq (y cuánto quiere esperar),
            // de media en la ventana: lo que se demora la voz del invitado antes de la mezcla.
            const uint64_t dEmitidos = jbEmitidos >= impl_->prevJbEmitidos ? jbEmitidos - impl_->prevJbEmitidos : 0;
            const double jbMs = dEmitidos > 0 ? (jbDemora - impl_->prevJbDemora) * 1000.0 / static_cast<double>(dEmitidos) : -1;
            const double jbObjMs =
                dEmitidos > 0 ? (jbObjetivo - impl_->prevJbObjetivo) * 1000.0 / static_cast<double>(dEmitidos) : -1;
            impl_->prevJbDemora = jbDemora;
            impl_->prevJbObjetivo = jbObjetivo;
            impl_->prevJbEmitidos = jbEmitidos;
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
            if (!salida.is_null()) {
                salida["codec"] = codecSale;
                j["salida"] = salida;
            }
            if (!codecEntra.empty())
                j["entrada"] = {{"codec", codecEntra}, {"decodificador", decodificador}, {"hardware", decodificaHw}};
            if (jbMs >= 0)
                j["audio_bufer"] = {{"ms", std::lround(jbMs)}, {"objetivo_ms", std::lround(jbObjMs)},
                                    {"descartados", jbDescartados}};
            impl_->statsJson = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            impl_->ajustarPeso(alto);
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

    /// El que sube 720p (o más) cuenta más para el reparto de las próximas (no se muda).
    void ajustarPeso(uint32_t alto) {
        std::lock_guard<std::mutex> l(lugarM);
        if (hub != nullptr && hub->reparto && lugar.valido() && alto > 0)
            hub->reparto->cambiarPeso(lugar, alto >= 540 ? capas::kPesoAlta : capas::kPesoNormal);
    }
    void soltarLugar() {
        std::lock_guard<std::mutex> l(lugarM);
        if (hub != nullptr && hub->reparto) hub->reparto->soltar(lugar);
    }

    bool configure(const SessionCallbacks& c, void* factory) {
        close();
        std::lock_guard<std::mutex> lock(m);
        cb = c;
        hub = static_cast<RtcHubState*>(factory);
        if (hub == nullptr || !hub->reparto || hub->pares.empty()) return false;
        // Al par de hilos menos cargado (F2).
        capas::Lugar l = hub->reparto->tomar();
        {
            std::lock_guard<std::mutex> g(lugarM);
            lugar = l;
        }
        ParRtc& par = hub->par(l);
        fab = par.factory;
        trabajo = hub->trabajo[size_t(l.trabajo)];
        if (!fab) {
            soltarLugar();
            return false;
        }
        webrtc::PeerConnectionInterface::RTCConfiguration config;
        config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
        config.bundle_policy = webrtc::PeerConnectionInterface::kBundlePolicyMaxBundle;
        config.rtcp_mux_policy = webrtc::PeerConnectionInterface::kRtcpMuxPolicyRequire;
        config.media_config.enable_dscp = ba::ajustes::siNo("red.dscp", true); // F4-06
        // Sin STUN/TURN: B atiende en la LAN (candidatos host) y, con el tunel,
        // en su IP publica. El medio no pasa por Cloudflare (spec §8.4).
        webrtc::PeerConnectionDependencies deps(this);
        hub->configurarRed(config, deps, l.red);
        candidatos = 0;
        auto res = fab->CreatePeerConnectionOrError(config, std::move(deps));
        if (!res.ok()) {
            soltarLugar();
            return false;
        }
        pc = res.MoveValue();
        videoSource = rtc::make_ref_counted<LocalVideoSource>();
        audioSource = rtc::make_ref_counted<LocalAudioSource>();
        tracksAdded = false;
        cameraMid.clear();
        capas.reiniciar();
        // Conexión nueva: el tope lo vuelve a poner el servidor (setTopeRetorno). El objeto
        // es el mismo (los hilos del MediaPump lo leen sin candado).
        retornoEstado->topeAlto = 0;
        retornoEstado->capaAlto = 0;
        retornoEstado->objetivoBps = 0;
        retornoEstado->fuente = 0;
        // El sondeo de la red de ESTA conexión (RetornoRtc.hpp): sube el tope de su estimación
        // y libwebrtc sondea hasta ahí. Por el hilo de señalización (el de la PC), sin esperar.
        {
            rtc::scoped_refptr<webrtc::PeerConnectionInterface> p = pc;
            rtc::Thread* sig = hub->signaling.get();
            retornoEstado->ponerSondeo([p, sig](std::int64_t desde, std::int64_t hasta) {
                sig->PostTask([p, desde, hasta]() {
                    webrtc::BitrateSettings b;
                    b.max_bitrate_bps = static_cast<int>(desde);
                    p->SetBitrate(b);
                    b.max_bitrate_bps = static_cast<int>(hasta);
                    p->SetBitrate(b);
                });
            });
        }
        return true;
    }

    /// Tracks de retorno: DESPUES de aplicar el offer, para engancharse a sus
    /// transceivers (el primero de audio y el primero de video).
    void addReturnTracks() {
        if (tracksAdded || !pc) return;
        tracksAdded = true;
        auto v = fab->CreateVideoTrack(videoSource, "ba-return-video");
        auto a = fab->CreateAudioTrack("ba-return-audio", audioSource.get());
        pc->AddTrack(a, {"ba-return"});
        auto sender = pc->AddTrack(v, {"ba-return"});
        if (sender.ok()) preferirCodec(sender.value());
    }

    /// El códec del video (ajuste `retorno.codec`): «auto» prefiere H.264 en macOS
    /// (VideoToolbox) y en Windows (Media Foundation: la placa o, si no, el H.264 por software
    /// de Windows; ver CodecsWin.cpp) si el navegador lo ofreció, y VP8 en Linux; «h264» lo prefiere
    /// siempre que se ofrezca; «vp8», lo que ofrezca el navegador (VP8 primero, como siempre).
    /// Va antes del answer: el orden del answer es el que usan los dos lados. Es la MISMA
    /// m-line que la cámara del invitado (sendrecv), así que su cámara también sube en ese
    /// códec (en el navegador, H.264 suele ir por hardware también).
    void preferirCodec(const rtc::scoped_refptr<webrtc::RtpSenderInterface>& sender) {
        std::string modo = ba::ajustes::texto("retorno.codec", "auto");
        // macOS y Windows: H.264 (VideoToolbox / Media Foundation); Linux: VP8 por software.
        if (modo == "auto") modo = codecs::h264PorOmision() ? "h264" : "vp8";
        if (modo != "auto" && modo != "h264") return;
        rtc::scoped_refptr<webrtc::RtpTransceiverInterface> tr;
        for (const auto& t : pc->GetTransceivers())
            if (t->sender() == sender) tr = t;
        if (!tr) return;
        const auto envio = fab->GetRtpSenderCapabilities(cricket::MEDIA_TYPE_VIDEO).codecs;
        const auto recibe = fab->GetRtpReceiverCapabilities(cricket::MEDIA_TYPE_VIDEO).codecs;
        std::vector<webrtc::RtpCodecCapability> h264, resto;
        for (const auto& c : recibe) {
            const bool tambienEnvia = std::any_of(envio.begin(), envio.end(), [&](const webrtc::RtpCodecCapability& e) {
                return e.name == c.name && e.parameters == c.parameters;
            });
            if (!tambienEnvia) continue;
            if (upper(c.name) == "H264") {
                h264.push_back(c);
            } else {
                resto.push_back(c);
            }
        }
        if (h264.empty()) return;
        // Constrained baseline con packetization-mode=1 primero (el que decodifica cualquier
        // navegador o televisor por hardware), después el resto con modo 1, al final el modo 0.
        const auto orden = [](const webrtc::RtpCodecCapability& c) {
            const auto pm = c.parameters.find("packetization-mode");
            const auto pl = c.parameters.find("profile-level-id");
            const bool modo1 = pm != c.parameters.end() && pm->second == "1";
            const bool cb = pl != c.parameters.end() && pl->second.size() >= 4 && pl->second.compare(0, 4, "42e0") == 0;
            return modo1 ? (cb ? 0 : 1) : 2;
        };
        std::stable_sort(h264.begin(), h264.end(),
                         [&](const webrtc::RtpCodecCapability& a, const webrtc::RtpCodecCapability& b) { return orden(a) < orden(b); });
        h264.insert(h264.end(), resto.begin(), resto.end());
        tr->SetCodecPreferences(h264); // si el navegador no ofreció H.264, queda lo que ofreció
    }

    /// El video de retorno lo codifica su flujo (RetornoCompartido.hpp), con capas temporales
    /// L1T3 en VP8 (una pérdida en el wifi saltea un cuadro en vez de clavar la imagen). Lo
    /// que libwebrtc adaptaría por sesión (bajar resolución o cuadros por CPU o por red) acá
    /// no tiene sentido: la red de cada uno se atiende eligiendo la capa. Va DESPUÉS de
    /// negociar (antes no hay encodings).
    static void retornoSinAdaptar(const rtc::scoped_refptr<webrtc::PeerConnectionInterface>& p) {
        for (const auto& sender : p->GetSenders()) {
            if (sender->media_type() != cricket::MEDIA_TYPE_VIDEO) continue;
            webrtc::RtpParameters par = sender->GetParameters();
            if (par.encodings.empty()) continue;
            par.degradation_preference = webrtc::DegradationPreference::DISABLED;
            sender->SetParameters(par);
        }
    }

    /// La prioridad de red (F4-06): con `red.dscp`, libwebrtc marca los paquetes del audio
    /// como EF (voz) y los del video como AF41 (video interactivo) a partir de
    /// `network_priority` alta. Va DESPUÉS de negociar (antes no hay encodings).
    static void prioridadDeRed(const rtc::scoped_refptr<webrtc::PeerConnectionInterface>& p) {
        for (const auto& sender : p->GetSenders()) marcarPrioridad(sender);
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
            retornoSinAdaptar(p);
            prioridadDeRed(p);
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

    void pushLocalAudio(const float* s, size_t n, int rate, uint16_t grupo = 0, uint64_t bloque = 0) {
        if (!connected.load(std::memory_order_relaxed) || !audioSource) return;
        audioSource->AddFrame(s, n, rate, kAudioChannels, grupo, bloque);
        localAudioBlocks.fetch_add(1, std::memory_order_relaxed);
    }

    void pushLocalVideo(const rtc::scoped_refptr<webrtc::I420BufferInterface>& i420,
                        const std::shared_ptr<retorno::Emision>& emision) {
        if (!connected.load(std::memory_order_relaxed) || !videoSource) return;
        videoSource->AddFrame(rtc::make_ref_counted<retorno::BufferRetorno>(i420, emision, retornoEstado));
        localVideoFrames.fetch_add(1, std::memory_order_relaxed);
    }

    void close() {
        rtc::scoped_refptr<webrtc::PeerConnectionInterface> p;
        {
            std::lock_guard<std::mutex> lock(m);
            p = std::move(pc);
            pc = nullptr;
        }
        if (!p) {
            soltarLugar();
            return;
        }
        retornoEstado->ponerSondeo(nullptr); // suelta la PC
        for (auto& [t, s] : videoAttached)
            t->RemoveSink(s);
        for (auto& [t, s] : audioAttached)
            t->RemoveSink(s);
        videoAttached.clear();
        audioAttached.clear();
        if (hub != nullptr) {
            hub->feed(slot * 2)->clearSource();
            hub->feed(slot * 2 + 1)->clearSource();
            hub->feed(-slot)->clearSource();
        }
        p->Close(); // sincronico: despues no llegan mas callbacks de esta PC
        soltarLugar();
        videoSinks.clear();
        audioSinks.clear();
        if (connected.exchange(false) && cb.onStateChange) cb.onStateChange(false);
    }

    // --- PeerConnectionObserver ------------------------------------------------
    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState) override {}
    void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface>) override {}
    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState s) override {
        // Terminó de juntar sin un solo candidato: no consiguió puerto en ninguna red.
        if (s == webrtc::PeerConnectionInterface::kIceGatheringComplete && candidatos.load() == 0 && cb.onSinPuerto)
            cb.onSinPuerto();
    }

    void OnIceCandidate(const webrtc::IceCandidateInterface* c) override {
        if (c != nullptr) ++candidatos;
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
            // Con dos capas, cada una es su propia recepcion y solo la que sale pasa
            // (convertida) a onRemoteVideo; sin capas, el primer video es la camara.
            const Capa capa = capas.deMid(mid);
            const bool dosCapas = capas.con();
            if (!dosCapas && cameraMid.empty()) cameraMid = mid;
            const bool camera = capa != Capa::Nada || (!dosCapas && mid == cameraMid);
            std::function<int()> pasa;
            if (capa != Capa::Nada)
                pasa = [this, capa]() {
                    bool va = false, anterior = false, empieza = false;
                    int ms = 0;
                    CapasActivas a;
                    if (capas.llego(capa, va, anterior, empieza, ms, a) && cb.onCapas) cb.onCapas(a.alta, a.baja);
                    if (empieza && cb.onFundido) cb.onFundido(ms); // antes del primer cuadro HD
                    return va ? RemoteVideoSink::kSale : anterior ? RemoteVideoSink::kAnterior : RemoteVideoSink::kNo;
                };
            auto sink = std::make_unique<RemoteVideoSink>(camera ? cb.onRemoteVideo : cb.onRemoteScreen, std::move(pasa),
                                                          camera ? cb.onRemoteVideoAnterior : RemoteVideoSink::Fn(),
                                                          camera ? cb.onRemoteVideoYuv : RemoteVideoSink::FnYuv(),
                                                          camera ? cb.camara : std::shared_ptr<DestinoCamara>());
            auto* video = static_cast<webrtc::VideoTrackInterface*>(track.get());
            // Relay a espectadores: los cuadros codificados pasan por el Feed de
            // esta sesion antes del decodificador. A los espectadores se les reenvia
            // la capa alta; la baja pasa por un Feed propio que solo cuenta los bytes
            // (lo que entra a la sala).
            if (hub != nullptr) {
                std::shared_ptr<Feed> feed = hub->feed(capa == Capa::Baja ? -slot : slot * 2 + (camera ? 0 : 1));
                feed->setSource(rtc::scoped_refptr<webrtc::VideoTrackSourceInterface>(video->GetSource()), trabajo);
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
    std::shared_ptr<retorno::Emision> emision; ///< de qué flujo es (FlujoRetorno::emitir)
#endif
};

struct FlujoRetorno::Impl {
#if BA_GATEWAY_HAVE_WEBRTC
    std::shared_ptr<retorno::Flujo> flujo;
#endif
};

struct AudioCompartido::Impl {
#if BA_GATEWAY_HAVE_WEBRTC
    std::shared_ptr<audioret::Grupo> grupo;
#endif
};

uint64_t AudioCompartido::codificar(const float* pcm, size_t n, int rate) const {
#if BA_GATEWAY_HAVE_WEBRTC
    return impl_ && impl_->grupo ? impl_->grupo->codificar(pcm, n, rate) : 0;
#else
    (void)pcm;
    (void)n;
    (void)rate;
    return 0;
#endif
}

CuadroSaliente FlujoRetorno::emitir(const CuadroSaliente& c) const {
#if BA_GATEWAY_HAVE_WEBRTC
    if (!impl_ || !impl_->flujo || c.vacio() || !c.impl_->i420) return c;
    auto impl = std::make_shared<CuadroSaliente::Impl>(*c.impl_);
    impl->emision = retorno::emitir(impl_->flujo, impl->i420);
    CuadroSaliente out;
    out.impl_ = std::move(impl);
    return out;
#else
    return c;
#endif
}

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
void WebrtcSession::setCapas(const std::string& midAlta, const std::string& midBaja) {
    if (impl_ != nullptr) impl_->capas.ponerMids(midAlta, midBaja);
}
void WebrtcSession::pedirCapa(Capa c) {
    if (impl_ == nullptr) return;
    CapasActivas a;
    if (impl_->capas.pedir(c, a) && impl_->cb.onCapas) impl_->cb.onCapas(a.alta, a.baja);
}
void WebrtcSession::setFundidoMs(int ms) {
    if (impl_ != nullptr) impl_->capas.setFundidoMs(ms);
}
bool WebrtcSession::conCapas() const { return impl_ != nullptr && impl_->capas.con(); }
Capa WebrtcSession::capaSale() const { return impl_ != nullptr ? impl_->capas.sale() : Capa::Nada; }
Capa WebrtcSession::capaPedida() const { return impl_ != nullptr ? impl_->capas.pedida() : Capa::Nada; }
void WebrtcSession::pushLocalAudio(const float* s, size_t n, int rate) {
    if (impl_ != nullptr) impl_->pushLocalAudio(s, n, rate);
}
void WebrtcSession::pushLocalAudio(const float* s, size_t n, int rate, const AudioCompartido& grupo, uint64_t bloque) {
    if (impl_ == nullptr) return;
#if BA_GATEWAY_HAVE_WEBRTC
    const uint16_t id = grupo.impl_ && grupo.impl_->grupo && bloque != 0 ? grupo.impl_->grupo->id() : 0;
    impl_->pushLocalAudio(s, n, rate, id, bloque);
#else
    (void)grupo;
    (void)bloque;
    impl_->pushLocalAudio(s, n, rate);
#endif
}
void WebrtcSession::pushLocalVideo(const uint8_t* p, size_t bytes, int w, int h) {
    if (impl_ == nullptr || p == nullptr || w <= 0 || h <= 0 || bytes < static_cast<size_t>(w) * h * 4u) return;
    if (!impl_->connected) return; // sin nadie del otro lado no se convierte nada
    pushLocalVideo(CuadroSaliente::desdeBgra(p, w, h));
}
void WebrtcSession::pushLocalVideo(const CuadroSaliente& c) {
    if (impl_ == nullptr || c.vacio()) return;
#if BA_GATEWAY_HAVE_WEBRTC
    impl_->pushLocalVideo(c.impl_->i420, c.impl_->emision);
#else
    impl_->pushLocalVideo();
#endif
}
bool WebrtcSession::connected() const { return impl_ != nullptr && impl_->connected; }
void WebrtcSession::close() {
    if (impl_ != nullptr) impl_->close();
}
int WebrtcSession::slot() const { return impl_ != nullptr ? impl_->slot : 0; }
void WebrtcSession::setTopeRetorno(int alto) {
    if (impl_ == nullptr) return;
#if BA_GATEWAY_HAVE_WEBRTC
    impl_->retornoEstado->topeAlto.store(std::max(0, alto), std::memory_order_relaxed);
#else
    impl_->topeRetorno.store(std::max(0, alto), std::memory_order_relaxed);
#endif
}
int WebrtcSession::topeRetorno() const {
    if (impl_ == nullptr) return 0;
#if BA_GATEWAY_HAVE_WEBRTC
    return impl_->retornoEstado->topeAlto.load(std::memory_order_relaxed);
#else
    return impl_->topeRetorno.load(std::memory_order_relaxed);
#endif
}
std::string WebrtcSession::estadisticas() {
    if (impl_ == nullptr) return "{}";
    std::string s = impl_->estadisticas();
#if BA_GATEWAY_HAVE_WEBRTC
    const bool conRetorno = impl_->retornoEstado->fuente.load(std::memory_order_relaxed) != 0;
#else
    const bool conRetorno = false;
#endif
    if (!impl_->capas.con() && !conRetorno) return s;
    json j = json::parse(s, nullptr, false);
    if (!j.is_object()) j = json::object();
    if (impl_->capas.con()) {
        // Con dos capas: cuál sale y cuál pidió la sala (para ver que lo que se pide llega).
        j["capa"] = nombreCapa(impl_->capas.sale());
        j["capa_pedida"] = nombreCapa(impl_->capas.pedida());
    }
#if BA_GATEWAY_HAVE_WEBRTC
    if (conRetorno) {
        // El video de retorno que le sale (RetornoCompartido.hpp): qué capa, por qué objetivo.
        const retorno::EstadoSesion& r = *impl_->retornoEstado;
        j["retorno"] = {{"capa", r.capaAlto.load(std::memory_order_relaxed)},
                        {"objetivo_kbps", r.objetivoBps.load(std::memory_order_relaxed) / 1000},
                        {"tope", r.topeAlto.load(std::memory_order_relaxed)},
                        {"fuente", r.fuente.load(std::memory_order_relaxed) == 1 ? "compartido" : "propio"},
                        {"cambios", r.cambiosCapa.load(std::memory_order_relaxed)},
                        {"sondeos", r.sondeos.load(std::memory_order_relaxed)},
                        {"cuadros", r.cuadrosCompartidos.load(std::memory_order_relaxed) +
                                        r.cuadrosPropios.load(std::memory_order_relaxed)}};
    }
#endif
    return j.dump(-1, ' ', false, json::error_handler_t::replace);
}
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
    // Sondeo en ALR (RetornoCompartido.hpp): con el retorno compartido cada conexión manda
    // una capa de bitrate FIJO, y el control de congestión no sube su estimación mucho más
    // allá de lo que se manda (1,5×). Sin sondear, una conexión que bajó a 360p no vuelve a
    // 720p nunca. Con esto libwebrtc sondea (cada 5 s, al doble de lo estimado) mientras la
    // conexión manda menos de lo que podría. Los otros números son los de siempre (ritmo
    // 2,5×, cola 2 s, umbrales del detector de ALR por omisión). Una sola vez por proceso,
    // antes de la fábrica.
    if (ba::ajustes::siNo("retorno.sondeoAlr", true)) {
        static const char kPruebas[] = "WebRTC-StrictPacingAndProbing/2.5,2000,65,80,50,0/";
        webrtc::field_trial::InitFieldTrialsFromString(kPruebas);
    }
    auto* st = new RtcHubState();
    // H.264 por la placa (VideoToolbox en la Mac, Media Foundation en Windows) además de
    // VP8/VP9/AV1 (CodecsVideo.cpp).
    st->codificadores = codecs::codificadores();
    const rtc::scoped_refptr<webrtc::AudioEncoderFactory> audioBase = webrtc::CreateBuiltinAudioEncoderFactory();
    st->audioGrupos = std::make_shared<audioret::Registro>(audioBase);
    // Las capas (F2 de PLAN-GATEWAY-FRAGMENTOS.md): R hilos de red, T de trabajo, una fábrica
    // por par compartiendo los hilos (`gateway.hilosRed`, `gateway.hilosTrabajo`; 0 =
    // automático). Con `gateway.capasCruzadas` apagado, cada red con su trabajo (T = R).
    // La frontera #4 (red → trabajo, rtc/LoteTrabajo.hpp): `gateway.redYTrabajo` = "lote" (de
    // fábrica: el de red despierta al de trabajo una vez por tanda), "juntos" (cada hilo de red
    // es también el de trabajo de su grupo: tantos como el mayor de los dos tamaños, sin
    // cruzar) o "separados" (como venía libwebrtc, para comparar).
    st->redYTrabajo = lote::modoDeLosAjustes();
    st->loteNs = lote::loteNsDeLosAjustes();
    const bool juntos = st->redYTrabajo == lote::Modo::Juntos;
    {
        capas::Tamanos tam = capas::deLosAjustes();
        if (juntos) tam.red = tam.trabajo = std::max(tam.red, tam.trabajo);
        st->reparto = std::make_unique<capas::Reparto>(tam, !juntos && ba::ajustes::siNo("gateway.capasCruzadas", true));
    }
    const int R = st->reparto->tamanos().red, T = st->reparto->tamanos().trabajo;
    const auto nombre = [](const char* base, int i, int n) {
        return n == 1 ? std::string(base) : std::string(base) + "-" + std::to_string(i + 1);
    };
    for (int i = 0; i < R; ++i) {
        CapaRed c;
        // Un PhysicalSocketServer como el de CreateWithSocketServer, con el despertar en lote.
        c.hilo = std::make_unique<rtcred::HiloRed>(st->redYTrabajo, st->loteNs);
        c.hilo->SetName(nombre("ba-rtc-net", i, R), nullptr);
        c.hilo->Start();
        c.hilo->PostTask([] { ba::platform::nucleos::alGrupoGateway(); });
        c.bucle = std::make_unique<rtcred::BucleRtc>(c.hilo.get());
        st->red.push_back(std::move(c));
    }
    for (int j = 0; j < T; ++j) {
        if (juntos) {
            st->trabajo.push_back(st->red[size_t(j)].hilo.get());
            continue;
        }
        // Sin sockets (como rtc::Thread::Create), con la espera de la frontera #4.
        const std::string id = T == 1 ? std::string("rtc.red-trabajo") : "rtc.red-trabajo-" + std::to_string(j + 1);
        auto d = std::make_shared<lote::Destino>(&ba::diag::frontera(
            id, 4, "red → trabajo de libwebrtc (RTP; PostTask): despertadas según gateway.redYTrabajo"));
        std::unique_ptr<rtc::Thread> t = std::make_unique<rtcred::HiloTrabajo>(d);
        t->SetName(nombre("ba-rtc-work", j, T), nullptr);
        t->Start();
        t->PostTask([] { ba::platform::nucleos::alGrupoGateway(); });
        st->trabajo.push_back(t.get());
        st->trabajoPropios.push_back(std::move(t));
        st->destinos.push_back(std::move(d));
    }
    st->signaling = rtc::Thread::Create();
    st->signaling->SetName("ba-rtc-sig", nullptr);
    st->signaling->Start();
    // La entrega en memoria (F1): R hilos de red y un productor de afuera (el túnel).
    if (ba::ajustes::siNo("gateway.turnEnMemoria", true)) {
        st->conmutador = std::make_unique<memoria::Conmutador>(R, 1, [st](int hilo, std::function<void()> f) {
            st->red[size_t(hilo)].hilo->PostTask(std::move(f));
        });
    }
    // Las colas de tareas de libwebrtc: las de TODAS las fábricas comparten dos grupos chicos
    // de hilos (rtc/ColasCompartidas.hpp, `gateway.hilosColas`); −1: las de libwebrtc (un hilo
    // por cola), para comparar o si algo anda mal.
    std::shared_ptr<webrtc::TaskQueueFactory> colasComunes;
    if (ba::ajustes::entero("gateway.hilosColas", 0) < 0) {
        colasComunes = webrtc::CreateDefaultTaskQueueFactory();
        ba::diag::registro().linea(ba::diag::Nivel::Info, "gateway", 0, "colas de libwebrtc: un hilo por cola (gateway.hilosColas = -1)");
    } else {
        colasComunes = colas::crearDeLosAjustes();
    }
    const auto audioPaso = audioret::fabricaDePaso(audioBase, st->audioGrupos);
    const auto audioDec = webrtc::CreateBuiltinAudioDecoderFactory();
    const bool cruzado = st->reparto->cruzado();
    st->pares.resize(size_t(R * T));
    bool ok = true;
    for (int r = 0; r < R && ok; ++r)
        for (int t = 0; t < T && ok; ++t) {
            ParRtc& par = st->pares[size_t(r * T + t)];
            par.red = r;
            par.trabajo = t;
            if (!cruzado && r != t) continue;
            rtc::Thread* hiloRed = st->red[size_t(r)].hilo.get();
            rtc::Thread* hiloTrabajo = st->trabajo[size_t(t)];
            // El ADM se crea en el worker (asi lo espera libwebrtc). Todas las fábricas tiran
            // del audio con el mismo reloj (un solo «webrtc-playout»).
            std::shared_ptr<RelojPlayout> reloj = st->reloj;
            rtc::scoped_refptr<webrtc::AudioDeviceModule> adm = hiloTrabajo->BlockingCall([reloj]() {
                return rtc::scoped_refptr<webrtc::AudioDeviceModule>(rtc::make_ref_counted<PullAdm>(reloj));
            });
            // Lo mismo que arma webrtc::CreatePeerConnectionFactory, salvo las colas de tareas.
            webrtc::PeerConnectionFactoryDependencies deps;
            deps.network_thread = hiloRed;
            deps.worker_thread = hiloTrabajo;
            deps.signaling_thread = st->signaling.get();
            deps.socket_factory = hiloRed->socketserver();
            deps.task_queue_factory = std::make_unique<ColasDePaso>(colasComunes);
            deps.event_log_factory = std::make_unique<webrtc::RtcEventLogFactory>();
            deps.trials = std::make_unique<webrtc::FieldTrialBasedConfig>();
            deps.adm = adm;
            // El Opus de cada sesión es de PASO: manda lo que codificó su grupo si oye lo mismo
            // que otros (AudioCompartido.hpp); si no, codifica el suyo.
            deps.audio_encoder_factory = audioPaso;
            deps.audio_decoder_factory = audioDec;
            // Cada sesión codifica de PASO: entrega lo que codificó su flujo (RetornoCompartido).
            deps.video_encoder_factory = retorno::fabricaDePaso(st->codificadores);
            // Los de software con un hilo interno cada uno (`gateway.hilosPorDecodificador`).
            deps.video_decoder_factory = codecs::decodificadores();
            // El APM por omisión (lo que ponía CreatePeerConnectionFactory con nullptr) y el mixer
            // por omisión (nullptr).
            deps.audio_processing = webrtc::AudioProcessingBuilder().Create();
            webrtc::EnableMedia(deps);
            par.factory = webrtc::CreateModularPeerConnectionFactory(std::move(deps));
            ok = bool(par.factory);
        }
    if (ok) {
        // Loopback solo para pruebas en una sola maquina (tests): en produccion
        // un candidato 127.0.0.1 no le sirve a ningun invitado.
        if (const char* lb = std::getenv("BA_WEBRTC_ALLOW_LOOPBACK"); lb != nullptr && lb[0] == '1') {
            webrtc::PeerConnectionFactoryInterface::Options opts;
            opts.network_ignore_mask = 0;
            for (ParRtc& p : st->pares)
                if (p.factory) p.factory->SetOptions(opts);
            st->loopback = true;
        }
        ba::diag::registro().linea(ba::diag::Nivel::Info, "gateway", 0,
                                   "capas de WebRTC: " + std::to_string(R) + " de red, " + std::to_string(T) +
                                       " de trabajo" + (cruzado ? "" : " (sin cruzar)") + ", red y trabajo: " +
                                       lote::nombre(st->redYTrabajo) +
                                       (st->redYTrabajo == lote::Modo::Lote
                                            ? " (" + std::to_string(st->loteNs / 1000) + " µs)"
                                            : std::string()) +
                                       ", TURN y túnel " +
                                       (st->conmutador ? "en memoria" : "por el kernel"));
        rtcFactory_ = st;
    } else {
        for (ParRtc& p : st->pares) p.factory = nullptr;
        apagarCapas(st);
    }
#endif
}

#if BA_GATEWAY_HAVE_WEBRTC
namespace {
/// Para y suelta todo lo de las capas (las fábricas ya no están).
void apagarCapas(RtcHubState* st) {
    st->signaling->BlockingCall([st]() {
        for (CapaRed& c : st->red) {
            c.redes.reset();
            c.sockets.reset();
        }
    });
    for (CapaRed& c : st->red) c.bucle.reset(); // en su hilo, antes de pararlo
    st->signaling->Stop();
    for (auto& t : st->trabajoPropios) t->Stop();
    for (CapaRed& c : st->red) c.hilo->Stop();
    st->reloj->parar();
    delete st;
}
} // namespace
#endif

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
        conectarTunel(nullptr);
        {
            std::lock_guard<std::mutex> lock(st->feedsM);
            st->feeds.clear(); // sueltan proxies de libwebrtc: con los threads vivos
        }
        {
            // Los hilos de los codificadores compartidos, antes que la fábrica.
            std::lock_guard<std::mutex> lock(st->flujosM);
            for (auto& [n, f] : st->flujos) retorno::parar(*f);
            st->flujos.clear();
        }
        for (ParRtc& p : st->pares) p.factory = nullptr;
        apagarCapas(st);
        rtcFactory_ = nullptr;
    }
    rtc::CleanupSSL();
#endif
}

void WebrtcHub::setNetwork(uint16_t minPort, uint16_t maxPort, bool allowLoopback, const std::vector<std::string>& ignorar) {
#if BA_GATEWAY_HAVE_WEBRTC
    auto* st = static_cast<RtcHubState*>(rtcFactory_);
    if (st == nullptr) return;
    st->minPort = minPort;
    st->maxPort = maxPort;
    st->loopback = st->loopback || allowLoopback;
    // Siempre la lista propia (aunque no haya redes que ignorar): la de la factory también
    // es compartida y tiene el mismo problema de los avisos (RedesSesionRtc.hpp). Una por
    // capa de red (cada una en su hilo), con sus sockets en memoria.
    st->signaling->BlockingCall([st, &ignorar]() {
        for (size_t i = 0; i < st->red.size(); ++i) {
            CapaRed& c = st->red[i];
            if (c.redes) continue;
            c.redes = std::make_unique<rtc::BasicNetworkManager>(c.hilo->socketserver());
            c.redes->set_network_ignore_list(ignorar); // antes de que empiece a enumerar
            c.sockets = std::make_unique<rtcred::FabricaEnMemoria>(c.hilo->socketserver(), st->conmutador.get(), int(i),
                                                                   st->tunel);
        }
    });
    if (allowLoopback) {
        webrtc::PeerConnectionFactoryInterface::Options opts;
        opts.network_ignore_mask = 0;
        for (ParRtc& p : st->pares)
            if (p.factory) p.factory->SetOptions(opts);
    }
#else
    (void)minPort;
    (void)maxPort;
    (void)allowLoopback;
    (void)ignorar;
#endif
}

std::vector<turn::Bucle*> WebrtcHub::buclesRed() const {
    std::vector<turn::Bucle*> v;
#if BA_GATEWAY_HAVE_WEBRTC
    if (auto* st = static_cast<RtcHubState*>(rtcFactory_))
        for (CapaRed& c : st->red) v.push_back(c.bucle.get());
#endif
    return v;
}

memoria::Conmutador* WebrtcHub::conmutador() const {
#if BA_GATEWAY_HAVE_WEBRTC
    if (auto* st = static_cast<RtcHubState*>(rtcFactory_)) return st->conmutador.get();
#endif
    return nullptr;
}

void WebrtcHub::conectarTunel(net::IpStack* pila) {
#if BA_GATEWAY_HAVE_WEBRTC && BA_HAVE_LWIP
    auto* st = static_cast<RtcHubState*>(rtcFactory_);
    if (st == nullptr) return;
    std::lock_guard<std::mutex> l(st->tunelM);
    // Primero que la pila deje de entregar y los sockets de mandar; después lo nuevo.
    if (st->pila != nullptr) {
        st->pila->setSumidero(nullptr);
        st->tunel->poner(nullptr);
        st->pila = nullptr;
    }
    if (pila == nullptr || !st->conmutador || !ba::ajustes::siNo("gateway.wgEnMemoria", true)) return;
    auto t = std::make_shared<rtcred::TunelEnMemoria>(st->conmutador.get(), st->conmutador->hilos(), pila);
    st->tunel->poner(t);
    pila->setSumidero(t);
    st->pila = pila;
#else
    (void)pila;
#endif
}

nlohmann::json WebrtcHub::capasEstado() const {
#if BA_GATEWAY_HAVE_WEBRTC
    if (auto* st = static_cast<RtcHubState*>(rtcFactory_)) {
        nlohmann::json j = st->reparto->estado();
        int fabricas = 0;
        for (const ParRtc& p : st->pares) fabricas += p.factory ? 1 : 0;
        j["fabricas"] = fabricas;
        j["cruzadas"] = st->reparto->cruzado();
        j["memoria"] = st->conmutador ? st->conmutador->estado() : nlohmann::json(nullptr);
        j["tunelEnMemoria"] = st->pila != nullptr;
        // La frontera #4: el modo y la espera de cada hilo de trabajo (los pases y despertadas
        // por segundo están en `fronteras` → rtc.red-trabajo*).
        nlohmann::json esperas = nlohmann::json::array();
        for (size_t k = 0; k < st->destinos.size(); ++k) {
            nlohmann::json e = st->destinos[k]->estado();
            e["hilo"] = "ba-rtc-work/" + std::to_string(k + 1);
            esperas.push_back(std::move(e));
        }
        j["redYTrabajo"] = {{"modo", lote::nombre(st->redYTrabajo)},
                            {"loteUs", st->loteNs / 1000},
                            {"trabajo", std::move(esperas)}};
        return j;
    }
#endif
    return nlohmann::json::object();
}

WebrtcSession* WebrtcHub::session(int idx) {
    if (idx < 1 || idx > guests_) return nullptr;
    return &sessions_[static_cast<size_t>(idx)];
}

FlujoRetorno WebrtcHub::flujoRetorno(const std::string& nombre) {
    FlujoRetorno out;
#if BA_GATEWAY_HAVE_WEBRTC
    auto* st = static_cast<RtcHubState*>(rtcFactory_);
    if (st == nullptr || !st->codificadores) return out;
    std::lock_guard<std::mutex> lock(st->flujosM);
    auto& f = st->flujos[nombre];
    if (!f) f = retorno::crearFlujo(nombre, st->codificadores);
    out.impl_ = std::make_shared<FlujoRetorno::Impl>();
    out.impl_->flujo = f;
#else
    (void)nombre;
#endif
    return out;
}

AudioCompartido WebrtcHub::audioCompartido(const std::string& nombre) {
    AudioCompartido out;
#if BA_GATEWAY_HAVE_WEBRTC
    auto* st = static_cast<RtcHubState*>(rtcFactory_);
    if (st == nullptr || !st->audioGrupos) return out;
    out.impl_ = std::make_shared<AudioCompartido::Impl>();
    out.impl_->grupo = st->audioGrupos->grupo(nombre);
#else
    (void)nombre;
#endif
    return out;
}

nlohmann::json WebrtcHub::audioCompartidoEstado() const {
#if BA_GATEWAY_HAVE_WEBRTC
    auto* st = static_cast<RtcHubState*>(rtcFactory_);
    if (st != nullptr && st->audioGrupos) return st->audioGrupos->estado();
#endif
    return nlohmann::json::array();
}

nlohmann::json WebrtcHub::retornoCompartido() const {
    nlohmann::json out = nlohmann::json::array();
#if BA_GATEWAY_HAVE_WEBRTC
    auto* st = static_cast<RtcHubState*>(rtcFactory_);
    if (st == nullptr) return out;
    std::lock_guard<std::mutex> lock(st->flujosM);
    for (const auto& [n, f] : st->flujos) out.push_back(retorno::estado(*f));
#endif
    return out;
}

nlohmann::json WebrtcHub::codecsVideo() const {
#if BA_GATEWAY_HAVE_WEBRTC
    if (rtcFactory_ != nullptr) return codecs::estado();
#endif
    return nlohmann::json::object();
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
    config.media_config.enable_dscp = ba::ajustes::siNo("red.dscp", true); // F4-06
    webrtc::PeerConnectionDependencies deps(v.get());
    // Al par de hilos menos cargado (F2); lo suelta el ViewerPc al irse.
    v->reparto = st->reparto.get();
    v->lugar = st->reparto->tomar();
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> fab = st->par(v->lugar).factory;
    if (!fab) return 0;
    st->configurarRed(config, deps, v->lugar.red);
    auto res = fab->CreatePeerConnectionOrError(config, std::move(deps));
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
    auto onSrd = [fab, p, vp, finish, onSld](webrtc::RTCError e) {
        if (!e.ok()) {
            finish(-1);
            return;
        }
        auto track = fab->CreateVideoTrack(rtc::make_ref_counted<SilentVideoSource>(), "ba-relay");
        auto sender = p->AddTrack(track, {"ba-relay"});
        if (!sender.ok()) {
            finish(-1);
            return;
        }
        sender.value()->SetEncoderToPacketizerFrameTransformer(vp->tx);
        marcarPrioridad(sender.value()); // AF41 (F4-06)
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

int WebrtcHub::espectadoresDe(int session) const {
    auto* st = static_cast<RtcHubState*>(rtcFactory_);
    if (st == nullptr) return 0;
    std::lock_guard<std::mutex> lock(st->viewersM);
    return static_cast<int>(std::count_if(st->viewers.begin(), st->viewers.end(),
                                          [session](const auto& v) { return v.second->feedKey == session * 2; }));
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
int WebrtcHub::espectadoresDe(int) const { return 0; }
#endif

} // namespace ba::gateway
