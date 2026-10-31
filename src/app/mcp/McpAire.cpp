// SPDX-License-Identifier: GPL-2.0-or-later
#include "mcp/McpAire.hpp"

#include "mcp/McpCore.hpp"

#include "i18n/I18n.hpp"

#include <fstream>

namespace ba::app::mcp {

using json = nlohmann::json;

namespace {

/// Lo que dice cada regla de AireSi sobre los argumentos. CamaraForzada no está:
/// necesita además saber qué cámara está al aire (McpAire::condicion_).
bool cumple(AireSi cuando, const json& a) {
    switch (cuando) {
        case AireSi::Siempre: return true;
        case AireSi::AjustesQueCortan: {
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
        case AireSi::RelojNuevo:
            // Mirarlo, la latencia o publicarlo es normal; cambiar el reloj rearma el motor.
            for (const char* k : {"tipo", "servidor", "puerto", "dominio"})
                if (a.contains(k)) return true;
            return false;
        case AireSi::MasterOPrograma: {
            // Silenciar o bajar el máster / el programa.
            const std::string f = a.value("source", a.value("fuente", std::string()));
            return f.empty() || f == "master" || f == "máster" || f == "programa";
        }
        case AireSi::SacarDelAire: return !a.value("on", a.value("al_aire", true));
        case AireSi::AlActivar: return a.value("activar", false); // sacarla no es de aire
        case AireSi::ChatEnFlujo: return a.value("modo", std::string("destacado")) == "flujo";
        case AireSi::BancaDirecto: return a.value("modo", std::string("armar")) == "directo";
        case AireSi::CamaraForzada: return a.value("forzar", false);
    }
    return true;
}

} // namespace

const char* McpAire::nombre(AireSi cuando) {
    switch (cuando) {
        case AireSi::Siempre: return "siempre";
        case AireSi::AjustesQueCortan: return "ajustes_que_cortan";
        case AireSi::RelojNuevo: return "reloj_nuevo";
        case AireSi::MasterOPrograma: return "master_o_programa";
        case AireSi::SacarDelAire: return "sacar_del_aire";
        case AireSi::AlActivar: return "al_activar";
        case AireSi::ChatEnFlujo: return "chat_en_flujo";
        case AireSi::BancaDirecto: return "banca_directo";
        case AireSi::CamaraForzada: return "camara_forzada";
    }
    return "siempre";
}

const Tool* McpAire::declarada(const std::string& h) const {
    const Tool* t = registro_.findTool(h);
    return t != nullptr && t->clase == Clase::Aire ? t : nullptr;
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
    if (const Tool* t = declarada(h)) return t->aireSi == AireSi::Siempre ? ClaseAire::Aire : ClaseAire::AireSi;
    std::lock_guard<std::mutex> l(mu_);
    return agregadas_.count(registro_.nombreActual(h)) ? ClaseAire::Aire : ClaseAire::Normal;
}

bool McpAire::esDeAire(const std::string& h, const json& args) const {
    if (const Tool* t = declarada(h)) {
        if (!cumple(t->aireSi, args)) return false;
        // PTZ: el interlock ya frena la cámara al aire; si se pide `forzar`, cuenta si ESA está al aire.
        if (t->aireSi == AireSi::CamaraForzada && condicion_) return condicion_(t->name, args);
        return true;
    }
    return clase(h) == ClaseAire::Aire;
}

json McpAire::lista() const {
    json out = json::array();
    std::set<std::string> declaradas;
    for (const Tool& t : registro_.tools()) {
        if (t.clase != Clase::Aire) continue;
        declaradas.insert(t.name);
        json e{{"herramienta", t.name},
               {"clase", t.aireSi == AireSi::Siempre ? "aire" : "aire_si"},
               {"fija", true},
               {"por_que", t.porQueAire != nullptr ? ba::i18n::traducir("mcp", t.porQueAire) : std::string()}};
        if (t.aireSi != AireSi::Siempre) e["cuando"] = nombre(t.aireSi);
        out.push_back(std::move(e));
    }
    std::lock_guard<std::mutex> l(mu_);
    for (const auto& h : agregadas_)
        if (!declaradas.count(h)) out.push_back({{"herramienta", h}, {"clase", "aire"}, {"fija", false}});
    return out;
}

std::string McpAire::ajustar(const std::string& h, const std::string& c) {
    if (h.empty()) return ba::i18n::traducir("mcp", "falta `herramienta`");
    if (c != "aire" && c != "normal") return ba::i18n::traducir("mcp", "`clase`: aire o normal");
    if (declarada(h) != nullptr)
        return c == "aire" ? "" : ba::i18n::llenar(ba::i18n::traducir("mcp", "«%1» es de aire siempre: no se puede bajar"), {h});
    const std::string nombre = registro_.nombreActual(h);
    {
        std::lock_guard<std::mutex> l(mu_);
        if (c == "aire")
            agregadas_.insert(nombre);
        else
            agregadas_.erase(nombre);
    }
    json j{{"aire", json::array()}};
    {
        std::lock_guard<std::mutex> l(mu_);
        for (const auto& x : agregadas_) j["aire"].push_back(x);
    }
    if (archivo_.empty()) return ""; // sin archivo (pruebas): queda en memoria
    std::ofstream o(archivo_);
    o << j.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
    return o ? "" : ba::i18n::llenar(ba::i18n::traducir("mcp", "no se pudo guardar %1"), {archivo_.string()});
}

} // namespace ba::app::mcp
