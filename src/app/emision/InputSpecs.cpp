// ============================================================================
// src/app/emision/InputSpecs.cpp — ver InputSpecs.hpp
// ============================================================================

#include "emision/InputSpecs.hpp"
#include "ajustes/Ajustes.hpp"
#include "diag/Registro.hpp"
#include "i18n/I18n.hpp"

#include "audio/EntradasAudio.hpp"
#include "video/Camaras.hpp"

#include <QtGlobal> // QT_TRANSLATE_NOOP

#include <algorithm>
#include <sstream>

namespace ba::app::inputs {

using json = nlohmann::json;

// Los nombres de grupo (kGrupoConferencia, kGrupoAsamblea) son claves: se guardan y se
// comparan en español. Esto solo los pone en el catálogo, para que la interfaz los
// traduzca al mostrarlos (qsTranslate("audio", grupo)).
[[maybe_unused]] constexpr const char* kGruposParaTraducir[] = {QT_TRANSLATE_NOOP("audio", "Conferencia"),
                                                                QT_TRANSLATE_NOOP("audio", "Asamblea")};

namespace {

std::string volcar(const json& j) { return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace); }

json leer(const std::string& s) {
    const json j = json::parse(s.empty() ? "{}" : s, nullptr, false);
    return j.is_object() ? j : json::object();
}

/// La latencia de una entrada de red según el modo: «low» (sin colchón) o «normal».
int latenciaDeModo(bool baja) { return baja ? 50 : 200; }

} // namespace

std::string networkScheme(const std::string& url) {
    const auto p = url.find("://");
    if (p == std::string::npos) return {};
    std::string s = url.substr(0, p);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    static const char* kOk[] = {"rtsp", "rtsps", "rtmp", "rtmps", "srt",
                                "http", "https", "udp",  "rtp",   "tcp"};
    return std::any_of(std::begin(kOk), std::end(kOk), [&](const char* k) { return s == k; }) ? s
                                                                                              : std::string();
}

SourceSpec hardwareSpec(const std::string& device, const std::string& name, const json& opts) {
    SourceSpec spec;
    spec.id = kMicType;
    spec.name = name;
    json s{{"uri", ""}, {"pipeline", ""}, {"pipeline_audio", ""}, {"audio", false}, {"normalizar", true}, {"camara", device}};
    if (opts.is_object() && opts.contains("modo") && opts["modo"].is_string() && !opts["modo"].get<std::string>().empty())
        s["camara_modo"] = opts["modo"];
    spec.settingsJson = volcar(s);
    spec.rect = CanvasRect{0.0f, 0.0f, 1.0f, 1.0f};
    return spec;
}

bool esCamara(const std::string& type, const json& settings) {
    if (type == kVideoType) return true;
    return type == kMicType && settings.is_object() && settings.contains("camara");
}

std::string camaraDe(const std::string& type, const json& settings) {
    if (!settings.is_object()) return {};
    if (type == kVideoType) return settings.value(kVideoProp, std::string());
    return type == kMicType ? settings.value("camara", std::string()) : std::string();
}

SourceSpec audioInputSpec(const std::string& device, const std::string& name) {
    SourceSpec spec;
    spec.id = kMicType;
    spec.name = name;
    spec.settingsJson = volcar(json{{"pipeline", ""}, // sin esto, ba_gst_fuente arma su video de prueba de fábrica
                                    {"pipeline_audio", audio::pipelineDeMicrofono(device)},
                                    {"audio", true},
                                    {"microfono", device}});
    spec.rect = CanvasRect{0.0f, 0.0f, 0.0f, 0.0f}; // solo audio
    return spec;
}

bool esMicrofono(const std::string& type, const json& settings) {
    if (type == kTipoMicViejo) return true;
    return type == kMicType && settings.is_object() && settings.contains("microfono");
}

