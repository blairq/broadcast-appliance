// src/gateway/CodecsVideo.hpp — Las fábricas de video del gateway, por plataforma. Ver CodecsVideo.cpp.
#pragma once

#if BA_GATEWAY_HAVE_WEBRTC

#include <memory>

#include <nlohmann/json.hpp>

#include "api/video_codecs/video_decoder_factory.h"
#include "api/video_codecs/video_encoder_factory.h"

namespace ba::gateway::codecs {

/// Codificadores: macOS VideoToolbox (H.264) + VP8/VP9/AV1; Windows Media Foundation
/// (H.264, CodecsWin) + VP8/VP9/AV1; Linux VP8/VP9/AV1 por software.
std::unique_ptr<webrtc::VideoEncoderFactory> codificadores();

/// Decodificadores, los mismos por plataforma. Todos envueltos: los de software con
/// `gateway.hilosPorDecodificador` hilos internos (1 por omisión), y cada uno anota qué
/// implementación usa y si es por hardware (log y `estado()`).
std::unique_ptr<webrtc::VideoDecoderFactory> decodificadores();

/// Lo que elige «auto» en `retorno.codec`: true = H.264 (macOS y Windows con H.264), false = VP8.
bool h264PorOmision();

/// Para el estado del gateway (`uso.codecsVideo` de /api/v1/room → room_status):
/// {plataforma, auto, h264: {decodifica, codifica, decodificador, codificador, placa, caidas},
///  hilos_por_decodificador, decodificadores: [{codec, implementacion, hardware, activos}]}.
nlohmann::json estado();

} // namespace ba::gateway::codecs

#endif // BA_GATEWAY_HAVE_WEBRTC
