// ============================================================================
// src/gateway/webrtc/CodecsWin.cpp — H.264 por la placa (D3D11) y Media Foundation en Windows
//
// Lo mismo que CodecsMac.mm con VideoToolbox: que la placa de video decodifique lo que
// sube cada invitado y codifique el retorno, en vez del procesador (libvpx). El libwebrtc
// que usamos (Shiguredo) viene SIN OpenH264, así que el H.264 en Windows sale todo de acá.
//
// Decodificar con la placa: directo por la API de video de D3D11 (DecodificadorD3d11, más
//   abajo: sin el MFT ni sus hilos; un dispositivo para todos). Si la placa o el flujo no
//   sirven para eso, o con `gateway.decodificadorPlaca` = "mft", lo de abajo.
//
// Decodificar por el MFT: el decodificador H.264 de Windows (CLSID_CMSH264DecoderMFT) con un
//   administrador de dispositivos D3D11: decodifica en la placa por DXVA (el motor «Video
//   Decode» de la GPU; AMD, Intel o NVIDIA, la que haya). El cuadro sale en una textura
//   NV12; Lock2D lo trae a memoria y libyuv lo pasa a I420 (el gateway lo necesita en RAM:
//   la memoria compartida con ba-app y la grilla).
//
// Sin la placa (o con `gateway.decodificarConPlaca` apagado) NO se recibe H.264: el mismo
//   decodificador de Windows por software no entregaba ningún cuadro (ProcessOutput E_FAIL)
//   y abría ~24 hilos por flujo. El H.264 no se ofrece para subir y, como la m-line va en los
//   dos sentidos, todo va en VP8 (decodificaConPlaca()).
//
// Codificar (el retorno compartido, RetornoCompartido.cpp): el MFT por hardware que
//   ofrezca la placa (MFTEnumEx con MFT_ENUM_FLAG_HARDWARE: «AMDh264Encoder», «Intel®
//   Quick Sync Video H.264 Encoder MFT», «NVIDIA H.264 Encoder MFT»). Son asincrónicos:
//   piden entrada y entregan salida por eventos (un hilo por codificador espera los
//   eventos). Constrained baseline, sin cuadros B, CBR, bajo retardo; cuadro clave a pedido
//   (CODECAPI_AVEncVideoForceKeyFrame) y bitrate en caliente (CODECAPI_AVEncCommonMeanBitRate).
//   Si no hay MFT por hardware o no arranca, el codificador H.264 de Windows por software
//   (CLSID_CMSH264EncoderMFT, sincrónico), avisado una vez en el log.
//
// Sin ningún H.264 (Windows N sin el paquete de medios) las fábricas quedan con
// VP8/VP9/AV1 y «auto» vuelve a VP8 (CodecsVideo.cpp).
// ============================================================================

#if defined(_WIN32)

#include "webrtc/CodecsWin.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// clang-format off
#include <d3d11.h>
#include <d3d11_4.h>
#include <d3d10.h>
#include <dxgi.h>
#include <dxva.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mftransform.h>
#include <codecapi.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>
// clang-format on

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ajustes/Ajustes.hpp"
#include "diag/Registro.hpp"
#include "platform/Hilo.hpp"

#include "webrtc/CodecsVideo.hpp" // CuadroEnPlaca (F3)
#include "H264Sintaxis.hpp"

#include "api/video/encoded_image.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_decoder.h"
#include "api/video_codecs/video_encoder.h"
#include "common_video/include/video_frame_buffer_pool.h"
#include "modules/video_coding/codecs/h264/include/h264_globals.h"
#include "modules/video_coding/codecs/interface/common_constants.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "rtc_base/ref_counted_object.h"
#include "third_party/libyuv/include/libyuv/convert_argb.h"
#include "third_party/libyuv/include/libyuv/convert.h"
#include "third_party/libyuv/include/libyuv/convert_from.h"

namespace ba::gateway::codecs::win {
namespace {

using Microsoft::WRL::ComPtr;

void log(ba::diag::Nivel n, const std::string& t) { ba::diag::registro().linea(n, "gateway", 0, t); }
void info(const std::string& t) { log(ba::diag::Nivel::Info, t); }
void aviso(const std::string& t) { log(ba::diag::Nivel::Aviso, t); }

std::string hex(HRESULT hr) {
    char b[16];
    std::snprintf(b, sizeof(b), "0x%08lx", static_cast<unsigned long>(hr));
    return b;
}

std::string utf8(const wchar_t* w) {
    if (w == nullptr) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

/// COM en este hilo (los de libwebrtc no lo inician). Una vez por hilo; MTA.
void comEnEsteHilo() {
    thread_local bool hecho = false;
    if (hecho) return;
    hecho = true;
    (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);
}

bool mfIniciado() {
    static const bool ok = [] {
        comEnEsteHilo();
        const HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
        if (FAILED(hr)) aviso("video: Media Foundation no arrancó (" + hex(hr) + "): sin H.264");
        return SUCCEEDED(hr);
    }();
    return ok;
}

// D3D11_DECODER_PROFILE_H264_VLD_NOFGT (sin dxguid.lib).
constexpr GUID kPerfilH264 = {0x1b81be68, 0xa0c7, 0x11d3, {0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5}};

std::string fabricanteDe(UINT vendor) {
    switch (vendor) {
    case 0x1002: return "AMD";
    case 0x10DE: return "NVIDIA";
    case 0x8086: return "Intel";
    default: return {};
    }
}

/// Un dispositivo D3D11 con su administrador para el DXVA de los decodificadores.
struct Dispositivo {
    ComPtr<ID3D11Device> dev;
    ComPtr<IMFDXGIDeviceManager> mgr;
    UINT token = 0;
};

/// El nombre de la placa y si decodifica H.264 a NV12.
void describirDispositivo(const ComPtr<ID3D11Device>& dev, std::string* placa, bool* decodificaH264) {
    if (placa != nullptr) {
        ComPtr<IDXGIDevice> dxgi;
        ComPtr<IDXGIAdapter> ad;
        DXGI_ADAPTER_DESC desc{};
        if (SUCCEEDED(dev.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&ad)) && SUCCEEDED(ad->GetDesc(&desc))) {
            *placa = utf8(desc.Description);
            const std::string f = fabricanteDe(desc.VendorId);
            if (!f.empty() && placa->find(f) == std::string::npos) *placa = f + " " + *placa;
        }
    }
    if (decodificaH264 != nullptr) {
        *decodificaH264 = false;
        ComPtr<ID3D11VideoDevice> vd;
        if (SUCCEEDED(dev.As(&vd))) {
            const UINT n = vd->GetVideoDecoderProfileCount();
            for (UINT i = 0; i < n; ++i) {
                GUID g{};
                if (SUCCEEDED(vd->GetVideoDecoderProfile(i, &g)) && IsEqualGUID(g, kPerfilH264)) {
                    BOOL nv12 = FALSE;
                    if (SUCCEEDED(vd->CheckVideoDecoderFormat(&kPerfilH264, DXGI_FORMAT_NV12, &nv12)) && nv12)
                        *decodificaH264 = true;
                }
            }
        }
    }
}

/// Un dispositivo para el MFT (con su administrador DXGI).
bool crearDispositivo(Dispositivo& d) {
    D3D_FEATURE_LEVEL nivel{};
    ComPtr<ID3D11DeviceContext> ctx;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                   D3D11_SDK_VERSION, &d.dev, &nivel, &ctx);
    if (FAILED(hr)) return false;
    // Varios decodificadores (cada uno en su hilo) comparten el dispositivo.
    ComPtr<ID3D10Multithread> mt;
    if (SUCCEEDED(d.dev.As(&mt))) mt->SetMultithreadProtected(TRUE);
    hr = MFCreateDXGIDeviceManager(&d.token, &d.mgr);
    if (FAILED(hr)) return false;
    return SUCCEEDED(d.mgr->ResetDevice(d.dev.Get(), d.token));
}

/// Un pozo chico de dispositivos (los decodificadores se reparten): uno solo serializa todas
/// las lecturas de textura de todos los invitados en su contexto.
constexpr int kDispositivos = 4;

ComPtr<IMFDXGIDeviceManager> administradorD3d() {
    static std::mutex m;
    static std::vector<Dispositivo> pozo;
    static int turno = 0;
    std::lock_guard<std::mutex> l(m);
    if (pozo.size() < static_cast<size_t>(kDispositivos)) {
        Dispositivo d;
        if (crearDispositivo(d)) pozo.push_back(std::move(d));
    }
    if (pozo.empty()) return nullptr;
    return pozo[static_cast<size_t>(turno++ % static_cast<int>(pozo.size()))].mgr;
}

// --- Lo que hay en la máquina (una vez) ------------------------------------------------

struct Hallazgo {
    bool decodificador = false; ///< hay H.264 para decodificar (placa o software)
    bool codificador = false;   ///< hay H.264 para codificar
    bool decodificaHw = false;
    bool codificaHw = false;
    std::string placa;
    std::string nombreCodHw; ///< el MFT por hardware («AMDh264Encoder»)
    std::string fabricanteCodHw;
};

ComPtr<IMFTransform> crearCodificadorHw(std::string* nombre, std::string* fabricante) {
    MFT_REGISTER_TYPE_INFO entra{MFMediaType_Video, MFVideoFormat_NV12};
    MFT_REGISTER_TYPE_INFO sale{MFMediaType_Video, MFVideoFormat_H264};
    IMFActivate** lista = nullptr;
    UINT32 n = 0;
    HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER, &entra,
                           &sale, &lista, &n);
    if (FAILED(hr) || n == 0) {
        if (lista != nullptr) CoTaskMemFree(lista);
        return nullptr;
    }
    ComPtr<IMFTransform> mft;
    for (UINT32 i = 0; i < n && !mft; ++i) {
        if (SUCCEEDED(lista[i]->ActivateObject(IID_PPV_ARGS(&mft)))) {
            wchar_t* s = nullptr;
            UINT32 largo = 0;
            if (nombre != nullptr && SUCCEEDED(lista[i]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &s, &largo))) {
                *nombre = utf8(s);
                CoTaskMemFree(s);
            }
            s = nullptr;
            if (fabricante != nullptr &&
                SUCCEEDED(lista[i]->GetAllocatedString(MFT_ENUM_HARDWARE_VENDOR_ID_Attribute, &s, &largo))) {
                const std::string v = utf8(s); // «VEN_1002»
                CoTaskMemFree(s);
                const UINT id = v.size() > 4 ? static_cast<UINT>(std::strtoul(v.c_str() + 4, nullptr, 16)) : 0;
                *fabricante = fabricanteDe(id);
            }
        }
    }
    for (UINT32 i = 0; i < n; ++i)
        lista[i]->Release();
    CoTaskMemFree(lista);
    return mft;
}

const Hallazgo& hallazgo();

// --- Cuentas para estado() -------------------------------------------------------------

std::atomic<int> gCodHw{0}, gCodSw{0}, gCaidasDec{0}, gCaidasCod{0};
std::atomic<bool> gAvisoDec{false}, gAvisoCod{false}, gAvisoDxva{false};

// --- Utilidades de Media Foundation ----------------------------------------------------

void ponerU32(ICodecAPI* api, const GUID& g, ULONG v) {
    if (api == nullptr) return;
    VARIANT x;
    VariantInit(&x);
    x.vt = VT_UI4;
    x.ulVal = v;
    (void)api->SetValue(&g, &x);
}
void ponerBool(ICodecAPI* api, const GUID& g, bool v) {
    if (api == nullptr) return;
    VARIANT x;
    VariantInit(&x);
    x.vt = VT_BOOL;
    x.boolVal = v ? VARIANT_TRUE : VARIANT_FALSE;
    (void)api->SetValue(&g, &x);
}

/// El bitrate del codificador: medio y máximo iguales (CBR) y un búfer de medio segundo, para
/// que un cambio en caliente se note enseguida (con el búfer de fábrica, ~1 s o más, el AMD
/// tardaba en bajar).
void ponerTasa(ICodecAPI* api, uint32_t kbps) {
    ponerU32(api, CODECAPI_AVEncCommonMeanBitRate, kbps * 1000u);
    ponerU32(api, CODECAPI_AVEncCommonMaxBitRate, kbps * 1000u);
    ponerU32(api, CODECAPI_AVEncCommonBufferSize, kbps * 1000u / 2u);
}

ComPtr<IMFSample> muestraConBytes(const uint8_t* datos, size_t n) {
    ComPtr<IMFMediaBuffer> buf;
    if (FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(n), &buf))) return nullptr;
    BYTE* p = nullptr;
    if (FAILED(buf->Lock(&p, nullptr, nullptr))) return nullptr;
    if (datos != nullptr) std::memcpy(p, datos, n);
    buf->Unlock();
    buf->SetCurrentLength(static_cast<DWORD>(n));
    ComPtr<IMFSample> s;
    if (FAILED(MFCreateSample(&s))) return nullptr;
    s->AddBuffer(buf.Get());
    return s;
}