std::string ajustesAudioAnillo(const std::string& canal, int slot) {
    const std::string c = canal == "remoto" ? "remoto" : "guest";
    const std::string anillo = (c == "remoto" ? "BA.Audio.Remoto." : "BA.Audio.Guest.") + std::to_string(slot);
    // estudio=false: el audio del invitado va con su propia demora, como su video
    // (bavideoanillo): los dos llevan la hora que les pone el gateway.
    return volcar(json{{"pipeline", ""},     {"pipeline_audio", "baanillo canal=" + anillo},
                       {"audio", true},      {"anillo", c},
                       {"slot", slot},       {"estudio", false}});
}

std::string tipoLogico(const std::string& type, const json& settings) {
    if (type == kMicType && settings.is_object()) {
        if (settings.contains("camara")) return kVideoType;
        if (settings.value("video_invitado", false)) return "ba_guest_video";
        if (settings.value("fisica", false)) return "ba_entrada_fisica";
        if (settings.value("virtual", false)) return "ba_entrada_virtual";
        if (settings.contains("voz")) return "ba_voz_asistente";
        if (settings.value("ndi", false)) return "ba_ndi_input";
        if (settings.contains("anillo")) return "ba_guest_audio";
        if (settings.contains("microfono")) return kTipoMicViejo;
    }
    return type;
}

std::string ajustesVideoInvitado(int slot, const std::string& channel) {
    const std::string c = (channel == "share" || channel == "remoto" || channel == "remoto_contenido") ? channel : "guest";
    const std::string canal = (c == "share"              ? "BA.GuestShare."
                               : c == "remoto"           ? "BA.Remoto."
                               : c == "remoto_contenido" ? "BA.RemotoContenido."
                                                         : "BA.Guest.") +
                              std::to_string(slot);
    // Sin normalizar (llega BGRA a los cuadros que lleguen: sin videorate ni
    // conversión) y fuera de la compensación del estudio, como su audio
    // (baanillo): los dos van por la hora que les pone el gateway.
    return volcar(json{{"pipeline", "bavideoanillo canal=" + canal},
                       {"pipeline_audio", ""},
                       {"audio", false},
                       {"normalizar", false},
                       {"estudio", false},
                       {"video_invitado", true},
                       {"slot", slot},
                       {"channel", c}});
}

SourceSpec videoInvitadoSpec(int slot, const std::string& channel, const std::string& name) {
    SourceSpec spec;
    spec.id = kMicType;
    spec.name = name;
    spec.settingsJson = ajustesVideoInvitado(slot, channel);
    spec.rect = CanvasRect{0.0f, 0.0f, 1.0f, 1.0f};
    return spec;
}

bool esVideoInvitado(const std::string& type, const std::string& settingsJson) {
    if (type == "ba_guest_video") return true;
    if (type != kMicType) return false;
    return leer(settingsJson).value("video_invitado", false);
}

std::string ajustesNdi(const std::string& nombre, const std::string& host, int port) {
    std::string esc;
    for (char c : nombre) {
        if (c == '\\' || c == '"') esc += '\\';
        esc += c;
    }
    std::string fuente = "bandisrc name=ba_ndi fuente=\"" + esc + "\"";
    if (!host.empty()) fuente += " host=" + host;
    if (port > 0) fuente += " port=" + std::to_string(port);
    // La cámara NDI es una cámara del lugar: normalizada y con el estudio.
    return volcar(json{{"pipeline", fuente + " ba_ndi.video ! avdec_speedhq"},
                       {"pipeline_audio", "ba_ndi.audio"},
                       {"audio", true},
                       {"normalizar", true},
                       {"ndi", true},
                       {"ndi_name", nombre},
                       {"host", host},
                       {"port", port}});
}

