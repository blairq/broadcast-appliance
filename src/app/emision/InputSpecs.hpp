// ============================================================================
// src/app/emision/InputSpecs.hpp — Como se arma una entrada de video (hardware o red)
//
// Lo usan las herramientas MCP (video_input_add, network_input_add) y la
// pagina Entradas, asi las dos arman exactamente la misma fuente.
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <string>

#include "emision/BroadcastEngine.hpp"

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

/// Los grupos de la gente conectada, con el mismo nombre en Audio, Video y el mezclador: los
/// invitados que se suman por video (la conferencia) y los remotos de la asamblea.
constexpr const char* kGrupoConferencia = "Conferencia";
constexpr const char* kGrupoAsamblea = "Asamblea";
/// El grupo de una fuente de gente conectada (audio o video de un invitado o de un remoto);
/// nullptr si no es de esas.
inline const char* grupoDeGente(const std::string& tipo, const nlohmann::json& ajustes) {
    if (tipo == "ba_guest_audio")
        return ajustes.is_object() && ajustes.value("anillo", std::string()) == "remoto" ? kGrupoAsamblea
                                                                                        : kGrupoConferencia;
    if (tipo == "ba_guest_video")
        return ajustes.is_object() && ajustes.value("channel", std::string()).rfind("remoto", 0) == 0
                   ? kGrupoAsamblea
                   : kGrupoConferencia;
    return nullptr;
}

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
/// `slot`) o remoto (el integrante remoto con la palabra).
std::string ajustesAudioAnillo(const std::string& canal, int slot);

/// El tipo «lógico» de una fuente, para las plantillas de rack y la interfaz:
/// el audio de un invitado o del remoto es `ba_guest_audio` y un micrófono es
/// kAudioInLibobs aunque hoy los dos sean `ba_gst_fuente` (así las plantillas
/// marcadas antes siguen valiendo). El resto, su tipo.
std::string tipoLogico(const std::string& type, const nlohmann::json& settings);

/// Un micrófono viejo (fuente de libobs) pasado a GStreamer, con el mismo
/// nombre, volumen y silencio. false si `spec` no era uno viejo.
bool migrarMicrofono(SourceSpec& spec);

/// Los archivos (video o audio) se reproducen por GStreamer: `ba_gst_medio`,
/// con los mismos ajustes que ffmpeg_source (local_file, looping…) y los
/// controles de medios de libobs.
constexpr const char* kMedioType = "ba_gst_medio";

/// El video de un invitado (cámara o pantalla compartida) o del remoto con la
/// palabra por GStreamer: `ba_gst_fuente` con `bavideoanillo` (lee el canal que
/// escribe el gateway). Guarda `slot` y `channel` como el ba_guest_video de antes.
std::string ajustesVideoInvitado(int slot, const std::string& channel);
/// ¿Es el video de un invitado? (el de GStreamer o un ba_guest_video viejo).
bool esVideoInvitado(const std::string& type, const std::string& settingsJson);
/// Un ba_guest_video pasado a GStreamer (mismo nombre, lugar y recorte).
bool migrarVideoInvitado(SourceSpec& spec);

/// Una entrada NDI por GStreamer: `ba_gst_fuente` con `bandisrc` (libNoIDea) y
/// `avdec_speedhq`. Guarda ndi_name/host/port como el ba_ndi_input de antes.
std::string ajustesNdi(const std::string& nombre, const std::string& host, int port);
/// Un ba_ndi_input pasado a GStreamer (mismo nombre, lugar y recorte).
bool migrarNdi(SourceSpec& spec);
/// Un ffmpeg_source de un archivo local pasado a ba_gst_medio (los de red
/// quedan). false si no era uno.
bool migrarMedio(SourceSpec& spec);
bool migrarCamara(SourceSpec& spec);

/// Esquema soportado de una URL de red ("" si no).
std::string networkScheme(const std::string& url);

/// Cámara o capturadora del equipo por GStreamer (`ba_gst_fuente` con `camara` =
/// su id estable; ver video/Camaras.hpp). `opts`: {modo: «1920x1080@30»} (vacío
/// = el de la cámara). kVideoType/kVideoProp quedan para listar con libobs
/// mientras GStreamer arranca y para migrar las guardadas.
SourceSpec hardwareSpec(const std::string& device, const std::string& name, const nlohmann::json& opts);

/// ¿Es una cámara del equipo? (la de GStreamer o una vieja de libobs).
bool esCamara(const std::string& type, const nlohmann::json& settings);
/// El id de la cámara ("" si no es una).
std::string camaraDe(const std::string& type, const nlohmann::json& settings);

/// Una cámara vieja (fuente de libobs) pasada a GStreamer: mismo nombre, lugar,
/// recorte y visibilidad. false si `spec` no era una vieja.

/// Fuente de red. `opts`: latency (low|normal), rtspTransport (tcp|udp),
/// hwDecode, reconnectSec, audio. `caFile`: raices para https/rtmps.
SourceSpec networkSpec(const std::string& url, const std::string& name, const nlohmann::json& opts,
                       const std::string& caFile);

/// URL sin usuario/clave (para mostrar).
std::string redactUrl(std::string url);

/// true si la fuente es una entrada de red (ffmpeg_source no local).
bool isNetworkInput(const std::string& type, const nlohmann::json& settings);

} // namespace ba::app::inputs
