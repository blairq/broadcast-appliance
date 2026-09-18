// ============================================================================
// src/app/BroadcastEngineScenes.cpp — Escenas, transiciones, fuentes y audio
//
// Parte de BroadcastEngine (ver BroadcastEngine.hpp): todo lo que arma lo que
// sale al aire. El canal de salida 0 de libobs es una TRANSICION cuyo destino
// es la escena actual; cambiar de escena es arrancar la transicion. El audio
// de los invitados es global (canales 1..4): suena en cualquier escena.
//
// Referencias: el motor guarda UNA referencia propia por escena y por fuente
// que crea; las fuentes reutilizadas entre escenas son la misma fuente.
// ============================================================================

#include "BroadcastEngine.hpp"

#include <obs.h>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace ba::app {

namespace {

constexpr std::uint32_t kProgramChannel = 0;
constexpr std::uint32_t kFirstGuestAudioChannel = 1; // 1..4: invitados

/// id de libobs de cada transicion.
const char* transitionId(const std::string& type) {
    if (type == "cut") return "cut_transition";
    if (type == "swipe") return "swipe_transition";
    if (type == "slide") return "slide_transition";
    if (type == "fade_to_color") return "fade_to_color_transition";
    return "fade_transition";
}

} // namespace

namespace {

void meterCallback(void* data, const float[MAX_AUDIO_CHANNELS], const float peak[MAX_AUDIO_CHANNELS],
                   const float[MAX_AUDIO_CHANNELS]) {
    auto* peakDb = static_cast<std::atomic<float>*>(data);
    float best = -100.0f;
    for (int i = 0; i < 2; ++i) {
        if (std::isfinite(peak[i])) {
            best = std::max(best, peak[i]);
        }
    }
    peakDb->store(best, std::memory_order_relaxed);
}

} // namespace

BroadcastEngine::Meter::~Meter() {
    if (vm != nullptr) {
        auto* v = static_cast<obs_volmeter_t*>(vm);
        obs_volmeter_remove_callback(v, meterCallback, &peakDb);
        obs_volmeter_detach_source(v);
        obs_volmeter_destroy(v);
    }
}

// ============================================================================
// Escenas y transicion
// ============================================================================

obs_scene* BroadcastEngine::sceneByName(const std::string& name) const {
    for (const auto& [n, sc] : scenes_) {
        if (n == name) {
            return sc;
        }
    }
    return nullptr;
}

bool BroadcastEngine::createScene(const std::string& name) {
    if (!started_ || name.empty() || sceneByName(name) != nullptr) {
        setError("createScene: sin motor, nombre vacio o repetido: '" + name + "'");
        return false;
    }
    obs_scene_t* sc = obs_scene_create(name.c_str());
    if (sc == nullptr) {
        setError("obs_scene_create fallo");
        return false;
    }
    scenes_.emplace_back(name, reinterpret_cast<obs_scene*>(sc)); // referencia de creacion
    return true;
}

bool BroadcastEngine::removeScene(const std::string& name) {
    auto it = std::find_if(scenes_.begin(), scenes_.end(), [&](const auto& p) { return p.first == name; });
    if (it == scenes_.end() || scenes_.size() == 1) {
        setError("removeScene: no existe o es la unica escena");
        return false;
    }
    if (currentScene_ == name) {
        const std::string other = (it == scenes_.begin()) ? (it + 1)->first : scenes_.front().first;
        const std::string keep = transitionType_;
        const int keepMs = transitionMs_;
        setTransition("cut", 0);
        setCurrentScene(other);
        setTransition(keep, keepMs);
    }
    obs_scene_release(reinterpret_cast<obs_scene_t*>(it->second));
    scenes_.erase(it);
    return true;
}

bool BroadcastEngine::renameScene(const std::string& from, const std::string& to) {
    if (to.empty() || sceneByName(to) != nullptr) {
        setError("renameScene: nombre vacio o repetido");
        return false;
    }
    for (auto& [n, sc] : scenes_) {
        if (n == from) {
            obs_source_set_name(obs_scene_get_source(reinterpret_cast<obs_scene_t*>(sc)), to.c_str());
            n = to;
            if (currentScene_ == from) {
                currentScene_ = to;
            }
            return true;
        }
    }
    return false;
}