namespace {

/// Lo que queda de `viejo` fuera de `usadas`: las opciones que no llegan al motor.
/// `inofensivas`: las que con ESE valor no hacen nada (lo de fábrica de OBS).
void sobrantes(const json& viejo, std::initializer_list<const char*> usadas, const json& inofensivas,
               std::vector<std::string>& out) {
    for (const auto& [k, v] : viejo.items()) {
        if (std::any_of(usadas.begin(), usadas.end(), [&](const char* u) { return k == u; })) continue;
        if (inofensivas.contains(k) && inofensivas[k] == v) continue;
        out.push_back(k);
    }
}

/// Una entrada de red de ffmpeg_source (la de antes) en los términos del motor.
json redDeFfmpeg(const json& s, std::vector<std::string>& sinEfecto) {
    json a{{"uri", s.value("input", std::string())}, {"audio", true}};
    // buffering_mb lo escribía networkSpec al lado de nobuffer: 1 MB = baja latencia.
    bool baja = s.value("buffering_mb", 4) <= 1;
    std::istringstream opciones(s.value("ffmpeg_options", std::string()));
    for (std::string o; opciones >> o;) {
        const auto igual = o.find('=');
        const std::string k = o.substr(0, igual), v = igual == std::string::npos ? std::string() : o.substr(igual + 1);
        if (k == "rtsp_transport" && (v == "tcp" || v == "udp")) a["rtsp_transporte"] = v;
        else if ((k == "fflags" && v == "nobuffer") || (k == "flags" && v == "low_delay")) baja = true;
        else if (k == "ca_file" && !v.empty()) a["tls_ca"] = v;
        else if (!(k == "tls_verify" && v == "1")) sinEfecto.push_back("ffmpeg_options:" + o); // con ca_file, el motor verifica
    }
    a["latencia"] = latenciaDeModo(baja);
    if (s.contains("reconnect_delay_sec") && s["reconnect_delay_sec"].is_number())
        a["reconectar_s"] = std::clamp(s["reconnect_delay_sec"].get<int>(), 0, 60);
    sobrantes(s, {"input", "is_local_file", "ffmpeg_options", "buffering_mb", "reconnect_delay_sec"},
              json{{"input_format", ""},
                   {"hw_decode", true}, // GStreamer elige el decodificador: no se puede forzar por software
                   {"restart_on_activate", false},
                   {"close_when_inactive", false},
                   {"clear_on_media_end", false}},
              sinEfecto);
    return a;
}

} // namespace

AlMotor alMotor(SourceSpec& spec) {
    AlMotor r;
    const json s = leer(spec.settingsJson);
    const auto pasar = [&](const char* tipo, const json& ajustes) {
        spec.id = tipo;
        spec.settingsJson = volcar(ajustes);
        r.cambio = true;
    };
    if (spec.id == kVideoType) {
        // La cámara de antes: su id de entonces al estable de GStreamer (video/Camaras).
        const SourceSpec nueva = hardwareSpec(video::idCamaraDesdeLibobs(s.value(kVideoProp, std::string())), spec.name, json::object());
        sobrantes(s, {kVideoProp}, json::object(), r.sinEfecto);
        pasar(kMicType, leer(nueva.settingsJson));
    } else if (spec.id == kTipoMicViejo) {
        const SourceSpec nuevo = audioInputSpec(audio::idDesdeLibobs(s.value(kAudioInProp, std::string("default"))), spec.name);
        sobrantes(s, {kAudioInProp}, json::object(), r.sinEfecto);
        pasar(kMicType, leer(nuevo.settingsJson));
    } else if (spec.id == kMicType && s.contains("microfono") && !s.contains("pipeline")) {
        // Uno de GStreamer guardado sin `pipeline` (2026-10-05, antes de que se
        // escribiera): ba_gst_fuente le armaba el video de prueba de fábrica.
        json a = s;
        a["pipeline"] = "";
        pasar(kMicType, a);
    } else if (spec.id == "ffmpeg_source" && s.value("is_local_file", true)) {
        json a = json::object();
        for (const char* k : {"local_file", "looping", "clear_on_media_end"})
            if (s.contains(k)) a[k] = s[k];
        sobrantes(s, {"local_file", "looping", "clear_on_media_end", "is_local_file"},
                  json{{"restart_on_activate", true}, {"close_when_inactive", false}, {"hw_decode", true},
                       {"speed_percent", 100}, {"input_format", ""}},
                  r.sinEfecto);
        pasar(kMedioType, a);
    } else if (spec.id == "ffmpeg_source") {
        pasar(kMicType, redDeFfmpeg(s, r.sinEfecto));
    } else if (spec.id == "ba_guest_video") {
        sobrantes(s, {"slot", "channel"}, json::object(), r.sinEfecto);
        pasar(kMicType, leer(ajustesVideoInvitado(s.value("slot", 1), s.value("channel", std::string("guest")))));
    } else if (spec.id == "ba_guest_audio") {
        sobrantes(s, {"slot", "channel"}, json::object(), r.sinEfecto);
        pasar(kMicType, leer(ajustesAudioAnillo(s.value("channel", std::string("guest")), s.value("slot", 1))));
    } else if (spec.id == "ba_ndi_input") {
        sobrantes(s, {"ndi_name", "host", "port"}, json::object(), r.sinEfecto);
        pasar(kMicType, leer(ajustesNdi(s.value("ndi_name", std::string()), s.value("host", std::string()), s.value("port", 0))));
    } else if (spec.id == "color_source") {
        pasar("color_source_v3", s); // mismos ajustes
    } else if (spec.id == "text_ft2_source" || spec.id == "text_gdiplus" || spec.id == "text_gdiplus_v2" || spec.id == "ba_texto") {
        // Al texto de siempre (text_ft2_source_v2): el de GDI+ guardaba el color en `color`;
        // ba_texto iba siempre en negrita (bandera 1 de la letra).
        json a = s;
        if (!a.contains("color1") && a.contains("color")) a["color1"] = a["color"];
        a.erase("color");
        if (spec.id == "ba_texto") {
            json f = a.value("font", json::object());
            f["flags"] = f.value("flags", 0) | 1;
            a["font"] = f;
        }
        sobrantes(a, {"text", "font", "color1", "outline"}, json::object(), r.sinEfecto);
        pasar("text_ft2_source_v2", a);
    }
    if (!r.sinEfecto.empty())
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "emision", 0,
                                   ba::i18n::llenar(ba::i18n::traducir("emision", "fuente '%1': sin efecto en el motor: %2"),
                                                    {spec.name, r.lista()}));
    return r;
}

