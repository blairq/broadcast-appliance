// ============================================================================
// src/gateway/rtc/CapasHilos.cpp — ver CapasHilos.hpp
// ============================================================================
#include "rtc/CapasHilos.hpp"

#include <algorithm>

#include "ajustes/Ajustes.hpp"
#include "rtc/ColasCompartidas.hpp" // nucleosDeUnBloque

namespace ba::gateway::capas {

Tamanos resolver(int red, int trabajo, int nucleos) {
    Tamanos t;
    const int n = nucleos > 0 ? nucleos : 0;
    t.red = red > 0 ? std::min(red, 16) : std::clamp(n / 3, 1, 4);
    t.trabajo = trabajo > 0 ? std::min(trabajo, 16) : std::clamp((n + 1) / 2, 1, 6);
    return t;
}

Tamanos deLosAjustes() {
    return resolver(ba::ajustes::entero("gateway.hilosRed", 0), ba::ajustes::entero("gateway.hilosTrabajo", 0),
                    colas::nucleosDeUnBloque());
}

namespace {
Tamanos emparejar(Tamanos t, bool cruzado) {
    if (!cruzado) t.trabajo = t.red;
    return t;
}
} // namespace

Reparto::Reparto(Tamanos t, bool cruzado)
    : t_(emparejar(t, cruzado)), cruzado_(cruzado), red_(size_t(std::max(1, t_.red))), trabajo_(size_t(std::max(1, t_.trabajo))) {}

int Reparto::elegir(const std::vector<Capa>& v) {
    int mejor = 0;
    for (int i = 1; i < int(v.size()); ++i) {
        const Capa& a = v[size_t(i)];
        const Capa& b = v[size_t(mejor)];
        if (a.carga < b.carga || (a.carga == b.carga && a.sesiones < b.sesiones)) mejor = i;
    }
    return mejor;
}

Lugar Reparto::tomar(int peso) {
    std::lock_guard<std::mutex> l(m_);
    Lugar x;
    x.peso = std::max(1, peso);
    x.red = elegir(red_);
    x.trabajo = cruzado_ ? elegir(trabajo_) : x.red;
    red_[size_t(x.red)].carga += x.peso;
    red_[size_t(x.red)].sesiones += 1;
    trabajo_[size_t(x.trabajo)].carga += x.peso;
    trabajo_[size_t(x.trabajo)].sesiones += 1;
    return x;
}

void Reparto::cambiarPeso(Lugar& x, int peso) {
    peso = std::max(1, peso);
    std::lock_guard<std::mutex> l(m_);
    if (!x.valido() || x.peso == peso) return;
    red_[size_t(x.red)].carga += peso - x.peso;
    trabajo_[size_t(x.trabajo)].carga += peso - x.peso;
    x.peso = peso;
}

void Reparto::soltar(Lugar& x) {
    std::lock_guard<std::mutex> l(m_);
    if (!x.valido()) return;
    red_[size_t(x.red)].carga -= x.peso;
    red_[size_t(x.red)].sesiones -= 1;
    trabajo_[size_t(x.trabajo)].carga -= x.peso;
    trabajo_[size_t(x.trabajo)].sesiones -= 1;
    x = Lugar{};
}

int Reparto::cargaRed(int i) const {
    std::lock_guard<std::mutex> l(m_);
    return i >= 0 && i < int(red_.size()) ? red_[size_t(i)].carga : 0;
}
int Reparto::cargaTrabajo(int j) const {
    std::lock_guard<std::mutex> l(m_);
    return j >= 0 && j < int(trabajo_.size()) ? trabajo_[size_t(j)].carga : 0;
}
int Reparto::sesionesRed(int i) const {
    std::lock_guard<std::mutex> l(m_);
    return i >= 0 && i < int(red_.size()) ? red_[size_t(i)].sesiones : 0;
}

nlohmann::json Reparto::estado() const {
    std::lock_guard<std::mutex> l(m_);
    nlohmann::json r = nlohmann::json::array(), t = nlohmann::json::array();
    for (size_t i = 0; i < red_.size(); ++i)
        r.push_back({{"hilo", "ba-rtc-net/" + std::to_string(i + 1)}, {"carga", red_[i].carga}, {"sesiones", red_[i].sesiones}});
    for (size_t j = 0; j < trabajo_.size(); ++j)
        t.push_back(
            {{"hilo", "ba-rtc-work/" + std::to_string(j + 1)}, {"carga", trabajo_[j].carga}, {"sesiones", trabajo_[j].sesiones}});
    return {{"red", r}, {"trabajo", t}};
}

} // namespace ba::gateway::capas