/// ¿Hay una NAL de tipo `tipo` en el flujo Annex B?
bool tieneNal(const uint8_t* d, size_t n, int tipo) {
    for (size_t i = 0; i + 3 < n; ++i) {
        if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1) {
            if ((d[i + 3] & 0x1f) == tipo) return true;
            i += 2;
        }
    }
    return false;
}

std::vector<webrtc::SdpVideoFormat> formatosH264(bool paraDecodificar) {
    // packetization-mode=1 (el de los navegadores). Codifica constrained baseline; decodifica
    // además main y high (lo que suba un navegador con H.264 por hardware).
    std::vector<const char*> perfiles{"42e01f", "42001f"};
    if (paraDecodificar) {
        perfiles.push_back("4d001f");
        perfiles.push_back("640c1f");
        perfiles.push_back("64001f");
    }
    std::vector<webrtc::SdpVideoFormat> r;
    for (const char* p : perfiles)
        r.emplace_back("H264", webrtc::CodecParameterMap{
                                   {"profile-level-id", p}, {"level-asymmetry-allowed", "1"}, {"packetization-mode", "1"}});
    return r;
}

bool esH264(const webrtc::SdpVideoFormat& f) { return _stricmp(f.name.c_str(), "H264") == 0; }

// ============================================================================
// Decodificador
// ============================================================================

class DecodificadorMf : public webrtc::VideoDecoder {
   public:
    ~DecodificadorMf() override { Release(); }

    bool Configure(const Settings& s) override {
        comEnEsteHilo();
        hilos_ = std::max(1, s.number_of_cores());
        const webrtc::RenderResolution r = s.max_render_resolution();
        if (r.Valid()) {
            anchoIni_ = r.Width();
            altoIni_ = r.Height();
        }
        // Solo con la placa: el MFT por software de Windows no sirve (ProcessOutput da E_FAIL
        // en la .177 y abre ~24 hilos por flujo). Sin la placa, el H.264 no se ofrece para
        // subir y los invitados van en VP8 (ver decodificaConPlaca()).
        if (iniciar(true)) return true;
        ++gCaidasDec;
        if (!gAvisoDec.exchange(true)) aviso("video: el decodificador H.264 por la placa (DXVA) no arrancó");
        return false;
    }

    int32_t Decode(const webrtc::EncodedImage& img, bool, int64_t) override {
        if (!mft_ || cb_ == nullptr) return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
        if (img.data() == nullptr || img.size() == 0) return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
        ComPtr<IMFSample> s = muestraConBytes(img.data(), img.size());
        if (!s) return WEBRTC_VIDEO_CODEC_MEMORY;
        tiempo_ += 333333;
        s->SetSampleTime(tiempo_);
        s->SetSampleDuration(333333);
        metas_.push_back({tiempo_, img.RtpTimestamp()});
        if (metas_.size() > 64) metas_.pop_front();
        HRESULT hr = mft_->ProcessInput(0, s.Get(), 0);
        if (hr == MF_E_NOTACCEPTING) {
            drenar();
            hr = mft_->ProcessInput(0, s.Get(), 0);
        }
        if (FAILED(hr)) return fallo("ProcessInput", hr);
        if (!drenar()) return fallo("ProcessOutput", hrUltimo_);
        errores_ = 0;
        return WEBRTC_VIDEO_CODEC_OK;
    }

    int32_t RegisterDecodeCompleteCallback(webrtc::DecodedImageCallback* cb) override {
        cb_ = cb;
        return WEBRTC_VIDEO_CODEC_OK;
    }

    int32_t Release() override {
        if (mft_) {
            mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
            mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
            if (hw_) mft_->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, 0);
        }
        mft_.Reset();
        salida_.Reset();
        metas_.clear();
        return WEBRTC_VIDEO_CODEC_OK;
    }

    DecoderInfo GetDecoderInfo() const override {
        DecoderInfo i;
        i.implementation_name = hw_ ? "MediaFoundation (DXVA " + hallazgo().placa + ")" : "MediaFoundation (software)";
        i.is_hardware_accelerated = hw_;
        return i;
    }
    const char* ImplementationName() const override { return hw_ ? "MediaFoundation-DXVA" : "MediaFoundation"; }

   private:
    struct Meta {
        LONGLONG t;
        uint32_t rtp;
    };

    bool iniciar(bool hw) {
        Release();
        hw_ = false;
        HRESULT hr = CoCreateInstance(CLSID_CMSH264DecoderMFT, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&mft_));
        if (FAILED(hr)) {
            aviso("video: no está el decodificador H.264 de Windows (" + hex(hr) + ")");
            return false;
        }
        ComPtr<IMFAttributes> at;
        if (SUCCEEDED(mft_->GetAttributes(&at))) at->SetUINT32(MF_LOW_LATENCY, TRUE);
        ComPtr<ICodecAPI> api;
        mft_.As(&api);
        ponerBool(api.Get(), CODECAPI_AVLowLatencyMode, true);
        // También con la placa: sin esto abre dos hilos por decodificador (msmpeg2vdec) que,
        // con 20 flujos, eran 40 hilos y un núcleo entero solo en armar los pedidos al DXVA.
        ponerU32(api.Get(), CODECAPI_AVDecNumWorkerThreads, static_cast<ULONG>(hilos_));
        if (hw) {
            UINT32 consciente = 0;
            if (!at || FAILED(at->GetUINT32(MF_SA_D3D11_AWARE, &consciente)) || !consciente) return false;
            ComPtr<IMFDXGIDeviceManager> mgr = administradorD3d();
            if (!mgr) return false;
            hr = mft_->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(mgr.Get()));
            if (FAILED(hr)) {
                mft_.Reset();
                return false;
            }
            hw_ = true;
        }
        ComPtr<IMFMediaType> in;
        MFCreateMediaType(&in);
        in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (anchoIni_ > 0 && altoIni_ > 0)
            MFSetAttributeSize(in.Get(), MF_MT_FRAME_SIZE, static_cast<UINT32>(anchoIni_), static_cast<UINT32>(altoIni_));
        hr = mft_->SetInputType(0, in.Get(), 0);
        if (FAILED(hr) || !elegirSalida()) {
            aviso("video: el decodificador H.264 no acepta el formato (" + hex(hr) + ")");
            mft_.Reset();
            hw_ = false;
            return false;
        }
        mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
        return true;
    }

    /// La salida en NV12 (de nuevo tras un cambio de formato: el primer SPS, otro tamaño).
    bool elegirSalida() {
        for (DWORD i = 0;; ++i) {
            ComPtr<IMFMediaType> t;
            if (FAILED(mft_->GetOutputAvailableType(0, i, &t))) return false;
            GUID sub{};
            t->GetGUID(MF_MT_SUBTYPE, &sub);
            if (!IsEqualGUID(sub, MFVideoFormat_NV12)) continue;
            if (FAILED(mft_->SetOutputType(0, t.Get(), 0))) return false;
            UINT32 w = 0, h = 0;
            MFGetAttributeSize(t.Get(), MF_MT_FRAME_SIZE, &w, &h);
            anchoSup_ = static_cast<int>(w);
            altoSup_ = static_cast<int>(h);
            ancho_ = anchoSup_;
            alto_ = altoSup_;
            x0_ = y0_ = 0;
            MFVideoArea area{};
            if (SUCCEEDED(t->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&area), sizeof(area),
                                     nullptr)) &&
                area.Area.cx > 0 && area.Area.cy > 0) {
                x0_ = area.OffsetX.value;
                y0_ = area.OffsetY.value;
                ancho_ = area.Area.cx;
                alto_ = area.Area.cy;
            }
            UINT32 paso = 0;
            paso_ = SUCCEEDED(t->GetUINT32(MF_MT_DEFAULT_STRIDE, &paso)) ? static_cast<int>(paso) : anchoSup_;
            MFT_OUTPUT_STREAM_INFO si{};
            mft_->GetOutputStreamInfo(0, &si);
            provee_ = (si.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
            tamSalida_ = si.cbSize;
            salida_.Reset();
            return true;
        }
    }

    /// Saca todo lo que el decodificador tenga listo. false si falló.
    bool drenar() {
        for (;;) {
            MFT_OUTPUT_DATA_BUFFER ob{};
            ob.dwStreamID = 0;
            if (!provee_) {
                if (!salida_) {
                    salida_ = muestraConBytes(nullptr, std::max<DWORD>(tamSalida_, 1));
                    if (!salida_) return false;
                }
                ob.pSample = salida_.Get();
            }
            DWORD estado = 0;
            const HRESULT hr = mft_->ProcessOutput(0, 1, &ob, &estado);
            if (ob.pEvents != nullptr) ob.pEvents->Release();
            ComPtr<IMFSample> s;
            if (provee_ && ob.pSample != nullptr) s.Attach(ob.pSample);
            else if (!provee_) s = salida_;
            if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) return true;
            if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
                if (!elegirSalida()) {
                    hrUltimo_ = hr;
                    return false;
                }
                continue;
            }
            if (FAILED(hr)) {
                hrUltimo_ = hr;
                return false;
            }
            if (s) entregar(s.Get());
        }
    }

    void entregar(IMFSample* s) {
        LONGLONG t = 0;
        s->GetSampleTime(&t);
        uint32_t rtp = 0;
        bool hay = false;
        while (!metas_.empty() && metas_.front().t <= t) {
            rtp = metas_.front().rtp;
            hay = metas_.front().t == t;
            metas_.pop_front();
        }
        if (!hay && rtp == 0) return; // no es de nadie (no debería pasar)
        ComPtr<IMFMediaBuffer> buf;
        if (FAILED(s->GetBufferByIndex(0, &buf))) return;
        if (hw_ && !dxgiVisto_) {
            dxgiVisto_ = true;
            ComPtr<IMFDXGIBuffer> dx;
            if (FAILED(buf.As(&dx))) {
                // Aceptó el administrador pero decodifica en el procesador (sin perfil DXVA
                // para este flujo o sin lugar en la placa).
                hw_ = false;
                ++gCaidasDec;
                if (!gAvisoDxva.exchange(true))
                    aviso("video: el DXVA no tomó este flujo H.264 (" + std::to_string(anchoSup_) + "x" +
                          std::to_string(altoSup_) + "): se decodifica por software");
            }
        }
        BYTE* p = nullptr;
        LONG paso = 0;
        DWORD largo = 0;
        ComPtr<IMF2DBuffer2> b2;
        ComPtr<IMF2DBuffer> b1;
        bool de2d = false;
        if (SUCCEEDED(buf.As(&b2))) {
            BYTE* inicio = nullptr;
            if (FAILED(b2->Lock2DSize(MF2DBuffer_LockFlags_Read, &p, &paso, &inicio, &largo))) return;
            de2d = true;
        } else if (SUCCEEDED(buf.As(&b1))) {
            if (FAILED(b1->Lock2D(&p, &paso))) return;
            de2d = true;
        } else {
            if (FAILED(buf->Lock(&p, nullptr, &largo))) return;
            paso = paso_;
        }
        // El plano UV va después de las filas de Y de la superficie (alineadas: 368 para
        // 360, 1088 para 1080). Con el largo del bloque se sabe sin adivinar.
        int filasY = altoSup_;
        if (largo > 0 && paso > 0) filasY = std::max(alto_, static_cast<int>(largo / static_cast<DWORD>(paso)) * 2 / 3);
        if (paso > 0 && ancho_ > 0 && alto_ > 0) {
            const uint8_t* y = p + static_cast<ptrdiff_t>(y0_) * paso + x0_;
            const uint8_t* uv = p + static_cast<ptrdiff_t>(filasY) * paso + static_cast<ptrdiff_t>(y0_ / 2) * paso + (x0_ & ~1);
            rtc::scoped_refptr<webrtc::I420Buffer> i420 = webrtc::I420Buffer::Create(ancho_, alto_);
            libyuv::NV12ToI420(y, paso, uv, paso, i420->MutableDataY(), i420->StrideY(), i420->MutableDataU(),
                               i420->StrideU(), i420->MutableDataV(), i420->StrideV(), ancho_, alto_);
            if (b2) b2->Unlock2D();
            else if (b1) b1->Unlock2D();
            else buf->Unlock();
            webrtc::VideoFrame f = webrtc::VideoFrame::Builder().set_video_frame_buffer(i420).set_timestamp_rtp(rtp).build();
            cb_->Decoded(f, absl::nullopt, absl::nullopt);
            return;
        }
        if (de2d) {
            if (b2) b2->Unlock2D();
            else b1->Unlock2D();
        } else {
            buf->Unlock();
        }
    }

    int32_t fallo(const char* que, HRESULT hr) {
        // Varios errores seguidos: se rehace (un cuadro clave lo pide libwebrtc al ver el error).
        if (++errores_ >= 3) {
            ++gCaidasDec;
            if (!gAvisoDec.exchange(true))
                aviso(std::string("video: el decodificador H.264 por la placa falla (") + que + " " + hex(hr) +
                      "): se rehace");
            iniciar(true);
            errores_ = 0;
        }
        return WEBRTC_VIDEO_CODEC_ERROR;
    }

    ComPtr<IMFTransform> mft_;
    ComPtr<IMFSample> salida_;
    webrtc::DecodedImageCallback* cb_ = nullptr;
    std::deque<Meta> metas_;
    LONGLONG tiempo_ = 0;
    HRESULT hrUltimo_ = S_OK;
    int hilos_ = 1, anchoIni_ = 0, altoIni_ = 0;
    int anchoSup_ = 0, altoSup_ = 0, ancho_ = 0, alto_ = 0, x0_ = 0, y0_ = 0, paso_ = 0;
    DWORD tamSalida_ = 0;
    bool provee_ = false, hw_ = false, dxgiVisto_ = false;
    int errores_ = 0;
};

