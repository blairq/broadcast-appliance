// src/app/sala/QuienMira.cpp — quién mira el programa de cada salida (ver el .hpp)

#include "sala/QuienMira.hpp"

namespace ba::app::sala {

using Reloj = std::chrono::steady_clock;

QuienMira::QuienMira(Fuentes fuentes, Pedir pedir, std::chrono::milliseconds gracia, QObject* parent)
    : QObject(parent), fuentes_(std::move(fuentes)), pedir_(std::move(pedir)), gracia_(gracia) {
    vence_.setSingleShot(true);
    connect(&vence_, &QTimer::timeout, this, &QuienMira::revisar);
}

std::optional<bool> QuienMira::enUso(const std::string& salida) const {
    const auto it = salidas_.find(salida);
    return it == salidas_.end() ? std::nullopt : it->second.pedido;
}

void QuienMira::revisarUna(const std::string& id, bool mira, Reloj::time_point ahora,
                           std::optional<std::chrono::milliseconds>& proxima) {
    Estado& e = salidas_[id];
    if (mira) e.visto = ahora;
    const auto desde = e.visto ? std::chrono::duration_cast<std::chrono::milliseconds>(ahora - *e.visto) : gracia_;
    const bool si = desde < gracia_;
    // Nadie mira pero todavía está en la gracia: se vuelve a mirar cuando vence.
    if (si && !mira) proxima = std::min(proxima.value_or(gracia_), gracia_ - desde);
    if (e.pedido == si) return;
    e.pedido = si;
    if (pedir_) pedir_(id, si);
}

void QuienMira::revisar() {
    const Reloj::time_point ahora = Reloj::now();
    std::optional<std::chrono::milliseconds> proxima;
    revisarUna({}, fuentes_.porOmision && fuentes_.porOmision(), ahora, proxima);
    const std::map<std::string, bool> otras = fuentes_.salidas ? fuentes_.salidas() : std::map<std::string, bool>{};
    for (const auto& [id, mira] : otras)
        if (!id.empty()) revisarUna(id, mira, ahora, proxima);
    // Las que ya no están: lo suyo se olvida (el motor las sacó con la salida).
    for (auto it = salidas_.begin(); it != salidas_.end();)
        it = !it->first.empty() && !otras.count(it->first) ? salidas_.erase(it) : std::next(it);
    if (proxima)
        vence_.start(std::max<int>(1, static_cast<int>(proxima->count()) + 1));
    else
        vence_.stop();
}

} // namespace ba::app::sala
