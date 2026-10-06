// ============================================================================
// src/app/AppSettings.cpp — ver AppSettings.hpp
// ============================================================================

#include "AppSettings.hpp"
#include "InputSpecs.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>

namespace ba::app {

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

template <typename T> T get(const json& j, const char* key, T def) {
    if (!j.is_object() || !j.contains(key)) {
        return def;
    }
    try {
        return j.at(key).get<T>();
    } catch (...) {
        return def;
    }
}

} // namespace

std::string& configRecuperada() {
    static std::string r;
    return r;
}

namespace {

json readJson(const fs::path& path, bool& exists) {
    std::error_code ec;
    exists = fs::exists(path, ec);
    if (!exists) {
        return json::object();
    }
    std::ifstream in(path);
    json j = json::parse(in, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
    if (j.is_discarded() || !j.is_object()) {
        // Dos generaciones: si la actual está rota, la anterior (la que había
        // antes del último guardado) suele estar bien.
        fs::path prev = path;
        prev += ".anterior";
        std::ifstream pin(prev);
        json pj = json::parse(pin, nullptr, false, true);
        if (pj.is_object()) {
            std::error_code cec;
            fs::path backup = path;
            backup += ".corrupto";
            fs::copy_file(path, backup, fs::copy_options::overwrite_existing, cec);
            std::printf("ba-app: %s no es JSON valido; se cargo la generacion anterior (%s)\n", path.string().c_str(),
                        prev.string().c_str());
            configRecuperada() = path.filename().string();
            return pj;
        }
        // El proximo guardado pisaria el archivo roto con los valores por
        // defecto: se deja una copia para poder recuperar escenas a mano.
        std::error_code cec;
        fs::path backup = path;
        backup += ".corrupto";
        fs::copy_file(path, backup, fs::copy_options::overwrite_existing, cec);
        std::printf("ba-app: %s no es JSON valido; se usan valores por defecto (copia en %s)\n",
                    path.string().c_str(), backup.string().c_str());
        return json::object();
    }
    return j;
}

/// Escritura atomica: un corte a mitad nunca deja un archivo roto.
bool writeJson(const fs::path& path, const json& j, std::string* err) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    const fs::path tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            if (err != nullptr) {
                *err = "no se pudo escribir " + tmp.string();
            }
            return false;
        }
        out << j.dump(2) << "\n";
    }
    // La generación anterior: la que estaba (si era JSON válido) queda como .anterior.
    if (fs::exists(path, ec)) {
        std::ifstream cur(path);
        const json cj = json::parse(cur, nullptr, false, true);
        if (cj.is_object()) {
            fs::path prev = path;
            prev += ".anterior";
            fs::copy_file(path, prev, fs::copy_options::overwrite_existing, ec);
        }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        if (err != nullptr) {
            *err = "no se pudo reemplazar " + path.string() + ": " + ec.message();
        }
        return false;
    }
    return true;
}

SourceSpec sourceFromJson(const json& s) {
    SourceSpec spec;
    spec.id = get<std::string>(s, "id", "");
    spec.name = get<std::string>(s, "name", spec.id);
    spec.settingsJson = s.value("settings", json::object()).dump();
    spec.visible = get(s, "visible", true);
    spec.volume = get(s, "volume", 1.0f);
    spec.muted = get(s, "muted", false);
    const json r = s.value("rect", json::array({0.0, 0.0, 1.0, 1.0}));
    if (r.is_array() && r.size() == 4) {
        spec.rect = CanvasRect{r[0].get<float>(), r[1].get<float>(), r[2].get<float>(), r[3].get<float>()};
    }
    const json rc = s.value("recorte", json());
    if (rc.is_array() && rc.size() == 4)
        for (int i = 0; i < 4; ++i)
            spec.recorte[i] = rc[i].is_number() ? rc[i].get<float>() : 0.0f;
    return spec;
}

json sourceToJson(const SourceSpec& s) {
    json settings = json::parse(s.settingsJson.empty() ? "{}" : s.settingsJson, nullptr, false);
    if (settings.is_discarded()) {
        settings = json::object();
    }
    json j{{"id", s.id},
           {"name", s.name},
           {"settings", settings},
           {"visible", s.visible},
           {"volume", s.volume},
           {"muted", s.muted},
           {"rect", {s.rect.x, s.rect.y, s.rect.w, s.rect.h}}};
    if (s.recorte[0] + s.recorte[1] + s.recorte[2] + s.recorte[3] > 0)
        j["recorte"] = {s.recorte[0], s.recorte[1], s.recorte[2], s.recorte[3]};
    return j;
}