// ============================================================================
// Decodificador por la API de video de D3D11 (sin el MFT)
// ============================================================================
//
// El MFT de Windows (msmpeg2vdec) con DXVA, medido en la .177 con 10 invitados en 720p:
// 405 % de un núcleo en el gateway contra 305 % de VP8 por software, con ~260 hilos sin
// nombre y la lectura de la textura por Lock2D. Acá lo mismo sin esa maquinaria: el H.264
// se lee en este hilo (H264Sintaxis: SPS/PPS/slice, POC y referencias), la placa decodifica
// los slices (ID3D11VideoDecoder, formato «corto»: el driver lee la cabecera de cada
// slice), y el cuadro se copia a una textura de paso PROPIA (una por decodificador, se
// reusa) y se espera con una cerca (ID3D11Fence: el hilo duerme, no gira) antes de leerla.
// Un solo dispositivo para todos (`gateway.dispositivosPlaca`, 1 de fábrica): lo que se
// hace con su contexto bajo candado es encolar trabajo (µs).
//
// Los cuadros salen en el orden en que llegan (sin reordenar por POC): WebRTC no manda
// cuadros B. Sin formato corto en la placa, con campos entrelazados, FMO, no 4:2:0 de 8
// bits o errores que se repiten: el MFT de siempre (DecodificadorMf), avisado una vez.
// `gateway.decodificadorPlaca` = "mft" vuelve al MFT para comparar.

/// Un dispositivo D3D11 para la API de video (los decodificadores se reparten si hay más).
struct DispositivoVideo {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<ID3D11VideoDevice> vdev;
    ComPtr<ID3D11VideoContext> vctx;
    ComPtr<ID3D11Device5> dev5; ///< cercas (Windows 10 1703+); sin esto, Map espera
    ComPtr<ID3D11DeviceContext4> ctx4;
    std::mutex m; ///< el contexto inmediato es uno solo
};

std::shared_ptr<DispositivoVideo> dispositivoVideo() {
    static std::mutex m;
    static std::vector<std::shared_ptr<DispositivoVideo>> pozo;
    static int turno = 0;
    static bool fallo = false;
    const int cuantos = std::clamp(ba::ajustes::entero("gateway.dispositivosPlaca", 1), 1, 8);
    std::lock_guard<std::mutex> l(m);
    if (pozo.size() < static_cast<size_t>(cuantos) && !fallo) {
        auto d = std::make_shared<DispositivoVideo>();
        D3D_FEATURE_LEVEL nivel{};
        const HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                                             nullptr, 0, D3D11_SDK_VERSION, &d->dev, &nivel, &d->ctx);
        if (SUCCEEDED(hr) && SUCCEEDED(d->dev.As(&d->vdev)) && SUCCEEDED(d->ctx.As(&d->vctx))) {
            ComPtr<ID3D10Multithread> mt;
            if (SUCCEEDED(d->dev.As(&mt))) mt->SetMultithreadProtected(TRUE);
            if (FAILED(d->dev.As(&d->dev5)) || FAILED(d->ctx.As(&d->ctx4))) {
                d->dev5.Reset();
                d->ctx4.Reset();
            }
            pozo.push_back(std::move(d));
        } else {
            fallo = true;
            aviso("video: no se pudo abrir la placa para decodificar con D3D11 (" + hex(hr) + ")");
        }
    }
    if (pozo.empty()) return nullptr;
    return pozo[static_cast<size_t>(turno++ % static_cast<int>(pozo.size()))];
}

std::atomic<int> gDecD3d{0};
std::atomic<bool> gAvisoVp{false};      ///< F3: la placa no achica la grilla (avisado una vez)
std::atomic<long long> gCeldasPlaca{0}; ///< F3: celdas de la grilla achicadas en la placa
std::atomic<long long> gCeldasConEntero{0}; ///< … de esas, en la misma vuelta que el entero
/// F3: el procesador de video no lee directo las superficies del decodificador (el arreglo
/// con BIND_DECODER solo): se copia cada cuadro a una textura intermedia y se achica de ahí.
/// Lo descubre el primer decodificador al que le falla y vale para todos (es la misma placa);
/// `gateway.celdaIntermedia` lo fuerza desde el arranque (si la directa no falla pero sale mal).
std::atomic<bool>& vpIntermedia() {
    static std::atomic<bool> v{ba::ajustes::siNo("gateway.celdaIntermedia", false)};
    return v;
}
std::atomic<bool> gAvisoD3d{false};

class DecodificadorD3d11 : public webrtc::VideoDecoder {
   public:
    ~DecodificadorD3d11() override { Release(); }

    bool Configure(const Settings& s) override {
        comEnEsteHilo();
        ajustes_ = s;
        mft_.reset();
        dev_ = dispositivoVideo();
        if (!dev_) return cambiarAMft("sin dispositivo D3D11");
        return true;
    }

    int32_t RegisterDecodeCompleteCallback(webrtc::DecodedImageCallback* cb) override {
        cb_ = cb;
        if (mft_) mft_->RegisterDecodeCompleteCallback(cb);
        return WEBRTC_VIDEO_CODEC_OK;
    }

    int32_t Release() override {
        if (mft_) mft_->Release();
        soltarPlaca();
        dpb_.reiniciar();
        conReferencia_ = false;
        return WEBRTC_VIDEO_CODEC_OK;
    }

    DecoderInfo GetDecoderInfo() const override {
        if (mft_) return mft_->GetDecoderInfo();
        DecoderInfo i;
        i.implementation_name = "D3D11 (" + hallazgo().placa + ")";
        i.is_hardware_accelerated = true;
        return i;
    }
    const char* ImplementationName() const override { return mft_ ? mft_->ImplementationName() : "D3D11VA"; }

    int32_t Decode(const webrtc::EncodedImage& img, bool faltan, int64_t render) override {
        if (mft_) return mft_->Decode(img, faltan, render);
        if (cb_ == nullptr || !dev_) return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
        if (img.data() == nullptr || img.size() == 0) return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
        const uint8_t* d = img.data();
        const std::vector<h264::Nal> nals = h264::partirAnnexB(d, img.size());
        slices_.clear();
        std::optional<h264::Slice> primero;
        bool intra = true;
        for (const h264::Nal& n : nals) {
            if (n.largo < 2) continue;
            const int tipo = d[n.inicio] & 0x1f;
            const int refIdc = (d[n.inicio] >> 5) & 3;
            if (tipo == 7) {
                const std::vector<uint8_t> r = h264::aRbsp(d + n.inicio + 1, n.largo - 1);
                if (auto s = h264::leerSps(r.data(), r.size())) {
                    const size_t id = static_cast<size_t>(s->id);
                    sps_[id] = std::make_unique<h264::Sps>(*s);
                    spsPtr_[id] = sps_[id].get();
                }
            } else if (tipo == 8) {
                const std::vector<uint8_t> r = h264::aRbsp(d + n.inicio + 1, n.largo - 1);
                if (auto p = h264::leerPps(r.data(), r.size(), spsPtr_.data())) {
                    const size_t id = static_cast<size_t>(p->id);
                    pps_[id] = std::make_unique<h264::Pps>(*p);
                    ppsPtr_[id] = pps_[id].get();
                }
            } else if (tipo == 1 || tipo == 5) {
                // Del primer slice, la cabecera entera; de los otros, solo el tipo.
                const size_t tope = primero ? std::min<size_t>(n.largo - 1, 24) : n.largo - 1;
                const std::vector<uint8_t> r = h264::aRbsp(d + n.inicio + 1, tope);
                if (!primero) {
                    primero = h264::leerSlice(r.data(), r.size(), refIdc, tipo, spsPtr_.data(), ppsPtr_.data());
                    if (!primero) return error("cabecera de slice ilegible");
                    if (!primero->intra()) intra = false;
                } else {
                    h264::Bits b(r.data(), r.size());
                    b.ue();
                    const int st = static_cast<int>(b.ue()) % 5;
                    if (st != 2 && st != 4) intra = false;
                }
                slices_.push_back(n);
            } else if (tipo == 20 || tipo == 14 || tipo == 15) {
                return pasarAlMft("H.264 escalable o multivista", img, faltan, render);
            }
        }
        if (!primero) return WEBRTC_VIDEO_CODEC_OK; // solo parámetros
        const h264::Pps& pps = *ppsPtr_[static_cast<size_t>(primero->pps_id)];
        const h264::Sps& sps = *spsPtr_[static_cast<size_t>(pps.sps_id)];
        if (!sps.frame_mbs_only || pps.num_slice_groups_minus1 > 0 || sps.chroma_format_idc != 1 ||
            sps.bit_depth_luma_minus8 != 0 || sps.bit_depth_chroma_minus8 != 0)
            return pasarAlMft("campos entrelazados, FMO o no 4:2:0 de 8 bits", img, faltan, render);
        if (!decodificador_ || sps.anchoCodificado() != anchoCod_ || sps.altoCodificado() != altoCod_ ||
            sps.max_num_ref_frames + 3 > superficies_) {
            if (!primero->idr) return WEBRTC_VIDEO_CODEC_ERROR; // hasta la clave
            if (!crearPlaca(sps))
                return pasarAlMft("la placa no abre el decodificador " + std::to_string(sps.ancho()) + "x" +
                                      std::to_string(sps.alto()) + ", " + hex(hrUltimo_),
                                  img, faltan, render);
        }
        if (primero->idr) conReferencia_ = true;
        if (!conReferencia_) return WEBRTC_VIDEO_CODEC_ERROR;

        dpb_.empezar(sps, *primero);
        int sup = -1;
        for (int i = 0; i < superficies_; ++i) {
            const int c = (siguiente_ + i) % superficies_;
            if (!dpb_.usa(c)) {
                sup = c;
                break;
            }
        }
        if (sup < 0) {
            dpb_.reiniciar();
            conReferencia_ = false;
            return error("sin superficie libre");
        }
        siguiente_ = (sup + 1) % superficies_;
        llenarParametros(sps, pps, *primero, intra, sup);
        if (!enviar(d, sup)) {
            dpb_.reiniciar();
            conReferencia_ = false;
            return error("la placa no tomó el cuadro (" + hex(hrUltimo_) + ")");
        }
        dpb_.terminar(sup);
        if (!entregar(img, sps, sup)) return error("no se pudo leer el cuadro de la placa (" + hex(hrUltimo_) + ")");
        errores_ = 0;
        return WEBRTC_VIDEO_CODEC_OK;
    }

   private:
    int32_t error(const std::string& que) {
        if (++errores_ >= 5) {
            errores_ = 0;
            cambiarAMft(que); // libwebrtc pide una clave al ver el error
        }
        return WEBRTC_VIDEO_CODEC_ERROR;
    }

    int32_t pasarAlMft(const std::string& porque, const webrtc::EncodedImage& img, bool faltan, int64_t render) {
        if (!cambiarAMft(porque)) return WEBRTC_VIDEO_CODEC_ERROR;
        return mft_->Decode(img, faltan, render);
    }

    bool cambiarAMft(const std::string& porque) {
        ++gCaidasDec;
        if (!gAvisoD3d.exchange(true))
            aviso("video: el H.264 por D3D11 no sirve para este flujo (" + porque + "): va por el MFT");
        soltarPlaca();
        auto m = std::make_unique<DecodificadorMf>();
        if (!m->Configure(ajustes_)) return false;
        if (cb_ != nullptr) m->RegisterDecodeCompleteCallback(cb_);
        mft_ = std::move(m);
        return true;
    }

    void soltarPlaca() {
        if (decodificador_) --gDecD3d;
        soltarProcesador(); // sus vistas apuntan a las superficies que se sueltan
        vistas_.clear();
        decodificador_.Reset();
        textura_.Reset();
        paso_.Reset();
        cerca_.Reset();
        if (evento_ != nullptr) {
            CloseHandle(evento_);
            evento_ = nullptr;
        }
        superficies_ = 0;
        anchoCod_ = altoCod_ = 0;
    }

