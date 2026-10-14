// src/gateway/CodecsMac.hpp — Codecs de video de macOS (H.264 por VideoToolbox). Ver CodecsMac.mm.
#pragma once

#include <memory>

#include "api/video_codecs/video_decoder_factory.h"
#include "api/video_codecs/video_encoder_factory.h"

namespace ba::gateway {

std::unique_ptr<webrtc::VideoEncoderFactory> codificadoresMac();
std::unique_ptr<webrtc::VideoDecoderFactory> decodificadoresMac();

} // namespace ba::gateway