std::string relativeToRoot(const fs::path& p, const fs::path& root) {
    const fs::path rel = p.lexically_relative(root);
    return (!rel.empty() && *rel.begin() != "..") ? rel.generic_string() : p.string();
}

} // namespace

fs::path appConfigPath(const fs::path& dataRoot) { return dataRoot / "Config" / "app.json"; }
fs::path gatewayConfigPath(const fs::path& dataRoot) { return dataRoot / "Config" / "gateway.json"; }
fs::path launcherConfigPath(const fs::path& dataRoot) { return dataRoot / "Config" / "launcher.json"; }

AppConfig defaultAppConfig(const fs::path& dataRoot) {
    AppConfig c;
    c.rec.directory = (dataRoot / "Recordings").string();
    SceneConfig main;
    main.name = "Principal";
    SourceSpec bg;
    bg.id = "color_source_v3";
    bg.name = "Fondo";
    bg.settingsJson = "{\"color\":4280295456,\"width\":" + std::to_string(c.video.baseWidth) +
                      ",\"height\":" + std::to_string(c.video.baseHeight) + "}"; // del tamaño del lienzo
    main.sources.push_back(bg);
    SourceSpec title;
    title.id = "text_ft2_source_v2";
    title.name = "Titulo";
    title.settingsJson = std::string(R"({"text":"Broadcast Appliance","font":{"face":")") + kDefaultFontFace +
                         R"(","size":96}})";
    title.rect = CanvasRect{0.05f, 0.05f, 0.6f, 0.12f};
    main.sources.push_back(title);
    c.scenes.push_back(main);
    c.currentScene = main.name;
    return c;
}

