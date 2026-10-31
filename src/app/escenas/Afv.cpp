// src/app/escenas/Afv.cpp — el audio que sigue al video de una salida (ver el .hpp)

#include "escenas/Afv.hpp"

#include <algorithm>

namespace ba::app::escenas {

std::vector<Afv::Fundido> Afv::alCortar(const std::vector<std::string>& enDestino,
                                         const std::function<float(const std::string&)>& volumen, bool* recordo) {
    std::vector<Fundido> out;
    if (recordo != nullptr) *recordo = false;
    for (const std::string& f : fuentes) {
        const bool entra = std::find(enDestino.begin(), enDestino.end(), f) != enDestino.end();
        const float actual = volumen(f);
        const auto it = niveles.find(f);
        if (!entra && actual > 0.001f && (it == niveles.end() || it->second != actual)) {
            niveles[f] = actual;
            if (recordo != nullptr) *recordo = true;
        }
        const auto n = niveles.find(f);
        out.push_back({f, actual, entra ? (n != niveles.end() ? n->second : 1.0f) : 0.0f});
    }
    return out;
}

nlohmann::json Afv::aJson() const {
    nlohmann::json lista = nlohmann::json::array();
    for (const std::string& f : fuentes) lista.push_back(f);
    nlohmann::json n = nlohmann::json::object();
    for (const auto& [f, v] : niveles)
        if (fuentes.count(f) > 0) n[f] = v;
    return {{"afv", lista}, {"afv_niveles", n}};
}

Afv Afv::deJson(const nlohmann::json& j) {
    Afv a;
    if (!j.is_object()) return a;
    const nlohmann::json lista = j.value("afv", nlohmann::json::array()); // con nombre: iterar un temporal es UB
    for (const auto& x : lista)
        if (x.is_string()) a.fuentes.insert(x.get<std::string>());
    // El nivel al que vuelve cada fuente AFV cuando entra (el del operador): sin
    // esto, tras reiniciar el primer fundido subía a 0 dB.
    const nlohmann::json niveles = j.value("afv_niveles", nlohmann::json::object());
    for (const auto& [f, v] : niveles.items())
        if (v.is_number()) a.niveles[f] = v.get<float>();
    return a;
}

} // namespace ba::app::escenas