SourceSpec networkSpec(const std::string& url, const std::string& name, const json& opts,
                       const std::string& caFile) {
    const std::string sch = networkScheme(url);
    const bool baja = opts.value("latency", ajustes::texto("entradas.redModo", "low")) == "low";
    json s{{"uri", url}, {"audio", true}, {"latencia", latenciaDeModo(baja)},
           {"reconectar_s", std::clamp(opts.value("reconnectSec", 5), 0, 60)}};
    if (sch == "rtsp" || sch == "rtsps")
        s["rtsp_transporte"] = opts.value("rtspTransport", ajustes::texto("entradas.rtspTransporte", "tcp")) == "udp" ? "udp" : "tcp";
    // TLS: se verifica el certificado del servidor con las raices del bundle.
    if ((sch == "https" || sch == "rtmps" || sch == "rtsps") && !caFile.empty()) s["tls_ca"] = caFile;
    SourceSpec spec;
    spec.id = kMicType;
    spec.name = name;
    spec.settingsJson = volcar(s);
    spec.muted = !opts.value("audio", true);
    spec.rect = CanvasRect{0.0f, 0.0f, 1.0f, 1.0f};
    return spec;
}

std::string redactUrl(std::string url) {
    const auto at = url.find('@');
    const auto sc = url.find("://");
    if (at != std::string::npos && sc != std::string::npos && at > sc)
        url = url.substr(0, sc + 3) + "***@" + url.substr(at + 1);
    return url;
}

bool isNetworkInput(const std::string& type, const json& settings) {
    // ba_gst_fuente: las de red (video_entrada_red, camara_agregar, network_input_add)
    // y las de los plugins de Python.
    // Los micrófonos, las cámaras del equipo y el audio de los invitados también
    // son ba_gst_fuente, pero no son «de red».
    if (type == kMicType && settings.is_object() &&
        (settings.contains("microfono") || settings.contains("camara") || settings.contains("anillo") ||
         settings.contains("video_invitado") || settings.contains("voz")))
        return false;
    return type == "ba_ndi_input" || type == kMicType ||
           (type == "ffmpeg_source" && settings.is_object() && !settings.value("is_local_file", true));
}

} // namespace ba::app::inputs
