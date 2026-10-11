// SPDX-License-Identifier: GPL-2.0-or-later
#include "mcp/McpAire.hpp"

#include <fstream>

namespace ba::app::mcp {

using json = nlohmann::json;

namespace {

/// Las de siempre: cambian lo que ve o escucha el público, o cortan la salida.
const std::map<std::string, std::string>& fijas() {
    static const std::map<std::string, std::string> m{
        {"scene_switch", "cambia la escena al aire"},
        {"scene_take", "pasa el preview al aire"},
        {"scene_cut", "corta la armada al aire"},
        {"transicion_manual", "lleva la transición a mano (T-bar)"},
        {"stream_stop", "corta la transmisión"},
        {"stream_start", "arranca o cambia la transmisión"},
        {"record_stop", "corta la grabación"},
        {"participant_kick", "saca a un invitado de la sala"},
        {"gateway_restart", "reinicia el gateway (corta a los invitados)"},
        {"proxy_configurar", "reinicia la sala (corta a los invitados) con el proxy nuevo"},
        {"app_quit", "cierra el programa"},
        {"transition_set", "cambia la transición"},
        {"speaker_on_air", "pone o saca oradores del aire"},
        {"source_remove", "borra una fuente (puede estar al aire)"},
        {"scene_delete", "borra una escena"},
        {"settings_set", "cambia ajustes (los de gateway o reinicio cortan)"},
        {"audio_set", "cambia el audio (mute del máster corta el sonido)"},
        {"ptz_mover", "mueve una cámara (si está al aire, se ve)"},
        {"ptz_preset", "lleva una cámara a un preset (si está al aire, se ve)"},
        {"ptz_ir_posicion", "lleva una cámara a una posición (si está al aire, se ve)"},
        {"ptz_apuntar", "apunta una cámara (si está al aire, se ve)"},
        {"ptz_encuadre", "reencuadra una cámara (si está al aire, se ve)"},
        {"ptz_seguir", "una cámara sigue a alguien (si está al aire, se ve)"},
        {"participant_talk", "abre o cierra el micrófono de un invitado"},
        {"aire_ftb", "manda la salida a negro"},
        {"banca_al_aire", "pone a un orador al aire (en directo corta la escena)"},
        {"dcn_microfono", "abre o cierra el micrófono de una banca en la central de conferencia"},
        {"dcn_siguiente", "le da la palabra (y el micrófono) al primero de la lista"},
        {"dcn_cerrar_todos", "cierra los micrófonos de la sala"},
        {"dcn_orador_quitar", "saca a un orador de la lista (su micrófono se cierra)"},
        {"dcn_modo", "cambia cómo se abren los micrófonos de la sala"},
        {"chat_overlay_add", "pone el chat del público en pantalla (en modo flujo, sin elegir)"},
        {"chat_destacar", "pone al aire un mensaje del público"},
        {"stream_destino_borrar", "borra un destino (si transmite, lo corta)"},
        {"aire_placa", "tapa la salida con una placa"},
        {"dsk_mostrar", "sube o baja una capa (DSK) encima del programa"},
    };
    return m;
}

/// Las que cuentan solo con ciertos argumentos (el resto del tiempo son normales).
bool condicionFija(const std::string& h, const json& a) {
    if (h == "settings_set") {
        // Lo que se aplica en caliente sin cortar es normal; gateway/reinicio, de aire.
        const json valores = a.value("values", json::object()); // en una variable: items() no alarga la vida del temporal
        for (const auto& [k, v] : valores.items()) {
            (void)v;
            if (k.rfind("gateway.", 0) == 0 || k.rfind("video.", 0) == 0 || k.rfind("encoder.", 0) == 0 ||
                k.rfind("stream.", 0) == 0)
                return true;
        }
        return false;
    }
    if (h == "audio_set") {
        // Silenciar o bajar el máster / el programa.
        const std::string f = a.value("source", a.value("fuente", std::string()));
        return f.empty() || f == "master" || f == "máster" || f == "programa";
    }
    if (h == "speaker_on_air") return !a.value("on", a.value("al_aire", true)); // sacar a alguien
    if (h == "aire_ftb" || h == "aire_placa") return a.value("activar", false); // sacarla no es de aire
    if (h == "chat_overlay_add") return a.value("modo", std::string("destacado")) == "flujo";
    if (h == "banca_al_aire") return a.value("modo", std::string("armar")) == "directo";
    return true;
}

bool esAireSi(const std::string& h) {
    return h == "settings_set" || h == "audio_set" || h == "speaker_on_air" || h == "aire_ftb" ||
           h == "aire_placa" || h == "chat_overlay_add" || h == "banca_al_aire" || h.rfind("ptz_", 0) == 0;
}

} // namespace

bool reversibleEnVivo(const std::string& h) {
    static const std::set<std::string> s{"audio_set", "prompter_control", "ptz_parar"};
    return s.count(h) != 0;
}

void McpAire::cargar(const std::filesystem::path& config) {
    archivo_ = config;
    std::ifstream f(config);
    const json j = json::parse(f, nullptr, false);
    std::lock_guard<std::mutex> l(mu_);
    agregadas_.clear();
    if (j.is_object())
        for (const auto& x : j.value("aire", json::array()))
            if (x.is_string()) agregadas_.insert(x.get<std::string>());
}

ClaseAire McpAire::clase(const std::string& h) const {
    if (fijas().count(h)) return esAireSi(h) ? ClaseAire::AireSi : ClaseAire::Aire;
    std::lock_guard<std::mutex> l(mu_);
    return agregadas_.count(h) ? ClaseAire::Aire : ClaseAire::Normal;
}

bool McpAire::esDeAire(const std::string& h, const json& args) const {
    switch (clase(h)) {
        case ClaseAire::Normal: return false;
        case ClaseAire::Aire: return true;
        case ClaseAire::AireSi:
            // PTZ: el interlock ya frena la cámara al aire; si se pide `forzar`, es de aire.
            if (h.rfind("ptz_", 0) == 0) return args.value("forzar", false) && (condicion_ ? condicion_(h, args) : true);
            return condicionFija(h, args);
    }
    return false;
}

json McpAire::lista() const {
    json out = json::array();
    for (const auto& [h, por] : fijas())
        out.push_back({{"herramienta", h},
                       {"clase", esAireSi(h) ? "aire_si" : "aire"},
                       {"fija", true},
                       {"por_que", por}});
    std::lock_guard<std::mutex> l(mu_);
    for (const auto& h : agregadas_)
        if (!fijas().count(h)) out.push_back({{"herramienta", h}, {"clase", "aire"}, {"fija", false}});
    return out;
}

std::string McpAire::ajustar(const std::string& h, const std::string& c) {
    if (h.empty()) return "falta `herramienta`";
    if (c != "aire" && c != "normal") return "`clase`: aire o normal";
    if (fijas().count(h)) return c == "aire" ? "" : "«" + h + "» es de aire siempre: no se puede bajar";
    {
        std::lock_guard<std::mutex> l(mu_);
        if (c == "aire")
            agregadas_.insert(h);
        else
            agregadas_.erase(h);
    }
    json j{{"aire", json::array()}};
    {
        std::lock_guard<std::mutex> l(mu_);
        for (const auto& x : agregadas_) j["aire"].push_back(x);
    }
    std::ofstream o(archivo_);
    o << j.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
    return o ? "" : "no se pudo guardar " + archivo_.string();
}

} // namespace ba::app::mcp
