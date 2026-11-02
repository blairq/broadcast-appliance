// src/app/sala/EnlaceNecesita.cpp — cuánto enlace necesita el evento (ver el .hpp)

#include "sala/EnlaceNecesita.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "i18n/I18n.hpp"

namespace ba::app::sala::enlace {

using ba::i18n::llenar;
using ba::i18n::traducir;
using json = nlohmann::json;

namespace {

std::string decimal(double x) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.1f", x);
    return b;
}
std::string mbps(double kbps) { return decimal(kbps / 1000.0); }

int peor(Veredicto a, Veredicto b) {
    const auto rango = [](Veredicto v) {
        switch (v) {
        case Veredicto::Entra: return 1;
        case Veredicto::Justo: return 2;
        case Veredicto::NoEntra: return 3;
        default: return 0;
        }
    };
    return std::max(rango(a), rango(b));
}

Veredicto deRango(int r) {
    switch (r) {
    case 1: return Veredicto::Entra;
    case 2: return Veredicto::Justo;
    case 3: return Veredicto::NoEntra;
    default: return Veredicto::SinMedir;
    }
}

/// Un sentido: lo medido contra lo que hace falta.
Veredicto sentido(double medido, bool alMenos, double nec, const Coeficientes& c, bool subida,
                  std::vector<std::string>& motivos) {
    if (medido < 0) return Veredicto::SinMedir;
    if (nec <= 0) return Veredicto::Entra;
    const double r = medido / nec;
    if (r >= c.margen) return Veredicto::Entra;
    const std::string que = subida ? traducir("sala", "subida") : traducir("sala", "bajada");
    if (r >= 1.0) {
        motivos.push_back(llenar(traducir("sala", "%1: %2 Mbps medidos para %3 Mbps que hacen falta (sin margen)"),
                                 {que, mbps(medido), mbps(nec)}));
        return Veredicto::Justo;
    }
    if (alMenos) {
        motivos.push_back(llenar(traducir("sala", "%1: se midió hasta %2 Mbps sin llenar el enlace y hacen falta %3 Mbps: "
                                                  "medí con un tope más alto"),
                                 {que, mbps(medido), mbps(nec)}));
        return Veredicto::Justo;
    }
    motivos.push_back(llenar(traducir("sala", "%1: %2 Mbps medidos y hacen falta %3 Mbps"), {que, mbps(medido), mbps(nec)}));
    return Veredicto::NoEntra;
}

/// ¿Con `n` invitados el sentido entra con este factor (1 = justo, margen = entra)?
bool alcanza(double medido, double nec, double factor) {
    if (medido < 0 || nec <= 0) return true; // sin medir: no limita
    return medido >= nec * factor;
}

} // namespace

const char* nombre(Veredicto v) {
    switch (v) {
    case Veredicto::Entra: return "entra";
    case Veredicto::Justo: return "justo";
    case Veredicto::NoEntra: return "no_entra";
    default: return "sin_medir";
    }
}

Necesidad necesita(const Perfil& p, const Coeficientes& c) {
    Necesidad n;
    const int inv = std::max(0, p.invitados);
    const bool todosAlta = p.altaHasta <= 0 || inv <= p.altaHasta;
    n.enAlta = todosAlta ? inv : std::min(inv, c.enAltaConMuchos);
    n.subidaKbps = inv * c.salidaPorInvitadoKbps + std::max(0.0, p.extraSubidaKbps);
    n.bajadaKbps = n.enAlta * c.entradaAltaKbps + (inv - n.enAlta) * c.entradaBajaKbps;
    n.subidaEnlaceKbps = n.subidaKbps * c.wireguard;
    n.bajadaEnlaceKbps = n.bajadaKbps * c.wireguard;
    return n;
}

