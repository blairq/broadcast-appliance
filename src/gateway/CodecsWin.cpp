// ============================================================================
// src/gateway/CodecsWin.cpp — H.264 por Media Foundation en Windows
//
// Lo mismo que CodecsMac.mm con VideoToolbox: que la placa de video decodifique lo que
// sube cada invitado y codifique el retorno, en vez del procesador (libvpx). El libwebrtc
// que usamos (Shiguredo) viene SIN OpenH264, así que el H.264 en Windows sale todo de acá.
//
// Decodificar: el decodificador H.264 de Windows (CLSID_CMSH264DecoderMFT) con un
//   administrador de dispositivos D3D11: decodifica en la placa por DXVA (el motor «Video
//   Decode» de la GPU; AMD, Intel o NVIDIA, la que haya). El cuadro sale en una textura
//   NV12; Lock2D lo trae a memoria y libyuv lo pasa a I420 (el gateway lo necesita en RAM:
//   la memoria compartida con ba-app y la grilla). Si la placa no decodifica H.264 o el
//   DXVA no arranca, el mismo decodificador por software (con `gateway.hilosPorDecodificador`
//   hilos), avisado una vez en el log.
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

#if BA_GATEWAY_HAVE_WEBRTC && defined(_WIN32)

#include "CodecsWin.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// clang-format off
#include <d3d11.h>
#include <d3d10.h>
#include <dxgi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mftransform.h>
#include <codecapi.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>
// clang-format on

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "diag/Registro.hpp"
#include "platform/Hilo.hpp"

#include "api/video/encoded_image.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_decoder.h"
#include "api/video_codecs/video_encoder.h"
#include "modules/video_coding/codecs/h264/include/h264_globals.h"
#include "modules/video_coding/codecs/interface/common_constants.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/include/video_error_codes.h"
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

bool crearDispositivo(Dispositivo& d, std::string* placa, bool* decodificaH264) {
    D3D_FEATURE_LEVEL nivel{};
    ComPtr<ID3D11DeviceContext> ctx;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                   D3D11_SDK_VERSION, &d.dev, &nivel, &ctx);
    if (FAILED(hr)) return false;
    // Varios decodificadores (cada uno en su hilo) comparten el dispositivo.
    ComPtr<ID3D10Multithread> mt;
    if (SUCCEEDED(d.dev.As(&mt))) mt->SetMultithreadProtected(TRUE);
    if (placa != nullptr) {
        ComPtr<IDXGIDevice> dxgi;
        ComPtr<IDXGIAdapter> ad;
        DXGI_ADAPTER_DESC desc{};
        if (SUCCEEDED(d.dev.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&ad)) && SUCCEEDED(ad->GetDesc(&desc))) {
            *placa = utf8(desc.Description);
            const std::string f = fabricanteDe(desc.VendorId);
            if (!f.empty() && placa->find(f) == std::string::npos) *placa = f + " " + *placa;
        }
    }
    if (decodificaH264 != nullptr) {
        *decodificaH264 = false;
        ComPtr<ID3D11VideoDevice> vd;
        if (SUCCEEDED(d.dev.As(&vd))) {
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
        if (crearDispositivo(d, nullptr, nullptr)) pozo.push_back(std::move(d));
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
        if (hallazgo().decodificaHw) {
            if (iniciar(true)) return true;
            ++gCaidasDec;
            if (!gAvisoDec.exchange(true))
                aviso("video: el decodificador H.264 por la placa (DXVA) no arrancó: decodifica por software");
        }
        return iniciar(false);
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
        } else {
            ponerU32(api.Get(), CODECAPI_AVDecNumWorkerThreads, static_cast<ULONG>(hilos_));
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
        // Con la placa, varios errores seguidos: se rehace por software (un cuadro clave
        // lo pide libwebrtc al ver el error).
        if (hw_ && ++errores_ >= 3) {
            ++gCaidasDec;
            if (!gAvisoDec.exchange(true))
                aviso(std::string("video: el decodificador H.264 por la placa falla (") + que + " " + hex(hr) +
                      "): se pasa a software");
            iniciar(false);
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
        Dispositivo d;
        bool dxva = false;
        if (crearDispositivo(d, &r.placa, &dxva)) r.decodificaHw = dxva;
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
             (r.decodificaHw ? "por la placa (DXVA)" : r.decodificador ? "por software" : "NO") + ", codifica " +
             (r.codificaHw ? "por la placa (" + r.nombreCodHw + (r.fabricanteCodHw.empty() ? "" : ", " + r.fabricanteCodHw) + ")"
                           : r.codificador ? "por software" : "NO"));
        if (r.decodificador && r.codificador && (!r.decodificaHw || !r.codificaHw))
            aviso("video: H.264 en parte por software (sin " +
                  std::string(!r.decodificaHw && !r.codificaHw ? "decodificar ni codificar"
                              : !r.decodificaHw                ? "decodificar"
                                                               : "codificar") +
                  " en la placa): más procesador por invitado");
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

class Decodificadores : public webrtc::VideoDecoderFactory {
   public:
    explicit Decodificadores(std::unique_ptr<webrtc::VideoDecoderFactory> sw) : sw_(std::move(sw)) {}
    std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
        std::vector<webrtc::SdpVideoFormat> r;
        if (hallazgo().decodificador) r = formatosH264(true);
        for (auto& f : sw_->GetSupportedFormats())
            if (!esH264(f)) r.push_back(f);
        return r;
    }
    CodecSupport QueryCodecSupport(const webrtc::SdpVideoFormat& f, bool escala) const override {
        if (esH264(f)) {
            CodecSupport c;
            c.is_supported = hallazgo().decodificador && !escala;
            c.is_power_efficient = hallazgo().decodificaHw;
            return c;
        }
        return sw_->QueryCodecSupport(f, escala);
    }
    std::unique_ptr<webrtc::VideoDecoder> Create(const webrtc::Environment& env, const webrtc::SdpVideoFormat& f) override {
        if (esH264(f)) return hallazgo().decodificador ? std::make_unique<DecodificadorMf>() : nullptr;
        return sw_->Create(env, f);
    }
    std::unique_ptr<webrtc::VideoDecoder> CreateVideoDecoder(const webrtc::SdpVideoFormat& f) override {
        if (esH264(f)) return hallazgo().decodificador ? std::make_unique<DecodificadorMf>() : nullptr;
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

bool hayH264() { return hallazgo().decodificador && hallazgo().codificador; }

nlohmann::json estado() {
    const Hallazgo& h = hallazgo();
    const auto modo = [](bool hw, bool hay) { return hw ? "hardware" : hay ? "software" : "no"; };
    return {{"decodifica", modo(h.decodificaHw, h.decodificador)},
            {"codifica", modo(h.codificaHw, h.codificador)},
            {"decodificador", h.decodificaHw ? "MediaFoundation DXVA" : "MediaFoundation (software)"},
            {"codificador", h.codificaHw ? h.nombreCodHw : std::string("MediaFoundation (software)")},
            {"fabricante", h.fabricanteCodHw},
            {"placa", h.placa},
            {"caidas_decodificador", gCaidasDec.load()},
            {"caidas_codificador", gCaidasCod.load()},
            {"codificadores", {{"hardware", gCodHw.load()}, {"software", gCodSw.load()}}}};
}

} // namespace ba::gateway::codecs::win

#endif // BA_GATEWAY_HAVE_WEBRTC && _WIN32
