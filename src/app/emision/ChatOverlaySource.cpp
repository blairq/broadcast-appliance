// ============================================================================
// src/app/emision/ChatOverlaySource.cpp — ver ChatOverlaySource.hpp
// ============================================================================

#include "emision/ChatOverlaySource.hpp"

#include "emision/FuentesApp.hpp"

#include <algorithm>

namespace ba::app::chatoverlay {

namespace {

using json = nlohmann::json;

int entero(const json& s, const char* k, int lo, int hi) {
    const auto it = s.find(k);
    return std::clamp(it != s.end() && it->is_number() ? it->get<int>() : lo, lo, hi);
}
bool logico(const json& s, const char* k) {
    const auto it = s.find(k);
    return it != s.end() && it->is_boolean() && it->get<bool>();
}

Instance leer(const fuentes::Instancia& f) {
    Instance in;
    const json& s = f.ajustes;
    in.id = f.id;
    in.name = f.nombre;
    in.width = entero(s, "width", 64, 3840);
    in.height = entero(s, "height", 64, 2160);
    in.fontSize = entero(s, "font_size", 10, 120);
    in.maxMessages = entero(s, "max_messages", 1, 50);
    in.showYoutube = logico(s, "show_youtube");
    in.showTwitch = logico(s, "show_twitch");
    in.showFacebook = logico(s, "show_facebook");
    in.showRoom = logico(s, "show_room");
    // Compatibilidad: "channel" (una sola fuente de mensajes) restringe.
    const std::string only = s.value("channel", std::string());
    if (only == "youtube" || only == "twitch" || only == "facebook" || only == "room") {
        in.showYoutube = only == "youtube";
        in.showTwitch = only == "twitch";
        in.showFacebook = only == "facebook";
        in.showRoom = only == "room";
    }
    in.bgOpacity = entero(s, "bg_opacity", 0, 100);
    in.showNames = logico(s, "show_names");
    in.fadeSec = entero(s, "fade_sec", 0, 3600);
    in.soloDestacados = logico(s, "solo_destacados");
    in.settingsVersion = f.version;
    return in;
}

} // namespace

void registerSourceType() {
    fuentes::Tipo t;
    t.id = kSourceId;
    t.nombre = "Chat en vivo (YouTube, Twitch, Facebook, sala)";
    t.deApp = true;
    t.defectos = {{"width", 640},         {"height", 720},         {"font_size", 28},     {"max_messages", 8},
                  {"channel", "all"},     {"solo_destacados", true}, {"show_youtube", true}, {"show_twitch", true},
                  {"show_facebook", true}, {"show_room", true},     {"bg_opacity", 55},    {"show_names", true},
                  {"fade_sec", 0}};
    t.props = [](const json&) {
        using P = fuentes::Prop;
        P canal{"channel", "Mensajes de", "list"};
        canal.opciones = json::array({{{"value", "all"}, {"label", "Los elegidos abajo"}},
                                      {{"value", "youtube"}, {"label", "Solo YouTube"}},
                                      {{"value", "twitch"}, {"label", "Solo Twitch"}},
                                      {{"value", "facebook"}, {"label", "Solo Facebook"}},
                                      {{"value", "room"}, {"label", "Solo la sala (invitados)"}}});
        return std::vector<P>{canal,
                              {"solo_destacados", "Solo los mensajes destacados por el operador", "bool"},
                              {"show_youtube", "YouTube", "bool"},
                              {"show_twitch", "Twitch", "bool"},
                              {"show_facebook", "Facebook", "bool"},
                              {"show_room", "Sala (invitados)", "bool"},
                              {"max_messages", "Mensajes a la vista", "int", 1, 50, 1},
                              {"font_size", "Tamaño de letra", "int", 10, 120, 1},
                              {"width", "Ancho", "int", 64, 3840, 8},
                              {"height", "Alto", "int", 64, 2160, 8},
                              {"bg_opacity", "Opacidad del fondo", "int", 0, 100, 5},
                              {"show_names", "Mostrar nombres", "bool"},
                              {"fade_sec", "Desaparecen a los (s, 0 = nunca)", "int", 0, 3600, 5}};
    };
    fuentes::registrar(std::move(t));
}

std::vector<Instance> instances() {
    std::vector<Instance> out;
    for (const auto& f : fuentes::de(kSourceId))
        out.push_back(leer(f));
    return out;
}

void push(std::uint64_t id, const std::uint8_t* bgra, int width, int height) {
    fuentes::empujar(id, bgra, width, height);
}

} // namespace ba::app::chatoverlay
