// ============================================================================
// src/gateway/webrtc/CodecsVideo.cpp — Las fábricas de video del gateway, por plataforma
//
// Qué códec sube cada invitado y cuál baja con su retorno lo decide preferirCodec
// (Webrtc.cpp) con `retorno.codec`; acá están las fábricas que lo hacen posible:
//
//   macOS    H.264 por VideoToolbox (CodecsMac.mm) + VP8/VP9/AV1 por software.
//   Windows  H.264 por Media Foundation (CodecsWin.cpp: la placa si hay, si no el H.264 por
//            software de Windows) + VP8/VP9/AV1 por software.
//   Linux    VP8/VP9/AV1 por software (con VA-API sería como Windows: ver
//            docs/planes/PLAN-GATEWAY-FRAGMENTOS.md, F3).
//
// Los decodificadores van envueltos (Decodificador, abajo):
//   - Un hilo interno por decodificador de software (`gateway.hilosPorDecodificador`, 1 por
//     omisión). libwebrtc les pasa todos los núcleos de la máquina y, con 40 invitados, cada
//     uno abría sus hilos (VP9 hasta 8, dav1d más): cientos de hilos peleándose el
//     procesador. Con 40 invitados, el paralelismo está en que son 40; uno por cada uno.
//   - Cada uno anota qué implementación usa (y si es por hardware): una línea en el log la
//     primera vez de cada combinación y la cuenta de los que están abiertos en estado().
//   - Cada Decode() se mide (CargaVideo.hpp): cuánto tarda y si el decodificador trabaja o
//     está en pausa (su capa no viaja: libwebrtc no lo suelta, pero no gasta la placa).
// ============================================================================


#include "webrtc/CodecsVideo.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <atomic>
#include <chrono>
#include <climits>
#include <cstdlib>

#include "CargaVideo.hpp"
#include "DestinoCamara.hpp"
#include "Grillas.hpp"
#include "PlanosYuv.hpp"
#include "ajustes/Ajustes.hpp"
#include "diag/Frontera.hpp"
#include "diag/Registro.hpp"
#include "diag/SondasCuadros.hpp"
#include "ipc/HealthBlock.hpp"

#include "api/video/i420_buffer.h"
#include "rtc_base/time_utils.h"
#include "third_party/libyuv/include/libyuv/convert_argb.h"
#include "third_party/libyuv/include/libyuv/scale_argb.h"

#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_decoder.h"
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

#ifdef __APPLE__
#include "webrtc/CodecsMac.hpp"
#endif
#ifdef _WIN32
#include "webrtc/CodecsWin.hpp"
#endif