    bool crearPlaca(const h264::Sps& sps) {
        soltarPlaca();
        dpb_.reiniciar();
        D3D11_VIDEO_DECODER_DESC desc{};
        desc.Guid = kPerfilH264;
        desc.SampleWidth = static_cast<UINT>(sps.anchoCodificado());
        desc.SampleHeight = static_cast<UINT>(sps.altoCodificado());
        desc.OutputFormat = DXGI_FORMAT_NV12;
        UINT n = 0;
        HRESULT hr = dev_->vdev->GetVideoDecoderConfigCount(&desc, &n);
        if (FAILED(hr)) {
            hrUltimo_ = hr;
            return false;
        }
        D3D11_VIDEO_DECODER_CONFIG cfg{};
        bool hay = false;
        for (UINT i = 0; i < n && !hay; ++i) {
            D3D11_VIDEO_DECODER_CONFIG c{};
            if (SUCCEEDED(dev_->vdev->GetVideoDecoderConfig(&desc, i, &c)) && c.ConfigBitstreamRaw == 2) {
                cfg = c;
                hay = true;
            }
        }
        if (!hay) {
            hrUltimo_ = E_NOTIMPL; // solo slices «largos»: no se arman acá
            return false;
        }
        const int cuantas = std::min(32, std::max(4, sps.max_num_ref_frames + 3));
        D3D11_TEXTURE2D_DESC td{};
        td.Width = desc.SampleWidth;
        td.Height = desc.SampleHeight;
        td.MipLevels = 1;
        td.ArraySize = static_cast<UINT>(cuantas);
        td.Format = DXGI_FORMAT_NV12;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_DECODER;
        D3D11_TEXTURE2D_DESC sd = td;
        sd.ArraySize = 1;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.BindFlags = 0;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(hr = dev_->vdev->CreateVideoDecoder(&desc, &cfg, &decodificador_)) ||
            FAILED(hr = dev_->dev->CreateTexture2D(&td, nullptr, &textura_)) ||
            FAILED(hr = dev_->dev->CreateTexture2D(&sd, nullptr, &paso_))) {
            hrUltimo_ = hr;
            decodificador_.Reset();
            textura_.Reset();
            paso_.Reset();
            return false;
        }
        vistas_.resize(static_cast<size_t>(cuantas));
        for (int i = 0; i < cuantas; ++i) {
            D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC vd{};
            vd.DecodeProfile = kPerfilH264;
            vd.ViewDimension = D3D11_VDOV_DIMENSION_TEXTURE2D;
            vd.Texture2D.ArraySlice = static_cast<UINT>(i);
            hr = dev_->vdev->CreateVideoDecoderOutputView(textura_.Get(), &vd, &vistas_[static_cast<size_t>(i)]);
            if (FAILED(hr)) {
                hrUltimo_ = hr;
                vistas_.clear();
                decodificador_.Reset();
                textura_.Reset();
                paso_.Reset();
                return false;
            }
        }
        if (dev_->dev5 && SUCCEEDED(dev_->dev5->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(&cerca_)))) {
            evento_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (evento_ == nullptr) cerca_.Reset();
        }
        superficies_ = cuantas;
        anchoCod_ = sps.anchoCodificado();
        altoCod_ = sps.altoCodificado();
        ++gDecD3d;
        siguiente_ = 0;
        conReferencia_ = false;
        return true;
    }

    void llenarParametros(const h264::Sps& sps, const h264::Pps& pps, const h264::Slice& s, bool intra, int sup) {
        DXVA_PicParams_H264& p = pp_;
        std::memset(&p, 0, sizeof(p));
        p.wFrameWidthInMbsMinus1 = static_cast<USHORT>(sps.ancho_mbs - 1);
        p.wFrameHeightInMbsMinus1 = static_cast<USHORT>(sps.altoMbs() - 1);
        p.CurrPic.Index7Bits = static_cast<UCHAR>(sup);
        p.CurrPic.AssociatedFlag = 0;
        p.num_ref_frames = static_cast<UCHAR>(sps.max_num_ref_frames);
        // field_pic_flag 0, MbaffFrameFlag, residual_colour_transform_flag, sp_for_switch_flag 0,
        // chroma_format_idc, RefPicFlag, constrained_intra_pred, weighted_pred, weighted_bipred_idc,
        // MbsConsecutiveFlag 1, frame_mbs_only, transform_8x8_mode, MinLumaBipredSize8x8, IntraPicFlag.
        p.wBitFields = static_cast<USHORT>(
            ((sps.mb_adaptive_frame_field ? 1 : 0) << 1) | ((sps.separate_colour_plane ? 1 : 0) << 2) |
            (sps.chroma_format_idc << 4) | ((s.nal_ref_idc != 0 ? 1 : 0) << 6) | ((pps.constrained_intra_pred ? 1 : 0) << 7) |
            ((pps.weighted_pred ? 1 : 0) << 8) | (pps.weighted_bipred_idc << 9) | (1 << 11) |
            ((sps.frame_mbs_only ? 1 : 0) << 12) | ((pps.transform_8x8_mode ? 1 : 0) << 13) |
            ((sps.level_idc >= 31 ? 1 : 0) << 14) | ((intra ? 1 : 0) << 15));
        p.bit_depth_luma_minus8 = static_cast<UCHAR>(sps.bit_depth_luma_minus8);
        p.bit_depth_chroma_minus8 = static_cast<UCHAR>(sps.bit_depth_chroma_minus8);
        p.Reserved16Bits = 3;
        p.StatusReportFeedbackNumber = ++informe_;
        for (auto& r : p.RefFrameList)
            r.bPicEntry = 0xFF;
        int i = 0;
        for (const h264::Ref& r : dpb_.refs()) {
            if (i >= 16 || s.idr) break; // una IDR no usa referencias (las viejas se sueltan al terminarla)
            p.RefFrameList[i].Index7Bits = static_cast<UCHAR>(r.superficie);
            p.RefFrameList[i].AssociatedFlag = r.largo ? 1 : 0;
            p.FieldOrderCntList[i][0] = r.poc_arriba;
            p.FieldOrderCntList[i][1] = r.poc_abajo;
            p.FrameNumList[i] = static_cast<USHORT>(r.largo ? r.idx_largo : r.frame_num);
            p.UsedForReferenceFlags |= 3u << (2 * i);
            ++i;
        }
        p.CurrFieldOrderCnt[0] = dpb_.pocArriba();
        p.CurrFieldOrderCnt[1] = dpb_.pocAbajo();
        p.pic_init_qs_minus26 = static_cast<CHAR>(pps.pic_init_qs_minus26);
        p.chroma_qp_index_offset = static_cast<CHAR>(pps.chroma_qp_index_offset);
        p.second_chroma_qp_index_offset = static_cast<CHAR>(pps.second_chroma_qp_index_offset);
        p.ContinuationFlag = 1;
        p.pic_init_qp_minus26 = static_cast<CHAR>(pps.pic_init_qp_minus26);
        p.num_ref_idx_l0_active_minus1 = static_cast<UCHAR>(pps.num_ref_idx_l0_default_minus1);
        p.num_ref_idx_l1_active_minus1 = static_cast<UCHAR>(pps.num_ref_idx_l1_default_minus1);
        p.frame_num = static_cast<USHORT>(s.frame_num);
        p.log2_max_frame_num_minus4 = static_cast<UCHAR>(sps.log2_max_frame_num_minus4);
        p.pic_order_cnt_type = static_cast<UCHAR>(sps.poc_type);
        p.log2_max_pic_order_cnt_lsb_minus4 = static_cast<UCHAR>(sps.log2_max_poc_lsb_minus4);
        p.delta_pic_order_always_zero_flag = sps.delta_pic_order_always_zero ? 1 : 0;
        p.direct_8x8_inference_flag = sps.direct_8x8_inference ? 1 : 0;
        p.entropy_coding_mode_flag = pps.entropy_coding_mode ? 1 : 0;
        p.pic_order_present_flag = pps.bottom_field_pic_order_in_frame_present ? 1 : 0;
        p.deblocking_filter_control_present_flag = pps.deblocking_filter_control_present ? 1 : 0;
        p.redundant_pic_cnt_present_flag = pps.redundant_pic_cnt_present ? 1 : 0;
        for (int k = 0; k < 6; ++k)
            std::memcpy(qm_.bScalingLists4x4[k], pps.escalado.l4[static_cast<size_t>(k)].data(), 16);
        std::memcpy(qm_.bScalingLists8x8[0], pps.escalado.l8[0].data(), 64);
        std::memcpy(qm_.bScalingLists8x8[1], pps.escalado.l8[1].data(), 64);
    }

    /// Un búfer de la placa: lo pide, copia `n` bytes y lo devuelve.
    bool copiarBufer(D3D11_VIDEO_DECODER_BUFFER_TYPE tipo, const void* datos, size_t n) {
        UINT tam = 0;
        void* p = nullptr;
        const HRESULT hr = dev_->vctx->GetDecoderBuffer(decodificador_.Get(), tipo, &tam, &p);
        if (FAILED(hr) || p == nullptr || tam < n) {
            if (SUCCEEDED(hr)) dev_->vctx->ReleaseDecoderBuffer(decodificador_.Get(), tipo);
            hrUltimo_ = FAILED(hr) ? hr : E_OUTOFMEMORY;
            return false;
        }
        std::memcpy(p, datos, n);
        dev_->vctx->ReleaseDecoderBuffer(decodificador_.Get(), tipo);
        return true;
    }

    /// Manda el cuadro a la placa y, detrás, la copia a la textura de paso y la cerca.
    bool enviar(const uint8_t* d, int sup) {
        // Los slices con su código de arranque (00 00 01), relleno a 128 con ceros.
        flujo_.clear();
        cortos_.clear();
        for (const h264::Nal& n : slices_) {
            DXVA_Slice_H264_Short c{};
            c.BSNALunitDataLocation = static_cast<UINT>(flujo_.size());
            c.SliceBytesInBuffer = static_cast<UINT>(n.largo + 3);
            c.wBadSliceChopping = 0;
            flujo_.push_back(0);
            flujo_.push_back(0);
            flujo_.push_back(1);
            flujo_.insert(flujo_.end(), d + n.inicio, d + n.inicio + n.largo);
            cortos_.push_back(c);
        }
        const size_t relleno = (128 - (flujo_.size() & 127)) & 127;
        flujo_.insert(flujo_.end(), relleno, 0);
        cortos_.back().SliceBytesInBuffer += static_cast<UINT>(relleno);
        const UINT mbs = static_cast<UINT>((pp_.wFrameWidthInMbsMinus1 + 1) * (pp_.wFrameHeightInMbsMinus1 + 1));

        std::unique_lock<std::mutex> l(dev_->m);
        HRESULT hr = E_PENDING;
        for (int i = 0; i < 200; ++i) {
            hr = dev_->vctx->DecoderBeginFrame(decodificador_.Get(), vistas_[static_cast<size_t>(sup)].Get(), 0, nullptr);
            if (hr != E_PENDING && hr != DXGI_ERROR_WAS_STILL_DRAWING) break;
            l.unlock();
            Sleep(1);
            l.lock();
        }
        if (FAILED(hr)) {
            hrUltimo_ = hr;
            return false;
        }
        bool ok = copiarBufer(D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS, &pp_, sizeof(pp_)) &&
                  copiarBufer(D3D11_VIDEO_DECODER_BUFFER_INVERSE_QUANTIZATION_MATRIX, &qm_, sizeof(qm_)) &&
                  copiarBufer(D3D11_VIDEO_DECODER_BUFFER_BITSTREAM, flujo_.data(), flujo_.size()) &&
                  copiarBufer(D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL, cortos_.data(),
                              cortos_.size() * sizeof(DXVA_Slice_H264_Short));
        if (ok) {
            D3D11_VIDEO_DECODER_BUFFER_DESC b[4]{};
            b[0].BufferType = D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS;
            b[0].DataSize = sizeof(pp_);
            b[1].BufferType = D3D11_VIDEO_DECODER_BUFFER_INVERSE_QUANTIZATION_MATRIX;
            b[1].DataSize = sizeof(qm_);
            b[2].BufferType = D3D11_VIDEO_DECODER_BUFFER_BITSTREAM;
            b[2].DataSize = static_cast<UINT>(flujo_.size());
            b[2].NumMBsInBuffer = mbs;
            b[3].BufferType = D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL;
            b[3].DataSize = static_cast<UINT>(cortos_.size() * sizeof(DXVA_Slice_H264_Short));
            b[3].NumMBsInBuffer = mbs;
            hr = dev_->vctx->SubmitDecoderBuffers(decodificador_.Get(), 4, b);
            if (FAILED(hr)) {
                hrUltimo_ = hr;
                ok = false;
            }
        }
        hr = dev_->vctx->DecoderEndFrame(decodificador_.Get());
        if (FAILED(hr) && ok) {
            hrUltimo_ = hr;
            ok = false;
        }
        if (!ok) return false;
        dev_->ctx->Flush();
        return true;
    }

    // --- F3: el cuadro se queda en la placa hasta que alguien pida algo ---------------------
    //
    // Antes cada cuadro se copiaba a la textura de paso, se bajaba entero (NV12, 1,4 MB un
    // 720p) y se pasaba a I420 aunque después nadie lo usara entero: la grilla lo achicaba a
    // un cuadrito y a ba-app solo van los que están en una ruta. Ahora sale un BufferPlaca
    // (kNative) que baja el cuadro recién cuando alguien pide el I420 (ToI420), y el
    // decodificador se anuncia como CuadroEnPlaca (CodecsVideo.hpp) mientras llama a
    // Decoded(): el sink de la cámara (codecs::entregarCamara) le pide la celda de la grilla
    // achicada en la placa (ID3D11VideoProcessor: NV12 → BGRA del tamaño de la celda, de una)
    // y baja solo eso, o el BGRA entero sin pasar por I420. La superficie es válida hasta el
    // próximo Decode de este decodificador (en este mismo hilo); si alguien se quedó con el
    // buffer después de Decoded(), se baja en ese momento (materializar).

    /// El buffer del cuadro que se está entregando. Vive en la superficie `sup_` del arreglo.
    class BufferPlaca : public webrtc::VideoFrameBuffer {
       public:
        BufferPlaca(DecodificadorD3d11* d, int w, int h) : dec_(d), w_(w), h_(h) {}
        Type type() const override { return Type::kNative; }
        int width() const override { return w_; }
        int height() const override { return h_; }
        rtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override {
            std::lock_guard<std::mutex> l(m_);
            if (!i420_ && dec_ != nullptr) i420_ = dec_->bajarI420();
            return i420_;
        }
        /// El decodificador va a reusar la superficie: si alguien se quedó con este buffer,
        /// se baja ahora; después ya no se toca la placa.
        void desligar(bool materializar) {
            std::lock_guard<std::mutex> l(m_);
            if (materializar && !i420_ && dec_ != nullptr) i420_ = dec_->bajarI420();
            dec_ = nullptr;
        }

       private:
        std::mutex m_;
        DecodificadorD3d11* dec_;
        int w_, h_;
        rtc::scoped_refptr<webrtc::I420BufferInterface> i420_;
    };

    /// El cuadro en la placa que ve entregarCamara mientras dura Decoded().
    class EnPlaca : public codecs::CuadroEnPlaca {
       public:
        EnPlaca(DecodificadorD3d11& d, const webrtc::VideoFrameBuffer* b) : d_(d), b_(b) {}
        const webrtc::VideoFrameBuffer* buffer() const override { return b_; }
        bool celdaBgra(int tw, int th, uint8_t* dst, int paso) override { return d_.celdaBgra(tw, th, dst, paso); }
        bool enteroBgra(uint8_t* dst, int paso) override { return d_.enteroBgra(dst, paso); }
        bool enteroYCelda(uint8_t* dst, int paso, int tw, int th, uint8_t* celda, int pasoCelda, bool& celdaHecha) override {
            return d_.enteroYCelda(dst, paso, tw, th, celda, pasoCelda, celdaHecha);
        }

       private:
        DecodificadorD3d11& d_;
        const webrtc::VideoFrameBuffer* b_;
    };

    /// Pone la cerca detrás de lo encolado, larga el candado y espera (durmiendo). Sin cerca,
    /// Map espera solo. false si la placa no terminó en 1 s.
    bool esperarPlaca(std::unique_lock<std::mutex>& l) {
        bool espera = false;
        if (cerca_) {
            ++valorCerca_;
            espera = SUCCEEDED(dev_->ctx4->Signal(cerca_.Get(), valorCerca_)) &&
                     SUCCEEDED(cerca_->SetEventOnCompletion(valorCerca_, evento_));
        }
        dev_->ctx->Flush();
        l.unlock();
        if (espera && WaitForSingleObject(evento_, 1000) != WAIT_OBJECT_0) {
            hrUltimo_ = HRESULT_FROM_WIN32(WAIT_TIMEOUT);
            l.lock();
            return false;
        }
        l.lock();
        return true;
    }

    /// Copia la superficie del cuadro a la textura de paso, espera y la deja mapeada (con el
    /// candado del dispositivo tomado en `l`). false si no se pudo.
    bool mapearPaso(std::unique_lock<std::mutex>& l, D3D11_MAPPED_SUBRESOURCE& m) {
        dev_->ctx->CopySubresourceRegion(paso_.Get(), 0, 0, 0, 0, textura_.Get(),
                                         D3D11CalcSubresource(0, static_cast<UINT>(sup_), 1), nullptr);
        if (!esperarPlaca(l)) return false;
        const HRESULT hr = dev_->ctx->Map(paso_.Get(), 0, D3D11_MAP_READ, 0, &m);
        if (FAILED(hr)) {
            hrUltimo_ = hr;
            return false;
        }
        return true;
    }

    /// El cuadro entero bajado a I420 (lo de siempre; para quien pida ToI420).
    rtc::scoped_refptr<webrtc::I420BufferInterface> bajarI420() {
        std::unique_lock<std::mutex> l(dev_->m);
        D3D11_MAPPED_SUBRESOURCE m{};
        if (!mapearPaso(l, m)) return nullptr;
        rtc::scoped_refptr<webrtc::I420Buffer> i420 = pozo_.CreateI420Buffer(w_, h_);
        if (!i420) i420 = webrtc::I420Buffer::Create(w_, h_);
        const auto* base = static_cast<const uint8_t*>(m.pData);
        const ptrdiff_t paso = static_cast<ptrdiff_t>(m.RowPitch);
        const uint8_t* y = base + static_cast<ptrdiff_t>(cropArr_) * paso + cropIzq_;
        const uint8_t* uv = base + static_cast<ptrdiff_t>(altoCod_) * paso + static_cast<ptrdiff_t>(cropArr_ / 2) * paso + (cropIzq_ & ~1);
        l.unlock(); // la copia, sin el candado del dispositivo (ver convertirPaso)
        libyuv::NV12ToI420(y, static_cast<int>(paso), uv, static_cast<int>(paso), i420->MutableDataY(), i420->StrideY(),
                           i420->MutableDataU(), i420->StrideU(), i420->MutableDataV(), i420->StrideV(), w_, h_);
        l.lock();
        dev_->ctx->Unmap(paso_.Get(), 0);
        return i420;
    }

    /// El cuadro entero en BGRA, del NV12 bajado directo (sin el I420 del medio).
    bool enteroBgra(uint8_t* dst, int pasoDst) {
        std::unique_lock<std::mutex> l(dev_->m);
        D3D11_MAPPED_SUBRESOURCE m{};
        if (!mapearPaso(l, m)) return false;
        convertirPaso(l, m, dst, pasoDst);
        return true;
    }

    /// El NV12 mapeado de la textura de paso → BGRA (con el recorte del SPS), y la suelta.
    /// La conversión (trabajo del procesador: ~1-2 ms un 720p) va SIN el candado del
    /// dispositivo: el candado es uno para todos los decodificadores (el contexto inmediato
    /// es uno) y con la conversión adentro los 6 hilos «dec» se hacían fila detrás de él.
    /// Medido en la .177 con 200 invitados (2026-11-06): 15-19 ms por cuadro en
    /// video.decodificado, cinco de los seis hilos esperando el candado, las colas de
    /// decodificar atrasadas segundos y, detrás de ellas, el hilo de trabajo de libwebrtc
    /// (VideoReceiveStream2::SetAndGetRecordingState espera a la cola de decodificar) y el de
    /// señalización: las ofertas y los «conectados» de los que entraban, trabados minutos.
    /// La textura de paso es de este decodificador (nadie más la toca) y el contexto queda
    /// libre para los otros mientras está mapeada; Unmap vuelve a tomar el candado.
    void convertirPaso(std::unique_lock<std::mutex>& l, const D3D11_MAPPED_SUBRESOURCE& m, uint8_t* dst, int pasoDst) {
        const auto* base = static_cast<const uint8_t*>(m.pData);
        const ptrdiff_t paso = static_cast<ptrdiff_t>(m.RowPitch);
        const uint8_t* y = base + static_cast<ptrdiff_t>(cropArr_) * paso + cropIzq_;
        const uint8_t* uv = base + static_cast<ptrdiff_t>(altoCod_) * paso + static_cast<ptrdiff_t>(cropArr_ / 2) * paso + (cropIzq_ & ~1);
        l.unlock();
        libyuv::NV12ToARGB(y, static_cast<int>(paso), uv, static_cast<int>(paso), dst, pasoDst, w_, h_);
        l.lock();
        dev_->ctx->Unmap(paso_.Get(), 0);
    }

    /// El entero (para ba-app) y la celda de la grilla en UNA vuelta de la placa: se encola
    /// la copia del NV12 a la de paso y, detrás, el achique de la celda (procesador de video);
    /// una sola espera, y se bajan los dos. Antes: el entero solo y la celda achicada del BGRA
    /// en el procesador (con 40 invitados, todas las celdas iban por ahí). `celdaHecha`: si
    /// salió la celda (si no, quien llama la achica del entero). false si el entero no salió.
    bool enteroYCelda(uint8_t* dst, int pasoDst, int tw, int th, uint8_t* celda, int pasoCelda, bool& celdaHecha) {
        celdaHecha = false;
        std::unique_lock<std::mutex> l(dev_->m);
        dev_->ctx->CopySubresourceRegion(paso_.Get(), 0, 0, 0, 0, textura_.Get(),
                                         D3D11CalcSubresource(0, static_cast<UINT>(sup_), 1), nullptr);
        const bool conCelda = encolarCelda(tw, th);
        if (!esperarPlaca(l)) return false;
        D3D11_MAPPED_SUBRESOURCE m{};
        const HRESULT hr = dev_->ctx->Map(paso_.Get(), 0, D3D11_MAP_READ, 0, &m);
        if (FAILED(hr)) {
            hrUltimo_ = hr;
            return false;
        }
        convertirPaso(l, m, dst, pasoDst);
        if (conCelda && leerCelda(tw, th, celda, pasoCelda)) {
            celdaHecha = true;
            ++gCeldasConEntero;
        }
        return true;
    }

    /// El procesador de video de la placa para achicar a `tw`x`th` (se rehace si cambia el
    /// tamaño de la celda o del flujo). Con el candado del dispositivo tomado.
    bool prepararProcesador(int tw, int th) {
        if (vpRoto_) return false;
        if (vp_ && vpW_ == tw && vpH_ == th && vpCodW_ == anchoCod_ && vpCodH_ == altoCod_) return true;
        soltarProcesador();
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC cd{};
        cd.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        cd.InputWidth = static_cast<UINT>(anchoCod_);
        cd.InputHeight = static_cast<UINT>(altoCod_);
        cd.OutputWidth = static_cast<UINT>(tw);
        cd.OutputHeight = static_cast<UINT>(th);
        cd.Usage = D3D11_VIDEO_USAGE_OPTIMAL_SPEED;
        HRESULT hr = dev_->vdev->CreateVideoProcessorEnumerator(&cd, &vpEnum_);
        UINT fmt = 0;
        if (SUCCEEDED(hr)) hr = vpEnum_->CheckVideoProcessorFormat(DXGI_FORMAT_NV12, &fmt);
        if (SUCCEEDED(hr) && !(fmt & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT)) hr = E_NOTIMPL;
        if (SUCCEEDED(hr)) hr = vpEnum_->CheckVideoProcessorFormat(DXGI_FORMAT_B8G8R8A8_UNORM, &fmt);
        if (SUCCEEDED(hr) && !(fmt & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT)) hr = E_NOTIMPL;
        if (SUCCEEDED(hr)) hr = dev_->vdev->CreateVideoProcessor(vpEnum_.Get(), 0, &vp_);
        D3D11_TEXTURE2D_DESC td{};
        td.Width = static_cast<UINT>(tw);
        td.Height = static_cast<UINT>(th);
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        D3D11_TEXTURE2D_DESC sd = td;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.BindFlags = 0;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (SUCCEEDED(hr)) hr = dev_->dev->CreateTexture2D(&td, nullptr, &vpSalida_);
        if (SUCCEEDED(hr)) hr = dev_->dev->CreateTexture2D(&sd, nullptr, &vpPaso_);
        if (SUCCEEDED(hr)) {
            D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC od{};
            od.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
            hr = dev_->vdev->CreateVideoProcessorOutputView(vpSalida_.Get(), vpEnum_.Get(), &od, &vpVistaSalida_);
        }
        if (FAILED(hr)) {
            hrUltimo_ = hr;
            soltarProcesador();
            // Sin procesador de video que sirva (o sin NV12 → BGRA): se achica en el procesador,
            // avisado una vez. No se reintenta en cada cuadro.
            vpRoto_ = true;
            if (!gAvisoVp.exchange(true))
                aviso("video: la placa no achica la grilla (" + hex(hr) + "): se achica en el procesador");
            return false;
        }
        // Color: lo mismo que libyuv (BT.601 de rango limitado → RGB completo), así la celda
        // achicada en la placa y la del procesador se ven iguales.
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE ent{};
        ent.YCbCr_Matrix = 0; // BT.601
        ent.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
        dev_->vctx->VideoProcessorSetStreamColorSpace(vp_.Get(), 0, &ent);
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE sal{};
        sal.RGB_Range = 0; // 0-255
        dev_->vctx->VideoProcessorSetOutputColorSpace(vp_.Get(), &sal);
        dev_->vctx->VideoProcessorSetStreamFrameFormat(vp_.Get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
        dev_->vctx->VideoProcessorSetStreamAutoProcessingMode(vp_.Get(), 0, FALSE);
        vpEntradas_.assign(static_cast<size_t>(superficies_), nullptr);
        vpW_ = tw;
        vpH_ = th;
        vpCodW_ = anchoCod_;
        vpCodH_ = altoCod_;
        return true;
    }

    void soltarProcesador() {
        vpEntradas_.clear();
        vpEntradaIntermedia_.Reset();
        vpIntermedia_.Reset();
        vpVistaSalida_.Reset();
        vpPaso_.Reset();
        vpSalida_.Reset();
        vp_.Reset();
        vpEnum_.Reset();
        vpW_ = vpH_ = vpCodW_ = vpCodH_ = 0;
    }

    /// La celda de la grilla achicada en la placa (NV12 → BGRA en un paso) y bajada sola.
    bool celdaBgra(int tw, int th, uint8_t* dst, int pasoDst) {
        std::unique_lock<std::mutex> l(dev_->m);
        if (!encolarCelda(tw, th)) return false;
        if (!esperarPlaca(l)) return false;
        return leerCelda(tw, th, dst, pasoDst);
    }

    /// La placa no achica (no se reintenta en cada cuadro): avisado una vez.
    void romperProcesador(HRESULT hr, const char* que) {
        hrUltimo_ = hr;
        soltarProcesador();
        vpRoto_ = true;
        if (!gAvisoVp.exchange(true)) aviso(std::string("video: ") + que + " (" + hex(hr) + "): se achica en el procesador");
    }

    /// El procesador de video no lee directo del arreglo del decodificador: desde ahora (y
    /// para todos los decodificadores) se copia a la textura intermedia. Avisado una vez.
    void pasarAIntermedia(HRESULT hr) {
        hrUltimo_ = hr;
        vpEntradas_.assign(static_cast<size_t>(superficies_), nullptr);
        if (!vpIntermedia().exchange(true))
            aviso("video: la placa no achica directo de las superficies del decodificador (" + hex(hr) +
                  "): se copia cada cuadro a una textura intermedia");
    }

    /// La entrada del procesador de video sobre la superficie del cuadro (una vista por
    /// superficie, hecha al usarla). nullptr si la placa no la acepta.
    ID3D11VideoProcessorInputView* entradaDirecta() {
        ComPtr<ID3D11VideoProcessorInputView>& ent = vpEntradas_[static_cast<size_t>(sup_)];
        if (!ent) {
            D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC iv{};
            iv.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
            iv.Texture2D.ArraySlice = static_cast<UINT>(sup_);
            const HRESULT hr = dev_->vdev->CreateVideoProcessorInputView(textura_.Get(), vpEnum_.Get(), &iv, &ent);
            if (FAILED(hr)) {
                hrUltimo_ = hr;
                ent.Reset();
                return nullptr;
            }
        }
        return ent.Get();
    }

    /// La alternativa: la superficie copiada (en la placa) a una textura NV12 propia, del
    /// tamaño codificado, y la entrada del procesador sobre esa. Se prueba con los permisos
    /// de más a menos (render target + shader, shader, ninguno): cuál acepta el procesador de
    /// video como entrada depende del driver. nullptr si ninguno sirve.
    ID3D11VideoProcessorInputView* entradaIntermedia() {
        if (!vpEntradaIntermedia_) {
            HRESULT hr = E_FAIL;
            for (const UINT bind : {UINT(D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE), UINT(D3D11_BIND_SHADER_RESOURCE), UINT(0)}) {
                D3D11_TEXTURE2D_DESC td{};
                td.Width = static_cast<UINT>(anchoCod_);
                td.Height = static_cast<UINT>(altoCod_);
                td.MipLevels = 1;
                td.ArraySize = 1;
                td.Format = DXGI_FORMAT_NV12;
                td.SampleDesc.Count = 1;
                td.Usage = D3D11_USAGE_DEFAULT;
                td.BindFlags = bind;
                vpIntermedia_.Reset();
                if (FAILED(hr = dev_->dev->CreateTexture2D(&td, nullptr, &vpIntermedia_))) continue;
                D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC iv{};
                iv.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
                iv.Texture2D.ArraySlice = 0;
                if (SUCCEEDED(hr = dev_->vdev->CreateVideoProcessorInputView(vpIntermedia_.Get(), vpEnum_.Get(), &iv,
                                                                              &vpEntradaIntermedia_)))
                    break;
                vpEntradaIntermedia_.Reset();
            }
            if (!vpEntradaIntermedia_) {
                vpIntermedia_.Reset();
                hrUltimo_ = hr;
                return nullptr;
            }
        }
        dev_->ctx->CopySubresourceRegion(vpIntermedia_.Get(), 0, 0, 0, 0, textura_.Get(),
                                         D3D11CalcSubresource(0, static_cast<UINT>(sup_), 1), nullptr);
        return vpEntradaIntermedia_.Get();
    }

    /// Encola (con el candado del dispositivo) el achique de la celda a `tw`x`th` y su copia a
    /// la de lectura; no espera. Primero desde la superficie del decodificador; si la placa no
    /// la acepta como entrada del procesador de video (la vista o el Blt fallan), desde la
    /// textura intermedia. false si no se pudo (se achica en el procesador).
    bool encolarCelda(int tw, int th) {
        if (tw <= 0 || th <= 0 || sup_ < 0) return false;
        if (!prepararProcesador(tw, th)) return false;
        for (int intento = 0; intento < 2; ++intento) {
            const bool intermedia = vpIntermedia().load(std::memory_order_relaxed);
            ID3D11VideoProcessorInputView* ent = intermedia ? entradaIntermedia() : entradaDirecta();
            if (ent == nullptr) {
                if (intermedia) {
                    romperProcesador(hrUltimo_, "la placa no deja achicar ni desde una textura intermedia");
                    return false;
                }
                pasarAIntermedia(hrUltimo_);
                continue;
            }
            RECT origen{cropIzq_, cropArr_, cropIzq_ + w_, cropArr_ + h_};
            RECT destino{0, 0, tw, th};
            dev_->vctx->VideoProcessorSetStreamSourceRect(vp_.Get(), 0, TRUE, &origen);
            dev_->vctx->VideoProcessorSetStreamDestRect(vp_.Get(), 0, TRUE, &destino);
            dev_->vctx->VideoProcessorSetOutputTargetRect(vp_.Get(), TRUE, &destino);
            D3D11_VIDEO_PROCESSOR_STREAM st{};
            st.Enable = TRUE;
            st.pInputSurface = ent;
            const HRESULT hr = dev_->vctx->VideoProcessorBlt(vp_.Get(), vpVistaSalida_.Get(), 0, 1, &st);
            if (SUCCEEDED(hr)) {
                dev_->ctx->CopyResource(vpPaso_.Get(), vpSalida_.Get());
                return true;
            }
            if (intermedia) {
                hrUltimo_ = hr; // pasajero (o no: lo dice hrUltimo_); el próximo cuadro prueba de nuevo
                return false;
            }
            pasarAIntermedia(hr);
        }
        return false;
    }

    /// Baja la celda ya achicada (después de esperarPlaca, con el candado).
    bool leerCelda(int tw, int th, uint8_t* dst, int pasoDst) {
        D3D11_MAPPED_SUBRESOURCE m{};
        const HRESULT hr = dev_->ctx->Map(vpPaso_.Get(), 0, D3D11_MAP_READ, 0, &m);
        if (FAILED(hr)) {
            hrUltimo_ = hr;
            return false;
        }
        const auto* src = static_cast<const uint8_t*>(m.pData);
        for (int y = 0; y < th; ++y)
            std::memcpy(dst + static_cast<ptrdiff_t>(y) * pasoDst, src + static_cast<ptrdiff_t>(y) * m.RowPitch,
                        static_cast<size_t>(tw) * 4);
        dev_->ctx->Unmap(vpPaso_.Get(), 0);
        ++gCeldasPlaca;
        return true;
    }

    /// Entrega el cuadro de la superficie `sup` sin bajarlo: lo baja quien lo necesite.
    bool entregar(const webrtc::EncodedImage& img, const h264::Sps& sps, int sup) {
        sup_ = sup;
        w_ = sps.ancho();
        h_ = sps.alto();
        cropIzq_ = sps.crop_izq;
        cropArr_ = sps.crop_arr;
        // RefCountedObject a mano (no make_ref_counted): hace falta HasOneRef() al final.
        const rtc::scoped_refptr<rtc::RefCountedObject<BufferPlaca>> buf(new rtc::RefCountedObject<BufferPlaca>(this, w_, h_));
        {
            EnPlaca enPlaca(*this, buf.get());
            codecs::AnuncioPlaca anuncio(&enPlaca);
            webrtc::VideoFrame f = webrtc::VideoFrame::Builder()
                                       .set_video_frame_buffer(buf)
                                       .set_timestamp_rtp(img.RtpTimestamp())
                                       .set_color_space(img.ColorSpace())
                                       .build();
            cb_->Decoded(f, absl::nullopt, absl::nullopt);
        }
        // Si alguien se quedó con el cuadro (una cola, otro sink), se baja ahora: la superficie
        // se reusa en el próximo Decode.
        buf->desligar(!buf->HasOneRef());
        sup_ = -1;
        return true;
    }

    Settings ajustes_;
    webrtc::DecodedImageCallback* cb_ = nullptr;
    std::unique_ptr<DecodificadorMf> mft_; ///< la caída
    std::shared_ptr<DispositivoVideo> dev_;
    ComPtr<ID3D11VideoDecoder> decodificador_;
    ComPtr<ID3D11Texture2D> textura_; ///< las superficies (un arreglo)
    std::vector<ComPtr<ID3D11VideoDecoderOutputView>> vistas_;
    ComPtr<ID3D11Texture2D> paso_; ///< la de lectura (staging): una, se lee antes del cuadro siguiente
    ComPtr<ID3D11Fence> cerca_;
    HANDLE evento_ = nullptr;
    UINT64 valorCerca_ = 0;
    // F3: el cuadro que se está entregando (BufferPlaca, EnPlaca) y el procesador de video que achica la celda.
    int sup_ = -1, w_ = 0, h_ = 0, cropIzq_ = 0, cropArr_ = 0;
    ComPtr<ID3D11VideoProcessorEnumerator> vpEnum_;
    ComPtr<ID3D11VideoProcessor> vp_;
    ComPtr<ID3D11Texture2D> vpSalida_; ///< BGRA del tamaño de la celda (render target)
    ComPtr<ID3D11Texture2D> vpPaso_;   ///< la de lectura de la celda (staging)
    ComPtr<ID3D11VideoProcessorOutputView> vpVistaSalida_;
    std::vector<ComPtr<ID3D11VideoProcessorInputView>> vpEntradas_; ///< una por superficie, al usarla
    ComPtr<ID3D11Texture2D> vpIntermedia_;                         ///< la alternativa (vpIntermedia()): NV12 propia
    ComPtr<ID3D11VideoProcessorInputView> vpEntradaIntermedia_;
    int vpW_ = 0, vpH_ = 0, vpCodW_ = 0, vpCodH_ = 0;
    bool vpRoto_ = false; ///< la placa no achica: no se reintenta
    int superficies_ = 0, siguiente_ = 0, anchoCod_ = 0, altoCod_ = 0, errores_ = 0;
    HRESULT hrUltimo_ = S_OK;
    UINT informe_ = 0;
    bool conReferencia_ = false;
    std::array<std::unique_ptr<h264::Sps>, 32> sps_{};
    std::array<const h264::Sps*, 32> spsPtr_{};
    std::array<std::unique_ptr<h264::Pps>, 256> pps_{};
    std::array<const h264::Pps*, 256> ppsPtr_{};
    h264::Dpb dpb_;
    std::vector<h264::Nal> slices_;
    std::vector<uint8_t> flujo_;
    std::vector<DXVA_Slice_H264_Short> cortos_;
    DXVA_PicParams_H264 pp_{};
    DXVA_Qmatrix_H264 qm_{};
    webrtc::VideoFrameBufferPool pozo_{false, 8};
};

