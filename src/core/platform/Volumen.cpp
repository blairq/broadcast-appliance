// ============================================================================
// src/core/platform/Volumen.cpp — ver Volumen.hpp (lo común a los sistemas)
// ============================================================================
#include "platform/Volumen.hpp"

#include <algorithm>
#include <map>
#include <system_error>

namespace ba::platform {

namespace {
/// La ruta, o su primer antecesor que existe (vacía si ninguno).
std::filesystem::path existente(std::filesystem::path p) {
    std::error_code ec;
    p = std::filesystem::absolute(p, ec);
    while (!p.empty() && !std::filesystem::exists(p, ec)) {
        if (p == p.parent_path()) return {};
        p = p.parent_path();
    }
    return p;
}
} // namespace

std::optional<std::uint64_t> volumenDe(const std::filesystem::path& ruta) {
    if (ruta.empty()) return std::nullopt;
    const std::filesystem::path p = existente(ruta);
    if (p.empty()) return std::nullopt;
    return volumenDeExistente(p);
}

bool mismoVolumen(const std::filesystem::path& a, const std::filesystem::path& b) {
    const auto va = volumenDe(a), vb = volumenDe(b);
    return !va || !vb || *va == *vb;
}

double bytesPorHora(double kbps) {
    // +5 %: lo que suma el contenedor (Matroska) sobre los bitrates de video y audio.
    return kbps > 0 ? kbps * 1000.0 / 8.0 * 3600.0 * 1.05 : 0.0;
}

DiscoRestante sumarPorVolumen(const std::vector<EscrituraEnVolumen>& escrituras) {
    DiscoRestante r;
    std::map<std::string, std::size_t> indice;
    for (const EscrituraEnVolumen& e : escrituras) {
        auto it = indice.find(e.volumen);
        if (it == indice.end()) {
            it = indice.emplace(e.volumen, r.volumenes.size()).first;
            VolumenRestante v;
            v.volumen = e.volumen;
            v.carpeta = e.carpeta;
            v.libresGb = e.libresBytes >= 0 ? static_cast<double>(e.libresBytes) / 1e9 : -1.0;
            r.volumenes.push_back(std::move(v));
        }
        VolumenRestante& v = r.volumenes[it->second];
        // El espacio es del volumen: si una de sus escrituras lo sabe, vale para todas.
        if (v.libresGb < 0 && e.libresBytes >= 0) v.libresGb = static_cast<double>(e.libresBytes) / 1e9;
        v.kbps += std::max(0.0, e.kbps);
        v.que.push_back(e.que);
        r.kbps += std::max(0.0, e.kbps);
    }
    for (VolumenRestante& v : r.volumenes) {
        const double porHora = bytesPorHora(v.kbps);
        v.horas = v.libresGb >= 0 && porHora > 0 ? v.libresGb * 1e9 / porHora : -1.0;
        if (v.horas >= 0 && (r.horas < 0 || v.horas < r.horas)) {
            r.horas = v.horas;
            r.libresGb = v.libresGb;
        }
    }
    return r;
}

DiscoRestante discoRestante(const std::vector<EscrituraDisco>& escrituras) {
    std::vector<EscrituraEnVolumen> ev;
    ev.reserve(escrituras.size());
    for (const EscrituraDisco& e : escrituras) {
        EscrituraEnVolumen x;
        x.que = e.que;
        x.kbps = e.kbps;
        x.carpeta = e.carpeta.string();
        const std::filesystem::path p = existente(e.carpeta);
        const auto v = p.empty() ? std::nullopt : volumenDeExistente(p);
        // Sin volumen conocido, cada carpeta cuenta aparte (no se suma a otra que quizá no comparte disco).
        x.volumen = v ? std::to_string(*v) : "?" + e.carpeta.string();
        std::error_code ec;
        const auto info = p.empty() ? std::filesystem::space_info{} : std::filesystem::space(p, ec);
        x.libresBytes = p.empty() || ec ? -1 : static_cast<std::int64_t>(info.available);
        ev.push_back(std::move(x));
    }
    return sumarPorVolumen(ev);
}

nlohmann::json aJson(const DiscoRestante& d) {
    nlohmann::json vs = nlohmann::json::array();
    for (const VolumenRestante& v : d.volumenes)
        vs.push_back({{"volumen", v.volumen}, {"carpeta", v.carpeta}, {"libre_gb", v.libresGb}, {"kbps", v.kbps}, {"horas", v.horas}, {"que", v.que}});
    return {{"horas", d.horas}, {"libre_gb", d.libresGb}, {"kbps", d.kbps}, {"volumenes", std::move(vs)}};
}

} // namespace ba::platform
