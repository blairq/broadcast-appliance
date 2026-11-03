// src/gateway/CpsSegunCarga.cpp — los cps de la capa baja según la carga (ver el .hpp)

#include "CpsSegunCarga.hpp"

#include <algorithm>
#include <cmath>

namespace ba::gateway {

namespace {
constexpr std::size_t kDecisiones = 16;

double redondear(double v) { return v < 0 ? v : std::round(v * 10) / 10; }
} // namespace

void ControlCps::ajustes(const AjustesCps& a) {
    a_ = a;
    if (a_.niveles.empty()) a_.niveles = {15};
    if (a_.partes < 1) a_.partes = 1;
    escalon_ = std::clamp(escalon_, 0, escalonMax());
}

int ControlCps::escalonMax() const {
    return static_cast<int>(a_.niveles.size() > 0 ? a_.niveles.size() - 1 : 0) * std::max(1, a_.partes);
}

double ControlCps::carga(const MedidaCarga& m, double presupuestoMs, double perdidosTope) {
    double c = -1;
    if (m.motorPct >= 0) c = std::max(c, std::min(100.0, m.motorPct));
    if (presupuestoMs > 0 && m.decodificarMs >= 0) c = std::max(c, std::min(100.0, m.decodificarMs / presupuestoMs * 100.0));
    // Los perdidos no dicen cuánto falta para saturar, solo que ya se saturó: 100 o nada.
    if (perdidosTope > 0 && m.perdidosPct >= 0) c = std::max(c, m.perdidosPct >= perdidosTope ? 100.0 : 0.0);
    return c;
}

void ControlCps::cambiar(int a, double carga, const char* motivo, std::int64_t ahoraMs) {
    Decision d;
    d.ms = ahoraMs;
    d.de = escalon_;
    d.a = a;
    d.carga = carga;
    d.motivo = motivo;
    decisiones_.push_back(std::move(d));
    while (decisiones_.size() > kDecisiones) decisiones_.pop_front();
    escalon_ = a;
    ultimoCambio_ = ahoraMs;
}

bool ControlCps::medir(double carga, const MedidaCarga& m, std::int64_t ahoraMs) {
    carga_ = carga;
    medida_ = m;
    if (!a_.activo) {
        altaDesde_ = bajaDesde_ = 0;
        if (escalon_ == 0) return false;
        cambiar(0, carga, "apagado", ahoraMs);
        return true;
    }
    const bool alta = carga >= a_.subir;
    const bool baja = carga < 0 || carga <= a_.bajar; // sin medida: no se degrada a ciegas
    if (alta) {
        bajaDesde_ = 0;
        if (altaDesde_ == 0) altaDesde_ = ahoraMs;
    } else if (baja) {
        altaDesde_ = 0;
        if (bajaDesde_ == 0) bajaDesde_ = ahoraMs;
    } else {
        // Entre los dos umbrales: se queda como está (la histéresis).
        altaDesde_ = bajaDesde_ = 0;
        return false;
    }
    const bool pasoLaEspera = ultimoCambio_ == 0 || ahoraMs - ultimoCambio_ >= a_.esperaMs;
    if (alta && escalon_ < escalonMax() && ahoraMs - altaDesde_ >= a_.sostenerSubirMs && pasoLaEspera) {
        cambiar(escalon_ + 1, carga, "carga alta", ahoraMs);
        altaDesde_ = ahoraMs; // el próximo escalón, si sigue alta otro tanto
        return true;
    }
    if (baja && escalon_ > 0 && ahoraMs - bajaDesde_ >= a_.sostenerBajarMs && pasoLaEspera) {
        cambiar(escalon_ - 1, carga, carga < 0 ? "sin medida" : "carga baja", ahoraMs);
        bajaDesde_ = ahoraMs;
        return true;
    }
    return false;
}

std::map<std::string, double> ControlCps::repartir(std::vector<CandidatoCps> candidatos, int escalon,
                                                   const std::vector<double>& niveles, int partes) {
    std::map<std::string, double> out;
    if (niveles.empty() || candidatos.empty()) return out;
    partes = std::max(1, partes);
    std::sort(candidatos.begin(), candidatos.end(), [](const CandidatoCps& x, const CandidatoCps& y) {
        return x.alto != y.alto ? x.alto < y.alto : x.id < y.id;
    });
    const int ultimo = static_cast<int>(niveles.size()) - 1;
    const int base = std::max(0, escalon) / partes;
    const int resto = std::max(0, escalon) % partes;
    const std::size_t n = candidatos.size();
    for (std::size_t r = 0; r < n; ++r) {
        // A qué parte cae (0 = la menos importante): las primeras `resto` bajan uno más.
        const int parte = static_cast<int>(r * static_cast<std::size_t>(partes) / n);
        const int nivel = std::min(ultimo, base + (parte < resto ? 1 : 0));
        out[candidatos[r].id] = niveles[static_cast<std::size_t>(nivel)];
    }
    return out;
}

std::map<std::string, double> ControlCps::repartir(std::vector<CandidatoCps> candidatos) const {
    return repartir(std::move(candidatos), escalon_, a_.niveles, a_.partes);
}

nlohmann::json ControlCps::estado() const {
    nlohmann::json dec = nlohmann::json::array();
    for (const Decision& d : decisiones_)
        dec.push_back({{"ms", d.ms}, {"de", d.de}, {"a", d.a}, {"carga", redondear(d.carga)}, {"motivo", d.motivo}});
    return {{"activo", a_.activo},
            {"escalon", escalon_},
            {"escalon_max", escalonMax()},
            {"carga", redondear(carga_)},
            {"medida",
             {{"motor_pct", redondear(medida_.motorPct)},
              {"decodificar_ms", redondear(medida_.decodificarMs)},
              {"perdidos_pct", redondear(medida_.perdidosPct)}}},
            {"umbrales", {{"subir", a_.subir}, {"bajar", a_.bajar}}},
            {"niveles", a_.niveles},
            {"partes", a_.partes},
            {"decisiones", std::move(dec)}};
}

} // namespace ba::gateway
