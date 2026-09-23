// ============================================================================
// src/gateway/Superficies.cpp — ver Superficies.hpp
// ============================================================================
#include "Superficies.hpp"

#include <algorithm>
#include <chrono>

namespace ba::gateway {
namespace {

using json = nlohmann::json;

std::int64_t ahoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string ipTexto(std::uint32_t redes) {
    const auto b = reinterpret_cast<const unsigned char*>(&redes);
    return std::to_string(b[0]) + "." + std::to_string(b[1]) + "." + std::to_string(b[2]) + "." +
           std::to_string(b[3]);
}

/// Lo que viene de afuera se recorta: es texto que va a terminar en la
/// pantalla del operador.
std::string corto(std::string s, std::size_t max) {
    if (s.size() > max) s.resize(max);
    return s;
}

} // namespace

void Superficies::latido(const std::string& tipo, const std::string& id, const std::string& que,
                         std::uint32_t ip) {
    if (id.empty() || id.size() > 64) return;
    std::lock_guard<std::mutex> lock(m_);
    const std::int64_t ahora = ahoraMs();
    // Se limpia al anotar: no hay temporizador acá y lo viejo se va solo.
    for (auto it = por_.begin(); it != por_.end();)
        it = ahora - it->second.vistaMs > kVivaMs * 20 ? por_.erase(it) : std::next(it);
    if (por_.size() >= kMax && por_.find(id) == por_.end()) {
        auto viejo = por_.begin();
        for (auto it = por_.begin(); it != por_.end(); ++it)
            if (it->second.vistaMs < viejo->second.vistaMs) viejo = it;
        por_.erase(viejo);
    }
    Viva& v = por_[id];
    if (v.desdeMs == 0) v.desdeMs = ahora;
    v.tipo = corto(tipo, 32);
    v.que = corto(que, 160);
    v.ip = ipTexto(ip);
    v.vistaMs = ahora;
    ++v.latidos;
}

json Superficies::vivas() const {
    std::lock_guard<std::mutex> lock(m_);
    const std::int64_t ahora = ahoraMs();
    json out = json::array();
    for (const auto& [id, v] : por_) {
        const std::int64_t hace = ahora - v.vistaMs;
        if (hace > kVivaMs) continue;
        out.push_back(json{{"id", id},
                           {"tipo", v.tipo},
                           {"que", v.que},
                           {"ip", v.ip},
                           {"haceMs", hace},
                           {"desdeSeg", (ahora - v.desdeMs) / 1000},
                           {"latidos", v.latidos}});
    }
    return out;
}

} // namespace ba::gateway
