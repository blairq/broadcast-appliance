// src/gateway/TopeRetorno.cpp — la resolución máxima del retorno de la sala (ver el .hpp)

#include "TopeRetorno.hpp"

#include "i18n/I18nTexto.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ba::gateway {

using ba::i18n::llenar;
using ba::i18n::traducir;

namespace {
constexpr std::size_t kVentana = 3;
constexpr std::size_t kDecisiones = 16;
/// Si al volver a un escalón vuelve a no llegar antes de esto, la próxima espera se duplica.
constexpr std::int64_t kRecaidaMs = 60000;

double redondear(double v) { return v < 0 ? v : std::round(v * 10) / 10; }
std::string num(double v) {
    if (v < 0) return "?";
    char b[32];
    std::snprintf(b, sizeof(b), "%.0f", v);
    return b;
}
std::string num1(double v) {
    if (v < 0) return "?";
    char b[32];
    std::snprintf(b, sizeof(b), "%.1f", v);
    return b;
}
nlohmann::json aJson(const MedidaRetorno& m) {
    return {{"cpu_maquina", redondear(m.cpuMaquina)},
            {"cpu_gateway", redondear(m.cpuGateway)},
            {"cps", redondear(m.cpsLogrado)},
            {"cps_debido", redondear(m.cpsDebido)},
            {"descartes_pct", redondear(m.descartesPct)},
            {"oyentes", m.oyentes}};
}
} // namespace

int altoDeAjusteTope(const std::string& valor) {
    if (valor == "360p") return 360;
    if (valor == "480p") return 480;
    if (valor == "720p") return 720;
    if (valor == "1080p") return 1080;
    return 0;
}

std::vector<int> escalonesDesde(int altoMax) {
    std::vector<int> out;
    for (const int e : {1080, 720, 480, 360})
        if (e <= altoMax) out.push_back(e);
    if (altoMax > 0 && (out.empty() || out.front() != altoMax)) out.insert(out.begin(), altoMax);
    return out;
}

const char* ReguladorRetorno::nombreCausa(Causa c) {
    switch (c) {
        case Causa::Inicio: return "inicio";
        case Causa::Fijo: return "fijo";
        case Causa::Cps: return "cps";
        case Causa::Codificador: return "codificador";
        case Causa::Margen: return "margen";
    }
    return "inicio";
}

void ReguladorRetorno::ajustes(const AjustesTope& a) {
    const std::vector<int> antes = a_.escalones;
    a_ = a;
    if (a_.escalones.empty()) a_.escalones = {720};
    if (a_.escalones != antes) {
        // Otros escalones (cambiaron las calidades): se arranca de nuevo desde arriba.
        escalon_ = 0;
        recuerdos_.clear();
    }
    recuerdos_.resize(a_.escalones.size());
}

int ReguladorRetorno::tope() const {
    if (a_.fijo > 0) return a_.fijo;
    return a_.escalones[static_cast<std::size_t>(std::clamp(escalon_, 0, static_cast<int>(a_.escalones.size()) - 1))];
}

MedidaRetorno ReguladorRetorno::promedio() const {
    MedidaRetorno p;
    if (ventana_.empty()) return p;
    const auto prom = [&](double MedidaRetorno::*campo) {
        double s = 0;
        int n = 0;
        for (const MedidaRetorno& m : ventana_)
            if (m.*campo >= 0) {
                s += m.*campo;
                ++n;
            }
        return n > 0 ? s / n : -1.0;
    };
    p.cpuMaquina = prom(&MedidaRetorno::cpuMaquina);
    p.cpuGateway = prom(&MedidaRetorno::cpuGateway);
    p.cpsLogrado = prom(&MedidaRetorno::cpsLogrado);
    p.cpsDebido = prom(&MedidaRetorno::cpsDebido);
    p.descartesPct = prom(&MedidaRetorno::descartesPct);
    p.oyentes = ventana_.back().oyentes;
    return p;
}

void ReguladorRetorno::cambiar(int escalon, Causa c, const MedidaRetorno& m, std::int64_t ahoraMs) {
    Decision d;
    d.ms = ahoraMs;
    d.de = tope();
    escalon_ = escalon;
    causa_ = c;
    medidaCambio_ = m;
    d.a = tope();
    d.causa = c;
    d.motivo = motivo();
    decisiones_.push_back(std::move(d));
    while (decisiones_.size() > kDecisiones) decisiones_.pop_front();
    ultimoCambio_ = ahoraMs;
    desdeMs_ = ahoraMs;
    presionDesde_ = margenDesde_ = 0;
}

