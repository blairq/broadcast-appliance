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
constexpr const char* kAudioInLibobs = "wasapi_input_capture";
constexpr const char* kAudioInProp = "device_id";
#elif defined(__APPLE__)
constexpr const char* kAudioInLibobs = "coreaudio_input_capture";
constexpr const char* kAudioInProp = "device_id";
#else
constexpr const char* kAudioInLibobs = "pulse_input_capture";
constexpr const char* kAudioInProp = "device_id";
#endif

// kAudioInLibobs: el micrófono de libobs de antes (solo para listar mientras
// GStreamer arranca y para pasar los viejos a GStreamer al cargar).

/// Los micrófonos son un pipeline de GStreamer (audio/EntradasAudio.hpp).
constexpr const char* kMicType = "ba_gst_fuente";

/// Entrada de audio del sistema (microfono, linea, interfaz) por GStreamer
/// (`ba_gst_fuente` con `pipeline_audio`; `microfono` guarda el dispositivo).
/// Sin video: no ocupa lugar en el lienzo.
SourceSpec audioInputSpec(const std::string& device, const std::string& name);

/// ¿Es un micrófono del equipo? (el de GStreamer o uno viejo de libobs).
bool esMicrofono(const std::string& type, const nlohmann::json& settings);

/// Los ajustes de un `ba_gst_fuente` con el audio que el gateway deja en un
/// anillo de memoria compartida (`baanillo`): `canal` guest (el invitado
/// `slot`) o remoto (el legislador remoto con la palabra).
std::string ajustesAudioAnillo(const std::string& canal, int slot);

/// El tipo «lógico» de una fuente, para las plantillas de rack y la interfaz:
/// el audio de un invitado o del remoto es `ba_guest_audio` y un micrófono es
/// kAudioInLibobs aunque hoy los dos sean `ba_gst_fuente` (así las plantillas
/// marcadas antes siguen valiendo). El resto, su tipo.
std::string tipoLogico(const std::string& type, const nlohmann::json& settings);

/// Un micrófono viejo (fuente de libobs) pasado a GStreamer, con el mismo
/// nombre, volumen y silencio. false si `spec` no era uno viejo.
bool migrarMicrofono(SourceSpec& spec);

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
