// ============================================================================
// src/gateway/webrtc/RetornoRtc.cpp — el retorno compartido con libwebrtc (RetornoRtc.hpp)
//
// Lo que no usa libwebrtc (capas, selector, agregador de claves) está en el núcleo:
// RetornoCompartido.cpp (qué y por qué: RetornoCompartido.hpp). Acá, cómo está hecho:
//
//   Flujo      lo que baja como video (programa, una salida, el operador, un tótem). Tiene
//              una Capa por (códec negociado, calidad) que alguien usó.
//   Emision    un cuadro repartido: el I420 (compartido con CuadroSaliente) y, por cada capa
//              que estaba en uso al emitirlo, su Resultado (pendiente, listo o descartado).
//   Capa       un codificador de verdad (de la fábrica interna) en su hilo, con una cola corta
//              (si se atrasa, tira lo más viejo: más vale saltear que demorar), el alquiler
//              (se codifica mientras alguien la usó hace menos de kAlquilerMs; sin uso suelta
//              el codificador) y el AgregadorClaves.
//   EncoderPaso el codificador de cada sesión. En Encode mira la emisión del cuadro (viene en
//              el BufferRetorno), elige la capa (SelectorRetorno con el objetivo que le dio
//              SetRates y el tope de su sesión), renueva el alquiler y espera el resultado de
//              la capa que sale y, si está cambiando, el de la nueva: cuando están, decide
//              (la nueva solo si trajo un cuadro clave) y entrega a libwebrtc los bytes con el
//              sello RTP de SU cuadro. El EncodedImage comparte los bytes: nada se copia por
//              sesión. Un cuadro sin emisión (pushLocalVideo de una sola sesión, las pruebas)
//              va a un codificador PROPIO, como antes.
//
// Hilos: Encode y SetRates llegan por la cola del codificador de cada sesión; los resultados,
// por el hilo de cada Capa (o el de VideoToolbox, que entrega aparte). La entrega a libwebrtc
// va bajo el candado de la sesión y en orden (una secuencia por sesión: nunca un cuadro más
// viejo que el último entregado).
// ============================================================================

#include "webrtc/RetornoRtc.hpp"

#include "RetornoCompartido.hpp"
#include "ajustes/Ajustes.hpp"
#include "diag/Frontera.hpp"
#include "diag/Registro.hpp"
#include "platform/Hilo.hpp"
#include "platform/Nucleos.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <optional>
#include <thread>
#include <utility>

#include "api/make_ref_counted.h"
#include "api/video/encoded_image.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_bitrate_allocator.h"
#include "api/video/video_frame.h"
#include "api/video_codecs/scalability_mode.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_encoder.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "modules/video_coding/utility/simulcast_rate_allocator.h"
#include "rtc_base/time_utils.h"

namespace ba::gateway {

namespace retorno {

namespace {

/// Mientras alguien usó una capa hace menos que esto, se codifica cada emisión del flujo.
constexpr std::int64_t kAlquilerMs = 3000;
/// Emisiones esperando en la cola de una capa: si el codificador no da abasto, se tira lo
/// más viejo (un retorno que se atrasa es peor que uno que saltea un cuadro).
constexpr size_t kColaCapa = 2;
/// Cuadros entregados a un codificador sin respuesta todavía (VideoToolbox entrega aparte):
/// los más viejos que esto se dan por descartados.
constexpr size_t kPendientesCapa = 8;
constexpr int kCuadrosPorSegundo = 30;
/// El sondeo de la red para subir de capa: el primero a los 2 s de querer subir, después cada
/// 3 s mientras sirva; si no sirvió (sigue en la misma capa), cada vez el doble, hasta 30 s.
constexpr std::int64_t kPrimerSondeoMs = 3000;
constexpr std::int64_t kMaxEsperaSondeoMs = 30000;
/// Lo que se suma al sondear por el resto de la conexión (el audio y los encabezados).
constexpr std::int64_t kOtrosBps = 100000;

std::int64_t ahoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// "VP8" | "H264;packetization-mode=1;profile-level-id=42e01f" (los parámetros en orden): las
/// sesiones que negociaron lo mismo comparten la capa.
std::string claveDeFormato(const webrtc::SdpVideoFormat& f) {
    std::string k = f.name;
    for (const auto& [n, v] : f.parameters) // std::map: ya en orden
        if (n != "level-asymmetry-allowed") k += ";" + n + "=" + v;
    return k;
}

class Capa;

/// Cómo terminó una capa con una emisión.
struct Resultado {
    enum class Estado : std::uint8_t { Pendiente, Listo, Descartado };
    Estado estado = Estado::Pendiente;
    webrtc::EncodedImage img;
    webrtc::CodecSpecificInfo info;
    std::vector<std::function<void()>> avisar; ///< se llaman al dejar de estar pendiente
    bool clave() const { return estado == Estado::Listo && img.FrameType() == webrtc::VideoFrameType::kVideoFrameKey; }
};

} // namespace

struct Emision {
    std::shared_ptr<Flujo> flujo;
    rtc::scoped_refptr<webrtc::I420BufferInterface> i420;
    std::uint64_t numero = 0;
    std::int64_t capturaUs = 0;
    std::shared_mutex m; ///< los que leen el resultado no se esperan entre sí (F2: varios hilos)
    std::map<const Capa*, Resultado> res; ///< solo las capas que estaban en uso al emitir

