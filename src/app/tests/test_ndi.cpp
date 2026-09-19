// ============================================================================
// src/app/tests/test_ndi.cpp — recepcion NDI(R) abierta (libNoIDea)
//
//   1. El descubrimiento mDNS arranca, vuelve a tiempo y no se cuelga (sin
//      emisores en la red: lista vacia).
//   2. El FFmpeg que viaja en el bundle tiene el decodificador SpeedHQ (el
//      video de NDI completo) y lo puede abrir.
// En Windows corre con las DLL de FFmpeg del runtime de OBS.
// ============================================================================

#include <chrono>
#include <cstdio>

extern "C" {
#include <libavcodec/avcodec.h>
}

#include "NdiSource.hpp"

int main() {
    int failures = 0;
    const auto t0 = std::chrono::steady_clock::now();
    const auto found = ba::app::ndi::discover(1500);
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::printf("descubrimiento: %zu emisor(es) en %lld ms\n", found.size(), static_cast<long long>(ms));
    if (ms > 6000) {
        std::printf("FALLO: el descubrimiento no volvio a tiempo\n");
        ++failures;
    }
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_SPEEDHQ);
    if (codec == nullptr) {
        std::printf("FALLO: el FFmpeg del bundle no tiene el decodificador SpeedHQ\n");
        ++failures;
    } else {
        AVCodecContext* ctx = avcodec_alloc_context3(codec);
        ctx->codec_tag = MKTAG('S', 'H', 'Q', '2'); // 4:2:2, lo que manda NDI
        ctx->width = 1920;
        ctx->height = 1080;
        if (avcodec_open2(ctx, codec, nullptr) < 0) {
            std::printf("FALLO: no se pudo abrir SpeedHQ\n");
            ++failures;
        }
        avcodec_free_context(&ctx);
    }
    if (failures == 0) std::printf("test_ndi: OK\n");
    return failures == 0 ? 0 : 1;
}
