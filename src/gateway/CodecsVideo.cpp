// ============================================================================
// src/gateway/CodecsVideo.cpp — Las fábricas de video del gateway, por plataforma
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
// ============================================================================

#if BA_GATEWAY_HAVE_WEBRTC

#include "CodecsVideo.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ajustes/Ajustes.hpp"
#include "diag/Registro.hpp"

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
#include "CodecsMac.hpp"
#endif
#ifdef _WIN32
#include "CodecsWin.hpp"
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
    int32_t Decode(const webrtc::EncodedImage& img, int64_t render) override { return interno_->Decode(img, render); }
    int32_t Decode(const webrtc::EncodedImage& img, bool faltan, int64_t render) override {
        return interno_->Decode(img, faltan, render);
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

} // namespace

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
    return j;
}

} // namespace ba::gateway::codecs

#endif // BA_GATEWAY_HAVE_WEBRTC
