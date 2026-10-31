// SPDX-License-Identifier: GPL-2.0-or-later
#include "fault/FallasProgramadas.hpp"

#include "i18n/I18n.hpp"

#include <algorithm>
#include <chrono>

namespace ba::fault {

using json = nlohmann::json;

FallasProgramadas& FallasProgramadas::instancia() {
    static FallasProgramadas f;
    return f;
}

std::int64_t FallasProgramadas::ahora() const {
    if (ahoraMs_ != nullptr) return ahoraMs_();
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string FallasProgramadas::programar(const std::string& salida, const std::string& pieza, std::int64_t enMs) {
    if (salida != "grabacion" && salida != "transmision")
        return ba::i18n::traducir("fallas", "`salida`: grabacion o transmision");
    if (pieza != "codificador" && pieza != "sumidero") return ba::i18n::traducir("fallas", "`pieza`: codificador o sumidero");
    std::lock_guard<std::mutex> l(mu_);
    pendientes_.push_back({salida, pieza, ahora() + std::max<std::int64_t>(0, enMs)});
    cuantas_ = static_cast<int>(pendientes_.size());
    return {};
}

void FallasProgramadas::limpiar() {
    std::lock_guard<std::mutex> l(mu_);
    pendientes_.clear();
    disparadas_ = json::array();
    cuantas_ = 0;
}

std::string FallasProgramadas::tomar(const std::string& salida) {
    if (!hay()) return {};
    std::lock_guard<std::mutex> l(mu_);
    const std::int64_t t = ahora();
    for (auto it = pendientes_.begin(); it != pendientes_.end(); ++it)
        if (it->salida == salida && it->cuandoMs <= t) {
            const std::string pieza = it->pieza;
            disparadas_.push_back({{"salida", salida}, {"pieza", pieza}, {"atraso_ms", t - it->cuandoMs}});
            pendientes_.erase(it);
            cuantas_ = static_cast<int>(pendientes_.size());
            return pieza;
        }
    return {};
}

json FallasProgramadas::estado() const {
    std::lock_guard<std::mutex> l(mu_);
    const std::int64_t t = ahora();
    json p = json::array();
    for (const auto& f : pendientes_) p.push_back({{"salida", f.salida}, {"pieza", f.pieza}, {"faltan_ms", std::max<std::int64_t>(0, f.cuandoMs - t)}});
    return {{"programadas", p}, {"disparadas", disparadas_}};
}

} // namespace ba::fault
