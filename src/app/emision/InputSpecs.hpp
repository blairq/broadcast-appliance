// ============================================================================
// src/app/emision/InputSpecs.hpp — Como se arma una entrada (hardware, red, invitados)
//
// Lo usan las herramientas MCP (video_input_add, network_input_add) y la
// pagina Entradas, asi las dos arman exactamente la misma fuente.
//
// Y LOS TIPOS DE ANTES: app.json guardaba tipos de la época de OBS (cámaras,
// micrófonos, ffmpeg_source, ba_guest_video…). `alMotor` los pasa, en un solo
// lugar, al tipo que arma el motor (ba_gst_fuente / ba_gst_medio y los tipos
// registrados de FuentesApp): al cargar app.json (una vez: se guardan así,
// esquema 2) y en la puerta del motor (crearFuente), por lo que todavía llegue
// con el vocabulario viejo (el MCP lo acepta). Toda opción guardada llega al
// motor o vuelve en `sinEfecto` para avisarla: nada se pierde callado.
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include "emision/BroadcastEngine.hpp"

namespace ba::app::inputs {

// Los tipos «lógicos» de las cámaras y los micrófonos (los de OBS de cada sistema):
// los nombres siguen valiendo para las plantillas de rack, la interfaz y el MCP
// (tipoLogico), y alMotor pasa los guardados así a GStreamer.
#if defined(_WIN32)
constexpr const char* kVideoType = "dshow_input";
constexpr const char* kVideoProp = "video_device_id";
constexpr const char* kTipoMicViejo = "wasapi_input_capture";
#elif defined(__APPLE__)
constexpr const char* kVideoType = "macos-avcapture";
constexpr const char* kVideoProp = "device";
constexpr const char* kTipoMicViejo = "coreaudio_input_capture";
#else
constexpr const char* kVideoType = "v4l2_input";
constexpr const char* kVideoProp = "device_id";
constexpr const char* kTipoMicViejo = "pulse_input_capture";
#endif
constexpr const char* kAudioInProp = "device_id";

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

/// Los micrófonos son un pipeline de GStreamer (audio/EntradasAudio.hpp).
constexpr const char* kMicType = "ba_gst_fuente";

/// Entrada de audio del sistema (microfono, linea, interfaz) por GStreamer
/// (`ba_gst_fuente` con `pipeline_audio`; `microfono` guarda el dispositivo).
/// Sin video: no ocupa lugar en el lienzo.
SourceSpec audioInputSpec(const std::string& device, const std::string& name);

/// ¿Es un micrófono del equipo? (el de GStreamer o uno viejo de antes).
bool esMicrofono(const std::string& type, const nlohmann::json& settings);

/// Los ajustes de un `ba_gst_fuente` con el audio que el gateway deja en un
/// anillo de memoria compartida (`baanillo`): `canal` guest (el invitado
/// `slot`) o remoto (el integrante remoto con la palabra).
std::string ajustesAudioAnillo(const std::string& canal, int slot);

/// El tipo «lógico» de una fuente, para las plantillas de rack y la interfaz:
/// el audio de un invitado o del remoto es `ba_guest_audio` y un micrófono es
/// kTipoMicViejo aunque hoy los dos sean `ba_gst_fuente` (así las plantillas
/// marcadas antes siguen valiendo). El resto, su tipo.
std::string tipoLogico(const std::string& type, const nlohmann::json& settings);

/// Los archivos (video o audio) se reproducen por GStreamer: `ba_gst_medio`,
/// con local_file, looping y clear_on_media_end.
constexpr const char* kMedioType = "ba_gst_medio";

/// El video de un invitado (cámara o pantalla compartida) o del remoto con la
/// palabra por GStreamer: `ba_gst_fuente` con `bavideoanillo` (lee el canal que
/// escribe el gateway). Guarda `slot` y `channel` como el ba_guest_video de antes.
std::string ajustesVideoInvitado(int slot, const std::string& channel);
/// La fuente entera: `channel` guest | share | remoto | remoto_contenido. A pantalla
/// completa; el que la crea le pone el lugar y si se ve.
SourceSpec videoInvitadoSpec(int slot, const std::string& channel, const std::string& name);
/// ¿Es el video de un invitado? (el de GStreamer o un ba_guest_video viejo).
bool esVideoInvitado(const std::string& type, const std::string& settingsJson);

/// Una entrada NDI por GStreamer: `ba_gst_fuente` con `bandisrc` (libNoIDea) y
/// `avdec_speedhq`. Guarda ndi_name/host/port como el ba_ndi_input de antes.
std::string ajustesNdi(const std::string& nombre, const std::string& host, int port);

/// Lo que pasó al llevar una fuente al motor.
struct AlMotor {
    bool cambio = false;                ///< era de un tipo de antes (o le faltaba algo) y se pasó
    std::vector<std::string> sinEfecto; ///< opciones guardadas que el motor no tiene: se avisan
    /// `sinEfecto` en una línea, para el aviso.
    std::string lista() const {
        std::string s;
        for (const auto& k : sinEfecto) s += (s.empty() ? "" : ", ") + k;
        return s;
    }
};
/// Una fuente con un tipo de antes pasada al del motor, con el mismo nombre, lugar,
/// recorte, volumen y silencio (ver arriba). Idempotente: lo del motor no cambia. Lo
/// que queda sin efecto se anota en el registro (aviso) además de volver.
AlMotor alMotor(SourceSpec& spec);

/// Esquema soportado de una URL de red ("" si no).
std::string networkScheme(const std::string& url);

/// Cámara o capturadora del equipo por GStreamer (`ba_gst_fuente` con `camara` =
/// su id estable; ver video/Camaras.hpp). `opts`: {modo: «1920x1080@30»} (vacío
/// = el de la cámara).
SourceSpec hardwareSpec(const std::string& device, const std::string& name, const nlohmann::json& opts);

/// ¿Es una cámara del equipo? (la de GStreamer o una vieja de antes).
bool esCamara(const std::string& type, const nlohmann::json& settings);
/// El id de la cámara ("" si no es una).
std::string camaraDe(const std::string& type, const nlohmann::json& settings);

/// Fuente de red: `ba_gst_fuente` con `uri` y lo que el motor sabe de la conexión
/// (ver motor/OrigenRed.hpp). `opts`: latency (low|normal), rtspTransport (tcp|udp),
/// reconnectSec, audio. `caFile`: raices para https/rtsps/rtmps.
SourceSpec networkSpec(const std::string& url, const std::string& name, const nlohmann::json& opts,
                       const std::string& caFile);

/// URL sin usuario/clave (para mostrar).
std::string redactUrl(std::string url);

/// true si la fuente es una entrada de red (una URL, NDI o un pipeline propio).
bool isNetworkInput(const std::string& type, const nlohmann::json& settings);

} // namespace ba::app::inputs
