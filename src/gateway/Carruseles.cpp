// ============================================================================
// src/gateway/Carruseles.cpp — ver Carruseles.hpp
// ============================================================================
#include "Carruseles.hpp"

#include <algorithm>

namespace ba::gateway {

using json = nlohmann::json;

bool Carruseles::publicar(const json& c) {
    if (!c.is_object()) return false;
    const std::string id = c.value("id", "");
    if (id.empty() || id.size() > 64) return false;
    const json pasos = c.value("pasos", json::array());
    if (!pasos.is_array() || pasos.empty()) return false;

    // Se copia paso a paso en vez de guardar lo que venga: lo que llegue de
    // más no tiene por qué terminar en el navegador de un televisor.
    json limpios = json::array();
    for (const json& p : pasos) {
        if (!p.is_object()) continue;
        const std::string url = p.value("url", "");
        if (url.empty()) continue;
        // Los segundos se acotan acá: un paso de 0 segundos dejaría la página
        // cambiando a toda velocidad, y uno de horas no es un carrusel.
        const int seg = std::clamp(p.value("segundos", 15), 3, 3600);
        limpios.push_back(json{{"url", url}, {"segundos", seg}, {"titulo", p.value("titulo", "")}});
    }
    if (limpios.empty()) return false;

    std::lock_guard<std::mutex> lock(m_);
    if (por_.size() >= kMax && !por_.contains(id)) return false;
    por_[id] = json{{"id", id},
                    {"nombre", c.value("nombre", id)},
                    {"pasos", limpios},
                    {"transicion", c.value("transicion", "fundido")},
                    {"version", ++version_}};
    return true;
}

bool Carruseles::borrar(const std::string& id) {
    std::lock_guard<std::mutex> lock(m_);
    return por_.erase(id) > 0;
}

json Carruseles::uno(const std::string& id) const {
    std::lock_guard<std::mutex> lock(m_);
    const auto it = por_.find(id);
    return it == por_.end() ? json() : *it;
}

json Carruseles::todos() const {
    std::lock_guard<std::mutex> lock(m_);
    json out = json::array();
    for (const auto& [id, c] : por_.items())
        out.push_back(c);
    return out;
}

} // namespace ba::gateway