std::vector<std::string> BroadcastEngine::sceneNames() const {
    std::vector<std::string> out;
    for (const auto& p : scenes_) {
        out.push_back(p.first);
    }
    return out;
}

bool BroadcastEngine::setTransition(const std::string& type, int durationMs) {
    if (!started_) {
        return false;
    }
    transitionMs_ = std::max(0, durationMs);
    if (transition_ != nullptr && type == transitionType_) {
        return true;
    }
    obs_source_t* tr = obs_source_create_private(transitionId(type), "transicion", nullptr);
    if (tr == nullptr) {
        setError("no existe la transicion '" + type + "'");
        return false;
    }
    obs_transition_set_size(tr, static_cast<uint32_t>(video_.baseWidth),
                            static_cast<uint32_t>(video_.baseHeight));
    // La transicion nueva arranca mostrando lo que ya estaba al aire.
    if (obs_scene* cur = sceneByName(currentScene_)) {
        obs_transition_set(tr, obs_scene_get_source(reinterpret_cast<obs_scene_t*>(cur)));
    }
    obs_set_output_source(kProgramChannel, tr);
    if (transition_ != nullptr) {
        obs_source_release(reinterpret_cast<obs_source_t*>(transition_));
    }
    transition_ = reinterpret_cast<obs_source*>(tr);
    transitionType_ = type;
    return true;
}

bool BroadcastEngine::setCurrentScene(const std::string& name) {
    obs_scene* sc = sceneByName(name);
    if (sc == nullptr) {
        setError("no existe la escena '" + name + "'");
        return false;
    }
    if (transition_ == nullptr && !setTransition(transitionType_, transitionMs_)) {
        return false;
    }
    auto* tr = reinterpret_cast<obs_source_t*>(transition_);
    obs_source_t* dest = obs_scene_get_source(reinterpret_cast<obs_scene_t*>(sc));
    if (currentScene_.empty() || transitionMs_ == 0) {
        obs_transition_set(tr, dest); // primera escena o corte: sin animacion
    } else {
        obs_transition_start(tr, OBS_TRANSITION_MODE_AUTO, static_cast<uint32_t>(transitionMs_), dest);
    }
    currentScene_ = name;
    scene_ = sc;
    return true;
}

bool BroadcastEngine::buildScene() {
    if (sceneByName("programa") == nullptr && !createScene("programa")) {
        return false;
    }
    return setCurrentScene("programa");
}

void BroadcastEngine::releaseScenes() {
    obs_set_output_source(kProgramChannel, nullptr);
    for (std::uint32_t ch = kFirstGuestAudioChannel; ch < kFirstGuestAudioChannel + 4; ++ch) {
        obs_set_output_source(ch, nullptr);
    }
    meters_.clear(); // antes que las fuentes: los vumetros las referencian
    if (transition_ != nullptr) {
        obs_source_release(reinterpret_cast<obs_source_t*>(transition_));
        transition_ = nullptr;
    }
    for (auto& [n, sc] : scenes_) {
        obs_scene_release(reinterpret_cast<obs_scene_t*>(sc));
    }
    scenes_.clear();
    for (auto& [n, src] : sources_) {
        obs_source_release(reinterpret_cast<obs_source_t*>(src));
    }
    sources_.clear();
    globalAudio_.clear();
    currentScene_.clear();
    scene_ = nullptr;
}

// ============================================================================
// Fuentes
// ============================================================================

obs_source* BroadcastEngine::ownedSource(const std::string& name) const {
    for (const auto& [n, src] : sources_) {
        if (n == name) {
            return src;
        }
    }
    return nullptr;
}

obs_source* BroadcastEngine::sourceHandle(const std::string& name) const { return ownedSource(name); }

