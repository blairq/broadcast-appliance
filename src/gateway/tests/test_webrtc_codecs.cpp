// ============================================================================
// src/gateway/tests/test_webrtc_codecs.cpp — H.264 de la plataforma, de punta a punta
//
// Con las fábricas del gateway (CodecsVideo.cpp: VideoToolbox en la Mac, Media Foundation
// en Windows) se codifican 90 cuadros 720p que se mueven y se decodifican de nuevo:
//   1. El primero sale clave y con SPS/PPS (si no, el navegador no arranca).
//   2. Cambio de bitrate en caliente (como el retorno compartido) y cuadro clave a pedido.
//   3. Se decodifica todo y la imagen vuelve parecida (luminancia media).
//   4. Dice si fue por hardware y cuánto tardó cada cuadro (la prueba de humo de la placa).
// Sin H.264 en la plataforma (Linux) da SKIP (77).
// ============================================================================

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "api/environment/environment_factory.h"
#include "api/video/encoded_image.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_decoder.h"
#include "api/video_codecs/video_encoder.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/include/video_error_codes.h"

#include "CodecsVideo.hpp"

using namespace ba::gateway;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        } else {                                                                                             \
            std::printf("ok: %s\n", msg);                                                                    \
        }                                                                                                    \
    } while (0)

constexpr int kW = 1280, kH = 720, kCuadros = 90;

double ahoraMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool tieneNal(const uint8_t* d, size_t n, int tipo) {
    for (size_t i = 0; i + 3 < n; ++i)
        if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1 && (d[i + 3] & 0x1f) == tipo) return true;
    return false;
}

/// Un cuadro que se mueve: degradé horizontal corrido `k` píxeles y un cuadrado claro.
rtc::scoped_refptr<webrtc::I420Buffer> cuadro(int k) {
    auto b = webrtc::I420Buffer::Create(kW, kH);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            int v = ((x + k * 8) % kW) * 200 / kW + 20;
            if (x > 200 + k * 4 && x < 400 + k * 4 && y > 200 && y < 400) v = 235;
            b->MutableDataY()[y * b->StrideY() + x] = static_cast<uint8_t>(v);
        }
    for (int y = 0; y < kH / 2; ++y)
        for (int x = 0; x < kW / 2; ++x) {
            b->MutableDataU()[y * b->StrideU() + x] = static_cast<uint8_t>(128 + (x % 32));
            b->MutableDataV()[y * b->StrideV() + x] = static_cast<uint8_t>(128 - (y % 32));
        }
    return b;
}

double mediaY(const webrtc::I420BufferInterface& b) {
    double s = 0;
    for (int y = 0; y < b.height(); y += 4)
        for (int x = 0; x < b.width(); x += 4) s += b.DataY()[y * b.StrideY() + x];
    return s / ((b.height() / 4) * (b.width() / 4));
}

struct Salidas : webrtc::EncodedImageCallback {
    std::mutex m;
    std::condition_variable cv;
    std::vector<webrtc::EncodedImage> imgs;
    Result OnEncodedImage(const webrtc::EncodedImage& img, const webrtc::CodecSpecificInfo* info) override {
        webrtc::EncodedImage c = img;
        c.SetEncodedData(webrtc::EncodedImageBuffer::Create(img.data(), img.size()));
        std::lock_guard<std::mutex> l(m);
        if (info == nullptr || info->codecType != webrtc::kVideoCodecH264) std::printf("  (salida sin info H.264)\n");
        imgs.push_back(std::move(c));
        cv.notify_all();
        return Result(Result::OK);
    }
};

struct Decodificados : webrtc::DecodedImageCallback {
    std::mutex m;
    std::condition_variable cv;
    int n = 0, ancho = 0, alto = 0;
    std::vector<std::pair<uint32_t, double>> medias; // sello RTP, luminancia media
    int32_t Decoded(webrtc::VideoFrame& f) override {
        auto i420 = f.video_frame_buffer()->ToI420();
        std::lock_guard<std::mutex> l(m);
        ++n;
        ancho = f.width();
        alto = f.height();
        if (i420) medias.emplace_back(f.timestamp(), mediaY(*i420));
        cv.notify_all();
        return 0;
    }
};

} // namespace