    /// La capa `c` terminó con esta emisión: avisa a los que esperaban (fuera del candado).
    void terminar(const Capa* c, Resultado::Estado estado, const webrtc::EncodedImage* img,
                  const webrtc::CodecSpecificInfo* info) {
        std::vector<std::function<void()>> avisar;
        {
            std::lock_guard<std::shared_mutex> l(m);
            const auto it = res.find(c);
            if (it == res.end() || it->second.estado != Resultado::Estado::Pendiente) return;
            it->second.estado = estado;
            if (img != nullptr) it->second.img = *img;
            if (info != nullptr) it->second.info = *info;
            avisar.swap(it->second.avisar);
        }
        for (auto& a : avisar) a();
    }
    /// Llama a `fn` cuando la capa `c` termina con esta emisión (ya, si terminó o si la capa no
    /// estaba en uso al emitir).
    void alTerminar(const Capa* c, std::function<void()> fn) {
        {
            std::lock_guard<std::shared_mutex> l(m);
            const auto it = res.find(c);
            if (it != res.end() && it->second.estado == Resultado::Estado::Pendiente) {
                it->second.avisar.push_back(std::move(fn));
                return;
            }
        }
        fn();
    }
    /// Una copia de lo que salió para la capa `c` (los bytes se comparten).
    std::optional<Resultado> resultado(const Capa* c) {
        std::shared_lock<std::shared_mutex> l(m);
        const auto it = res.find(c);
        if (it == res.end() || it->second.estado != Resultado::Estado::Listo) return std::nullopt;
        Resultado r;
        r.estado = it->second.estado;
        r.img = it->second.img;
        r.info = it->second.info;
        return r;
    }
};

namespace {

/// Un codificador de verdad del flujo, en su hilo. Ver la cabecera del archivo.
class Capa : public webrtc::EncodedImageCallback {
   public:
    Capa(std::string nombreFlujo, webrtc::SdpVideoFormat formato, std::string clave, int indice,
         std::shared_ptr<webrtc::VideoEncoderFactory> interna)
        : formato_(std::move(formato)), clave_(std::move(clave)), indice_(indice), interna_(std::move(interna)) {
        hilo_ = std::thread([this, n = std::move(nombreFlujo)] {
            ba::platform::nombrarHilo("ba-retorno-" + std::to_string(indice_));
            ba::platform::nucleos::alGrupoGateway(); // F4: el bloque de caché del gateway
            (void)n;
            bucle();
        });
    }
    ~Capa() override { parar(); }

    const std::string& clave() const { return clave_; }
    int indice() const { return indice_; }

    /// Alguien la usa: se codifican las emisiones que vienen.
    void tocar(std::int64_t ahora) { alquiler_.store(ahora, std::memory_order_relaxed); }
    bool enUso(std::int64_t ahora) const { return ahora - alquiler_.load(std::memory_order_relaxed) < kAlquilerMs; }
    void pedirClave() {
        std::lock_guard<std::mutex> l(m_);
        claves_.pedir();
    }

    /// La emisión `e` se codifica en esta capa (la anota pendiente; si la cola está llena, la
    /// más vieja se descarta).
    void encolar(const std::shared_ptr<Emision>& e) {
        {
            std::lock_guard<std::shared_mutex> l(e->m);
            e->res[this];
        }
        std::shared_ptr<Emision> vieja;
        {
            std::lock_guard<std::mutex> l(m_);
            if (parar_) return;
            if (cola_.size() >= kColaCapa) {
                vieja = std::move(cola_.front());
                cola_.pop_front();
                ++descartados_;
            }
            cola_.push_back(e);
        }
        cv_.notify_one();
        if (vieja) vieja->terminar(this, Resultado::Estado::Descartado, nullptr, nullptr);
    }

    void parar() {
        {
            std::lock_guard<std::mutex> l(m_);
            if (parar_) return;
            parar_ = true;
        }
        cv_.notify_all();
        if (hilo_.joinable()) hilo_.join();
        std::deque<std::shared_ptr<Emision>> resto;
        {
            std::lock_guard<std::mutex> l(m_);
            resto.swap(cola_);
        }
        for (auto& e : resto) e->terminar(this, Resultado::Estado::Descartado, nullptr, nullptr);
        descartarPendientes(UINT32_MAX, true);
    }

    nlohmann::json estado(std::int64_t ahora) const {
        std::lock_guard<std::mutex> l(m_);
        const double seg = std::max(0.001, (ahora - ventanaDesde_) / 1000.0);
        return {{"codec", clave_},
                {"codificador", implementacion_},
                {"hardware", hardware_},
                {"indice", indice_},
                {"alto", alto_},
                {"ancho", ancho_},
                {"kbps", kbps_},
                {"en_uso", enUso(ahora)},
                {"cuadros", cuadros_},
                {"claves", clavesSalidas_},
                {"pedidos_clave", claves_.pedidos()},
                {"claves_pedidas_atendidas", claves_.atendidos()},
                {"descartados", descartados_},
                {"kbps_medio", std::lround(static_cast<double>(bytesVentana_) * 8.0 / 1000.0 / seg)}};
    }

    // --- EncodedImageCallback (el codificador de verdad entrega acá) -------------------------
    Result OnEncodedImage(const webrtc::EncodedImage& img, const webrtc::CodecSpecificInfo* info) override {
        std::shared_ptr<Emision> e;
        {
            std::lock_guard<std::mutex> l(pm_);
            const auto it = pendientes_.find(img.RtpTimestamp());
            if (it != pendientes_.end()) {
                e = std::move(it->second);
                pendientes_.erase(it);
            }
        }
        descartarPendientes(img.RtpTimestamp(), false); // los anteriores no van a salir
        if (!e) return Result(Result::OK);
        // Una copia propia de los bytes (una por capa y cuadro, no por sesión): el codificador
        // puede reusar su buffer para el próximo.
        webrtc::EncodedImage copia = img;
        copia.SetEncodedData(webrtc::EncodedImageBuffer::Create(img.data(), img.size()));
        const bool clave = img.FrameType() == webrtc::VideoFrameType::kVideoFrameKey;
        {
            std::lock_guard<std::mutex> l(m_);
            ++cuadros_;
            bytesVentana_ += img.size();
            if (clave) {
                ++clavesSalidas_;
                claves_.salioClave(ahoraMs());
            }
        }
        webrtc::CodecSpecificInfo ci;
        if (info != nullptr) ci = *info;
        e->terminar(this, Resultado::Estado::Listo, &copia, &ci);
        return Result(Result::OK);
    }

