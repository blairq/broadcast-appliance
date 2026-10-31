// src/core/diag/PoliticaTermica.cpp — ver PoliticaTermica.hpp

#include "diag/PoliticaTermica.hpp"

#include <algorithm>
#include <limits>

namespace ba::diag {

using platform::NivelTermico;

const std::vector<std::string>& PoliticaTermica::pasos() {
    static const std::vector<std::string> p{"superres", "modelos", "vistas", "invitados"};
    return p;
}

int PoliticaTermica::objetivo(NivelTermico n) {
    switch (n) {
        case NivelTermico::Templado: return 1;
        case NivelTermico::Serio: return 3;
        case NivelTermico::Critico: return static_cast<int>(pasos().size());
        default: return 0;
    }
}

int PoliticaTermica::actualizar(NivelTermico n, std::int64_t ahora) {
    if (n != nivel_) {
        nivel_ = n;
        nivelDesdeMs_ = ahora;
    }
    // Apagada (o sin saber el nivel): se devuelve todo de a uno, igual que al enfriarse.
    const int meta = cfg_.activa ? objetivo(n) : 0;
    const std::int64_t enNivel = ahora - nivelDesdeMs_;
    const std::int64_t desdeCambio = ultimoCambioMs_ < 0 ? std::numeric_limits<std::int64_t>::max() : ahora - ultimoCambioMs_;
    if (meta > apagados_) {
        // Crítico no espera: un paso por medición. Lo demás, sostenido y de a escalones.
        const bool critico = n == NivelTermico::Critico;
        const bool sostenido = critico || enNivel >= std::int64_t{cfg_.sostenidoS} * 1000;
        const bool escalon = critico || apagados_ == 0 || desdeCambio >= std::int64_t{cfg_.escalonS} * 1000;
        if (sostenido && escalon) {
            ++apagados_;
            ultimoCambioMs_ = ahora;
        }
    } else if (meta < apagados_) {
        // Se devuelve recién con el nivel bajo un rato, y de a uno (el último apagado primero).
        const std::int64_t espera = std::int64_t{cfg_.recuperacionS} * 1000;
        if (enNivel >= espera && desdeCambio >= espera) {
            --apagados_;
            ultimoCambioMs_ = ahora;
        }
    }
    return apagados_;
}

bool PoliticaTermica::apagado(const std::string& paso) const {
    const auto& p = pasos();
    const auto it = std::find(p.begin(), p.end(), paso);
    return it != p.end() && (it - p.begin()) < apagados_;
}

nlohmann::json PoliticaTermica::informe(std::int64_t ahora) const {
    nlohmann::json ap = nlohmann::json::array();
    for (int i = 0; i < apagados_; ++i) ap.push_back(pasos()[static_cast<std::size_t>(i)]);
    return {{"activa", cfg_.activa},
            {"nivel", platform::nombre(nivel_)},
            {"apagados", ap},
            {"siguiente", apagados_ < static_cast<int>(pasos().size()) ? nlohmann::json(pasos()[static_cast<std::size_t>(apagados_)])
                                                                     : nlohmann::json(nullptr)},
            {"nivel_desde_s", nivelDesdeMs_ < 0 ? 0 : (ahora - nivelDesdeMs_) / 1000}};
}

} // namespace ba::diag