// ============================================================================
// Codificador
// ============================================================================

/// Lo que comparten el codificador y el hilo de sus eventos (si es asincrónico). Vive en un
/// shared_ptr: si un MFT no suelta su espera al cerrar, el hilo queda suelto con esto.
struct NucleoCod {
    ComPtr<IMFTransform> mft;
    ComPtr<ICodecAPI> api;
    ComPtr<IMFMediaEventGenerator> eventos;
    bool asinc = false, provee = false, hw = false;
    DWORD tamSalida = 0;
    int ancho = 0, alto = 0;
    std::vector<uint8_t> cabecera; ///< SPS+PPS (MF_MT_MPEG_SEQUENCE_HEADER), por si la clave no los trae

    struct Meta {
        LONGLONG t;
        uint32_t rtp;
        int64_t capturaMs;
        webrtc::VideoRotation rot;
    };
    std::mutex m;
    std::condition_variable cv;
    std::deque<Meta> metas;
    int pideEntrada = 0;
    bool parar = false, terminado = false;
    webrtc::EncodedImageCallback* cb = nullptr;
    std::mutex mCb; ///< entrega vs. cambio de callback

    /// Saca una salida (asincrónico: cuando el MFT avisa; sincrónico: hasta que pida más).
    /// Devuelve false si no había (o falló).
    bool sacar() {
        MFT_OUTPUT_DATA_BUFFER ob{};
        ComPtr<IMFSample> propia;
        if (!provee) {
            propia = muestraConBytes(nullptr, std::max<DWORD>(tamSalida, 1u << 20));
            if (!propia) return false;
            ob.pSample = propia.Get();
        }
        DWORD estado = 0;
        HRESULT hr = mft->ProcessOutput(0, 1, &ob, &estado);
        if (ob.pEvents != nullptr) ob.pEvents->Release();
        ComPtr<IMFSample> s;
        if (provee && ob.pSample != nullptr) s.Attach(ob.pSample);
        else if (!provee) s = propia;
        if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
            ComPtr<IMFMediaType> t;
            if (SUCCEEDED(mft->GetOutputAvailableType(0, 0, &t))) mft->SetOutputType(0, t.Get(), 0);
            leerCabecera();
            return true;
        }
        if (FAILED(hr) || !s) return false;
        entregar(s.Get());
        return true;
    }

    void leerCabecera() {
        ComPtr<IMFMediaType> t;
        if (FAILED(mft->GetOutputCurrentType(0, &t))) return;
        UINT32 n = 0;
        if (FAILED(t->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &n)) || n == 0) return;
        cabecera.resize(n);
        if (FAILED(t->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER, cabecera.data(), n, nullptr))) cabecera.clear();
    }

    void entregar(IMFSample* s) {
        LONGLONG t = 0;
        s->GetSampleTime(&t);
        Meta meta{t, 0, 0, webrtc::kVideoRotation_0};
        bool hay = false;
        {
            std::lock_guard<std::mutex> l(m);
            while (!metas.empty() && metas.front().t <= t) {
                meta = metas.front();
                hay = true;
                metas.pop_front();
            }
        }
        if (!hay) return;
        ComPtr<IMFMediaBuffer> buf;
        if (FAILED(s->ConvertToContiguousBuffer(&buf))) return;
        BYTE* p = nullptr;
        DWORD n = 0;
        if (FAILED(buf->Lock(&p, nullptr, &n))) return;
        UINT32 limpio = 0;
        bool clave = SUCCEEDED(s->GetUINT32(MFSampleExtension_CleanPoint, &limpio)) && limpio != 0;
        if (!clave) clave = tieneNal(p, n, 5);
        rtc::scoped_refptr<webrtc::EncodedImageBuffer> datos;
        if (clave && !tieneNal(p, n, 7)) {
            if (cabecera.empty()) leerCabecera();
            datos = webrtc::EncodedImageBuffer::Create(cabecera.size() + n);
            std::memcpy(datos->data(), cabecera.data(), cabecera.size());
            std::memcpy(datos->data() + cabecera.size(), p, n);
        } else {
            datos = webrtc::EncodedImageBuffer::Create(p, n);
        }
        buf->Unlock();
        webrtc::EncodedImage img;
        img.SetEncodedData(datos);
        img._encodedWidth = static_cast<uint32_t>(ancho);
        img._encodedHeight = static_cast<uint32_t>(alto);
        img.SetRtpTimestamp(meta.rtp);
        img.capture_time_ms_ = meta.capturaMs;
        img._frameType = clave ? webrtc::VideoFrameType::kVideoFrameKey : webrtc::VideoFrameType::kVideoFrameDelta;
        img.rotation_ = meta.rot;
        webrtc::CodecSpecificInfo info;
        info.codecType = webrtc::kVideoCodecH264;
        info.codecSpecific.H264.packetization_mode = webrtc::H264PacketizationMode::NonInterleaved;
        info.codecSpecific.H264.temporal_idx = webrtc::kNoTemporalIdx;
        info.codecSpecific.H264.base_layer_sync = false;
        info.codecSpecific.H264.idr_frame = clave;
        std::lock_guard<std::mutex> l(mCb);
        if (cb != nullptr) cb->OnEncodedImage(img, &info);
    }

    /// El hilo de eventos de un MFT asincrónico.
    void bucleEventos() {
        comEnEsteHilo();
        ba::platform::nombrarHilo("ba-mf-cod");
        for (;;) {
            ComPtr<IMFMediaEvent> ev;
            if (FAILED(eventos->GetEvent(0, &ev))) break;
            MediaEventType tipo = MEUnknown;
            ev->GetType(&tipo);
            if (tipo == METransformNeedInput) {
                std::lock_guard<std::mutex> l(m);
                ++pideEntrada;
                cv.notify_all();
            } else if (tipo == METransformHaveOutput) {
                sacar();
            } else if (tipo == METransformDrainComplete) {
                // Cerrando: el drenaje terminó (si no, lo corta el apagado: GetEvent falla).
                std::lock_guard<std::mutex> l(m);
                if (parar) break;
            }
        }
        std::lock_guard<std::mutex> l(m);
        terminado = true;
        cv.notify_all();
    }
};

