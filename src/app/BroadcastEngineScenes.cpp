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

#include <graphics/vec4.h>
#include <nlohmann/json.hpp>
#include <obs.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#if !defined(_WIN32)
#include <dlfcn.h>
#endif
#include <chrono>
#include <thread>

namespace ba::app {

namespace {

constexpr std::uint32_t kProgramChannel = 0;
constexpr std::uint32_t kFirstGuestAudioChannel = 1;  // 1..16: invitados
constexpr std::uint32_t kMaxGuestAudioChannels = 16;
constexpr std::uint32_t kFirstMicChannel = 17; // 17..: entradas de audio del equipo
constexpr std::uint32_t kMaxMicChannels = 8;   // (libobs admite 64 canales)

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
    for (std::uint32_t ch = kFirstGuestAudioChannel; ch < kFirstGuestAudioChannel + kMaxGuestAudioChannels;
         ++ch) {
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
    micInputs_.clear();
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

bool BroadcastEngine::captureSource(const std::string& name, int maxWidth, std::vector<std::uint8_t>& rgba,
                                    int& width, int& height) {
    if (!started_) {
        setError("captura: el motor no arranco");
        return false;
    }
    const std::string target = name.empty() ? currentScene_ : name;
    obs_source_t* src = nullptr;
    if (obs_scene* sc = sceneByName(target)) {
        src = obs_source_get_ref(obs_scene_get_source(reinterpret_cast<obs_scene_t*>(sc)));
    } else if (obs_source* own = ownedSource(target)) {
        src = obs_source_get_ref(reinterpret_cast<obs_source_t*>(own));
    } else {
        src = obs_get_source_by_name(target.c_str());
    }
    if (src == nullptr) {
        setError("captura: no existe la fuente o escena '" + target + "'");
        return false;
    }
    const uint32_t cx = obs_source_get_width(src);
    const uint32_t cy = obs_source_get_height(src);
    if (cx == 0 || cy == 0) {
        obs_source_release(src);
        setError("captura: '" + target + "' no tiene imagen (tamaño 0: sin video, apagada o sin señal)");
        return false;
    }
    uint32_t w = cx;
    uint32_t h = cy;
    if (maxWidth > 0 && w > static_cast<uint32_t>(maxWidth)) {
        w = static_cast<uint32_t>(maxWidth);
        h = std::max<uint32_t>(1, static_cast<uint32_t>(static_cast<uint64_t>(cy) * w / cx));
    }
    bool ok = false;
    // Las fuentes asincronas (video de red, camaras, reproductores) solo suben
    // su ultimo cuadro a la GPU mientras se "muestran": si la escena no esta al
    // aire, se muestra un instante (unos cuadros del thread de video) y recien
    // ahi se dibuja. Sin esto, en una escena preparada fuera del aire salen vacias.
    const bool wasShowing = obs_source_showing(src);
    if (!wasShowing) {
        obs_source_inc_showing(src);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    // Render fuera de pantalla con el contexto grafico tomado (el thread de
    // render de libobs espera): textura -> superficie de lectura -> RAM.
    obs_enter_graphics();
    gs_texrender_t* tr = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    gs_stagesurf_t* stage = gs_stagesurface_create(w, h, GS_RGBA);
    if (tr != nullptr && stage != nullptr && gs_texrender_begin(tr, w, h)) {
        vec4 clear;
        vec4_zero(&clear);
        gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
        gs_ortho(0.0f, static_cast<float>(cx), 0.0f, static_cast<float>(cy), -100.0f, 100.0f);
        gs_blend_state_push();
        gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
        obs_source_inc_active(src);
        obs_source_video_render(src);
        obs_source_dec_active(src);
        gs_blend_state_pop();
        gs_texrender_end(tr);
        gs_stage_texture(stage, gs_texrender_get_texture(tr));
        uint8_t* data = nullptr;
        uint32_t linesize = 0;
        if (gs_stagesurface_map(stage, &data, &linesize)) {
            rgba.resize(static_cast<size_t>(w) * h * 4);
            for (uint32_t y = 0; y < h; ++y)
                std::memcpy(rgba.data() + static_cast<size_t>(y) * w * 4,
                            data + static_cast<size_t>(y) * linesize, static_cast<size_t>(w) * 4);
            gs_stagesurface_unmap(stage);
            ok = true;
        }
    }
    if (stage != nullptr) gs_stagesurface_destroy(stage);
    if (tr != nullptr) gs_texrender_destroy(tr);
    obs_leave_graphics();
    if (!wasShowing) obs_source_dec_showing(src);
    obs_source_release(src);
    if (!ok) {
        setError("captura: no se pudo leer la imagen de '" + target + "' (GPU)");
        return false;
    }
    width = static_cast<int>(w);
    height = static_cast<int>(h);
    return true;
}

bool BroadcastEngine::enableScenePreview(const std::string& name, int maxWidth) {
    if (name == scenePreviewName_ && scenePreviewSrc_ != nullptr) {
        scenePreviewMaxW_ = maxWidth;
        return true;
    }
    disableScenePreview();
    if (name.empty()) return true;
    if (!started_) {
        setError("vista previa de escena: el motor no arranco");
        return false;
    }
    obs_source_t* src = nullptr;
    if (obs_scene* sc = sceneByName(name)) {
        src = obs_source_get_ref(obs_scene_get_source(reinterpret_cast<obs_scene_t*>(sc)));
    } else if (obs_source* own = ownedSource(name)) {
        src = obs_source_get_ref(reinterpret_cast<obs_source_t*>(own));
    } else {
        src = obs_get_source_by_name(name.c_str());
    }
    if (src == nullptr) {
        setError("vista previa de escena: no existe '" + name + "'");
        return false;
    }
    // Se mantiene mientras dure la vista previa: sin esto las camaras y los
    // videos de red no suben cuadros si la escena no esta al aire.
    obs_source_inc_showing(src);
    scenePreviewSrc_ = reinterpret_cast<struct obs_source*>(src);
    scenePreviewName_ = name;
    scenePreviewMaxW_ = maxWidth;
    return true;
}

void BroadcastEngine::disableScenePreview() {
    if (scenePreviewSrc_ == nullptr) {
        scenePreviewName_.clear();
        return;
    }
    auto* src = reinterpret_cast<obs_source_t*>(scenePreviewSrc_);
    obs_source_dec_showing(src);
    obs_source_release(src);
    scenePreviewSrc_ = nullptr;
    scenePreviewName_.clear();
}

bool BroadcastEngine::renderScenePreview(std::vector<std::uint8_t>& dst, int& width, int& height) {
    if (scenePreviewSrc_ == nullptr || !started_) return false;
    auto* src = reinterpret_cast<obs_source_t*>(scenePreviewSrc_);
    const uint32_t cx = obs_source_get_width(src);
    const uint32_t cy = obs_source_get_height(src);
    if (cx == 0 || cy == 0) return false; // escena vacia o fuente sin señal
    uint32_t w = cx;
    uint32_t h = cy;
    if (scenePreviewMaxW_ > 0 && w > static_cast<uint32_t>(scenePreviewMaxW_)) {
        w = static_cast<uint32_t>(scenePreviewMaxW_);
        h = std::max<uint32_t>(1, static_cast<uint32_t>(static_cast<uint64_t>(cy) * w / cx));
    }
    bool ok = false;
    obs_enter_graphics();
    gs_texrender_t* tr = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    gs_stagesurf_t* stage = gs_stagesurface_create(w, h, GS_RGBA);
    if (tr != nullptr && stage != nullptr && gs_texrender_begin(tr, w, h)) {
        vec4 clear;
        vec4_zero(&clear);
        gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
        gs_ortho(0.0f, static_cast<float>(cx), 0.0f, static_cast<float>(cy), -100.0f, 100.0f);
        gs_blend_state_push();
        gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
        obs_source_video_render(src);
        gs_blend_state_pop();
        gs_texrender_end(tr);
        gs_stage_texture(stage, gs_texrender_get_texture(tr));
        uint8_t* data = nullptr;
        uint32_t linesize = 0;
        if (gs_stagesurface_map(stage, &data, &linesize)) {
            dst.resize(static_cast<size_t>(w) * h * 4);
            for (uint32_t y = 0; y < h; ++y)
                std::memcpy(dst.data() + static_cast<size_t>(y) * w * 4,
                            data + static_cast<size_t>(y) * linesize, static_cast<size_t>(w) * 4);
            gs_stagesurface_unmap(stage);
            ok = true;
        }
    }
    if (stage != nullptr) gs_stagesurface_destroy(stage);
    if (tr != nullptr) gs_texrender_destroy(tr);
    obs_leave_graphics();
    if (!ok) return false;
    width = static_cast<int>(w);
    height = static_cast<int>(h);
    return true;
}

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

void BroadcastEngine::holdShowing(const std::string& name, bool on) {
    obs_source* src = ownedSource(name);
    if (src == nullptr) return;
    if (on)
        obs_source_inc_showing(reinterpret_cast<obs_source_t*>(src));
    else
        obs_source_dec_showing(reinterpret_cast<obs_source_t*>(src));
}

bool BroadcastEngine::sourceSize(const std::string& name, int& width, int& height) const {
    width = height = 0;
    obs_source* src = ownedSource(name);
    if (src == nullptr) return false;
    width = static_cast<int>(obs_source_get_width(reinterpret_cast<obs_source_t*>(src)));
    height = static_cast<int>(obs_source_get_height(reinterpret_cast<obs_source_t*>(src)));
    return true;
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

bool BroadcastEngine::addGlobalAudio(const SourceSpec& spec) {
    const bool known = std::find(micInputs_.begin(), micInputs_.end(), spec.name) != micInputs_.end();
    if (!known && micInputs_.size() >= kMaxMicChannels) {
        setError("no entran mas entradas de audio (maximo " + std::to_string(kMaxMicChannels) + ")");
        return false;
    }
    obs_source_t* src = reinterpret_cast<obs_source_t*>(ownedSource(spec.name));
    if (src == nullptr) {
        obs_data_t* settings = obs_data_create_from_json(spec.settingsJson.c_str());
        src = obs_source_create(spec.id.c_str(), spec.name.c_str(), settings, nullptr);
        if (settings != nullptr) obs_data_release(settings);
        if (src == nullptr) {
            setError("no se pudo crear la entrada de audio '" + spec.id + "'");
            return false;
        }
        obs_source_set_volume(src, spec.volume);
        obs_source_set_muted(src, spec.muted);
        sources_.emplace_back(spec.name, reinterpret_cast<obs_source*>(src));
        attachMeter(spec.name, reinterpret_cast<obs_source*>(src));
    }
    if (!known) micInputs_.push_back(spec.name);
    if (std::find(globalAudio_.begin(), globalAudio_.end(), spec.name) == globalAudio_.end())
        globalAudio_.push_back(spec.name);
    // Global: suena en cualquier escena (no es un item de escena).
    const auto idx = static_cast<std::uint32_t>(std::find(micInputs_.begin(), micInputs_.end(), spec.name) -
                                                micInputs_.begin());
    obs_set_output_source(kFirstMicChannel + idx, src);
    return true;
}

bool BroadcastEngine::removeGlobalAudio(const std::string& name) {
    const auto it = std::find(micInputs_.begin(), micInputs_.end(), name);
    if (it == micInputs_.end()) return false;
    micInputs_.erase(it);
    globalAudio_.erase(std::remove(globalAudio_.begin(), globalAudio_.end(), name), globalAudio_.end());
    // Se reasignan los canales de las que quedan (sin huecos).
    for (std::uint32_t ch = kFirstMicChannel; ch < kFirstMicChannel + kMaxMicChannels; ++ch)
        obs_set_output_source(ch, nullptr);
    for (std::size_t i = 0; i < micInputs_.size(); ++i)
        obs_set_output_source(kFirstMicChannel + static_cast<std::uint32_t>(i),
                              reinterpret_cast<obs_source_t*>(ownedSource(micInputs_[i])));
    // Si no quedo en ninguna escena, se libera la fuente (igual que removeSource).
    for (const auto& [n, other] : scenes_)
        if (obs_scene_find_source(reinterpret_cast<obs_scene_t*>(other), name.c_str()) != nullptr)
            return true;
    meters_.erase(
        std::remove_if(meters_.begin(), meters_.end(), [&](const auto& m) { return m.first == name; }),
        meters_.end());
    for (auto it = sources_.begin(); it != sources_.end(); ++it)
        if (it->first == name) {
            obs_source_release(reinterpret_cast<obs_source_t*>(it->second));
            sources_.erase(it);
            break;
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

bool BroadcastEngine::setSourceSyncOffset(const std::string& name, int ms) {
    auto* src = reinterpret_cast<obs_source_t*>(ownedSource(name));
    if (src == nullptr) {
        setError("no existe la fuente '" + name + "'");
        return false;
    }
    // libobs lo quiere en NANOSEGUNDOS y con signo. Se acota a +-5 s: mas que
    // eso no es una camara con retardo, es un error de quien lo puso.
    const int64_t acotado = std::clamp(ms, -5000, 5000);
    obs_source_set_sync_offset(src, acotado * 1000000LL);
    return true;
}

int BroadcastEngine::sourceSyncOffset(const std::string& name) const {
    auto* src = reinterpret_cast<obs_source_t*>(ownedSource(name));
    if (src == nullptr) return 0;
    return static_cast<int>(obs_source_get_sync_offset(src) / 1000000LL);
}

bool BroadcastEngine::playMedia(const std::string& name, const std::string& file, bool loop,
                                float volume) {
    if (file.empty()) {
        setError("no se dijo que archivo reproducir");
        return false;
    }
    const nlohmann::json s{{"local_file", file},
                           {"is_local_file", true},
                           {"looping", loop},
                           {"restart_on_activate", false},
                           {"close_when_inactive", false},
                           // Al terminar se queda en negro/silencio en vez de
                           // dejar congelado el ultimo cuadro.
                           {"clear_on_media_end", true},
                           {"hw_decode", true}};

    auto* existente = reinterpret_cast<obs_source_t*>(ownedSource(name));
    if (existente != nullptr) {
        // Ya existe: se le cambia el archivo y se rebobina. Crear otra fuente
        // dejaria la anterior sonando por debajo.
        obs_data_t* d = obs_data_create_from_json(s.dump().c_str());
        obs_source_update(existente, d);
        if (d != nullptr) obs_data_release(d);
        obs_source_set_volume(existente, volume);
        obs_source_set_muted(existente, false);
        obs_source_media_restart(existente);
        return true;
    }

    SourceSpec spec;
    spec.id = "ffmpeg_source";
    spec.name = name;
    spec.settingsJson = s.dump();
    spec.volume = volume;
    if (!addGlobalAudio(spec)) return false;
    auto* nueva = reinterpret_cast<obs_source_t*>(ownedSource(name));
    if (nueva != nullptr) obs_source_media_restart(nueva);
    return true;
}

bool BroadcastEngine::restartMedia(const std::string& name) {
    auto* src = reinterpret_cast<obs_source_t*>(ownedSource(name));
    if (src == nullptr) {
        setError("no existe la fuente de medios \"" + name + "\"");
        return false;
    }
    obs_source_media_restart(src);
    return true;
}

bool BroadcastEngine::stopMedia(const std::string& name) {
    auto* src = reinterpret_cast<obs_source_t*>(ownedSource(name));
    if (src == nullptr) return false;
    obs_source_media_stop(src);
    // Se libera el canal: una fuente parada sigue ocupando uno de los pocos
    // que hay, y el proximo tema no entraria.
    return removeGlobalAudio(name);
}

std::vector<BroadcastEngine::MediaInfo> BroadcastEngine::mediaSources() const {
    std::vector<MediaInfo> out;
    for (const auto& [n, src] : sources_) {
        auto* s = reinterpret_cast<obs_source_t*>(src);
        if (s == nullptr || (obs_source_get_output_flags(s) & OBS_SOURCE_CONTROLLABLE_MEDIA) == 0) continue;
        MediaInfo m;
        m.name = n;
        switch (obs_source_media_get_state(s)) {
            case OBS_MEDIA_STATE_PLAYING: m.state = "playing"; break;
            case OBS_MEDIA_STATE_OPENING: m.state = "opening"; break;
            case OBS_MEDIA_STATE_BUFFERING: m.state = "buffering"; break;
            case OBS_MEDIA_STATE_PAUSED: m.state = "paused"; break;
            case OBS_MEDIA_STATE_STOPPED: m.state = "stopped"; break;
            case OBS_MEDIA_STATE_ENDED: m.state = "ended"; break;
            case OBS_MEDIA_STATE_ERROR: m.state = "error"; break;
            default: m.state = "none"; break;
        }
        m.durationMs = obs_source_media_get_duration(s);
        m.timeMs = obs_source_media_get_time(s);
        out.push_back(std::move(m));
    }
    return out;
}

std::vector<std::string> BroadcastEngine::sourceNames() const {
    std::vector<std::string> out;
    for (const auto& [n, src] : sources_)
        if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
    return out;
}

bool BroadcastEngine::sourceTypeExists(const std::string& typeId) const {
    const char* id = nullptr;
    for (size_t i = 0; obs_enum_input_types(i, &id); ++i)
        if (id != nullptr && typeId == id) return true;
    return false;
}

std::vector<BroadcastEngine::ListItem>
BroadcastEngine::sourceTypeListItems(const std::string& typeId, const std::string& prop,
                                     const std::string& settingsJson) const {
    std::vector<ListItem> out;
    if (!started_ || !sourceTypeExists(typeId)) return out;
    // Algunos plugins (camaras V4L2) arman sus propiedades leyendo su instancia:
    // se consultan con una instancia privada descartable, creada SIN dispositivo
    // (no abre ni captura nada); los settings pedidos se aplican solo a las
    // propiedades, que es lo que llena las listas dependientes.
    obs_data_t* none = obs_data_create();
    obs_source_t* probe = obs_source_create_private(typeId.c_str(), "ba-probe", none);
    obs_data_release(none);
    obs_properties_t* props = probe != nullptr ? obs_source_properties(probe) : nullptr;
    if (props == nullptr) {
        if (probe != nullptr) obs_source_release(probe);
        return out;
    }
    obs_data_t* settings =
        settingsJson.empty() ? obs_data_create() : obs_data_create_from_json(settingsJson.c_str());
    if (settings == nullptr) settings = obs_data_create();
    obs_data_t* defaults = obs_get_source_defaults(typeId.c_str());
    if (defaults != nullptr) {
        obs_data_apply(defaults, settings);
        obs_properties_apply_settings(props, defaults);
        obs_data_release(defaults);
    } else {
        obs_properties_apply_settings(props, settings);
    }
    obs_data_release(settings);
    obs_property_t* p = obs_properties_get(props, prop.c_str());
    if (p != nullptr && obs_property_get_type(p) == OBS_PROPERTY_LIST) {
        const obs_combo_format fmt = obs_property_list_format(p);
        const size_t n = obs_property_list_item_count(p);
        for (size_t i = 0; i < n; ++i) {
            ListItem it;
            const char* nm = obs_property_list_item_name(p, i);
            it.name = nm != nullptr ? nm : "";
            if (fmt == OBS_COMBO_FORMAT_STRING) {
                const char* v = obs_property_list_item_string(p, i);
                it.value = v != nullptr ? v : "";
            } else if (fmt == OBS_COMBO_FORMAT_INT) {
                it.value = std::to_string(obs_property_list_item_int(p, i));
            } else if (fmt == OBS_COMBO_FORMAT_FLOAT) {
                it.value = std::to_string(obs_property_list_item_float(p, i));
            }
            it.disabled = obs_property_list_item_disabled(p, i);
            out.push_back(std::move(it));
        }
    }
    obs_properties_destroy(props);
    obs_source_release(probe);
    return out;
}

std::string BroadcastEngine::graphicsDeviceName() const {
    if (!started_) return {};
    obs_enter_graphics();
    const char* n = gs_get_device_name();
    std::string out = n != nullptr ? n : "";
#if !defined(_WIN32)
    // OpenGL: el nombre real de la placa y el driver (GL_RENDERER / GL_VERSION),
    // del contexto de libobs que esta tomado.
    using GetString = const unsigned char* (*)(unsigned int);
    if (auto fn = reinterpret_cast<GetString>(dlsym(RTLD_DEFAULT, "glGetString"))) {
        const unsigned char* r = fn(0x1F01); // GL_RENDERER
        const unsigned char* v = fn(0x1F02); // GL_VERSION
        if (r != nullptr)
            out = std::string(reinterpret_cast<const char*>(r)) +
                  (v != nullptr ? " — OpenGL " + std::string(reinterpret_cast<const char*>(v)) : "");
    }
#endif
    obs_leave_graphics();
    return out;
}

std::vector<BroadcastEngine::EncoderInfo> BroadcastEngine::videoEncoders() const {
    std::vector<EncoderInfo> out;
    const char* id = nullptr;
    for (size_t i = 0; obs_enum_encoder_types(i, &id); ++i) {
        if (id == nullptr || obs_get_encoder_type(id) != OBS_ENCODER_VIDEO) continue;
        if ((obs_get_encoder_caps(id) & OBS_ENCODER_CAP_DEPRECATED) != 0) continue;
        EncoderInfo e;
        e.id = id;
        const char* dn = obs_encoder_get_display_name(id);
        e.name = dn != nullptr ? dn : id;
        const char* codec = obs_get_encoder_codec(id);
        e.codec = codec != nullptr ? codec : "";
        const std::string lid = e.id;
        e.hardware = lid.find("nvenc") != std::string::npos || lid.find("vaapi") != std::string::npos ||
                     lid.find("qsv") != std::string::npos || lid.find("amf") != std::string::npos ||
                     lid.find("videotoolbox") != std::string::npos || lid.find("mf_") != std::string::npos;
        out.push_back(std::move(e));
    }
    return out;
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

bool BroadcastEngine::setSourceMonitoring(const std::string& name, bool on) {
    obs_source* src = ownedSource(name);
    if (src == nullptr) {
        return false;
    }
    // MONITOR_AND_OUTPUT y NO MONITOR_ONLY: ver el porque en el .hpp.
    obs_source_set_monitoring_type(reinterpret_cast<obs_source_t*>(src),
                                   on ? OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT
                                      : OBS_MONITORING_TYPE_NONE);
    return true;
}

bool BroadcastEngine::sourceMonitoring(const std::string& name) const {
    obs_source* src = ownedSource(name);
    return src != nullptr && obs_source_get_monitoring_type(reinterpret_cast<obs_source_t*>(src)) !=
                                 OBS_MONITORING_TYPE_NONE;
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
