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

void GanchosTermicos::poner(const std::string& paso, Gancho g) {
    ganchos_[paso] = std::move(g);
    // Llegó tarde y su paso ya estaba apagado (sin gancho): se lo apaga ahora.
    const auto it = std::find(sinGancho_.begin(), sinGancho_.end(), paso);
    if (it != sinGancho_.end() && ganchos_[paso] && ganchos_[paso](true)) sinGancho_.erase(it);
}

std::vector<std::pair<std::string, bool>> GanchosTermicos::aplicar(int apagados) {
    std::vector<std::pair<std::string, bool>> cambios;
    const auto& p = PoliticaTermica::pasos();
    apagados = std::clamp(apagados, 0, static_cast<int>(p.size()));
    while (aplicados_ != apagados) {
        const bool apagar = apagados > aplicados_;
        const std::string& paso = p[static_cast<std::size_t>(apagar ? aplicados_ : aplicados_ - 1)];
        const auto g = ganchos_.find(paso);
        const bool sinAtender = std::find(sinGancho_.begin(), sinGancho_.end(), paso) != sinGancho_.end();
        sinGancho_.erase(std::remove(sinGancho_.begin(), sinGancho_.end(), paso), sinGancho_.end());
        // Al devolver, solo a quien lo apagó (uno sin gancho no tiene qué devolver).
        const bool atendido = g != ganchos_.end() && g->second && (apagar || !sinAtender) && g->second(apagar);
        if (apagar && !atendido) sinGancho_.push_back(paso);
        cambios.emplace_back(paso, apagar);
        aplicados_ += apagar ? 1 : -1;
    }
    return cambios;
}

} // namespace ba::diag
