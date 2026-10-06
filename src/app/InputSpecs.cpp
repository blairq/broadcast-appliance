// ============================================================================
// src/app/InputSpecs.cpp — ver InputSpecs.hpp
// ============================================================================

#include "InputSpecs.hpp"

#include "audio/EntradasAudio.hpp"
#include "video/Camaras.hpp"

#include <algorithm>

namespace ba::app::inputs {

using json = nlohmann::json;

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
    spec.settingsJson = s.dump();
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

bool migrarCamara(SourceSpec& spec) {
    if (spec.id != kVideoType) return false;
    const json viejo = json::parse(spec.settingsJson, nullptr, false);
    const std::string dev = video::idCamaraDesdeLibobs(viejo.is_object() ? viejo.value(kVideoProp, std::string()) : "");
    const SourceSpec nuevo = hardwareSpec(dev, spec.name, json::object());
    spec.id = nuevo.id;
    spec.settingsJson = nuevo.settingsJson;
    return true;
}

SourceSpec audioInputSpec(const std::string& device, const std::string& name) {
    SourceSpec spec;
    spec.id = kMicType;
    spec.name = name;
    spec.settingsJson =
        json{{"pipeline", ""}, // sin esto, ba_gst_fuente arma su video de prueba de fábrica
             {"pipeline_audio", audio::pipelineDeMicrofono(device)},
             {"audio", true},
             {"microfono", device}}
            .dump();
    spec.rect = CanvasRect{0.0f, 0.0f, 0.0f, 0.0f}; // solo audio
    return spec;
}

bool esMicrofono(const std::string& type, const json& settings) {
    if (type == kAudioInLibobs) return true;
    return type == kMicType && settings.is_object() && settings.contains("microfono");
}

std::string ajustesAudioAnillo(const std::string& canal, int slot) {
    const std::string c = canal == "remoto" ? "remoto" : "guest";
    const std::string anillo = (c == "remoto" ? "BA.Audio.Remoto." : "BA.Audio.Guest.") + std::to_string(slot);
    // estudio=false: el video del invitado todavía va por libobs sin esperar la
    // compensación del estudio; su audio tampoco la espera (si no, se desfasan).
    return json{{"pipeline", ""},     {"pipeline_audio", "baanillo canal=" + anillo},
                {"audio", true},      {"anillo", c},
                {"slot", slot},       {"estudio", false}}
        .dump();
}

std::string tipoLogico(const std::string& type, const json& settings) {
    if (type == kMicType && settings.is_object()) {
        if (settings.contains("camara")) return kVideoType;
        if (settings.contains("anillo")) return "ba_guest_audio";
        if (settings.contains("microfono")) return kAudioInLibobs;
    }
    return type;
}

bool migrarMedio(SourceSpec& spec) {
    if (spec.id != "ffmpeg_source") return false;
    const json s = json::parse(spec.settingsJson.empty() ? "{}" : spec.settingsJson, nullptr, false);
    if (s.is_object() && !s.value("is_local_file", true)) return false; // de red: queda
    spec.id = kMedioType;
    return true;
}

bool migrarMicrofono(SourceSpec& spec) {
    if (spec.id == kMicType) {
        // Uno de GStreamer guardado sin `pipeline` (2026-10-05, antes de que se
        // escribiera): ba_gst_fuente le armaba el video de prueba de fábrica.
        json s = json::parse(spec.settingsJson, nullptr, false);
        if (!s.is_object() || !s.contains("microfono") || s.contains("pipeline")) return false;
        s["pipeline"] = "";
        spec.settingsJson = s.dump();
        return true;
    }
    if (spec.id != kAudioInLibobs) return false;
    const json viejo = json::parse(spec.settingsJson, nullptr, false);
    const std::string dev =
        audio::idDesdeLibobs(viejo.is_object() ? viejo.value(kAudioInProp, std::string("default")) : "default");
    const SourceSpec nuevo = audioInputSpec(dev, spec.name);
    spec.id = nuevo.id;
    spec.settingsJson = nuevo.settingsJson;
    return true;
}

SourceSpec networkSpec(const std::string& url, const std::string& name, const json& opts,
                       const std::string& caFile) {
    const std::string sch = networkScheme(url);
    const bool low = opts.value("latency", std::string("low")) == "low";
    std::string ff;
    if (sch == "rtsp" || sch == "rtsps")
        ff += "rtsp_transport=" + opts.value("rtspTransport", std::string("tcp")) + " ";
    if (low) ff += "fflags=nobuffer flags=low_delay ";
    // TLS: se verifica el certificado del servidor con las raices del bundle.
    if ((sch == "https" || sch == "rtmps" || sch == "rtsps") && !caFile.empty())
        ff += "ca_file=" + caFile + " tls_verify=1 ";
    json s{{"is_local_file", false},
           {"input", url},
           {"input_format", ""},
           {"hw_decode", opts.value("hwDecode", true)},
           {"reconnect_delay_sec", opts.value("reconnectSec", 5)},
           {"buffering_mb", low ? 1 : 4},
           {"restart_on_activate", false},
           {"close_when_inactive", false},
           {"clear_on_media_end", false},
           {"ffmpeg_options", ff}};
    SourceSpec spec;
    spec.id = "ffmpeg_source";
    spec.name = name;
    spec.settingsJson = s.dump();
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
    // ba_gst_fuente: las de red sincronizadas (video_entrada_red, camara_agregar)
    // y las de los plugins de Python.
    // Los micrófonos, las cámaras del equipo y el audio de los invitados también
    // son ba_gst_fuente, pero no son «de red».
    if (type == "ba_gst_fuente" && settings.is_object() &&
        (settings.contains("microfono") || settings.contains("camara") || settings.contains("anillo")))
        return false;
    return type == "ba_ndi_input" || type == "ba_gst_fuente" ||
           (type == "ffmpeg_source" && !settings.value("is_local_file", true));
}

} // namespace ba::app::inputs
