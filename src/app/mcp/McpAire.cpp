// SPDX-License-Identifier: GPL-2.0-or-later
#include "mcp/McpAire.hpp"

#include "i18n/I18n.hpp"

#include <fstream>

namespace ba::app::mcp {

using json = nlohmann::json;

namespace {

#ifndef QT_TRANSLATE_NOOP
#define QT_TRANSLATE_NOOP(scope, x) x
#endif

/// Las de siempre: cambian lo que ve o escucha el público, o cortan la salida.
const std::map<std::string, std::string>& fijas() {
    static const std::map<std::string, std::string> m{
        {"scene_switch", QT_TRANSLATE_NOOP("mcp", "cambia la escena al aire")},
        {"preview_al_aire", QT_TRANSLATE_NOOP("mcp", "pasa el preview al aire")},
        {"scene_cut", QT_TRANSLATE_NOOP("mcp", "corta la armada al aire")},
        {"transicion_manual", QT_TRANSLATE_NOOP("mcp", "lleva la transición a mano (T-bar)")},
        {"stream_stop", QT_TRANSLATE_NOOP("mcp", "corta la transmisión")},
        {"stream_start", QT_TRANSLATE_NOOP("mcp", "arranca o cambia la transmisión")},
        {"record_stop", QT_TRANSLATE_NOOP("mcp", "corta la grabación")},
        {"participant_kick", QT_TRANSLATE_NOOP("mcp", "saca a un invitado de la conferencia")},
        {"gateway_restart", QT_TRANSLATE_NOOP("mcp", "reinicia el gateway (corta a los invitados)")},
        {"proxy_configurar", QT_TRANSLATE_NOOP("mcp", "reinicia la conferencia (corta a los invitados) con el proxy nuevo")},
        {"app_quit", QT_TRANSLATE_NOOP("mcp", "cierra el programa")},
        {"transition_set", QT_TRANSLATE_NOOP("mcp", "cambia la transición")},
        {"speaker_on_air", QT_TRANSLATE_NOOP("mcp", "pone o saca oradores del aire")},
        {"source_remove", QT_TRANSLATE_NOOP("mcp", "borra una fuente (puede estar al aire)")},
        {"scene_delete", QT_TRANSLATE_NOOP("mcp", "borra una escena")},
        {"settings_set", QT_TRANSLATE_NOOP("mcp", "cambia ajustes (los de gateway o reinicio cortan)")},
        {"audio_set", QT_TRANSLATE_NOOP("mcp", "cambia el audio (mute del máster corta el sonido)")},
        {"ptz_mover", QT_TRANSLATE_NOOP("mcp", "mueve una cámara (si está al aire, se ve)")},
        {"ptz_preset", QT_TRANSLATE_NOOP("mcp", "lleva una cámara a un preset (si está al aire, se ve)")},
        {"ptz_ir_posicion", QT_TRANSLATE_NOOP("mcp", "lleva una cámara a una posición (si está al aire, se ve)")},
        {"enfocar", QT_TRANSLATE_NOOP("mcp", "lleva una cámara a una banca, persona o posición (si está al aire, se ve)")},
        {"ptz_apuntar", QT_TRANSLATE_NOOP("mcp", "apunta una cámara (si está al aire, se ve)")},
        {"ptz_encuadre", QT_TRANSLATE_NOOP("mcp", "reencuadra una cámara (si está al aire, se ve)")},
        {"ptz_seguir", QT_TRANSLATE_NOOP("mcp", "una cámara sigue a alguien (si está al aire, se ve)")},
        {"participant_talk", QT_TRANSLATE_NOOP("mcp", "abre o cierra el micrófono de un invitado")},
        {"aire_ftb", QT_TRANSLATE_NOOP("mcp", "manda la salida a negro")},
        {"banca_al_aire", QT_TRANSLATE_NOOP("mcp", "pone a un orador al aire (en directo corta la escena)")},
        {"dcn_microfono", QT_TRANSLATE_NOOP("mcp", "abre o cierra el micrófono de una banca en la central de conferencia")},
        {"dcn_siguiente", QT_TRANSLATE_NOOP("mcp", "le da la palabra (y el micrófono) al primero de la lista")},
        {"dcn_cerrar_todos", QT_TRANSLATE_NOOP("mcp", "cierra los micrófonos de la sala")},
        {"dcn_orador_quitar", QT_TRANSLATE_NOOP("mcp", "saca a un orador de la lista (su micrófono se cierra)")},
        {"dcn_modo", QT_TRANSLATE_NOOP("mcp", "cambia cómo se abren los micrófonos de la sala")},
        {"chat_overlay_add", QT_TRANSLATE_NOOP("mcp", "pone el chat del público en pantalla (en modo flujo, sin elegir)")},
        {"chat_destacar", QT_TRANSLATE_NOOP("mcp", "pone al aire un mensaje del público")},
        {"stream_destino_borrar", QT_TRANSLATE_NOOP("mcp", "borra un destino (si transmite, lo corta)")},
        {"aire_placa", QT_TRANSLATE_NOOP("mcp", "tapa la salida con una placa")},
        {"dsk_mostrar", QT_TRANSLATE_NOOP("mcp", "sube o baja una capa (DSK) encima del programa")},
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
           h == "aire_placa" || h == "chat_overlay_add" || h == "banca_al_aire" || h == "enfocar" ||
           h.rfind("ptz_", 0) == 0;
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
            if (h.rfind("ptz_", 0) == 0 || h == "enfocar")
                return args.value("forzar", false) && (condicion_ ? condicion_(h, args) : true);
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
                       {"por_que", ba::i18n::traducir("mcp", por.c_str())}});
    std::lock_guard<std::mutex> l(mu_);
    for (const auto& h : agregadas_)
        if (!fijas().count(h)) out.push_back({{"herramienta", h}, {"clase", "aire"}, {"fija", false}});
    return out;
}

std::string McpAire::ajustar(const std::string& h, const std::string& c) {
    if (h.empty()) return ba::i18n::traducir("mcp", "falta `herramienta`");
    if (c != "aire" && c != "normal") return ba::i18n::traducir("mcp", "`clase`: aire o normal");
    if (fijas().count(h)) return c == "aire" ? "" : ba::i18n::llenar(ba::i18n::traducir("mcp", "«%1» es de aire siempre: no se puede bajar"), {h});
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
    return o ? "" : ba::i18n::llenar(ba::i18n::traducir("mcp", "no se pudo guardar %1"), {archivo_.string()});
}

} // namespace ba::app::mcp
