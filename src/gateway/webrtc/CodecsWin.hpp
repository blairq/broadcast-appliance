// src/gateway/webrtc/CodecsWin.hpp — H.264 por Media Foundation en Windows (la placa si hay). Ver CodecsWin.cpp.
#pragma once

#if BA_GATEWAY_HAVE_WEBRTC && defined(_WIN32)

#include <memory>

#include <nlohmann/json.hpp>

#include "api/video_codecs/video_decoder_factory.h"
#include "api/video_codecs/video_encoder_factory.h"

namespace ba::gateway::codecs::win {

/// H.264 por Media Foundation delante de `software` (VP8/VP9/AV1). Sin H.264 en la máquina
/// (Windows N sin el paquete de medios), solo `software`.
std::unique_ptr<webrtc::VideoEncoderFactory> codificadores(std::unique_ptr<webrtc::VideoEncoderFactory> software);
std::unique_ptr<webrtc::VideoDecoderFactory> decodificadores(std::unique_ptr<webrtc::VideoDecoderFactory> software);

/// ¿Hay H.264 para subir y bajar (por la placa o, si no, por el software de Windows)?
bool hayH264();

/// {decodifica: hardware|software|no, codifica: …, decodificador, codificador, placa,
///  caidas_decodificador, caidas_codificador, codificadores: {hardware, software}}.
nlohmann::json estado();

} // namespace ba::gateway::codecs::win

#endif