   private:
    void bucle() {
        for (;;) {
            std::shared_ptr<Emision> e;
            {
                std::unique_lock<std::mutex> l(m_);
                cv_.wait_for(l, std::chrono::seconds(1), [this] { return parar_ || !cola_.empty(); });
                if (parar_) break;
                if (!cola_.empty()) {
                    e = std::move(cola_.front());
                    cola_.pop_front();
                }
            }
            const std::int64_t ahora = ahoraMs();
            if (!enUso(ahora) && cod_) soltar(); // nadie la usa: el codificador se suelta
            if (e) {
                // #11 del plan: ba-app → codificar el retorno (una capa, su hilo). La espera es
                // desde la emisión (rtc::TimeMicros, el mismo reloj): lo que estuvo en la cola.
                static ba::diag::Frontera& fCodificar =
                    ba::diag::frontera("retorno.codificar", 11, "emisión del retorno → codificar la capa (ba-retorno-N)");
                fCodificar.despertada();
                ba::diag::MedirPase medir(fCodificar, e->i420 ? static_cast<std::uint64_t>(e->i420->width()) * e->i420->height() * 3 / 2 : 0);
                const std::int64_t esperaUs = rtc::TimeMicros() - e->capturaUs;
                if (esperaUs > 0) medir.espera(static_cast<std::uint64_t>(esperaUs) * 1000);
                codificar(e, ahora);
            }
        }
        soltar();
    }

    void soltar() {
        if (!cod_) return;
        cod_->RegisterEncodeCompleteCallback(nullptr);
        cod_->Release();
        cod_.reset();
        descartarPendientes(UINT32_MAX, true);
    }

    /// Crea (o recrea) el codificador para `w`×`h` a `kbps`.
    bool preparar(int w, int h, int kbps) {
        if (cod_ && w == ancho_ && h == alto_ && kbps == kbps_) return true;
        soltar();
        cod_ = interna_->CreateVideoEncoder(formato_);
        if (!cod_) return false;
        const webrtc::VideoCodecType tipo = webrtc::PayloadStringToCodecType(formato_.name);
        const bool vp8 = tipo == webrtc::kVideoCodecVP8;
        webrtc::VideoCodec c;
        c.codecType = tipo;
        c.width = static_cast<std::uint16_t>(w);
        c.height = static_cast<std::uint16_t>(h);
        c.startBitrate = static_cast<unsigned>(kbps);
        c.maxBitrate = static_cast<unsigned>(kbps);
        c.minBitrate = static_cast<unsigned>(std::min(30, kbps));
        c.maxFramerate = kCuadrosPorSegundo;
        c.qpMax = 56;
        c.active = true;
        c.mode = webrtc::VideoCodecMode::kRealtimeVideo;
        c.numberOfSimulcastStreams = 1;
        webrtc::SimulcastStream& s = c.simulcastStream[0];
        s.width = c.width;
        s.height = c.height;
        s.maxFramerate = kCuadrosPorSegundo;
        // VP8 con tres capas temporales (L1T3): dos de cada tres cuadros no son referencia de
        // nadie, así una pérdida en el wifi del que mira saltea un cuadro en vez de clavar la
        // imagen hasta recuperarlo. VideoToolbox (H.264) no tiene capas: una.
        s.numberOfTemporalLayers = vp8 ? 3 : 1;
        s.maxBitrate = c.maxBitrate;
        s.targetBitrate = c.maxBitrate;
        s.minBitrate = c.minBitrate;
        s.qpMax = c.qpMax;
        s.active = true;
        if (vp8) {
            *c.VP8() = webrtc::VideoEncoder::GetDefaultVp8Settings();
            c.VP8()->numberOfTemporalLayers = 3;
            c.VP8()->automaticResizeOn = false;
            c.VP8()->denoisingOn = false;
            c.SetScalabilityMode(webrtc::ScalabilityMode::kL1T3);
        } else if (tipo == webrtc::kVideoCodecH264) {
            *c.H264() = webrtc::VideoEncoder::GetDefaultH264Settings();
            c.H264()->numberOfTemporalLayers = 1;
            c.SetScalabilityMode(webrtc::ScalabilityMode::kL1T1);
        }
        const webrtc::VideoEncoder::Settings ajustes(webrtc::VideoEncoder::Capabilities(false), 2, 1200);
        if (cod_->InitEncode(&c, ajustes) != WEBRTC_VIDEO_CODEC_OK) {
            cod_.reset();
            return false;
        }
        cod_->RegisterEncodeCompleteCallback(this);
        {
            // Con qué codifica (la placa o no): en el estado y, si cambió, una línea en el log.
            const webrtc::VideoEncoder::EncoderInfo ei = cod_->GetEncoderInfo();
            std::lock_guard<std::mutex> l(m_);
            implementacion_ = ei.implementation_name;
            hardware_ = ei.is_hardware_accelerated;
        }
        if (implementacion_ + (hardware_ ? "+" : "-") != anotado_) {
            anotado_ = implementacion_ + (hardware_ ? "+" : "-");
            ba::diag::registro().linea(ba::diag::Nivel::Info, "gateway", 0,
                                       "video: retorno " + formato_.name + " capa " + std::to_string(indice_) + " (" +
                                           std::to_string(w) + "x" + std::to_string(h) + ") codifica con " +
                                           implementacion_ + (hardware_ ? " (hardware)" : " (software)"));
        }
        webrtc::SimulcastRateAllocator reparto(c);
        const webrtc::VideoBitrateAllocation a =
            reparto.Allocate(webrtc::VideoBitrateAllocationParameters(static_cast<std::uint32_t>(kbps) * 1000u,
                                                                      static_cast<std::uint32_t>(kCuadrosPorSegundo)));
        cod_->SetRates(webrtc::VideoEncoder::RateControlParameters(a, kCuadrosPorSegundo));
        {
            std::lock_guard<std::mutex> l(m_);
            ancho_ = w;
            alto_ = h;
            kbps_ = kbps;
        }
        primero_ = true;
        return true;
    }