AppConfig loadAppConfig(const fs::path& path, const fs::path& dataRoot) {
    bool exists = false;
    const json j = readJson(path, exists);
    AppConfig c = defaultAppConfig(dataRoot);
    c.configPath = path;
    if (!exists) {
        std::string err;
        if (saveAppConfig(path, c, dataRoot, &err)) {
            std::printf("ba-app: configuracion por defecto escrita en %s\n", path.string().c_str());
        }
        return c;
    }

    const json video = j.value("video", json::object());
    c.video.baseWidth = get(video, "width", c.video.baseWidth);
    c.video.baseHeight = get(video, "height", c.video.baseHeight);
    c.video.outputWidth = get(video, "outputWidth", c.video.baseWidth);
    c.video.outputHeight = get(video, "outputHeight", c.video.baseHeight);
    // fps: un número (30) o "30000/1001" / "29.97" para las cadencias NTSC.
    if (video.contains("fps") && video["fps"].is_string()) {
        if (!fpsDeTexto(video["fps"].get<std::string>(), c.video.fpsNum, c.video.fpsDen)) c.video.fpsNum = 30, c.video.fpsDen = 1;
    } else {
        c.video.fpsNum = get(video, "fps", 30);
        c.video.fpsDen = 1;
    }
    c.video.formato = get(video, "formato", c.video.formato);
    c.video.color = get(video, "color", c.video.color);
    c.video.rango = get(video, "rango", c.video.rango);
    c.legalizar = get<std::string>(video, "legalizar", "no");
    const json mej = video.value("mejora", json::object());
    c.mejora.activa = get(mej, "activa", c.mejora.activa);
    c.mejora.ruido = get(mej, "ruido", c.mejora.ruido);
    c.mejora.color = get(mej, "color", c.mejora.color);
    c.mejora.nitidez = get(mej, "nitidez", c.mejora.nitidez);

    const json enc = j.value("encoder", json::object());
    c.encoder.videoEncoder = get<std::string>(enc, "video", "auto");
    c.encoder.videoBitrateKbps = get(enc, "bitrateKbps", 6000);
    c.encoder.audioBitrateKbps = get(enc, "audioBitrateKbps", 160);
    c.encoder.keyintSec = get(enc, "keyintSec", 2);
    c.encoder.preset = get<std::string>(enc, "preset", "veryfast");
    c.encoder.rateControl = get<std::string>(enc, "rateControl", "CBR");

    const json rec = j.value("record", json::object());
    c.record = get(rec, "enabled", false);
    const fs::path recDir = get<std::string>(rec, "dir", "Recordings");
    c.rec.directory = (recDir.is_absolute() ? recDir : dataRoot / recDir).string();
    c.rec.container = get<std::string>(rec, "container", "mkv");
    c.rec.remuxAlTerminar = get(rec, "remux", true);
    c.rec.calidad = get<std::string>(rec, "calidad", "como_stream");
    c.rec.calidadCrf = get(rec, "crf", 18);
    c.rec.pistas.clear();
    if (rec.contains("pistas") && rec["pistas"].is_array())
        for (const auto& p : rec["pistas"])
            if (p.is_number_integer()) c.rec.pistas.push_back(p.get<int>());
    {
        const std::string copia = get<std::string>(rec, "copiaDir", "");
        c.rec.copiaDir = copia.empty() || fs::path(copia).is_absolute() ? copia : (dataRoot / copia).string();
    }
    c.replaySegundos = get(rec, "replaySegundos", 0);

    const json st = j.value("stream", json::object());
    c.stream = get(st, "enabled", false);
    c.streamCfg.server = get<std::string>(st, "server", "");
    c.streamKeyEnv = get<std::string>(st, "keyEnv", "BA_STREAM_KEY");

    const json g = j.value("guests", json::object());
    c.guests = std::clamp(get(g, "count", 2), 0, 16);
    c.guestVideo = get(g, "video", true);
    c.guestAudio = get(g, "audio", true);
    c.guestAudioPreset = get<std::string>(g, "audioPreset", "podium");
    c.returnFeeds = get(j, "returnFeeds", true);
    const json conf = j.value("conference", json::object());
    c.operatorVideo = get<std::string>(conf, "operatorVideo", "");
    c.operatorAudio = get<std::string>(conf, "operatorAudio", "");

    const json tr = j.value("transition", json::object());
    c.transition.type = get<std::string>(tr, "type", "fade");
    c.transition.durationMs = get(tr, "durationMs", 300);
    const json ui = j.value("ui", json::object());
    c.theme = get<std::string>(ui, "theme", "dark");
    c.balistica = get<std::string>(ui, "balistica", "pico");
    c.previewWidth = get(ui, "previewWidth", 960);
    const json mcp = j.value("mcp", json::object());
    c.mcpEnabled = get(mcp, "enabled", true);
    c.mcpPort = std::clamp(get(mcp, "port", 8790), 1024, 65535);
    c.mcpApproval = get<std::string>(mcp, "approval", "invitados");
    if (c.mcpApproval != "siempre" && c.mcpApproval != "nunca") c.mcpApproval = "invitados";

    if (j.contains("scenes") && j["scenes"].is_array()) {
        c.scenes.clear();
        for (const auto& sc : j["scenes"]) {
            SceneConfig scene;
            scene.name = get<std::string>(sc, "name", "Escena");
            scene.folder = get<std::string>(sc, "folder", "");
            scene.protegida = get<bool>(sc, "protegida", false);
            for (const auto& s : sc.value("sources", json::array())) {
                SourceSpec spec = sourceFromJson(s);
                // Las cámaras de libobs de antes pasan a GStreamer (mismo nombre,
                // lugar y recorte: las tarjetas, el PTZ y los vínculos siguen).
                if (inputs::migrarCamara(spec))
                    std::printf("ba-app: cámara '%s' pasada a GStreamer\n", spec.name.c_str());
                if (inputs::migrarMedio(spec))
                    std::printf("ba-app: medio '%s' pasado a GStreamer\n", spec.name.c_str());
                if (inputs::migrarVideoInvitado(spec))
                    std::printf("ba-app: video de invitado '%s' pasado a GStreamer\n", spec.name.c_str());
                if (inputs::migrarNdi(spec)) std::printf("ba-app: NDI '%s' pasado a GStreamer\n", spec.name.c_str());
                scene.sources.push_back(spec);
            }
            c.scenes.push_back(scene);
        }
    } else if (j.contains("sources") && j["sources"].is_array()) {
        // Formato viejo: una lista de fuentes sin escenas.
        c.scenes.clear();
        SceneConfig scene;
        scene.name = "Principal";
        for (const auto& s : j["sources"]) {
            scene.sources.push_back(sourceFromJson(s));
        }
        c.scenes.push_back(scene);
    }
    if (j.contains("audioInputs") && j["audioInputs"].is_array()) {
        c.audioInputs.clear();
        for (const auto& s : j["audioInputs"]) {
            SourceSpec spec = sourceFromJson(s);
            // Los micrófonos de libobs de antes pasan a GStreamer (mismo nombre,
            // volumen y silencio: los racks y los vínculos siguen andando).
            if (inputs::migrarMicrofono(spec))
                std::printf("ba-app: micrófono '%s' pasado a GStreamer\n", spec.name.c_str());
            if (inputs::migrarMedio(spec)) std::printf("ba-app: medio '%s' pasado a GStreamer\n", spec.name.c_str());
            c.audioInputs.push_back(spec);
        }
    }
    if (c.scenes.empty()) {
        c.scenes.push_back(SceneConfig{"Principal", {}});
    }
    c.currentScene = get<std::string>(j, "currentScene", c.scenes.front().name);
    if (findScene(c, c.currentScene) == nullptr) {
        c.currentScene = c.scenes.front().name;
    }
    return c;
}