void BroadcastEngine::placeItem(struct obs_scene_item* itemRaw, const CanvasRect& rect) const {
    auto* item = reinterpret_cast<obs_sceneitem_t*>(itemRaw);
    if (item == nullptr) {
        return;
    }
    const float W = static_cast<float>(video_.baseWidth);
    const float H = static_cast<float>(video_.baseHeight);
    // Escalado "dentro" de la caja, centrado: respeta el aspecto de la fuente.
    obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
    obs_sceneitem_set_bounds_alignment(item, 0); // OBS_ALIGN_CENTER
    struct vec2 pos;
    vec2_set(&pos, rect.x * W, rect.y * H);
    obs_sceneitem_set_pos(item, &pos);
    struct vec2 bounds;
    vec2_set(&bounds, rect.w * W, rect.h * H);
    obs_sceneitem_set_bounds(item, &bounds);
}

void BroadcastEngine::attachMeter(const std::string& name, obs_source* srcRaw) {
    auto* src = reinterpret_cast<obs_source_t*>(srcRaw);
    if ((obs_source_get_output_flags(src) & OBS_SOURCE_AUDIO) == 0) {
        return;
    }
    for (const auto& m : meters_) {
        if (m.first == name) {
            return;
        }
    }
    auto meter = std::make_unique<Meter>();
    obs_volmeter_t* vm = obs_volmeter_create(OBS_FADER_LOG);
    obs_volmeter_attach_source(vm, src);
    obs_volmeter_add_callback(vm, meterCallback, &meter->peakDb);
    meter->vm = vm;
    meters_.emplace_back(name, std::move(meter));
}

bool BroadcastEngine::addSource(const std::string& sceneName, const SourceSpec& spec) {
    obs_scene* sc = sceneByName(sceneName);
    if (sc == nullptr) {
        setError("addSource: no existe la escena '" + sceneName + "'");
        return false;
    }
    if (spec.id.empty() || spec.name.empty()) {
        setError("addSource: fuente sin id o sin nombre");
        return false;
    }
    obs_source_t* src = reinterpret_cast<obs_source_t*>(ownedSource(spec.name));
    if (src == nullptr) {
        obs_data_t* settings =
            obs_data_create_from_json(spec.settingsJson.empty() ? "{}" : spec.settingsJson.c_str());
        if (settings == nullptr) {
            setError("fuente '" + spec.name + "': settings no son JSON valido");
            return false;
        }
        src = obs_source_create(spec.id.c_str(), spec.name.c_str(), settings, nullptr);
        obs_data_release(settings);
        if (src == nullptr) {
            setError("fuente '" + spec.name + "': libobs no conoce el tipo '" + spec.id + "'");
            return false;
        }
        obs_source_set_volume(src, spec.volume);
        obs_source_set_muted(src, spec.muted);
        sources_.emplace_back(spec.name, reinterpret_cast<obs_source*>(src)); // referencia de creacion
        attachMeter(spec.name, reinterpret_cast<obs_source*>(src));
    }
    obs_sceneitem_t* item = obs_scene_add(reinterpret_cast<obs_scene_t*>(sc), src);
    if (item != nullptr) {
        if ((obs_source_get_output_flags(src) & OBS_SOURCE_VIDEO) != 0) {
            placeItem(reinterpret_cast<obs_scene_item*>(item), spec.rect);
        }
        obs_sceneitem_set_visible(item, spec.visible);
    }
    return true;
}

bool BroadcastEngine::addSource(const SourceSpec& spec) {
    if (currentScene_.empty() && !buildScene()) {
        return false;
    }
    return addSource(currentScene_, spec);
}