    void codificar(const std::shared_ptr<Emision>& e, std::int64_t ahora) {
        const int srcW = e->i420->width(), srcH = e->i420->height();
        const std::vector<CapaRetorno> niveles = capasPara(srcH, capasRetorno());
        if (!enUso(ahora) || indice_ >= static_cast<int>(niveles.size()) || srcH <= 0) {
            e->terminar(this, Resultado::Estado::Descartado, nullptr, nullptr);
            return;
        }
        const CapaRetorno& n = niveles[static_cast<size_t>(indice_)];
        const int h = n.alto & ~1;
        const int w = std::max(2, static_cast<int>(std::lround(static_cast<double>(srcW) * h / srcH)) & ~1);
        if (!preparar(w, h, n.kbps)) {
            e->terminar(this, Resultado::Estado::Descartado, nullptr, nullptr);
            return;
        }
        rtc::scoped_refptr<webrtc::VideoFrameBuffer> buf = e->i420;
        if (w != srcW || h != srcH) {
            rtc::scoped_refptr<webrtc::I420Buffer> chico = webrtc::I420Buffer::Create(w, h);
            chico->ScaleFrom(*e->i420);
            buf = chico;
        }
        // El sello RTP sale del momento de la emisión (90 kHz): igual para todas las capas.
        std::uint32_t ts = static_cast<std::uint32_t>(static_cast<std::uint64_t>(e->capturaUs) * 9u / 100u);
        if (hayTs_ && static_cast<std::int32_t>(ts - ultimoTs_) <= 0) ts = ultimoTs_ + 1;
        hayTs_ = true;
        ultimoTs_ = ts;
        bool clave = primero_;
        {
            std::lock_guard<std::mutex> l(m_);
            if (claves_.tocaAhora(ahora, ba::ajustes::entero("retorno.claveMinMs", 500))) clave = true;
        }
        primero_ = false;
        const webrtc::VideoFrame cuadro = webrtc::VideoFrame::Builder()
                                              .set_video_frame_buffer(buf)
                                              .set_timestamp_rtp(ts)
                                              .set_timestamp_us(e->capturaUs)
                                              .set_rotation(webrtc::kVideoRotation_0)
                                              .build();
        {
            std::lock_guard<std::mutex> l(pm_);
            pendientes_.emplace(ts, e);
        }
        const std::vector<webrtc::VideoFrameType> tipos{clave ? webrtc::VideoFrameType::kVideoFrameKey
                                                              : webrtc::VideoFrameType::kVideoFrameDelta};
        if (cod_->Encode(cuadro, &tipos) != WEBRTC_VIDEO_CODEC_OK) {
            std::shared_ptr<Emision> x;
            {
                std::lock_guard<std::mutex> l(pm_);
                const auto it = pendientes_.find(ts);
                if (it != pendientes_.end()) {
                    x = std::move(it->second);
                    pendientes_.erase(it);
                }
            }
            if (x) x->terminar(this, Resultado::Estado::Descartado, nullptr, nullptr);
            if (clave) pedirClave(); // que lo intente el próximo
        }
        descartarSobrantes();
    }