class CodificadorMf : public webrtc::VideoEncoder {
   public:
    ~CodificadorMf() override { Release(); }

    int InitEncode(const webrtc::VideoCodec* c, const Settings&) override {
        if (c == nullptr || c->width == 0 || c->height == 0) return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
        comEnEsteHilo();
        Release();
        ancho_ = c->width;
        alto_ = c->height;
        fps_ = std::max(1u, c->maxFramerate);
        kbps_ = std::max(30u, c->startBitrate > 0 ? c->startBitrate : c->maxBitrate);
        if (hallazgo().codificaHw) {
            if (iniciar(true)) return WEBRTC_VIDEO_CODEC_OK;
            ++gCaidasCod;
            if (!gAvisoCod.exchange(true))
                aviso("video: el codificador H.264 por la placa (" + hallazgo().nombreCodHw + ") no arrancó con " +
                      std::to_string(ancho_) + "x" + std::to_string(alto_) + ": codifica por software");
        }
        return iniciar(false) ? WEBRTC_VIDEO_CODEC_OK : WEBRTC_VIDEO_CODEC_ERROR;
    }

    int32_t RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback* cb) override {
        cb_ = cb;
        if (n_) {
            std::lock_guard<std::mutex> l(n_->mCb);
            n_->cb = cb;
        }
        return WEBRTC_VIDEO_CODEC_OK;
    }

    int32_t Release() override {
        if (!n_) return WEBRTC_VIDEO_CODEC_OK;
        std::shared_ptr<NucleoCod> n = std::move(n_);
        (n->hw ? gCodHw : gCodSw)--;
        {
            std::lock_guard<std::mutex> l(n->mCb);
            n->cb = nullptr;
        }
        if (n->asinc) {
            {
                std::lock_guard<std::mutex> l(n->m);
                n->parar = true;
            }
            n->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
            n->mft->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
            bool listo = esperarFin(*n, 500);
            if (!listo) {
                ComPtr<IMFShutdown> apagar;
                if (SUCCEEDED(n->mft.As(&apagar))) apagar->Shutdown();
                listo = esperarFin(*n, 1000);
            }
            if (listo) {
                hilo_.join();
            } else {
                hilo_.detach(); // el hilo se queda con su núcleo (shared_ptr)
                aviso("video: el codificador H.264 por la placa no soltó su hilo al cerrar");
            }
        }
        n->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
        return WEBRTC_VIDEO_CODEC_OK;
    }

    int32_t Encode(const webrtc::VideoFrame& frame, const std::vector<webrtc::VideoFrameType>* tipos) override {
        if (!n_ || cb_ == nullptr) return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
        const bool clave = tipos != nullptr && std::any_of(tipos->begin(), tipos->end(), [](webrtc::VideoFrameType t) {
                               return t == webrtc::VideoFrameType::kVideoFrameKey;
                           });
        rtc::scoped_refptr<webrtc::I420BufferInterface> i420 = frame.video_frame_buffer()->ToI420();
        if (!i420) return WEBRTC_VIDEO_CODEC_ERROR;
        if (i420->width() != static_cast<int>(ancho_) || i420->height() != static_cast<int>(alto_)) {
            // Otro tamaño: se rehace (el retorno compartido ya lo rehace al cambiar de capa).
            webrtc::VideoCodec c;
            c.codecType = webrtc::kVideoCodecH264;
            c.width = static_cast<uint16_t>(i420->width());
            c.height = static_cast<uint16_t>(i420->height());
            c.maxFramerate = fps_;
            c.startBitrate = kbps_;
            webrtc::EncodedImageCallback* cb = cb_;
            if (InitEncode(&c, Settings(Capabilities(false), 1, 0)) != WEBRTC_VIDEO_CODEC_OK) return WEBRTC_VIDEO_CODEC_ERROR;
            RegisterEncodeCompleteCallback(cb);
        }
        const int w = static_cast<int>(ancho_), h = static_cast<int>(alto_);
        const size_t tam = static_cast<size_t>(w) * h * 3 / 2;
        ComPtr<IMFSample> s = muestraConBytes(nullptr, tam);
        if (!s) return WEBRTC_VIDEO_CODEC_MEMORY;
        {
            ComPtr<IMFMediaBuffer> buf;
            s->GetBufferByIndex(0, &buf);
            BYTE* p = nullptr;
            if (!buf || FAILED(buf->Lock(&p, nullptr, nullptr))) return WEBRTC_VIDEO_CODEC_MEMORY;
            libyuv::I420ToNV12(i420->DataY(), i420->StrideY(), i420->DataU(), i420->StrideU(), i420->DataV(),
                               i420->StrideV(), p, w, p + static_cast<size_t>(w) * h, w, w, h);
            buf->Unlock();
        }
        tiempo_ += 10'000'000 / fps_;
        s->SetSampleTime(tiempo_);
        s->SetSampleDuration(10'000'000 / fps_);
        if (clave) {
            ponerU32(n_->api.Get(), CODECAPI_AVEncVideoForceKeyFrame, 1);
            s->SetUINT32(MFSampleExtension_CleanPoint, TRUE);
        }
        {
            std::lock_guard<std::mutex> l(n_->m);
            n_->metas.push_back({tiempo_, frame.timestamp(), frame.render_time_ms(), frame.rotation()});
            while (n_->metas.size() > 30)
                n_->metas.pop_front();
        }
        if (n_->asinc) {
            std::unique_lock<std::mutex> l(n_->m);
            if (!n_->cv.wait_for(l, std::chrono::milliseconds(200), [&] { return n_->pideEntrada > 0 || n_->terminado; }) ||
                n_->terminado) {
                n_->metas.pop_back();
                return WEBRTC_VIDEO_CODEC_ERROR; // el MFT no pidió entrada: se saltea este cuadro
            }
            --n_->pideEntrada;
        }
        const HRESULT hr = n_->mft->ProcessInput(0, s.Get(), 0);
        if (FAILED(hr)) {
            std::lock_guard<std::mutex> l(n_->m);
            if (!n_->metas.empty()) n_->metas.pop_back();
            return WEBRTC_VIDEO_CODEC_ERROR;
        }
        if (!n_->asinc)
            while (n_->sacar()) {
            }
        return WEBRTC_VIDEO_CODEC_OK;
    }

    void SetRates(const RateControlParameters& p) override {
        const uint32_t kbps = std::max(30u, p.bitrate.get_sum_kbps());
        if (p.framerate_fps >= 1.0) fps_ = static_cast<uint32_t>(p.framerate_fps + 0.5);
        if (kbps == kbps_ || !n_) {
            kbps_ = kbps;
            return;
        }
        kbps_ = kbps;
        ponerTasa(n_->api.Get(), kbps_);
    }

    EncoderInfo GetEncoderInfo() const override {
        EncoderInfo i;
        const bool hw = n_ && n_->hw;
        i.implementation_name =
            hw ? "MediaFoundation (" + hallazgo().nombreCodHw + ")" : std::string("MediaFoundation (software)");
        i.is_hardware_accelerated = hw;
        i.supports_native_handle = false;
        i.has_trusted_rate_controller = hw;
        i.scaling_settings = VideoEncoder::ScalingSettings::kOff;
        i.supports_simulcast = false;
        return i;
    }

   private:
    static bool esperarFin(NucleoCod& n, int ms) {
        std::unique_lock<std::mutex> l(n.m);
        return n.cv.wait_for(l, std::chrono::milliseconds(ms), [&] { return n.terminado; });
    }

    bool iniciar(bool hw) {
        auto n = std::make_shared<NucleoCod>();
        n->hw = hw;
        if (hw) {
            n->mft = crearCodificadorHw(nullptr, nullptr);
        } else {
            CoCreateInstance(CLSID_CMSH264EncoderMFT, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&n->mft));
        }
        if (!n->mft) {
            if (!hw) aviso("video: no está el codificador H.264 de Windows");
            return false;
        }
        ComPtr<IMFAttributes> at;
        if (SUCCEEDED(n->mft->GetAttributes(&at))) {
            UINT32 asinc = 0;
            if (SUCCEEDED(at->GetUINT32(MF_TRANSFORM_ASYNC, &asinc)) && asinc) {
                at->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
                n->asinc = true;
            }
            at->SetUINT32(MF_LOW_LATENCY, TRUE);
        }
        n->mft.As(&n->api);
        ICodecAPI* api = n->api.Get();
        // Antes de los tipos: algunos (el de software) fijan el control de tasa al poner la salida.
        ponerU32(api, CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_CBR);
        ponerBool(api, CODECAPI_AVLowLatencyMode, true);
        ponerTasa(api, kbps_);

        ComPtr<IMFMediaType> out;
        MFCreateMediaType(&out);
        out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        out->SetUINT32(MF_MT_AVG_BITRATE, kbps_ * 1000u);
        out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(out.Get(), MF_MT_FRAME_SIZE, ancho_, alto_);
        MFSetAttributeRatio(out.Get(), MF_MT_FRAME_RATE, fps_, 1);
        MFSetAttributeRatio(out.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        out->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_ConstrainedBase);
        HRESULT hr = n->mft->SetOutputType(0, out.Get(), 0);
        if (FAILED(hr)) {
            out->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Base);
            hr = n->mft->SetOutputType(0, out.Get(), 0);
        }
        if (FAILED(hr)) {
            aviso(std::string("video: el codificador H.264 ") + (hw ? "por la placa" : "de Windows") +
                  " no acepta la salida (" + hex(hr) + ")");
            return false;
        }
        ComPtr<IMFMediaType> in;
        MFCreateMediaType(&in);
        in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(in.Get(), MF_MT_FRAME_SIZE, ancho_, alto_);
        MFSetAttributeRatio(in.Get(), MF_MT_FRAME_RATE, fps_, 1);
        MFSetAttributeRatio(in.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        hr = n->mft->SetInputType(0, in.Get(), 0);
        if (FAILED(hr)) {
            // Algunos quieren uno de los suyos: el NV12 que ofrezcan, con el tamaño.
            for (DWORD i = 0; FAILED(hr); ++i) {
                ComPtr<IMFMediaType> t;
                if (FAILED(n->mft->GetInputAvailableType(0, i, &t))) break;
                GUID sub{};
                t->GetGUID(MF_MT_SUBTYPE, &sub);
                if (!IsEqualGUID(sub, MFVideoFormat_NV12)) continue;
                MFSetAttributeSize(t.Get(), MF_MT_FRAME_SIZE, ancho_, alto_);
                MFSetAttributeRatio(t.Get(), MF_MT_FRAME_RATE, fps_, 1);
                hr = n->mft->SetInputType(0, t.Get(), 0);
            }
        }
        if (FAILED(hr)) {
            aviso(std::string("video: el codificador H.264 ") + (hw ? "por la placa" : "de Windows") +
                  " no acepta NV12 " + std::to_string(ancho_) + "x" + std::to_string(alto_) + " (" + hex(hr) + ")");
            return false;
        }
        // Después de los tipos: sin cuadros B, GOP largo (las claves van a pedido), CBR.
        ponerU32(api, CODECAPI_AVEncMPVDefaultBPictureCount, 0);
        ponerU32(api, CODECAPI_AVEncMPVGOPSize, fps_ * 60);
        ponerU32(api, CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_CBR);
        ponerTasa(api, kbps_);
        ponerBool(api, CODECAPI_AVLowLatencyMode, true);

        MFT_OUTPUT_STREAM_INFO si{};
        n->mft->GetOutputStreamInfo(0, &si);
        n->provee = (si.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
        n->tamSalida = si.cbSize;
        n->ancho = static_cast<int>(ancho_);
        n->alto = static_cast<int>(alto_);
        n->cb = cb_;
        n->leerCabecera();
        if (n->asinc) {
            if (FAILED(n->mft.As(&n->eventos))) return false;
        }
        n->mft->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
        if (FAILED(n->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0)) ||
            FAILED(n->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0)))
            return false;
        if (n->asinc) hilo_ = std::thread([n] { n->bucleEventos(); });
        (hw ? gCodHw : gCodSw)++;
        n_ = std::move(n);
        return true;
    }

    std::shared_ptr<NucleoCod> n_;
    std::thread hilo_;
    webrtc::EncodedImageCallback* cb_ = nullptr;
    uint32_t ancho_ = 0, alto_ = 0, fps_ = 30, kbps_ = 1000;
    LONGLONG tiempo_ = 0;
};

