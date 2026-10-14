// ============================================================================
// src/gateway/CodecsMac.mm — Codecs de video de macOS para el gateway
//
// El libwebrtc que usamos (Shiguredo) viene SIN OpenH264: con las fábricas de
// plantilla, el gateway solo sabe VP8/VP9/AV1. Un televisor o un Chromecast casi
// nunca decodifica VP8 en hardware, y a 1080p el aparato no llega: la pantalla se
// atrasa y se clava. En macOS el SDK trae H.264 por VideoToolbox (codifica en el
// hardware de la Mac, sin gastar procesador) y las fábricas «por defecto» de
// Objective-C lo suman a VP8/VP9/AV1. Ver Webrtc.cpp (fabricasDeVideo).
// ============================================================================

#include "CodecsMac.hpp"

#import "components/video_codec/RTCDefaultVideoDecoderFactory.h"
#import "components/video_codec/RTCDefaultVideoEncoderFactory.h"
#include "sdk/objc/native/api/video_decoder_factory.h"
#include "sdk/objc/native/api/video_encoder_factory.h"

namespace ba::gateway {

std::unique_ptr<webrtc::VideoEncoderFactory> codificadoresMac() {
    return webrtc::ObjCToNativeVideoEncoderFactory([[RTCDefaultVideoEncoderFactory alloc] init]);
}

std::unique_ptr<webrtc::VideoDecoderFactory> decodificadoresMac() {
    return webrtc::ObjCToNativeVideoDecoderFactory([[RTCDefaultVideoDecoderFactory alloc] init]);
}

} // namespace ba::gateway
