// ============================================================================
// src/app/ChatOverlaySource.cpp — ver ChatOverlaySource.hpp
// ============================================================================

#include "ChatOverlaySource.hpp"

#include <obs.h>
#include <util/platform.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>

namespace ba::app::chatoverlay {

namespace {

struct Ctx {
    obs_source_t* source = nullptr;
    Instance info;
};

std::mutex g_lock;
std::map<std::uint64_t, Ctx*> g_live;
std::atomic<std::uint64_t> g_nextId{1};

void readSettings(Instance& in, obs_data_t* s) {
    in.width = std::clamp(static_cast<int>(obs_data_get_int(s, "width")), 64, 3840);
    in.height = std::clamp(static_cast<int>(obs_data_get_int(s, "height")), 64, 2160);
    in.fontSize = std::clamp(static_cast<int>(obs_data_get_int(s, "font_size")), 10, 120);
    in.maxMessages = std::clamp(static_cast<int>(obs_data_get_int(s, "max_messages")), 1, 50);
    in.showYoutube = obs_data_get_bool(s, "show_youtube");
    in.showTwitch = obs_data_get_bool(s, "show_twitch");
    in.showFacebook = obs_data_get_bool(s, "show_facebook");
    in.showRoom = obs_data_get_bool(s, "show_room");
    // Compatibilidad: "channel" (una sola fuente de mensajes) restringe.
    const char* ch = obs_data_get_string(s, "channel");
    const std::string only = ch != nullptr ? ch : "";
    if (only == "youtube" || only == "twitch" || only == "facebook" || only == "room") {
        in.showYoutube = only == "youtube";
        in.showTwitch = only == "twitch";
        in.showFacebook = only == "facebook";
        in.showRoom = only == "room";
    }
    in.bgOpacity = std::clamp(static_cast<int>(obs_data_get_int(s, "bg_opacity")), 0, 100);
    in.showNames = obs_data_get_bool(s, "show_names");
    in.fadeSec = std::clamp(static_cast<int>(obs_data_get_int(s, "fade_sec")), 0, 3600);
    in.soloDestacados = obs_data_get_bool(s, "solo_destacados");
    ++in.settingsVersion;
}

const char* getName(void*) { return "Chat en vivo (YouTube, Twitch, Facebook, sala)"; }

void* create(obs_data_t* settings, obs_source_t* source) {
    auto* c = new Ctx();
    c->source = source;
    c->info.id = g_nextId.fetch_add(1);
    c->info.name = obs_source_get_name(source) != nullptr ? obs_source_get_name(source) : "";
    readSettings(c->info, settings);
    // Cada cuadro se muestra en cuanto llega (no hay reloj de video que seguir).
    obs_source_set_async_unbuffered(source, true);
    std::lock_guard<std::mutex> lock(g_lock);
    g_live[c->info.id] = c;
    return c;
}

void destroy(void* data) {
    auto* c = static_cast<Ctx*>(data);
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_live.erase(c->info.id);
    }
    delete c;
}

void update(void* data, obs_data_t* settings) {
    auto* c = static_cast<Ctx*>(data);
    std::lock_guard<std::mutex> lock(g_lock);
    readSettings(c->info, settings);
}

void defaults(obs_data_t* s) {
    obs_data_set_default_int(s, "width", 640);
    obs_data_set_default_int(s, "height", 720);
    obs_data_set_default_int(s, "font_size", 28);
    obs_data_set_default_int(s, "max_messages", 8);
    obs_data_set_default_string(s, "channel", "all");
    obs_data_set_default_bool(s, "solo_destacados", true);
    obs_data_set_default_bool(s, "show_youtube", true);
    obs_data_set_default_bool(s, "show_twitch", true);
    obs_data_set_default_bool(s, "show_facebook", true);
    obs_data_set_default_bool(s, "show_room", true);
    obs_data_set_default_int(s, "bg_opacity", 55);
    obs_data_set_default_bool(s, "show_names", true);
    obs_data_set_default_int(s, "fade_sec", 0);
}

obs_properties_t* properties(void*) {
    obs_properties_t* p = obs_properties_create();
    obs_property_t* ch =
        obs_properties_add_list(p, "channel", "Mensajes de", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(ch, "Los elegidos abajo", "all");
    obs_property_list_add_string(ch, "Solo YouTube", "youtube");
    obs_property_list_add_string(ch, "Solo Twitch", "twitch");
    obs_property_list_add_string(ch, "Solo Facebook", "facebook");
    obs_property_list_add_string(ch, "Solo la sala (invitados)", "room");
    obs_properties_add_bool(p, "solo_destacados", "Solo los mensajes destacados por el operador");
    obs_properties_add_bool(p, "show_youtube", "YouTube");
    obs_properties_add_bool(p, "show_twitch", "Twitch");
    obs_properties_add_bool(p, "show_facebook", "Facebook");
    obs_properties_add_bool(p, "show_room", "Sala (invitados)");
    obs_properties_add_int(p, "max_messages", "Mensajes a la vista", 1, 50, 1);
    obs_properties_add_int(p, "font_size", "Tamaño de letra", 10, 120, 1);
    obs_properties_add_int(p, "width", "Ancho", 64, 3840, 8);
    obs_properties_add_int(p, "height", "Alto", 64, 2160, 8);
    obs_properties_add_int_slider(p, "bg_opacity", "Opacidad del fondo", 0, 100, 5);
    obs_properties_add_bool(p, "show_names", "Mostrar nombres");
    obs_properties_add_int(p, "fade_sec", "Desaparecen a los (s, 0 = nunca)", 0, 3600, 5);
    return p;
}

struct obs_source_info g_info = {};

} // namespace

void registerSourceType() {
    g_info.id = kSourceId;
    g_info.type = OBS_SOURCE_TYPE_INPUT;
    g_info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_DO_NOT_DUPLICATE;
    g_info.get_name = getName;
    g_info.create = create;
    g_info.destroy = destroy;
    g_info.update = update;
    g_info.get_defaults = defaults;
    g_info.get_properties = properties;
    g_info.icon_type = OBS_ICON_TYPE_TEXT;
    obs_register_source(&g_info);
}

std::vector<Instance> instances() {
    std::lock_guard<std::mutex> lock(g_lock);
    std::vector<Instance> out;
    out.reserve(g_live.size());
    for (const auto& [id, c] : g_live)
        out.push_back(c->info);
    return out;
}

void push(std::uint64_t id, const std::uint8_t* bgra, int width, int height) {
    std::lock_guard<std::mutex> lock(g_lock); // la instancia no se destruye mientras se entrega
    const auto it = g_live.find(id);
    if (it == g_live.end()) return;
    obs_source_frame frame = {};
    frame.data[0] = const_cast<std::uint8_t*>(bgra);
    frame.linesize[0] = static_cast<uint32_t>(width) * 4;
    frame.width = static_cast<uint32_t>(width);
    frame.height = static_cast<uint32_t>(height);
    frame.format = VIDEO_FORMAT_BGRA;
    frame.full_range = true;
    frame.timestamp = os_gettime_ns();
    obs_source_output_video(it->second->source, &frame);
}

} // namespace ba::app::chatoverlay