bool BroadcastEngine::removeSource(const std::string& sceneName, const std::string& name) {
    obs_scene* sc = sceneByName(sceneName);
    if (sc == nullptr) {
        return false;
    }
    obs_sceneitem_t* item = obs_scene_find_source(reinterpret_cast<obs_scene_t*>(sc), name.c_str());
    if (item == nullptr) {
        return false;
    }
    obs_sceneitem_remove(item);
    // Si ya no esta en ninguna escena ni es global, se libera la fuente.
    for (const auto& [n, other] : scenes_) {
        if (obs_scene_find_source(reinterpret_cast<obs_scene_t*>(other), name.c_str()) != nullptr) {
            return true;
        }
    }
    if (std::find(globalAudio_.begin(), globalAudio_.end(), name) != globalAudio_.end()) {
        return true;
    }
    meters_.erase(
        std::remove_if(meters_.begin(), meters_.end(), [&](const auto& m) { return m.first == name; }),
        meters_.end());
    for (auto it = sources_.begin(); it != sources_.end(); ++it) {
        if (it->first == name) {
            obs_source_release(reinterpret_cast<obs_source_t*>(it->second));
            sources_.erase(it);
            break;
        }
    }
    return true;
}

bool BroadcastEngine::moveSource(const std::string& sceneName, const std::string& name, int delta) {
    obs_scene* sc = sceneByName(sceneName);
    obs_sceneitem_t* item =
        sc != nullptr ? obs_scene_find_source(reinterpret_cast<obs_scene_t*>(sc), name.c_str()) : nullptr;
    if (item == nullptr || delta == 0) {
        return false;
    }
    obs_sceneitem_set_order(item, delta > 0 ? OBS_ORDER_MOVE_UP : OBS_ORDER_MOVE_DOWN);
    return true;
}

std::vector<std::string> BroadcastEngine::sceneSources(const std::string& sceneName) const {
    std::vector<std::string> out;
    obs_scene* sc = sceneByName(sceneName);
    if (sc == nullptr) {
        return out;
    }
    obs_scene_enum_items(
        reinterpret_cast<obs_scene_t*>(sc),
        [](obs_scene_t*, obs_sceneitem_t* item, void* param) {
            static_cast<std::vector<std::string>*>(param)->emplace_back(
                obs_source_get_name(obs_sceneitem_get_source(item)));
            return true;
        },
        &out);
    return out;
}

bool BroadcastEngine::setSourceVisible(const std::string& sceneName, const std::string& name, bool visible) {
    obs_scene* sc = sceneByName(sceneName);
    obs_sceneitem_t* item =
        sc != nullptr ? obs_scene_find_source(reinterpret_cast<obs_scene_t*>(sc), name.c_str()) : nullptr;
    if (item == nullptr) {
        return false;
    }
    obs_sceneitem_set_visible(item, visible);
    return true;
}

bool BroadcastEngine::sourceVisible(const std::string& sceneName, const std::string& name) const {
    obs_scene* sc = sceneByName(sceneName);
    obs_sceneitem_t* item =
        sc != nullptr ? obs_scene_find_source(reinterpret_cast<obs_scene_t*>(sc), name.c_str()) : nullptr;
    return item != nullptr && obs_sceneitem_visible(item);
}

bool BroadcastEngine::setSourceRect(const std::string& sceneName, const std::string& name,
                                    const CanvasRect& rect) {
    obs_scene* sc = sceneByName(sceneName);
    obs_sceneitem_t* item =
        sc != nullptr ? obs_scene_find_source(reinterpret_cast<obs_scene_t*>(sc), name.c_str()) : nullptr;
    if (item == nullptr) {
        return false;
    }
    placeItem(reinterpret_cast<obs_scene_item*>(item), rect);
    return true;
}

bool BroadcastEngine::renameSource(const std::string& from, const std::string& to) {
    if (to.empty() || ownedSource(to) != nullptr) {
        setError("renameSource: nombre vacio o repetido");
        return false;
    }
    for (auto& [n, src] : sources_) {
        if (n == from) {
            obs_source_set_name(reinterpret_cast<obs_source_t*>(src), to.c_str());
            n = to;
            for (auto& m : meters_) {
                if (m.first == from) {
                    m.first = to;
                }
            }
            std::replace(globalAudio_.begin(), globalAudio_.end(), from, to);
            return true;
        }
    }
    return false;
}