bool saveAppConfig(const fs::path& path, const AppConfig& c, const fs::path& dataRoot, std::string* err) {
    json audioIns = json::array();
    for (const auto& s : c.audioInputs) {
        audioIns.push_back(sourceToJson(s));
    }
    json scenes = json::array();
    for (const auto& sc : c.scenes) {
        json sources = json::array();
        for (const auto& s : sc.sources) {
            sources.push_back(sourceToJson(s));
        }
        json j{{"name", sc.name}, {"folder", sc.folder}, {"sources", sources}};
        // Solo si esta: un app.json lleno de "protegida": false no dice nada.
        if (sc.protegida) j["protegida"] = true;
        scenes.push_back(std::move(j));
    }
    const json j{
        {"video",
         {{"width", c.video.baseWidth},
          {"height", c.video.baseHeight},
          {"outputWidth", c.video.outputWidth},
          {"outputHeight", c.video.outputHeight},
          {"fps", c.video.fpsDen <= 1 ? json(c.video.fpsNum) : json(textoDeFps(c.video.fpsNum, c.video.fpsDen))},
          {"formato", c.video.formato},
          {"color", c.video.color},
          {"rango", c.video.rango},
          {"legalizar", c.legalizar},
          {"mejora",
           {{"activa", c.mejora.activa},
            {"ruido", c.mejora.ruido},
            {"color", c.mejora.color},
            {"nitidez", c.mejora.nitidez}}}}},
        {"encoder",
         {{"video", c.encoder.videoEncoder},
          {"bitrateKbps", c.encoder.videoBitrateKbps},
          {"audioBitrateKbps", c.encoder.audioBitrateKbps},
          {"keyintSec", c.encoder.keyintSec},
          {"preset", c.encoder.preset},
          {"rateControl", c.encoder.rateControl}}},
        {"record",
         {{"enabled", c.record},
          {"dir", relativeToRoot(c.rec.directory, dataRoot)},
          {"container", c.rec.container},
          {"remux", c.rec.remuxAlTerminar},
          {"calidad", c.rec.calidad},
          {"crf", c.rec.calidadCrf},
          {"pistas", c.rec.pistas},
          {"copiaDir", c.rec.copiaDir.empty() ? std::string() : relativeToRoot(c.rec.copiaDir, dataRoot)},
          {"replaySegundos", c.replaySegundos}}},
        {"stream", {{"enabled", c.stream}, {"server", c.streamCfg.server}, {"keyEnv", c.streamKeyEnv}}},
        {"guests",
         {{"count", c.guests},
          {"video", c.guestVideo},
          {"audio", c.guestAudio},
          {"audioPreset", c.guestAudioPreset}}},
        {"returnFeeds", c.returnFeeds},
        {"conference", {{"operatorVideo", c.operatorVideo}, {"operatorAudio", c.operatorAudio}}},
        {"transition", {{"type", c.transition.type}, {"durationMs", c.transition.durationMs}}},
        {"ui", {{"theme", c.theme}, {"previewWidth", c.previewWidth}, {"balistica", c.balistica}}},
        {"mcp", {{"enabled", c.mcpEnabled}, {"port", c.mcpPort}, {"approval", c.mcpApproval}}},
        {"scenes", scenes},
        {"audioInputs", audioIns},
        {"currentScene", c.currentScene},
    };
    return writeJson(path, j, err);
}