// --- El hallazgo ------------------------------------------------------------------------

const Hallazgo& hallazgo() {
    static const Hallazgo h = [] {
        Hallazgo r;
        if (!mfIniciado()) return r;
        comEnEsteHilo();
        // Con el mismo dispositivo que después decodifica (dispositivoVideo): el driver de AMD
        // abre ~25 hilos por dispositivo y no los suelta al cerrarlo; uno de prueba aparte
        // eran 25 hilos de más (medido en la .177).
        bool dxva = false;
        if (const auto dv = dispositivoVideo()) {
            describirDispositivo(dv->dev, &r.placa, &dxva);
            r.decodificaHw = dxva;
        }
        {
            ComPtr<IMFTransform> dec;
            r.decodificador = SUCCEEDED(
                CoCreateInstance(CLSID_CMSH264DecoderMFT, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dec)));
            if (!r.decodificador) r.decodificaHw = false;
        }
        {
            ComPtr<IMFTransform> hw = crearCodificadorHw(&r.nombreCodHw, &r.fabricanteCodHw);
            r.codificaHw = static_cast<bool>(hw);
            ComPtr<IMFShutdown> apagar;
            if (hw && SUCCEEDED(hw.As(&apagar))) apagar->Shutdown();
            ComPtr<IMFTransform> sw;
            r.codificador = r.codificaHw || SUCCEEDED(CoCreateInstance(CLSID_CMSH264EncoderMFT, nullptr,
                                                                       CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sw)));
        }
        const std::string placa = r.placa.empty() ? "sin placa" : r.placa;
        info("video: H.264 por Media Foundation — placa " + placa + "; decodifica " +
             (r.decodificaHw && r.decodificador ? "por la placa" : "NO") + ", codifica " +
             (r.codificaHw ? "por la placa (" + r.nombreCodHw + (r.fabricanteCodHw.empty() ? "" : ", " + r.fabricanteCodHw) + ")"
                           : r.codificador ? "por software" : "NO"));
        if (!r.decodificaHw || !r.decodificador)
            aviso("video: la placa no decodifica H.264: los invitados suben y reciben en VP8 (por software)");
        return r;
    }();
    return h;
}