std::string BroadcastEngine::sourceTypeId(const std::string& name) const {
    obs_source* src = ownedSource(name);
    return src != nullptr ? obs_source_get_id(reinterpret_cast<obs_source_t*>(src)) : "";
}

std::string BroadcastEngine::sourceSettingsJson(const std::string& name) const {
    obs_source* src = ownedSource(name);
    if (src == nullptr) {
        return "{}";
    }
    obs_data_t* d = obs_source_get_settings(reinterpret_cast<obs_source_t*>(src));
    const std::string out = obs_data_get_json(d);
    obs_data_release(d);
    return out;
}

bool BroadcastEngine::updateSourceSettings(const std::string& name, const std::string& settingsJson) {
    obs_source* src = ownedSource(name);
    if (src == nullptr) {
        return false;
    }
    obs_data_t* d = obs_data_create_from_json(settingsJson.c_str());
    if (d == nullptr) {
        setError("settings no son JSON valido");
        return false;
    }
    obs_source_update(reinterpret_cast<obs_source_t*>(src), d);
    obs_data_release(d);
    return true;
}

std::vector<SourceType> BroadcastEngine::inputTypes() const {
    std::vector<SourceType> out;
    const char* id = nullptr;
    for (size_t i = 0; obs_enum_input_types(i, &id); ++i) {
        const uint32_t caps = obs_get_source_output_flags(id);
        if ((caps & (OBS_SOURCE_DEPRECATED | OBS_SOURCE_CAP_DISABLED)) != 0) {
            continue;
        }
        const char* display = obs_source_get_display_name(id);
        out.push_back(SourceType{id, display != nullptr ? display : id, (caps & OBS_SOURCE_VIDEO) != 0,
                                 (caps & OBS_SOURCE_AUDIO) != 0});
    }
    return out;
}

// ============================================================================
// Invitados
// ============================================================================

bool BroadcastEngine::addGuestAudio(int slot) {
    const std::string name = "Invitado " + std::to_string(slot) + " (audio)";
    obs_source_t* src = reinterpret_cast<obs_source_t*>(ownedSource(name));
    if (src == nullptr) {
        obs_data_t* settings = obs_data_create();
        obs_data_set_int(settings, "slot", slot);
        // `ba_guest_audio` lo registra ba-sources: lee BA.Audio.Guest.<slot>.
        src = obs_source_create("ba_guest_audio", name.c_str(), settings, nullptr);
        obs_data_release(settings);
        if (src == nullptr) {
            setError("no se pudo crear ba_guest_audio (¿esta cargado ba-sources?)");
            return false;
        }
        sources_.emplace_back(name, reinterpret_cast<obs_source*>(src));
        attachMeter(name, reinterpret_cast<obs_source*>(src));
    }
    // Global: suena en cualquier escena, como un microfono.
    obs_set_output_source(kFirstGuestAudioChannel + static_cast<std::uint32_t>(slot - 1), src);
    if (std::find(globalAudio_.begin(), globalAudio_.end(), name) == globalAudio_.end()) {
        globalAudio_.push_back(name);
    }
    return true;
}

bool BroadcastEngine::addGuestVideo(int slot) {
    return addGuestVideo(slot, CanvasRect{0.5f, 0.5f, 0.5f, 0.5f});
}

bool BroadcastEngine::addGuestVideo(int slot, const CanvasRect& rect) {
    SourceSpec spec;
    spec.id = "ba_guest_video";
    spec.name = "Invitado " + std::to_string(slot);
    spec.settingsJson = "{\"slot\":" + std::to_string(slot) + "}";
    spec.rect = rect;
    return addSource(spec);
}

// ============================================================================
// Audio por fuente
// ============================================================================

std::vector<std::string> BroadcastEngine::audioSources() const {
    std::vector<std::string> out;
    for (const auto& m : meters_) {
        out.push_back(m.first);
    }
    return out;
}

bool BroadcastEngine::setSourceVolume(const std::string& name, float mul) {
    obs_source* src = ownedSource(name);
    if (src == nullptr) {
        return false;
    }
    obs_source_set_volume(reinterpret_cast<obs_source_t*>(src), std::max(0.0f, mul));
    return true;
}

