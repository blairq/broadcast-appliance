// ============================================================================
// src/app/InputSpecs.hpp — Como se arma una entrada de video (hardware o red)
//
// Lo usan las herramientas MCP (video_input_add, network_input_add) y la
// pagina Entradas, asi las dos arman exactamente la misma fuente.
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <string>

#include "BroadcastEngine.hpp"

namespace ba::app::inputs {

#if defined(_WIN32)
constexpr const char* kVideoType = "dshow_input";
constexpr const char* kVideoProp = "video_device_id";
#elif defined(__APPLE__)
constexpr const char* kVideoType = "macos-avcapture";
constexpr const char* kVideoProp = "device";
#else
constexpr const char* kVideoType = "v4l2_input";
constexpr const char* kVideoProp = "device_id";
#endif

#if defined(_WIN32)
constexpr const char* kAudioInType = "wasapi_input_capture";
constexpr const char* kAudioInProp = "device_id";
#elif defined(__APPLE__)
constexpr const char* kAudioInType = "coreaudio_input_capture";
constexpr const char* kAudioInProp = "device_id";
#else
constexpr const char* kAudioInType = "pulse_input_capture";
constexpr const char* kAudioInProp = "device_id";
#endif

/// Entrada de audio del sistema (microfono, linea, interfaz). Sin video: no
/// ocupa lugar en el lienzo.
SourceSpec audioInputSpec(const std::string& device, const std::string& name);

/// Esquema soportado de una URL de red ("" si no).
std::string networkScheme(const std::string& url);

/// Fuente de camara/capturadora. `opts`: input, pixelformat, resolution, framerate.
SourceSpec hardwareSpec(const std::string& device, const std::string& name, const nlohmann::json& opts);

/// Fuente de red. `opts`: latency (low|normal), rtspTransport (tcp|udp),
/// hwDecode, reconnectSec, audio. `caFile`: raices para https/rtmps.
SourceSpec networkSpec(const std::string& url, const std::string& name, const nlohmann::json& opts,
                       const std::string& caFile);

/// URL sin usuario/clave (para mostrar).
std::string redactUrl(std::string url);

/// true si la fuente es una entrada de red (ffmpeg_source no local).
bool isNetworkInput(const std::string& type, const nlohmann::json& settings);

} // namespace ba::app::inputs