int main() {
    std::printf("== test_webrtc_codecs (H.264 de la plataforma) ==\n");
    auto cods = codecs::codificadores();
    auto decs = codecs::decodificadores();
    std::printf("estado: %s\n", codecs::estado().dump().c_str());
    webrtc::SdpVideoFormat h264("H264");
    bool hay = false;
    for (const auto& f : cods->GetSupportedFormats()) {
        std::printf("  codifica: %s\n", f.ToString().c_str());
        if (f.name == "H264" && !hay) {
            const auto it = f.parameters.find("packetization-mode");
            if (it != f.parameters.end() && it->second == "1") {
                h264 = f;
                hay = true;
            }
        }
    }
    if (!hay) {
        std::printf("SKIP: sin H.264 en esta plataforma\n");
        return 77;
    }
    CHECK(codecs::h264PorOmision(), "«auto» elige H.264");

    // --- Codificar ------------------------------------------------------------------------
    auto enc = cods->CreateVideoEncoder(h264);
    CHECK(enc != nullptr, "hay codificador H.264");
    if (!enc) return 1;
    webrtc::VideoCodec c;
    c.codecType = webrtc::kVideoCodecH264;
    c.width = kW;
    c.height = kH;
    c.startBitrate = 1500;
    c.maxBitrate = 1500;
    c.minBitrate = 30;
    c.maxFramerate = 30;
    c.qpMax = 56;
    c.active = true;
    c.mode = webrtc::VideoCodecMode::kRealtimeVideo;
    c.numberOfSimulcastStreams = 1;
    c.simulcastStream[0].width = kW;
    c.simulcastStream[0].height = kH;
    c.simulcastStream[0].maxFramerate = 30;
    c.simulcastStream[0].numberOfTemporalLayers = 1;
    c.simulcastStream[0].maxBitrate = 1500;
    c.simulcastStream[0].targetBitrate = 1500;
    c.simulcastStream[0].active = true;
    *c.H264() = webrtc::VideoEncoder::GetDefaultH264Settings();
    c.H264()->numberOfTemporalLayers = 1;
    const webrtc::VideoEncoder::Settings ajustes(webrtc::VideoEncoder::Capabilities(false), 2, 1200);
    double t0 = ahoraMs();
    CHECK(enc->InitEncode(&c, ajustes) == WEBRTC_VIDEO_CODEC_OK, "InitEncode 720p");
    std::printf("  iniciar el codificador: %.0f ms\n", ahoraMs() - t0);
    const auto ei = enc->GetEncoderInfo();
    std::printf("  codificador: %s (%s)\n", ei.implementation_name.c_str(), ei.is_hardware_accelerated ? "hardware" : "software");
    Salidas salidas;
    enc->RegisterEncodeCompleteCallback(&salidas);
    const auto tasas = [&](int kbps) {
        webrtc::VideoBitrateAllocation a;
        a.SetBitrate(0, 0, static_cast<uint32_t>(kbps) * 1000u);
        enc->SetRates(webrtc::VideoEncoder::RateControlParameters(a, 30.0));
    };
    tasas(1500);
    t0 = ahoraMs();
    for (int k = 0; k < kCuadros; ++k) {
        if (k == 45) tasas(400); // en caliente, como el retorno compartido
        const bool clave = k == 0 || k == 60;
        const std::vector<webrtc::VideoFrameType> tipos{clave ? webrtc::VideoFrameType::kVideoFrameKey
                                                              : webrtc::VideoFrameType::kVideoFrameDelta};
        const auto f = webrtc::VideoFrame::Builder()
                           .set_video_frame_buffer(cuadro(k))
                           .set_timestamp_rtp(static_cast<uint32_t>(90000 + k * 3000))
                           .set_timestamp_us(static_cast<int64_t>(k) * 33333)
                           .build();
        if (enc->Encode(f, &tipos) != WEBRTC_VIDEO_CODEC_OK) std::printf("  Encode %d: error\n", k);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    {
        std::unique_lock<std::mutex> l(salidas.m);
        salidas.cv.wait_for(l, std::chrono::seconds(3), [&] { return salidas.imgs.size() >= static_cast<size_t>(kCuadros); });
    }
    const double tCod = ahoraMs() - t0;
    enc->Release();
    std::vector<webrtc::EncodedImage> imgs;
    {
        std::lock_guard<std::mutex> l(salidas.m);
        imgs = salidas.imgs;
    }
    std::printf("  %zu cuadros codificados en %.0f ms (con 5 ms de espera entre cuadros)\n", imgs.size(), tCod);
    CHECK(imgs.size() >= kCuadros - 5, "salen (casi) todos los cuadros");
    if (imgs.empty()) return 1;
    CHECK(imgs.front().FrameType() == webrtc::VideoFrameType::kVideoFrameKey, "el primero es clave");
    CHECK(tieneNal(imgs.front().data(), imgs.front().size(), 7) && tieneNal(imgs.front().data(), imgs.front().size(), 8),
          "el primero trae SPS y PPS");
    size_t bytesAntes = 0, bytesDespues = 0;
    int nAntes = 0, nDespues = 0;
    bool claveTarde = false;
    for (const auto& i : imgs) {
        const int k = static_cast<int>((i.RtpTimestamp() - 90000) / 3000);
        if (k >= 5 && k < 45 && k != 0) bytesAntes += i.size(), ++nAntes;
        if (k >= 50 && k != 60 && k < kCuadros) bytesDespues += i.size(), ++nDespues;
        if (k >= 60 && k <= 62 && i.FrameType() == webrtc::VideoFrameType::kVideoFrameKey) claveTarde = true;
    }
    const double kbpsAntes = nAntes ? bytesAntes * 8.0 * 30 / nAntes / 1000 : 0;
    const double kbpsDespues = nDespues ? bytesDespues * 8.0 * 30 / nDespues / 1000 : 0;
    std::printf("  ~%.0f kbps pedido 1500, ~%.0f kbps pedido 400\n", kbpsAntes, kbpsDespues);
    CHECK(kbpsDespues < kbpsAntes, "el bitrate baja en caliente");
    CHECK(claveTarde, "el cuadro clave a pedido sale");

    // --- Decodificar ----------------------------------------------------------------------
    auto dec = decs->Create(webrtc::CreateEnvironment(), h264);
    CHECK(dec != nullptr, "hay decodificador H.264");
    if (!dec) return 1;
    webrtc::VideoDecoder::Settings ds;
    ds.set_codec_type(webrtc::kVideoCodecH264);
    ds.set_number_of_cores(8);
    ds.set_max_render_resolution({kW, kH});
    t0 = ahoraMs();
    CHECK(dec->Configure(ds), "Configure del decodificador");
    std::printf("  iniciar el decodificador: %.0f ms\n", ahoraMs() - t0);
    Decodificados decodificados;
    dec->RegisterDecodeCompleteCallback(&decodificados);
    t0 = ahoraMs();
    int errores = 0;
    for (const auto& i : imgs)
        if (dec->Decode(i, false, 0) != WEBRTC_VIDEO_CODEC_OK) ++errores;
    {
        std::unique_lock<std::mutex> l(decodificados.m);
        decodificados.cv.wait_for(l, std::chrono::seconds(3),
                                  [&] { return decodificados.n >= static_cast<int>(imgs.size()) - 2; });
    }
    const double tDec = ahoraMs() - t0;
    const auto di = dec->GetDecoderInfo();
    std::printf("  decodificador: %s (%s)\n", di.implementation_name.c_str(), di.is_hardware_accelerated ? "hardware" : "software");
    std::printf("  %d cuadros decodificados en %.0f ms (%.2f ms por cuadro), %d errores, %dx%d\n", decodificados.n, tDec,
                decodificados.n ? tDec / decodificados.n : 0.0, errores, decodificados.ancho, decodificados.alto);
    CHECK(errores == 0, "decodifica sin errores");
    CHECK(decodificados.n >= static_cast<int>(imgs.size()) - 2, "salen (casi) todos los decodificados");
    CHECK(decodificados.ancho == kW && decodificados.alto == kH, "el tamaño vuelve igual (sin el relleno de 1088/736)");
    double peor = 0;
    for (const auto& [ts, m] : decodificados.medias) {
        const int k = static_cast<int>((ts - 90000) / 3000);
        if (k < 0 || k >= kCuadros) continue;
        peor = std::max(peor, std::fabs(m - mediaY(*cuadro(k))));
    }
    std::printf("  diferencia de luminancia media, la peor: %.1f\n", peor);
    CHECK(!decodificados.medias.empty() && peor < 12.0, "la imagen vuelve parecida (y con su sello RTP)");
    std::printf("estado al final: %s\n", codecs::estado().dump().c_str());
    dec->Release();
    std::printf(g_fallos == 0 ? "OK\n" : "FALLOS: %d\n", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
