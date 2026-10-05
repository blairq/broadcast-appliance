// ============================================================================
// src/app/InputSpecs.cpp — ver InputSpecs.hpp
// ============================================================================

#include "InputSpecs.hpp"

#include "audio/EntradasAudio.hpp"

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
    spec.id = kVideoType;
    spec.name = name;
    json s{{kVideoProp, device}};
    for (const char* k : {"input", "pixelformat", "resolution", "framerate"})
        if (opts.contains(k) && !opts[k].is_null()) s[k] = opts[k];
    spec.settingsJson = s.dump();
    spec.rect = CanvasRect{0.0f, 0.0f, 1.0f, 1.0f};
    return spec;
}

SourceSpec audioInputSpec(const std::string& device, const std::string& name) {
    SourceSpec spec;
    spec.id = kMicType;
    spec.name = name;
    spec.settingsJson =
        json{{"pipeline_audio", audio::pipelineDeMicrofono(device)}, {"audio", true}, {"microfono", device}}.dump();
    spec.rect = CanvasRect{0.0f, 0.0f, 0.0f, 0.0f}; // solo audio
    return spec;
}

bool esMicrofono(const std::string& type, const json& settings) {
    if (type == kAudioInLibobs) return true;
    return type == kMicType && settings.is_object() && settings.contains("microfono");
}

bool migrarMicrofono(SourceSpec& spec) {
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
    return type == "ba_ndi_input" || type == "ba_gst_fuente" ||
           (type == "ffmpeg_source" && !settings.value("is_local_file", true));
}

} // namespace ba::app::inputs