    /// Los pendientes anteriores a `ts` (o todos) no van a salir: descartados.
    void descartarPendientes(std::uint32_t ts, bool todos) {
        std::vector<std::shared_ptr<Emision>> fuera;
        {
            std::lock_guard<std::mutex> l(pm_);
            for (auto it = pendientes_.begin(); it != pendientes_.end();) {
                if (todos || static_cast<std::int32_t>(it->first - ts) < 0) {
                    fuera.push_back(std::move(it->second));
                    it = pendientes_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        for (auto& e : fuera) e->terminar(this, Resultado::Estado::Descartado, nullptr, nullptr);
    }
    /// Un codificador que tira cuadros sin avisar (libvpx cuando se pasa del bitrate): no
    /// quedan más de kPendientesCapa esperando.
    void descartarSobrantes() {
        std::vector<std::shared_ptr<Emision>> fuera;
        {
            std::lock_guard<std::mutex> l(pm_);
            while (pendientes_.size() > kPendientesCapa) {
                // El más viejo según el orden del sello (que da la vuelta cada ~13 h).
                auto viejo = pendientes_.begin();
                for (auto it = pendientes_.begin(); it != pendientes_.end(); ++it)
                    if (static_cast<std::int32_t>(it->first - viejo->first) < 0) viejo = it;
                fuera.push_back(std::move(viejo->second));
                pendientes_.erase(viejo);
            }
        }
        for (auto& e : fuera) e->terminar(this, Resultado::Estado::Descartado, nullptr, nullptr);
    }

    const webrtc::SdpVideoFormat formato_;
    const std::string clave_;
    const int indice_;
    std::shared_ptr<webrtc::VideoEncoderFactory> interna_;
    std::atomic<std::int64_t> alquiler_{-kAlquilerMs};

    mutable std::mutex m_; // la cola, el agregador y los números
    std::condition_variable cv_;
    std::deque<std::shared_ptr<Emision>> cola_;
    bool parar_ = false;
    AgregadorClaves claves_;
    int ancho_ = 0, alto_ = 0, kbps_ = 0;
    std::uint64_t cuadros_ = 0, clavesSalidas_ = 0, descartados_ = 0, bytesVentana_ = 0;
    std::int64_t ventanaDesde_ = ahoraMs();

    std::mutex pm_; // los entregados al codificador sin respuesta todavía
    std::map<std::uint32_t, std::shared_ptr<Emision>> pendientes_;

    // Solo del hilo de la capa.
    std::unique_ptr<webrtc::VideoEncoder> cod_;
    bool primero_ = true;
    bool hayTs_ = false;
    std::uint32_t ultimoTs_ = 0;
    std::thread hilo_;
    std::string implementacion_; ///< el codificador de verdad (m_)
    bool hardware_ = false;      ///< (m_)
    std::string anotado_;        ///< lo último anotado en el log (solo el hilo de la capa)
};

} // namespace

class Flujo {
   public:
    Flujo(std::string nombre, std::shared_ptr<webrtc::VideoEncoderFactory> interna)
        : nombre_(std::move(nombre)), interna_(std::move(interna)) {}
    ~Flujo() { parar(); }

    const std::string& nombre() const { return nombre_; }

    /// La capa `indice` del formato `f` (se crea la primera vez que alguien la pide).
    Capa* capa(const webrtc::SdpVideoFormat& f, const std::string& clave, int indice) {
        std::lock_guard<std::mutex> l(m_);
        for (auto& c : capas_)
            if (c->indice() == indice && c->clave() == clave) return c.get();
        if (parado_) return nullptr;
        capas_.push_back(std::make_unique<Capa>(nombre_, f, clave, indice, interna_));
        return capas_.back().get();
    }

    void emitir(const std::shared_ptr<Emision>& e) {
        const std::int64_t ahora = ahoraMs();
        std::vector<Capa*> enUso;
        {
            std::lock_guard<std::mutex> l(m_);
            e->numero = ++emisiones_;
            for (auto& c : capas_)
                if (c->enUso(ahora)) enUso.push_back(c.get());
        }
        for (Capa* c : enUso) c->encolar(e);
    }

    void parar() {
        std::vector<std::unique_ptr<Capa>> capas;
        {
            std::lock_guard<std::mutex> l(m_);
            parado_ = true;
            capas.swap(capas_);
        }
        for (auto& c : capas) c->parar();
        // Se destruyen acá (después de parar todas): una emisión en la cola de otra puede
        // apuntar a esta por su clave en `res`, pero nadie la desreferencia.
    }

    nlohmann::json estado() const {
        const std::int64_t ahora = ahoraMs();
        nlohmann::json capas = nlohmann::json::array();
        std::lock_guard<std::mutex> l(m_);
        for (const auto& c : capas_) capas.push_back(c->estado(ahora));
        return {{"nombre", nombre_}, {"emisiones", emisiones_}, {"capas", capas}};
    }

   private:
    const std::string nombre_;
    std::shared_ptr<webrtc::VideoEncoderFactory> interna_;
    mutable std::mutex m_;
    std::vector<std::unique_ptr<Capa>> capas_;
    std::uint64_t emisiones_ = 0;
    bool parado_ = false;
};

std::shared_ptr<Flujo> crearFlujo(std::string nombre, std::shared_ptr<webrtc::VideoEncoderFactory> interna) {
    return std::make_shared<Flujo>(std::move(nombre), std::move(interna));
}

std::shared_ptr<Emision> emitir(const std::shared_ptr<Flujo>& flujo, rtc::scoped_refptr<webrtc::I420BufferInterface> i420) {
    if (!flujo || !i420) return nullptr;
    auto e = std::make_shared<Emision>();
    e->flujo = flujo;
    e->i420 = std::move(i420);
    e->capturaUs = rtc::TimeMicros();
    flujo->emitir(e);
    return e;
}

void parar(Flujo& flujo) { flujo.parar(); }
nlohmann::json estado(const Flujo& flujo) { return flujo.estado(); }

// ============================================================================
// El codificador de cada sesión
// ============================================================================
namespace {

/// Lo que comparten el codificador de paso y los avisos que dejó esperando (pueden llegar
/// después de que libwebrtc lo suelte).
struct Paso : std::enable_shared_from_this<Paso> {
    std::mutex m;
    webrtc::EncodedImageCallback* cb = nullptr;
    webrtc::SdpVideoFormat formato{""};
    std::string claveFormato;
    std::shared_ptr<webrtc::VideoEncoderFactory> interna;
    std::atomic<std::int64_t> objetivoBps{0};
    std::shared_ptr<EstadoSesion> estado; ///< el de la sesión (llega con cada cuadro)

    // El retorno compartido.
    const Flujo* flujo = nullptr; ///< de qué flujo viene lo que sale (nullptr: nada o propio)
    Capa* sale = nullptr;         ///< la capa que sale ahora
    bool esperaClave = true;      ///< hasta un cuadro clave de lo que sale, no se entrega nada
    SelectorRetorno sel;
    std::uint64_t secuencia = 0;     ///< una por cuadro que pide libwebrtc
    // El sondeo para subir (RetornoRtc.hpp, EstadoSesion::sondear).
    std::int64_t proximoSondeoMs = 0;
    std::int64_t esperaSondeoMs = kPrimerSondeoMs;
    int capaAlSondear = -1;
    std::uint64_t ultimaEntregada = 0;

    // El propio (cuadros sin flujo): un codificador de verdad como antes.
    std::unique_ptr<webrtc::VideoEncoder> propio;
    std::optional<webrtc::VideoCodec> codec;
    std::optional<webrtc::VideoEncoder::Settings> ajustes;
    std::optional<webrtc::VideoEncoder::RateControlParameters> tasas;
    bool propioDebeClave = true;
    std::deque<std::pair<std::uint32_t, std::uint64_t>> secuenciasPropio; ///< sello -> secuencia

    /// Entrega `r` a libwebrtc como el cuadro `ts` (bajo `m`).
    void entregar(const webrtc::EncodedImage& img, const webrtc::CodecSpecificInfo& info, std::uint32_t ts,
                  std::int64_t renderMs, std::uint64_t sec) {
        if (cb == nullptr || sec <= ultimaEntregada) return;
        webrtc::EncodedImage out = img; // comparte los bytes
        out.SetRtpTimestamp(ts);
        out.capture_time_ms_ = renderMs;
        out.ntp_time_ms_ = 0;
        out.rotation_ = webrtc::kVideoRotation_0;
        out.timing_ = webrtc::EncodedImage::Timing();
        out.SetSimulcastIndex(absl::nullopt);
        cb->OnEncodedImage(out, &info);
        ultimaEntregada = sec;
    }
};

/// Lo que espera un cuadro de una sesión: el resultado de la capa que sale y, si está
/// cambiando, el de la nueva. Cuando están los dos (o se descartaron), decide.
struct Espera : std::enable_shared_from_this<Espera> {
    std::weak_ptr<Paso> paso;
    std::shared_ptr<Emision> e;
    Capa* sale = nullptr;
    Capa* deseada = nullptr;
    std::uint32_t ts = 0;
    std::int64_t renderMs = 0;
    std::uint64_t sec = 0;
    std::atomic<int> faltan{1};

    void arrancar() {
        std::vector<Capa*> capas{deseada};
        if (sale != nullptr && sale != deseada) capas.push_back(sale);
        faltan.store(static_cast<int>(capas.size()) + 1);
        auto self = shared_from_this();
        for (Capa* c : capas) e->alTerminar(c, [self] { self->uno(); });
        uno();
    }
    void uno() {
        if (faltan.fetch_sub(1) == 1) decidir();
    }
    void decidir() {
        const auto p = paso.lock();
        if (!p) return;
        const std::optional<Resultado> rd = e->resultado(deseada);
        const std::optional<Resultado> rs = sale != nullptr && sale != deseada ? e->resultado(sale) : std::nullopt;
        std::lock_guard<std::mutex> l(p->m);
        if (p->cb == nullptr || sec <= p->ultimaEntregada || p->flujo != e->flujo.get()) return;
        const Resultado* r = nullptr;
        if (rd && (rd->clave() || (deseada == p->sale && !p->esperaClave))) {
            r = &*rd;
            if (p->sale != deseada) {
                p->sale = deseada;
                if (p->estado) p->estado->cambiosCapa.fetch_add(1, std::memory_order_relaxed);
            }
        } else if (rs && sale == p->sale && !p->esperaClave) {
            r = &*rs;
        }
        if (r == nullptr) return; // ni la nueva trajo clave ni hay de la vieja: este no sale
        p->esperaClave = false;
        {
            // #12 del plan: el cuadro codificado (una vez por capa) a cada oyente; la espera,
            // desde la emisión hasta que sale hacia la sesión.
            static ba::diag::Frontera& fOyente =
                ba::diag::frontera("retorno.video_a_oyente", 12, "retorno codificado → cada oyente (un pase por oyente)");
            ba::diag::MedirPase medir(fOyente, r->img.size());
            const std::int64_t esperaUs = rtc::TimeMicros() - e->capturaUs;
            if (esperaUs > 0) medir.espera(static_cast<std::uint64_t>(esperaUs) * 1000);
            p->entregar(r->img, r->info, ts, renderMs, sec);
        }
        if (p->estado) {
            p->estado->capaAlto.store(static_cast<int>(r->img._encodedHeight), std::memory_order_relaxed);
            p->estado->cuadrosCompartidos.fetch_add(1, std::memory_order_relaxed);
        }
    }
};

/// Entrega lo del codificador propio de la sesión (cuadros sin flujo).
class SalidaPropia : public webrtc::EncodedImageCallback {
   public:
    explicit SalidaPropia(std::weak_ptr<Paso> p) : p_(std::move(p)) {}
    Result OnEncodedImage(const webrtc::EncodedImage& img, const webrtc::CodecSpecificInfo* info) override {
        const auto p = p_.lock();
        if (!p) return Result(Result::OK);
        std::lock_guard<std::mutex> l(p->m);
        if (p->flujo != nullptr) return Result(Result::OK); // ya volvió a lo compartido
        std::uint64_t sec = 0;
        while (!p->secuenciasPropio.empty() &&
               static_cast<std::int32_t>(p->secuenciasPropio.front().first - img.RtpTimestamp()) <= 0) {
            if (p->secuenciasPropio.front().first == img.RtpTimestamp()) sec = p->secuenciasPropio.front().second;
            p->secuenciasPropio.pop_front();
        }
        if (sec == 0) return Result(Result::OK);
        webrtc::CodecSpecificInfo ci;
        if (info != nullptr) ci = *info;
        p->entregar(img, ci, img.RtpTimestamp(), img.capture_time_ms_, sec);
        if (p->estado) {
            p->estado->capaAlto.store(static_cast<int>(img._encodedHeight), std::memory_order_relaxed);
            p->estado->cuadrosPropios.fetch_add(1, std::memory_order_relaxed);
        }
        return Result(Result::OK);
    }

   private:
    std::weak_ptr<Paso> p_;
};

class EncoderPaso : public webrtc::VideoEncoder {
   public:
    EncoderPaso(const webrtc::SdpVideoFormat& f, std::shared_ptr<webrtc::VideoEncoderFactory> interna)
        : p_(std::make_shared<Paso>()) {
        p_->formato = f;
        p_->claveFormato = claveDeFormato(f);
        p_->interna = std::move(interna);
        salidaPropia_ = std::make_unique<SalidaPropia>(p_);
    }
    ~EncoderPaso() override { Release(); }

    int InitEncode(const webrtc::VideoCodec* c, const webrtc::VideoEncoder::Settings& s) override {
        if (c == nullptr) return WEBRTC_VIDEO_CODEC_ERROR;
        std::unique_ptr<webrtc::VideoEncoder> viejo;
        {
            std::lock_guard<std::mutex> l(p_->m);
            p_->codec = *c;
            p_->ajustes = s;
            viejo = std::move(p_->propio); // se rearma con lo nuevo cuando haga falta
            p_->propioDebeClave = true;
        }
        if (viejo) viejo->Release();
        return WEBRTC_VIDEO_CODEC_OK;
    }

    int32_t RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback* cb) override {
        std::lock_guard<std::mutex> l(p_->m);
        p_->cb = cb;
        return WEBRTC_VIDEO_CODEC_OK;
    }

    int32_t Release() override {
        std::unique_ptr<webrtc::VideoEncoder> propio;
        {
            std::lock_guard<std::mutex> l(p_->m);
            p_->cb = nullptr;
            propio = std::move(p_->propio);
            p_->flujo = nullptr;
            p_->sale = nullptr;
            p_->esperaClave = true;
            p_->sel.reiniciar();
        }
        if (propio) propio->Release();
        return WEBRTC_VIDEO_CODEC_OK;
    }

    void SetRates(const webrtc::VideoEncoder::RateControlParameters& r) override {
        const std::int64_t bps = std::max<std::int64_t>(static_cast<std::int64_t>(r.bitrate.get_sum_bps()),
                                                        r.bandwidth_allocation.bps_or(0));
        p_->objetivoBps.store(bps, std::memory_order_relaxed);
        std::lock_guard<std::mutex> l(p_->m);
        p_->tasas = r;
        if (p_->estado) p_->estado->objetivoBps.store(bps, std::memory_order_relaxed);
        if (p_->propio) p_->propio->SetRates(r);
    }

    EncoderInfo GetEncoderInfo() const override {
        EncoderInfo i;
        i.implementation_name = "ba-retorno-compartido";
        i.supports_native_handle = true;    // el BufferRetorno llega tal cual
        i.has_trusted_rate_controller = true; // nada de tirar cuadros por «pasarse»: el bitrate es de la capa
        i.is_hardware_accelerated = true;   // sin adaptar por CPU: acá no se codifica
        i.scaling_settings = webrtc::VideoEncoder::ScalingSettings::kOff;
        i.requested_resolution_alignment = 1;
        i.supports_simulcast = false;
        return i;
    }

    int32_t Encode(const webrtc::VideoFrame& frame, const std::vector<webrtc::VideoFrameType>* tipos) override {
        const bool pideClave = tipos != nullptr && std::any_of(tipos->begin(), tipos->end(), [](webrtc::VideoFrameType t) {
                                   return t == webrtc::VideoFrameType::kVideoFrameKey;
                               });
        const auto buf = frame.video_frame_buffer();
        if (!buf || buf->type() != webrtc::VideoFrameBuffer::Type::kNative) return WEBRTC_VIDEO_CODEC_ERROR;
        // Las fuentes de las sesiones solo emiten BufferRetorno (ver RetornoRtc.hpp).
        const auto* b = static_cast<const BufferRetorno*>(buf.get());
        if (b->emision()) return compartido(frame, *b, pideClave);
        return propio(frame, *b, pideClave);
    }

   private:
    int32_t compartido(const webrtc::VideoFrame& frame, const BufferRetorno& b, bool pideClave) {
        const std::shared_ptr<Emision>& e = b.emision();
        Flujo* flujo = e->flujo.get();
        const std::int64_t ahora = ahoraMs();
        auto espera = std::make_shared<Espera>();
        std::int64_t sondeoDesde = 0, sondeoHasta = 0;
        std::shared_ptr<EstadoSesion> estado;
        {
            std::lock_guard<std::mutex> l(p_->m);
            p_->estado = b.estado();
            if (p_->estado) p_->estado->fuente.store(1, std::memory_order_relaxed);
            const std::vector<CapaRetorno> niveles = capasPara(e->i420->height(), capasRetorno());
            if (niveles.empty()) return WEBRTC_VIDEO_CODEC_OK;
            std::vector<int> kbps;
            for (const CapaRetorno& c : niveles) kbps.push_back(c.kbps);
            const int tope = indiceTope(niveles, p_->estado ? p_->estado->topeAlto.load(std::memory_order_relaxed) : 0);
            if (p_->flujo != flujo) { // otro flujo (cambió de ruta, o venía del propio)
                p_->flujo = flujo;
                p_->sale = nullptr;
                p_->esperaClave = true;
            }
            const std::int64_t objetivo = p_->objetivoBps.load(std::memory_order_relaxed);
            const int idx = p_->sel.elegir(kbps, objetivo, tope, ahora);
            // ¿Quiere subir y su objetivo no alcanza? Una conexión que manda una capa de bitrate
            // fijo no hace crecer su estimación sola (libwebrtc no la sube mucho más allá de lo
            // que se manda): hay que sondear la red.
            if (idx > tope && objetivo > 0) {
                const std::int64_t quiere = static_cast<std::int64_t>(1.15 * 1000.0 * kbps[static_cast<size_t>(idx - 1)]);
                if (objetivo >= quiere) {
                    p_->proximoSondeoMs = 0; // entra: el selector sube solo
                } else if (p_->proximoSondeoMs == 0) {
                    p_->proximoSondeoMs = ahora + 2000;
                } else if (ahora >= p_->proximoSondeoMs) {
                    // Si el sondeo anterior no sirvió (sigue en la misma capa), se espacia.
                    if (p_->capaAlSondear >= 0 && idx >= p_->capaAlSondear)
                        p_->esperaSondeoMs = std::min(p_->esperaSondeoMs * 2, kMaxEsperaSondeoMs);
                    else
                        p_->esperaSondeoMs = kPrimerSondeoMs;
                    p_->capaAlSondear = idx;
                    p_->proximoSondeoMs = ahora + p_->esperaSondeoMs;
                    sondeoDesde = std::max<std::int64_t>(objetivo, 100000);
                    sondeoHasta = static_cast<std::int64_t>(1.15 * static_cast<double>(quiere)) + kOtrosBps;
                }
            } else {
                p_->proximoSondeoMs = 0;
            }
            Capa* deseada = flujo->capa(p_->formato, p_->claveFormato, idx);
            if (deseada == nullptr) return WEBRTC_VIDEO_CODEC_OK; // el flujo se está cerrando
            // Si la capa que sale ya no es de las que hay (la fuente se achicó), se suelta.
            if (p_->sale != nullptr && p_->sale->indice() >= static_cast<int>(niveles.size())) {
                p_->sale = nullptr;
                p_->esperaClave = true;
            }
            deseada->tocar(ahora);
            if (p_->sale != nullptr) p_->sale->tocar(ahora);
            if (p_->sale == nullptr || p_->esperaClave || deseada != p_->sale) deseada->pedirClave();
            else if (pideClave) p_->sale->pedirClave(); // PLI/FIR de este invitado: juntado con los demás
            espera->paso = p_;
            espera->e = e;
            espera->sale = p_->sale;
            espera->deseada = deseada;
            espera->ts = frame.timestamp();
            espera->renderMs = frame.render_time_ms();
            espera->sec = ++p_->secuencia;
            estado = p_->estado;
        }
        if (sondeoHasta > sondeoDesde && estado) estado->sondear(sondeoDesde, sondeoHasta);
        espera->arrancar(); // fuera del candado: puede decidir ya
        return WEBRTC_VIDEO_CODEC_OK;
    }

    int32_t propio(const webrtc::VideoFrame& frame, const BufferRetorno& b, bool pideClave) {
        webrtc::VideoEncoder* cod = nullptr;
        bool clave = pideClave;
        {
            std::lock_guard<std::mutex> l(p_->m);
            p_->estado = b.estado();
            if (p_->estado) p_->estado->fuente.store(2, std::memory_order_relaxed);
            if (p_->flujo != nullptr) { // venía de lo compartido: arranca en una clave
                p_->flujo = nullptr;
                p_->sale = nullptr;
                p_->propioDebeClave = true;
            }
            if (!p_->propio) {
                if (!p_->codec || !p_->ajustes) return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
                p_->propio = p_->interna->CreateVideoEncoder(p_->formato);
                if (!p_->propio || p_->propio->InitEncode(&*p_->codec, *p_->ajustes) != WEBRTC_VIDEO_CODEC_OK) {
                    p_->propio.reset();
                    return WEBRTC_VIDEO_CODEC_ERROR;
                }
                p_->propio->RegisterEncodeCompleteCallback(salidaPropia_.get());
                if (p_->tasas) p_->propio->SetRates(*p_->tasas);
                p_->propioDebeClave = true;
            }
            clave = clave || p_->propioDebeClave;
            p_->propioDebeClave = false;
            p_->secuenciasPropio.emplace_back(frame.timestamp(), ++p_->secuencia);
            while (p_->secuenciasPropio.size() > 16) p_->secuenciasPropio.pop_front();
            cod = p_->propio.get();
        }
        // Fuera del candado: libvpx entrega adentro de Encode (SalidaPropia lo toma). El
        // codificador propio solo lo toca este hilo (la cola del codificador de la sesión).
        const webrtc::VideoFrame crudo = webrtc::VideoFrame::Builder()
                                             .set_video_frame_buffer(b.i420())
                                             .set_timestamp_rtp(frame.timestamp())
                                             .set_timestamp_ms(frame.render_time_ms())
                                             .set_rotation(frame.rotation())
                                             .build();
        const std::vector<webrtc::VideoFrameType> tipos{clave ? webrtc::VideoFrameType::kVideoFrameKey
                                                              : webrtc::VideoFrameType::kVideoFrameDelta};
        return cod->Encode(crudo, &tipos);
    }

    std::shared_ptr<Paso> p_;
    std::unique_ptr<SalidaPropia> salidaPropia_;
};

class FabricaDePaso : public webrtc::VideoEncoderFactory {
   public:
    explicit FabricaDePaso(std::shared_ptr<webrtc::VideoEncoderFactory> interna) : interna_(std::move(interna)) {}
    std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override { return interna_->GetSupportedFormats(); }
    CodecSupport QueryCodecSupport(const webrtc::SdpVideoFormat& f,
                                   absl::optional<std::string> modo) const override {
        return interna_->QueryCodecSupport(f, std::move(modo));
    }
    std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(const webrtc::SdpVideoFormat& f) override {
        return std::make_unique<EncoderPaso>(f, interna_);
    }

   private:
    std::shared_ptr<webrtc::VideoEncoderFactory> interna_;
};

} // namespace

std::unique_ptr<webrtc::VideoEncoderFactory> fabricaDePaso(std::shared_ptr<webrtc::VideoEncoderFactory> interna) {
    return std::make_unique<FabricaDePaso>(std::move(interna));
}

} // namespace retorno

} // namespace ba::gateway