// --- Fábricas ---------------------------------------------------------------------------

class Codificadores : public webrtc::VideoEncoderFactory {
   public:
    explicit Codificadores(std::unique_ptr<webrtc::VideoEncoderFactory> sw) : sw_(std::move(sw)) {}
    std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
        std::vector<webrtc::SdpVideoFormat> r;
        if (hallazgo().codificador) r = formatosH264(false);
        for (auto& f : sw_->GetSupportedFormats())
            if (!esH264(f)) r.push_back(f);
        return r;
    }
    CodecSupport QueryCodecSupport(const webrtc::SdpVideoFormat& f, absl::optional<std::string> modo) const override {
        if (esH264(f)) {
            CodecSupport c;
            c.is_supported = hallazgo().codificador && (!modo || *modo == "L1T1");
            c.is_power_efficient = hallazgo().codificaHw;
            return c;
        }
        return sw_->QueryCodecSupport(f, std::move(modo));
    }
    std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(const webrtc::SdpVideoFormat& f) override {
        if (esH264(f)) return hallazgo().codificador ? std::make_unique<CodificadorMf>() : nullptr;
        return sw_->CreateVideoEncoder(f);
    }

   private:
    std::unique_ptr<webrtc::VideoEncoderFactory> sw_;
};

/// ¿Se recibe H.264? Solo si la placa lo decodifica y `gateway.decodificarConPlaca` está
/// prendido. Si no, el H.264 no se ofrece para subir: como la m-line de video va en los dos
/// sentidos, el invitado y su retorno van en VP8 (libvpx, probado). El H.264 por software de
/// Windows NO es respaldo: en la .177 no entregaba ningún cuadro (ProcessOutput E_FAIL) y
/// abría ~24 hilos por flujo (634 con 10 invitados).
bool decodificaConPlaca() {
    return hallazgo().decodificador && hallazgo().decodificaHw && ba::ajustes::siNo("gateway.decodificarConPlaca", true);
}

/// La API de video de D3D11 (`gateway.decodificadorPlaca` "d3d11", de fábrica) o el MFT
/// con DXVA ("mft").
std::unique_ptr<webrtc::VideoDecoder> crearDecodificadorH264() {
    if (!decodificaConPlaca()) return nullptr;
    if (ba::ajustes::texto("gateway.decodificadorPlaca", "d3d11") != "mft") return std::make_unique<DecodificadorD3d11>();
    return std::make_unique<DecodificadorMf>();
}

class Decodificadores : public webrtc::VideoDecoderFactory {
   public:
    explicit Decodificadores(std::unique_ptr<webrtc::VideoDecoderFactory> sw) : sw_(std::move(sw)) {}
    std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
        std::vector<webrtc::SdpVideoFormat> r;
        if (decodificaConPlaca()) r = formatosH264(true);
        for (auto& f : sw_->GetSupportedFormats())
            if (!esH264(f)) r.push_back(f);
        return r;
    }
    CodecSupport QueryCodecSupport(const webrtc::SdpVideoFormat& f, bool escala) const override {
        if (esH264(f)) {
            CodecSupport c;
            c.is_supported = decodificaConPlaca() && !escala;
            c.is_power_efficient = hallazgo().decodificaHw;
            return c;
        }
        return sw_->QueryCodecSupport(f, escala);
    }
    std::unique_ptr<webrtc::VideoDecoder> Create(const webrtc::Environment& env, const webrtc::SdpVideoFormat& f) override {
        if (esH264(f)) return crearDecodificadorH264();
        return sw_->Create(env, f);
    }
    std::unique_ptr<webrtc::VideoDecoder> CreateVideoDecoder(const webrtc::SdpVideoFormat& f) override {
        if (esH264(f)) return crearDecodificadorH264();
        return sw_->CreateVideoDecoder(f);
    }

   private:
    std::unique_ptr<webrtc::VideoDecoderFactory> sw_;
};

} // namespace

std::unique_ptr<webrtc::VideoEncoderFactory> codificadores(std::unique_ptr<webrtc::VideoEncoderFactory> software) {
    (void)hallazgo(); // una vez, al arrancar: la línea del log con lo que hay
    return std::make_unique<Codificadores>(std::move(software));
}

std::unique_ptr<webrtc::VideoDecoderFactory> decodificadores(std::unique_ptr<webrtc::VideoDecoderFactory> software) {
    (void)hallazgo();
    return std::make_unique<Decodificadores>(std::move(software));
}

bool hayH264() { return decodificaConPlaca() && hallazgo().codificador; }

nlohmann::json estado() {
    const Hallazgo& h = hallazgo();
    const auto modo = [](bool hw, bool hay) { return hw ? "hardware" : hay ? "software" : "no"; };
    const bool placa = decodificaConPlaca();
    return {{"decodifica", placa ? "hardware" : "no"}, // sin placa: no se ofrece H.264, va VP8
            {"codifica", modo(h.codificaHw, h.codificador)},
            {"decodificador", !placa ? "ninguno (VP8)"
                              : ba::ajustes::texto("gateway.decodificadorPlaca", "d3d11") == "mft" ? "MediaFoundation DXVA"
                                                                                                   : "D3D11"},
            {"decodificadores_d3d11", gDecD3d.load()},
            {"celdas_en_placa", gCeldasPlaca.load()},  // F3: la grilla achicada en la placa
            {"achica_en_placa", !gAvisoVp.load()},     // false: la placa no pudo y se achica en el procesador
            {"celdas_con_entero", gCeldasConEntero.load()}, // … en la misma vuelta que el entero para ba-app
            {"achica_desde", vpIntermedia().load() ? "intermedia" : "superficie"}, // de dónde lee el procesador de video
            {"codificador", h.codificaHw ? h.nombreCodHw : std::string("MediaFoundation (software)")},
            {"fabricante", h.fabricanteCodHw},
            {"placa", h.placa},
            {"caidas_decodificador", gCaidasDec.load()},
            {"caidas_codificador", gCaidasCod.load()},
            {"codificadores", {{"hardware", gCodHw.load()}, {"software", gCodSw.load()}}}};
}

} // namespace ba::gateway::codecs::win

#endif // _WIN32