Medicion deResultado(const json& u) {
    Medicion m;
    if (!u.is_object() || !u.value("ok", false)) return m;
    const auto leer = [](const json& s, double& kbps, bool& alMenos, double& perd) {
        if (!s.is_object() || !s.contains("capacidadKbps")) return;
        const double sinPerdida = s.value("kbpsSinPerdida", 0.0);
        kbps = sinPerdida > 0 ? sinPerdida : s.value("capacidadKbps", 0.0);
        alMenos = !s.value("saturo", true) && sinPerdida > 0;
        perd = s.value("perdida", 0.0);
    };
    const json sub = u.value("subida", json::object());
    const json baj = u.value("bajada", json::object());
    leer(sub, m.subidaKbps, m.subidaAlMenos, m.perdidaSubida);
    leer(baj, m.bajadaKbps, m.bajadaAlMenos, m.perdidaBajada);
    m.jitterMs = std::max(sub.value("jitterMs", 0.0), baj.value("jitterMs", 0.0));
    m.rttReposoMs = u.value("reposo", json::object()).value("rttMs", -1.0);
    m.rttCargaMs = std::max(sub.value("rttCargaMs", -1.0), baj.value("rttCargaMs", -1.0));
    return m;
}

Juicio juzgar(const Perfil& p, const Medicion& m, const Coeficientes& c) {
    Juicio j;
    const double wg = m.porTunel ? 1.0 : c.wireguard;
    const Necesidad n = necesita(p, c);
    j.subida = sentido(m.subidaKbps, m.subidaAlMenos, n.subidaKbps * wg, c, true, j.motivos);
    j.bajada = sentido(m.bajadaKbps, m.bajadaAlMenos, n.bajadaKbps * wg, c, false, j.motivos);
    // La subida es la que manda: sin medirla, no hay veredicto.
    if (j.subida == Veredicto::SinMedir) {
        j.veredicto = Veredicto::SinMedir;
        return j;
    }
    int r = peor(j.subida, j.bajada);
    // La calidad con el enlace cargado: puede bajar un «entra» a «justo».
    std::vector<std::string> calidad;
    const double perd = std::max(m.perdidaSubida, m.perdidaBajada);
    if (perd > c.perdidaJusta)
        calidad.push_back(llenar(traducir("sala", "pierde %1 % de los paquetes con carga"), {decimal(perd * 100)}));
    if (m.jitterMs > c.jitterJustoMs)
        calidad.push_back(llenar(traducir("sala", "jitter de %1 ms"), {std::to_string(static_cast<int>(std::lround(m.jitterMs)))}));
    if (m.rttReposoMs >= 0 && m.rttCargaMs >= 0 && m.rttCargaMs - m.rttReposoMs > c.colaJustaMs)
        calidad.push_back(llenar(traducir("sala", "con el enlace cargado la demora sube de %1 a %2 ms"),
                                 {std::to_string(static_cast<int>(std::lround(m.rttReposoMs))),
                                  std::to_string(static_cast<int>(std::lround(m.rttCargaMs)))}));
    if (!calidad.empty() && r == 1) r = 2;
    for (auto& t : calidad) j.motivos.push_back(std::move(t));
    j.veredicto = deRango(r);
    // ¿Hasta cuántos?
    const auto hasta = [&](double factor) {
        int ultimo = 0;
        for (int k = 1; k <= kMaxInvitados; ++k) {
            Perfil q = p;
            q.invitados = k;
            const Necesidad nk = necesita(q, c);
            if (!alcanza(m.subidaKbps, nk.subidaKbps * wg, factor) || !alcanza(m.bajadaKbps, nk.bajadaKbps * wg, factor))
                break;
            ultimo = k;
        }
        return ultimo;
    };
    j.entranConMargen = hasta(c.margen);
    j.entranJusto = hasta(1.0);
    return j;
}

json aJson(const Perfil& p, const Necesidad& n, const Juicio& j) {
    return {{"invitados", p.invitados},
            {"altaHasta", p.altaHasta},
            {"necesita",
             {{"subidaKbps", std::lround(n.subidaKbps)},
              {"bajadaKbps", std::lround(n.bajadaKbps)},
              {"subidaEnlaceKbps", std::lround(n.subidaEnlaceKbps)},
              {"bajadaEnlaceKbps", std::lround(n.bajadaEnlaceKbps)},
              {"enAlta", n.enAlta}}},
            {"veredicto", nombre(j.veredicto)},
            {"subida", nombre(j.subida)},
            {"bajada", nombre(j.bajada)},
            {"motivos", j.motivos},
            {"entranConMargen", j.entranConMargen},
            {"entranJusto", j.entranJusto}};
}

} // namespace ba::app::sala::enlace