bool ReguladorRetorno::medir(const MedidaRetorno& m, std::int64_t ahoraMs) {
    ventana_.push_back(m);
    while (ventana_.size() > kVentana) ventana_.pop_front();
    const MedidaRetorno p = promedio();

    // ¿Cambió el modo (Configuración)? Fijo: rige ya. De vuelta a automático: desde arriba.
    if (a_.fijo != fijoAntes_) {
        fijoAntes_ = a_.fijo;
        recuerdos_.assign(a_.escalones.size(), Recuerdo{});
        cambiar(0, a_.fijo > 0 ? Causa::Fijo : Causa::Inicio, p, ahoraMs);
        ultimoCambio_ = 0; // un cambio a mano no hace esperar al regulador
        return true;
    }
    if (a_.fijo > 0) return false;

    const int n = static_cast<int>(a_.escalones.size());
    const double ratio = p.cpsDebido > 0 && p.cpsLogrado >= 0 ? p.cpsLogrado / p.cpsDebido : -1;
    const bool maquinaAlta = p.cpuMaquina >= a_.cpuAlta;
    const bool descartaMucho = p.descartesPct >= a_.descartesAltoPct;
    const bool cpsCorto = ratio >= 0 && ratio < a_.cpsBajo && maquinaAlta;
    const bool presion = descartaMucho || cpsCorto;
    const bool margen = (ratio < 0 || ratio >= a_.cpsBien) && (p.cpuMaquina < 0 || p.cpuMaquina <= a_.cpuBaja) &&
                        (p.descartesPct < 0 || p.descartesPct <= a_.descartesBienPct);
    const bool pasoLaEspera = ultimoCambio_ == 0 || ahoraMs - ultimoCambio_ >= a_.esperaMs;

    if (presion) {
        margenDesde_ = 0;
        if (presionDesde_ == 0) presionDesde_ = ahoraMs;
        if (escalon_ < n - 1 && ahoraMs - presionDesde_ >= a_.sostenerBajarMs && pasoLaEspera) {
            // Se anota con cuánta gente no llegaba este escalón (y si recayó enseguida de
            // volver a él, la próxima vez espera el doble).
            Recuerdo& r = recuerdos_[static_cast<std::size_t>(escalon_)];
            const bool recayo = r.subioMs > 0 && ahoraMs - r.subioMs < kRecaidaMs;
            r.esperarMs = recayo ? std::min(std::max(r.esperarMs, a_.reintentarMs) * 2, a_.reintentarMaxMs) : a_.reintentarMs;
            r.oyentes = p.oyentes;
            r.bajoMs = ahoraMs;
            cambiar(escalon_ + 1, descartaMucho ? Causa::Codificador : Causa::Cps, p, ahoraMs);
            return true;
        }
        return false;
    }
    if (margen) {
        presionDesde_ = 0;
        if (margenDesde_ == 0) margenDesde_ = ahoraMs;
        if (escalon_ > 0 && ahoraMs - margenDesde_ >= a_.sostenerSubirMs && pasoLaEspera) {
            Recuerdo& r = recuerdos_[static_cast<std::size_t>(escalon_ - 1)];
            const bool menosGente = r.oyentes < 0 || p.oyentes < 0.8 * r.oyentes;
            if (menosGente || ahoraMs - r.bajoMs >= r.esperarMs) {
                r.subioMs = ahoraMs;
                cambiar(escalon_ - 1, Causa::Margen, p, ahoraMs);
                return true;
            }
        }
        return false;
    }
    // Entre los dos umbrales: se queda como está (la histéresis).
    presionDesde_ = margenDesde_ = 0;
    return false;
}

std::string ReguladorRetorno::motivo() const {
    const MedidaRetorno& m = medidaCambio_;
    const std::string alto = std::to_string(tope());
    switch (causa_) {
        case Causa::Fijo:
            return llenar(traducir("gateway", "retorno: fijo en %1p (Configuración)"), {alto});
        case Causa::Inicio:
            return llenar(traducir("gateway", "retorno: automático, arranca en %1p"), {alto});
        case Causa::Cps:
            return llenar(traducir("gateway", "retorno: baja a %1p: salía a %2 de %3 cps con la máquina al %4 % (%5 invitados)"),
                          {alto, num1(m.cpsLogrado), num1(m.cpsDebido), num(m.cpuMaquina), std::to_string(m.oyentes)});
        case Causa::Codificador:
            return llenar(traducir("gateway", "retorno: baja a %1p: el codificador descartaba el %2 % de los cuadros"),
                          {alto, num1(m.descartesPct)});
        case Causa::Margen:
            return llenar(traducir("gateway", "retorno: sube a %1p: sobra margen (salía a %2 de %3 cps, máquina al %4 %, %5 invitados)"),
                          {alto, num1(m.cpsLogrado), num1(m.cpsDebido), num(m.cpuMaquina), std::to_string(m.oyentes)});
    }
    return {};
}

nlohmann::json ReguladorRetorno::estado(bool historial) const {
    nlohmann::json j = {{"modo", automatico() ? "auto" : "fijo"},
                        {"alto", tope()},
                        {"causa", nombreCausa(causa_)},
                        {"motivo", motivo()},
                        {"desdeMs", desdeMs_},
                        {"medida", aJson(promedio())},
                        {"decision", aJson(medidaCambio_)},
                        {"escalones", a_.escalones}};
    if (historial) {
        nlohmann::json dec = nlohmann::json::array();
        for (const Decision& d : decisiones_)
            dec.push_back({{"ms", d.ms}, {"de", d.de}, {"a", d.a}, {"causa", nombreCausa(d.causa)}, {"motivo", d.motivo}});
        j["decisiones"] = std::move(dec);
        j["umbrales"] = {{"cpu_alta", a_.cpuAlta},
                         {"cpu_baja", a_.cpuBaja},
                         {"cps_bajo", a_.cpsBajo},
                         {"cps_bien", a_.cpsBien},
                         {"descartes_alto_pct", a_.descartesAltoPct},
                         {"sostener_bajar_ms", a_.sostenerBajarMs},
                         {"sostener_subir_ms", a_.sostenerSubirMs},
                         {"espera_ms", a_.esperaMs}};
    }
    return j;
}

} // namespace ba::gateway