namespace ba::gateway::codecs {
namespace {

using SoftwareDecoders =
    webrtc::VideoDecoderFactoryTemplate<webrtc::LibvpxVp8DecoderTemplateAdapter, webrtc::LibvpxVp9DecoderTemplateAdapter,
                                        webrtc::OpenH264DecoderTemplateAdapter, webrtc::Dav1dDecoderTemplateAdapter>;
using SoftwareEncoders =
    webrtc::VideoEncoderFactoryTemplate<webrtc::LibvpxVp8EncoderTemplateAdapter, webrtc::LibvpxVp9EncoderTemplateAdapter,
                                        webrtc::OpenH264EncoderTemplateAdapter, webrtc::LibaomAv1EncoderTemplateAdapter>;

std::string mayus(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

int hilosPorDecodificador() { return std::clamp(ba::ajustes::entero("gateway.hilosPorDecodificador", 1), 1, 16); }

/// Los decodificadores abiertos por (códec, implementación, hardware), y las combinaciones
/// que ya se anotaron en el log.
struct Cuenta {
    std::mutex m;
    std::map<std::tuple<std::string, std::string, bool>, int> activos;
    std::set<std::tuple<std::string, std::string, bool>> anotados;
};
Cuenta& cuenta() {
    static Cuenta c;
    return c;
}

/// El decodificador de libwebrtc envuelto. Ver la cabecera del archivo.
class Decodificador : public webrtc::VideoDecoder {
   public:
    Decodificador(std::unique_ptr<webrtc::VideoDecoder> interno, std::string codec)
        : interno_(std::move(interno)), codec_(std::move(codec)) {}
    ~Decodificador() override { soltarCuenta(); }

    bool Configure(const Settings& s) override {
        Settings mio = s;
        const int hilos = hilosPorDecodificador();
        mio.set_number_of_cores(std::min(std::max(1, s.number_of_cores()), hilos));
        const bool ok = interno_->Configure(mio);
        anotar(ok, hilos);
        return ok;
    }
    int32_t Decode(const webrtc::EncodedImage& img, int64_t render) override {
        const auto t0 = std::chrono::steady_clock::now();
        const int32_t r = interno_->Decode(img, render);
        actividad_.decodifico(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
        return r;
    }
    int32_t Decode(const webrtc::EncodedImage& img, bool faltan, int64_t render) override {
        const auto t0 = std::chrono::steady_clock::now();
        const int32_t r = interno_->Decode(img, faltan, render);
        actividad_.decodifico(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
        return r;
    }
    int32_t RegisterDecodeCompleteCallback(webrtc::DecodedImageCallback* cb) override {
        return interno_->RegisterDecodeCompleteCallback(cb);
    }
    int32_t Release() override {
        soltarCuenta();
        return interno_->Release();
    }
    DecoderInfo GetDecoderInfo() const override {
        DecoderInfo i = interno_->GetDecoderInfo();
#ifdef __APPLE__
        // El H.264 de la Mac es el de VideoToolbox (el SDK de Objective-C no lo dice).
        if (codec_ == "H264") i.is_hardware_accelerated = true;
#endif
        return i;
    }
    const char* ImplementationName() const override { return interno_->ImplementationName(); }

   private:
    void anotar(bool ok, int hilos) {
        const DecoderInfo i = GetDecoderInfo();
        const auto clave = std::make_tuple(codec_, i.implementation_name, i.is_hardware_accelerated);
        bool nuevo = false;
        {
            std::lock_guard<std::mutex> l(cuenta().m);
            if (contado_) --cuenta().activos[clave_];
            contado_ = ok;
            clave_ = clave;
            if (ok) ++cuenta().activos[clave];
            nuevo = cuenta().anotados.insert(clave).second;
        }
        if (!nuevo && ok) return;
        std::string t = "video: decodifica " + codec_ + " con " + i.implementation_name +
                        (i.is_hardware_accelerated ? " (hardware)" : " (software, " + std::to_string(hilos) + " hilo" +
                                                                         (hilos == 1 ? "" : "s") + " por decodificador)");
        if (!ok) t = "video: no arrancó el decodificador " + codec_ + " (" + i.implementation_name + ")";
        ba::diag::registro().linea(ok ? ba::diag::Nivel::Info : ba::diag::Nivel::Aviso, "gateway", 0, t);
    }
    void soltarCuenta() {
        std::lock_guard<std::mutex> l(cuenta().m);
        if (contado_) --cuenta().activos[clave_];
        contado_ = false;
    }

    std::unique_ptr<webrtc::VideoDecoder> interno_;
    std::string codec_;
    cargavideo::Actividad actividad_; ///< trabaja o está en pausa, y cuánto tarda (CargaVideo.hpp)
    bool contado_ = false;
    std::tuple<std::string, std::string, bool> clave_;
};

class Decodificadores : public webrtc::VideoDecoderFactory {
   public:
    explicit Decodificadores(std::unique_ptr<webrtc::VideoDecoderFactory> interna) : interna_(std::move(interna)) {}
    std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override { return interna_->GetSupportedFormats(); }
    CodecSupport QueryCodecSupport(const webrtc::SdpVideoFormat& f, bool escala) const override {
        return interna_->QueryCodecSupport(f, escala);
    }
    std::unique_ptr<webrtc::VideoDecoder> Create(const webrtc::Environment& env, const webrtc::SdpVideoFormat& f) override {
        return envolver(interna_->Create(env, f), f);
    }
    std::unique_ptr<webrtc::VideoDecoder> CreateVideoDecoder(const webrtc::SdpVideoFormat& f) override {
        return envolver(interna_->CreateVideoDecoder(f), f);
    }

   private:
    static std::unique_ptr<webrtc::VideoDecoder> envolver(std::unique_ptr<webrtc::VideoDecoder> d,
                                                           const webrtc::SdpVideoFormat& f) {
        if (!d) return nullptr;
        return std::make_unique<Decodificador>(std::move(d), mayus(f.name));
    }
    std::unique_ptr<webrtc::VideoDecoderFactory> interna_;
};

// --- F3: entregar la cámara en el hilo que decodificó ------------------------------

thread_local CuadroEnPlaca* tlPlaca = nullptr;

/// Lo que pasó con cada cuadro (estado() → `entrega`).
struct CuentaEntrega {
    std::atomic<std::uint64_t> salteados{0};    ///< nadie lo usaba: no se convirtió nada
    std::atomic<std::uint64_t> enteros{0};      ///< BGRA entero a ba-app
    std::atomic<std::uint64_t> enterosPlaca{0}; ///< … bajado de la placa directo a BGRA
    std::atomic<std::uint64_t> celdasPlaca{0};  ///< celda achicada en la placa (sola o con el entero)
    std::atomic<std::uint64_t> celdasConEntero{0}; ///< … de esas, en la misma vuelta que el entero
    std::atomic<std::uint64_t> celdasCpu{0};    ///< celda achicada en el procesador
    std::atomic<std::uint64_t> fallas{0};       ///< no se pudo leer el cuadro
    std::atomic<std::uint64_t> viejos{0};       ///< salió más tarde que gateway.cuadroViejoMs: no se entregó
};
CuentaEntrega& cuentaEntrega() {
    static CuentaEntrega c;
    return c;
}

/// La hora del contenido (horaDeContenido): el desfase entre el reloj de libwebrtc y el de los
/// sellos, y lo que se vio (estado() → `hora_contenido`).
struct HoraContenido {
    std::atomic<std::int64_t> desfaseNs{0};    ///< sellos − libwebrtc (ns)
    std::atomic<bool> hay{false};
    std::atomic<std::uint64_t> con{0}, sin{0}; ///< cuadros con y sin la hora de libwebrtc
    /// SONDA: cuánto falta, al entregarlo, para la hora de su contenido (µs; negativo = ya pasó:
    /// llegó tarde): el último, el mínimo y el máximo desde que arrancó.
    std::atomic<std::int64_t> adelantoUs{0}, adelantoMinUs{INT64_MAX}, adelantoMaxUs{INT64_MIN};
};
HoraContenido& horaContenido() {
    static HoraContenido h;
    return h;
}

/// Un I420 (con el BGRA entero si ya está) achicado a la celda.
void celdaDesdeI420(const webrtc::I420BufferInterface& i, const std::uint8_t* bgra, Grillas::PedidoCelda& p) {
    thread_local std::vector<std::uint8_t> tmp;
    const PlanosYuv c{i.DataY(), i.DataU(), i.DataV(), i.StrideY(), i.StrideU(), i.StrideV(), i.width(), i.height(), bgra};
    Grillas::achicarYuv(c, p.bgra.data(), p.ancho, p.alto, tmp);
}

} // namespace

std::uint64_t horaDeContenido(const webrtc::VideoFrame& frame) {
    HoraContenido& h = horaContenido();
    const std::int64_t renderUs = frame.timestamp_us(); // el «render time» que le puso libwebrtc (-1/0: no hay)
    if (renderUs <= 0) {
        h.sin.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    // El desfase entre los dos relojes, cada 64 cuadros (por hilo) y no por cuadro: los dos
    // leídos juntos (el de los sellos antes y después, el de libwebrtc en el medio). Se suaviza
    // (1/8 por medida): en Windows el de libwebrtc avanza de a 1 ms (timeGetTime) y cada medida
    // cae en otro punto del escalón; un salto de más de 50 ms (un reloj que se corrigió) se toma entero.
    thread_local std::uint32_t cuenta = 0;
    if ((cuenta++ & 63u) == 0 || !h.hay.load(std::memory_order_relaxed)) {
        const std::uint64_t q0 = ba::ipc::HealthBlock::nowQpc();
        const std::int64_t rtcNs = rtc::TimeNanos();
        const std::uint64_t q1 = ba::ipc::HealthBlock::nowQpc();
        const std::int64_t muestra = static_cast<std::int64_t>(q0 + (q1 - q0) / 2) - rtcNs;
        const std::int64_t antes = h.desfaseNs.load(std::memory_order_relaxed);
        const bool salto = !h.hay.load(std::memory_order_relaxed) || std::llabs(muestra - antes) > 50'000'000;
        h.desfaseNs.store(salto ? muestra : antes + (muestra - antes) / 8, std::memory_order_relaxed);
        h.hay.store(true, std::memory_order_relaxed);
    }
    const std::int64_t q = renderUs * 1000 + h.desfaseNs.load(std::memory_order_relaxed);
    if (q <= 0) {
        h.sin.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    h.con.fetch_add(1, std::memory_order_relaxed);
    // SONDA: lo que falta para su hora (o cuánto pasó), para ver que es la que se espera.
    BA_SONDA(const std::int64_t adelanto = renderUs - rtc::TimeMicros();
             h.adelantoUs.store(adelanto, std::memory_order_relaxed);
             if (adelanto < h.adelantoMinUs.load(std::memory_order_relaxed)) h.adelantoMinUs.store(adelanto, std::memory_order_relaxed);
             if (adelanto > h.adelantoMaxUs.load(std::memory_order_relaxed)) h.adelantoMaxUs.store(adelanto, std::memory_order_relaxed);)
    return static_cast<std::uint64_t>(q);
}

AnuncioPlaca::AnuncioPlaca(CuadroEnPlaca* c) : anterior_(tlPlaca) { tlPlaca = c; }
AnuncioPlaca::~AnuncioPlaca() { tlPlaca = anterior_; }

void entregarCamara(const webrtc::VideoFrame& frame, DestinoCamara& destino, std::vector<std::uint8_t>& bgra) {
    // #8 del plan (y #6 adentro): lo que sale del decodificador, en su hilo. La espera es desde
    // que llegó el primer paquete del cuadro (red → armar → decodificar).
    static ba::diag::Frontera& fDecodificado =
        ba::diag::frontera("video.decodificado", 8, "cuadro decodificado → BGRA + grilla + ba-app (hilo del decodificador)");
    static ba::diag::Frontera& fCelda = ba::diag::frontera("grilla.cuadro", 6, "decodificador → retrato de la grilla (achicar)");
    CuentaEntrega& cuenta = cuentaEntrega();
    const rtc::scoped_refptr<webrtc::VideoFrameBuffer> buf = frame.video_frame_buffer();
    if (!buf) return;
    const bool rotado = frame.rotation() != webrtc::kVideoRotation_0;
    const bool deCostado = frame.rotation() == webrtc::kVideoRotation_90 || frame.rotation() == webrtc::kVideoRotation_270;
    const int w = deCostado ? buf->height() : buf->width();
    const int h = deCostado ? buf->width() : buf->height();
    if (w <= 0 || h <= 0) return;

    // Un cuadro que sale del decodificador mucho después de llegar de la red (la máquina no da
    // abasto: colas de decodificar atrasadas) no se convierte ni se entrega: ya no le sirve a
    // nadie en vivo, y convertirlo (bajarlo de la placa) es lo que más cuesta. Así la cola se
    // pone al día sola en vez de atrasarse cada vez más (con 200 invitados en la .177 llegaban
    // con 1-8 s de atraso y, detrás, se trababan las ofertas de los que entraban).
    if (!frame.packet_infos().empty()) {
        const std::int64_t llego = frame.packet_infos().begin()->receive_time().us();
        const int viejoMs = ba::ajustes::entero("gateway.cuadroViejoMs", 1000);
        if (viejoMs > 0 && llego > 0 && rtc::TimeMicros() - llego > static_cast<std::int64_t>(viejoMs) * 1000) {
            cuenta.viejos.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
    // Primero preguntar: lo que nadie usa no se convierte.
    const bool entero = destino.quiereEntero();
    // SONDA (diag/SondasCuadros.hpp): cuándo llegó de la red el primer paquete de este cuadro y
    // cuándo salió del decodificador, en el reloj de libwebrtc (el ritmo de cada tramo, Grillas.hpp).
    std::int64_t decoUs = 0, redUs = 0;
    BA_SONDA(decoUs = rtc::TimeMicros();
             redUs = frame.packet_infos().empty() ? 0 : frame.packet_infos().begin()->receive_time().us();
             destino.anotarLlegada(redUs, decoUs);)
    Grillas::PedidoCelda celda = destino.pedirCelda(w, h, redUs, decoUs);
    if (!entero && !celda.vale()) {
        cuenta.salteados.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // La hora del contenido: ba-app la usa de PTS. La grilla elige el cuadro de cada celda por
    // su instante de captura (el mismo en todas, Grillas::Retrato::elegir): su hora RTP y cuándo
    // llegó de la red su primer paquete, en el reloj de los sellos.
    const std::uint64_t contenido = horaDeContenido(frame);
    celda.contenidoQpc = contenido;
    celda.rtp = frame.timestamp();
    if (!frame.packet_infos().empty() && horaContenido().hay.load(std::memory_order_relaxed)) {
        const std::int64_t q = frame.packet_infos().begin()->receive_time().us() * 1000 +
                               horaContenido().desfaseNs.load(std::memory_order_relaxed);
        if (frame.packet_infos().begin()->receive_time().us() > 0 && q > 0) celda.llegadaRedQpc = static_cast<std::uint64_t>(q);
    }
    ba::diag::MedirPase medir(fDecodificado, static_cast<std::uint64_t>(w) * h * 3 / 2);
    if (!frame.packet_infos().empty()) {
        const std::int64_t llego = frame.packet_infos().begin()->receive_time().us();
        const std::int64_t esperaUs = rtc::TimeMicros() - llego;
        if (llego > 0 && esperaUs > 0) medir.espera(static_cast<std::uint64_t>(esperaUs) * 1000);
    }
    // ¿Es el cuadro que el decodificador de este hilo dejó en la placa?
    CuadroEnPlaca* placa = tlPlaca;
    if (placa != nullptr && (placa->buffer() != buf.get() || rotado)) placa = nullptr;

    // 1) Solo la grilla y el cuadro en la placa: se achica allá y se baja lo chico.
    if (!entero && placa != nullptr && DestinoCamara::celdaEnPlaca()) {
        ba::diag::MedirPase m6(fCelda, static_cast<std::uint64_t>(celda.ancho) * celda.alto * 4);
        if (placa->celdaBgra(celda.ancho, celda.alto, celda.bgra.data(), celda.ancho * 4)) {
            destino.guardarCelda(std::move(celda));
            cuenta.celdasPlaca.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }

    // 2) ba-app lo publica: el BGRA entero (de la placa directo, o del I420) y la celda. Con
    // el cuadro en la placa, la celda sale de allá en la misma vuelta que el entero (una sola
    // espera); si no, se achica del entero en el procesador.
    if (entero) {
        bgra.resize(static_cast<std::size_t>(w) * h * 4);
        rtc::scoped_refptr<webrtc::I420BufferInterface> i420;
        bool celdaHecha = false;
        bool listo = false;
        if (placa != nullptr) {
            if (celda.vale() && DestinoCamara::celdaEnPlaca())
                listo = placa->enteroYCelda(bgra.data(), w * 4, celda.ancho, celda.alto, celda.bgra.data(), celda.ancho * 4,
                                            celdaHecha);
            else
                listo = placa->enteroBgra(bgra.data(), w * 4);
        }
        if (listo) {
            cuenta.enterosPlaca.fetch_add(1, std::memory_order_relaxed);
        } else {
            i420 = buf->ToI420();
            if (i420 && rotado) i420 = webrtc::I420Buffer::Rotate(*i420, frame.rotation());
            if (!i420) {
                cuenta.fallas.fetch_add(1, std::memory_order_relaxed);
                destino.soltarCelda(std::move(celda));
                return;
            }
            libyuv::I420ToARGB(i420->DataY(), i420->StrideY(), i420->DataU(), i420->StrideU(), i420->DataV(), i420->StrideV(),
                               bgra.data(), w * 4, w, h);
        }
        if (celda.vale() && celdaHecha) {
            // Ya achicada en la placa (su tiempo va en el del entero, #8).
            destino.guardarCelda(std::move(celda));
            cuenta.celdasPlaca.fetch_add(1, std::memory_order_relaxed);
            cuenta.celdasConEntero.fetch_add(1, std::memory_order_relaxed);
        } else if (celda.vale()) {
            ba::diag::MedirPase m6(fCelda, static_cast<std::uint64_t>(w) * h * 3 / 2);
            if (i420)
                celdaDesdeI420(*i420, bgra.data(), celda);
            else if (celda.ancho == w && celda.alto == h)
                std::memcpy(celda.bgra.data(), bgra.data(), bgra.size());
            else
                libyuv::ARGBScale(bgra.data(), w * 4, w, h, celda.bgra.data(), celda.ancho * 4, celda.ancho, celda.alto,
                                  libyuv::kFilterBox);
            destino.guardarCelda(std::move(celda));
            cuenta.celdasCpu.fetch_add(1, std::memory_order_relaxed);
        }
        destino.entero(bgra.data(), w, h, contenido);
        cuenta.enteros.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // 3) Solo la celda, en el procesador: del I420 tal cual (lo del decodificador por
    // software), o achicando el buffer nativo primero (la Mac: CVPixelBuffer NV12, que
    // ObjCFrameBuffer::CropAndScale achica con libyuv al pedir el I420; Windows sin la placa
    // a mano: NV12/I420 bajado). Nunca se convierte el cuadro entero a BGRA.
    ba::diag::MedirPase m6(fCelda, static_cast<std::uint64_t>(w) * h * 3 / 2);
    rtc::scoped_refptr<webrtc::I420BufferInterface> chico;
    if (buf->type() == webrtc::VideoFrameBuffer::Type::kI420 && !rotado) {
        celdaDesdeI420(*buf->GetI420(), nullptr, celda);
        destino.guardarCelda(std::move(celda));
        cuenta.celdasCpu.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (rotado) {
        rtc::scoped_refptr<webrtc::I420BufferInterface> i420 = buf->ToI420();
        if (i420) i420 = webrtc::I420Buffer::Rotate(*i420, frame.rotation());
        if (!i420) {
            cuenta.fallas.fetch_add(1, std::memory_order_relaxed);
            destino.soltarCelda(std::move(celda));
            return;
        }
        celdaDesdeI420(*i420, nullptr, celda);
    } else {
        const rtc::scoped_refptr<webrtc::VideoFrameBuffer> escalado = buf->Scale(celda.ancho, celda.alto);
        chico = escalado ? escalado->ToI420() : nullptr;
        if (!chico || chico->width() != celda.ancho || chico->height() != celda.alto) {
            cuenta.fallas.fetch_add(1, std::memory_order_relaxed);
            destino.soltarCelda(std::move(celda));
            return;
        }
        libyuv::I420ToARGB(chico->DataY(), chico->StrideY(), chico->DataU(), chico->StrideU(), chico->DataV(), chico->StrideV(),
                           celda.bgra.data(), celda.ancho * 4, celda.ancho, celda.alto);
    }
    destino.guardarCelda(std::move(celda));
    cuenta.celdasCpu.fetch_add(1, std::memory_order_relaxed);
}

std::unique_ptr<webrtc::VideoEncoderFactory> codificadores() {
#if defined(__APPLE__)
    return codificadoresMac();
#elif defined(_WIN32)
    return win::codificadores(std::make_unique<SoftwareEncoders>());
#else
    return std::make_unique<SoftwareEncoders>();
#endif
}

std::unique_ptr<webrtc::VideoDecoderFactory> decodificadores() {
#if defined(__APPLE__)
    return std::make_unique<Decodificadores>(decodificadoresMac());
#elif defined(_WIN32)
    return std::make_unique<Decodificadores>(win::decodificadores(std::make_unique<SoftwareDecoders>()));
#else
    return std::make_unique<Decodificadores>(std::make_unique<SoftwareDecoders>());
#endif
}

bool h264PorOmision() {
#if defined(__APPLE__)
    return true;
#elif defined(_WIN32)
    return win::hayH264();
#else
    return false;
#endif
}

nlohmann::json estado() {
    nlohmann::json j;
#if defined(__APPLE__)
    j["plataforma"] = "macos";
    j["h264"] = {{"decodifica", "hardware"},
                 {"codifica", "hardware"},
                 {"decodificador", "VideoToolbox"},
                 {"codificador", "VideoToolbox"}};
#elif defined(_WIN32)
    j["plataforma"] = "windows";
    j["h264"] = win::estado();
#else
    j["plataforma"] = "linux";
    j["h264"] = {{"decodifica", "no"}, {"codifica", "no"}};
#endif
    j["auto"] = h264PorOmision() ? "h264" : "vp8";
    j["hilos_por_decodificador"] = hilosPorDecodificador();
    nlohmann::json lista = nlohmann::json::array();
    {
        std::lock_guard<std::mutex> l(cuenta().m);
        for (const auto& [k, n] : cuenta().activos)
            if (n > 0)
                lista.push_back({{"codec", std::get<0>(k)},
                                 {"implementacion", std::get<1>(k)},
                                 {"hardware", std::get<2>(k)},
                                 {"activos", n}});
    }
    j["decodificadores"] = std::move(lista);
    // F3: de los abiertos, cuántos decodificaron en los últimos 2 s y cuántos están en pausa
    // (su capa no viaja: nadie ve a ese invitado).
    const cargavideo::Decodificadores dec = cargavideo::decodificadores();
    j["decodificando"] = {{"trabajando", dec.trabajando}, {"en_pausa", dec.enPausa}};
    // F3: qué se hizo con cada cuadro de cámara (entregarCamara), desde que arrancó.
    const CuentaEntrega& e = cuentaEntrega();
    // La hora del contenido de cada cuadro (horaDeContenido): cuántos la traían y el desfase de relojes.
    const HoraContenido& hc = horaContenido();
    j["hora_contenido"] = {{"con", hc.con.load()}, {"sin", hc.sin.load()}, {"desfase_ms", static_cast<double>(hc.desfaseNs.load()) / 1e6}};
    if constexpr (ba::diag::kSondasCuadros)
        if (hc.con.load() > 0)
            j["hora_contenido"]["adelanto_ms"] = {{"ultimo", static_cast<double>(hc.adelantoUs.load()) / 1000},
                                                  {"min", static_cast<double>(hc.adelantoMinUs.load()) / 1000},
                                                  {"max", static_cast<double>(hc.adelantoMaxUs.load()) / 1000}};
    j["entrega"] = {{"viejos", e.viejos.load()},
                    {"salteados", e.salteados.load()},       {"enteros", e.enteros.load()},
                    {"enteros_placa", e.enterosPlaca.load()}, {"celdas_placa", e.celdasPlaca.load()},
                    {"celdas_cpu", e.celdasCpu.load()},       {"fallas", e.fallas.load()},
                    {"celdas_placa_con_entero", e.celdasConEntero.load()}};
    return j;
}

} // namespace ba::gateway::codecs