GatewaySettings loadGatewaySettings(const fs::path& path) {
    bool exists = false;
    const json j = readJson(path, exists);
    GatewaySettings s;
    s.guests = get(j, "guests", s.guests);
    s.guestsReserva = get(j, "guestsReserva", s.guestsReserva);
    s.port = get(j, "port", s.port);
    s.bindAddress = get<std::string>(j, "bindAddress", s.bindAddress);
    s.room = get<std::string>(j, "room", s.room);
    s.tunnel = get(j, "tunnel", s.tunnel);
    s.tunnelHostname = get<std::string>(j, "tunnelHostname", s.tunnelHostname);
    s.metricsPort = get(j, "metricsPort", s.metricsPort);
    s.videoW = get(j, "videoW", s.videoW);
    s.videoH = get(j, "videoH", s.videoH);
    s.guestVideoW = get(j, "guestVideoW", s.guestVideoW);
    s.guestVideoH = get(j, "guestVideoH", s.guestVideoH);
    s.adminPort = get(j, "adminPort", s.adminPort);
    s.lobby = get(j, "lobby", s.lobby);
    s.autoIntake = get(j, "autoIntake", s.autoIntake);
    s.lobbySlots = std::clamp(get(j, "lobbySlots", s.lobbySlots), 0, 8);
    s.maxBackstage = std::clamp(get(j, "maxBackstage", s.maxBackstage), 0, 32);
    s.backstagePreviews = std::clamp(get(j, "backstagePreviews", s.backstagePreviews), 0, 8);
    s.maxUploadMB = get(j, "maxUploadMB", s.maxUploadMB);
    s.https = get(j, "https", s.https);
    s.httpsPort = get(j, "httpsPort", s.httpsPort);
    s.rtcPortMin = get(j, "rtcPortMin", s.rtcPortMin);
    s.rtcPortMax = get(j, "rtcPortMax", s.rtcPortMax);
    const json w = j.value("wireguard", json::object());
    s.wgEnabled = get(w, "enabled", s.wgEnabled);
    s.wgEndpoint = get<std::string>(w, "endpoint", s.wgEndpoint);
    s.wgServerPublicKey = get<std::string>(w, "serverPublicKey", s.wgServerPublicKey);
    s.wgAddress = get<std::string>(w, "address", s.wgAddress);
    s.wgPublicAddress = get<std::string>(w, "publicAddress", s.wgPublicAddress);
    s.wgKeepalive = get(w, "keepalive", s.wgKeepalive);
    if (j.contains("proxy") && j["proxy"].is_object()) s.proxy = j["proxy"];
    return s;
}

bool saveGatewaySettings(const fs::path& path, const GatewaySettings& s, std::string* err) {
    return writeJson(path,
                     json{{"guests", s.guests},
                          {"guestsReserva", std::max(s.guestsReserva, s.guests)},
                          {"port", s.port},
                          {"bindAddress", s.bindAddress},
                          {"room", s.room},
                          {"tunnel", s.tunnel},
                          {"tunnelHostname", s.tunnelHostname},
                          {"metricsPort", s.metricsPort},
                          {"videoW", s.videoW},
                          {"videoH", s.videoH},
                          {"guestVideoW", s.guestVideoW},
                          {"guestVideoH", s.guestVideoH},
                          {"adminPort", s.adminPort},
                          {"lobby", s.lobby},
                          {"autoIntake", s.autoIntake},
                          {"lobbySlots", s.lobbySlots},
                          {"maxBackstage", s.maxBackstage},
                          {"backstagePreviews", s.backstagePreviews},
                          {"maxUploadMB", s.maxUploadMB},
                          {"https", s.https},
                          {"httpsPort", s.httpsPort},
                          {"rtcPortMin", s.rtcPortMin},
                          {"rtcPortMax", s.rtcPortMax},
                          {"wireguard",
                           {{"enabled", s.wgEnabled},
                            {"endpoint", s.wgEndpoint},
                            {"serverPublicKey", s.wgServerPublicKey},
                            {"address", s.wgAddress},
                            {"publicAddress", s.wgPublicAddress},
                            {"keepalive", s.wgKeepalive}}},
                          {"proxy", s.proxy}},
                     err);
}

LauncherSettings loadLauncherSettings(const fs::path& path) {
    bool exists = false;
    const json j = readJson(path, exists);
    LauncherSettings s;
    s.shadowCopy = get(j, "shadowCopy", s.shadowCopy);
    s.recoveryUi = get(j, "recoveryUi", s.recoveryUi);
    return s;
}

bool saveLauncherSettings(const fs::path& path, const LauncherSettings& s, std::string* err) {
    // Se preserva lo que no maneja la interfaz (appArgs/gatewayArgs).
    bool exists = false;
    json j = readJson(path, exists);
    j["shadowCopy"] = s.shadowCopy;
    j["recoveryUi"] = s.recoveryUi;
    return writeJson(path, j, err);
}

SceneConfig* findScene(AppConfig& cfg, const std::string& name) {
    for (auto& s : cfg.scenes) {
        if (s.name == name) {
            return &s;
        }
    }
    return nullptr;
}

} // namespace ba::app
