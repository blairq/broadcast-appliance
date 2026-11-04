// src/core/diag/Frontera.cpp — el registro de fronteras y su JSON (ver Frontera.hpp)
#include "diag/Frontera.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace ba::diag {
namespace {

struct Registro {
    std::mutex m;
    std::vector<std::unique_ptr<Frontera>> todas; // nunca se borran
    std::map<std::string, Frontera*> porId;
    // Para las tasas: la lectura anterior de cada una y cuándo.
    std::map<std::string, Frontera::Lectura> previas;
    std::uint64_t previaNs = 0;
    nlohmann::json ultima;
};

Registro& registro() {
    static Registro* r = new Registro(); // a propósito sin destruir: hay hilos que cuentan al salir
    return *r;
}

double redondo(double x, int dec) {
    const double p = std::pow(10.0, dec);
    return std::round(x * p) / p;
}

} // namespace

Frontera& frontera(const std::string& id, int numero, const std::string& descripcion) {
    Registro& r = registro();
    std::lock_guard<std::mutex> l(r.m);
    const auto it = r.porId.find(id);
    if (it != r.porId.end()) return *it->second;
    r.todas.push_back(std::make_unique<Frontera>(id, numero, descripcion));
    Frontera* f = r.todas.back().get();
    r.porId.emplace(id, f);
    return *f;
}

nlohmann::json fronterasJson(int ventanaMinMs) {
    Registro& r = registro();
    std::lock_guard<std::mutex> l(r.m);
    const std::uint64_t ahora = relojNs();
    if (r.previaNs != 0 && ahora - r.previaNs < static_cast<std::uint64_t>(std::max(0, ventanaMinMs)) * 1000000ULL &&
        !r.ultima.is_null())
        return r.ultima;
    const bool primera = r.previaNs == 0;
    const double seg = primera ? 0.0 : static_cast<double>(ahora - r.previaNs) / 1e9;
    r.previaNs = ahora;
    std::vector<Frontera*> orden;
    for (const auto& f : r.todas) orden.push_back(f.get());
    std::sort(orden.begin(), orden.end(), [](const Frontera* a, const Frontera* b) {
        if (a->numero() != b->numero()) return (a->numero() == 0 ? 1000 : a->numero()) < (b->numero() == 0 ? 1000 : b->numero());
        return a->id() < b->id();
    });
    nlohmann::json lista = nlohmann::json::array();
    for (Frontera* f : orden) {
        const Frontera::Lectura a = f->leer();
        Frontera::Lectura p;
        if (const auto it = r.previas.find(f->id()); it != r.previas.end()) p = it->second;
        r.previas[f->id()] = a;
        nlohmann::json j = {{"id", f->id()}, {"numero", f->numero()}, {"descripcion", f->descripcion()}, {"pases", a.pases}};
        if (seg > 0) {
            const double pases = static_cast<double>(a.pases - p.pases);
            const double desp = static_cast<double>(a.despertadas - p.despertadas);
            const double tn = static_cast<double>(a.trabajoN - p.trabajoN);
            const double tns = static_cast<double>(a.trabajoNs - p.trabajoNs);
            const double en = static_cast<double>(a.esperaN - p.esperaN);
            const double ens = static_cast<double>(a.esperaNs - p.esperaNs);
            j["pases_s"] = redondo(pases / seg, 1);
            j["despertadas_s"] = redondo(desp / seg, 1);
            j["pases_por_despertada"] = desp > 0 ? nlohmann::json(redondo(pases / desp, 2)) : nlohmann::json(nullptr);
            j["kbytes_s"] = redondo(static_cast<double>(a.bytes - p.bytes) / 1024.0 / seg, 1);
            j["trabajo_us_pase"] = tn > 0 ? nlohmann::json(redondo(tns / tn / 1000.0, 2)) : nlohmann::json(nullptr);
            j["trabajo_max_us"] = a.trabajoMaxNs ? nlohmann::json(redondo(static_cast<double>(a.trabajoMaxNs) / 1000.0, 1))
                                                 : nlohmann::json(nullptr);
            // El trabajo de la frontera como % de un núcleo (cuánto del hilo que la atiende se va acá).
            j["trabajo_pct_nucleo"] = tn > 0 ? nlohmann::json(redondo(100.0 * tns / 1e9 / seg, 2)) : nlohmann::json(nullptr);
            j["espera_us"] = en > 0 ? nlohmann::json(redondo(ens / en / 1000.0, 1)) : nlohmann::json(nullptr);
            j["espera_max_us"] = a.esperaMaxNs ? nlohmann::json(redondo(static_cast<double>(a.esperaMaxNs) / 1000.0, 1))
                                               : nlohmann::json(nullptr);
        }
        lista.push_back(std::move(j));
    }
    r.ultima = {{"ventana_s", redondo(seg, 2)}, {"fronteras", std::move(lista)}};
    return r.ultima;
}

} // namespace ba::diag