float BroadcastEngine::sourceVolume(const std::string& name) const {
    obs_source* src = ownedSource(name);
    return src != nullptr ? obs_source_get_volume(reinterpret_cast<obs_source_t*>(src)) : 0.0f;
}

bool BroadcastEngine::setSourceMuted(const std::string& name, bool muted) {
    obs_source* src = ownedSource(name);
    if (src == nullptr) {
        return false;
    }
    obs_source_set_muted(reinterpret_cast<obs_source_t*>(src), muted);
    return true;
}

bool BroadcastEngine::sourceMuted(const std::string& name) const {
    obs_source* src = ownedSource(name);
    return src != nullptr && obs_source_muted(reinterpret_cast<obs_source_t*>(src));
}

float BroadcastEngine::sourcePeakDb(const std::string& name) const {
    for (const auto& m : meters_) {
        if (m.first == name) {
            return m.second->peakDb.load(std::memory_order_relaxed);
        }
    }
    return -100.0f;
}

// ============================================================================
// Cambios en caliente
// ============================================================================

bool BroadcastEngine::outputsActive() const {
    const auto active = [](obs_output* o) {
        return o != nullptr && obs_output_active(reinterpret_cast<obs_output_t*>(o));
    };
    return active(streamOutput_) || active(recordOutput_);
}

bool BroadcastEngine::resetEncoders() {
    if (outputsActive()) {
        setError("no se puede cambiar la codificacion con salidas activas");
        return false;
    }
    releaseVideoEncoder();
    encoderCandidates_.clear();
    encoderIndex_ = 0;
    if (audioEncoder_ != nullptr) {
        obs_encoder_release(reinterpret_cast<obs_encoder_t*>(audioEncoder_));
        audioEncoder_ = nullptr;
    }
    return true;
}

bool BroadcastEngine::resetVideo(const VideoConfig& video) {
    if (!started_) {
        return false;
    }
    if (outputsActive()) {
        setError("no se puede cambiar la resolucion con salidas activas");
        return false;
    }
    resetEncoders(); // quedan atados al video viejo
    const bool feeds = returnFeedsActive_;
    const bool preview = previewActive_;
    const int pw = previewW_;
    stopReturnFeeds();
    programVideo_ = ba::ipc::SpoutSender{}; // el slot tiene resolucion fija: se republica
    disablePreview();

    struct obs_video_info ovi = {};
    ovi.graphics_module = defaultGraphicsModule();
    ovi.fps_num = static_cast<uint32_t>(video.fpsNum);
    ovi.fps_den = static_cast<uint32_t>(video.fpsDen);
    ovi.base_width = static_cast<uint32_t>(video.baseWidth);
    ovi.base_height = static_cast<uint32_t>(video.baseHeight);
    ovi.output_width = static_cast<uint32_t>(video.outputWidth);
    ovi.output_height = static_cast<uint32_t>(video.outputHeight);
    ovi.output_format = VIDEO_FORMAT_NV12;
    ovi.colorspace = VIDEO_CS_709;
    ovi.range = VIDEO_RANGE_PARTIAL;
    ovi.gpu_conversion = true;
    ovi.scale_type = OBS_SCALE_BICUBIC;
    const int rc = obs_reset_video(&ovi);
    if (rc != OBS_VIDEO_SUCCESS) {
        setError("obs_reset_video fallo (" + std::to_string(rc) + ")");
        return false;
    }
    video_ = video;
    if (transition_ != nullptr) {
        obs_transition_set_size(reinterpret_cast<obs_source_t*>(transition_),
                                static_cast<uint32_t>(video.baseWidth),
                                static_cast<uint32_t>(video.baseHeight));
    }
    if (feeds) {
        publishReturnFeeds();
    }
    if (preview && pw > 0) {
        enablePreview(pw, (pw * video.outputHeight / std::max(1, video.outputWidth)) & ~1);
    }
    return true;
}

} // namespace ba::app
