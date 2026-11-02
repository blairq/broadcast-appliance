// src/app/sala/PruebaSalaReglas.cpp — la prueba de sala: las reglas puras (ver el .hpp)

#include "sala/PruebaSalaReglas.hpp"

#include "i18n/I18n.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ba::app::sala::prueba {

using ba::i18n::llenar;
using ba::i18n::traducir;
using json = nlohmann::json;

namespace {
std::string mb(double v) { return std::to_string(static_cast<long long>(std::llround(v))); }
std::string num(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", v);
    return b;
}
/// El percentil `p` (0..100) de una lista (vacía: -1).
int percentil(std::vector<int> v, double p) {
    if (v.empty()) return -1;
    std::sort(v.begin(), v.end());
    const auto i = static_cast<std::size_t>(std::ceil(p / 100.0 * static_cast<double>(v.size()))) - 1;
    return v[std::min(i, v.size() - 1)];
}
json muestraJson(const Muestra& m) {
    return json{{"t", m.t},
                {"kbpsSubida", m.kbpsSubida},
                {"kbpsBajada", m.kbpsBajada},
                {"cpuApp", m.cpuApp},
                {"cpuGateway", m.cpuGateway},
                {"memLibreMb", m.memLibreMb},
                {"carga", m.carga},
                {"hilosApp", m.hilosApp}};
}
} // namespace

// --- La guardia --------------------------------------------------------------------------

std::string puedeArrancar(const Medida& m, const Umbrales& u, int n, bool generadorLocal) {
    if (n < 1) return traducir("sala", "la prueba necesita al menos un participante");
    if (n > u.maxParticipantes)
        return llenar(traducir("sala", "la prueba admite hasta %1 participantes (el cupo más grande de la conferencia)"),
                      {std::to_string(u.maxParticipantes)});
    // Sin saber cuánta memoria hay no se arranca: es justo lo que tiró el kernel.
    if (m.memLibreMb < 0) return traducir("sala", "no se pudo medir la memoria disponible: no se arranca a ciegas");
    if (m.memLibreMb < u.arrancarMemMb)
        return llenar(traducir("sala", "la máquina tiene poca memoria disponible (%1 MB; hacen falta %2 MB para arrancar): "
                                       "cerrá programas o máquinas virtuales y volvé a probar"),
                      {mb(m.memLibreMb), mb(u.arrancarMemMb)});
    if (m.carga >= 0 && m.carga > u.arrancarCarga)
        return llenar(traducir("sala", "la máquina está ocupada (carga %1, el tope para arrancar es %2): esperá a que se calme"),
                      {num(m.carga), num(u.arrancarCarga)});
    // Lo que sumarían los N: tiene que quedar arriba del umbral de corte con margen.
    const double necesita = n * u.mbPorParticipante * (generadorLocal ? 1.0 : 0.5);
    if (m.memLibreMb - necesita < u.cortarMemMb * 1.5)
        return llenar(traducir("sala", "con %1 participantes la memoria disponible bajaría a unos %2 MB (se corta a los %3): "
                                       "probá con menos o liberá memoria"),
                      {std::to_string(n), mb(m.memLibreMb - necesita), mb(u.cortarMemMb)});
    return {};
}

std::string debeCortar(const Medida& m, const Umbrales& u) {
    if (m.memLibreMb >= 0 && m.memLibreMb < u.cortarMemMb)
        return llenar(traducir("sala", "memoria disponible %1 MB (el mínimo es %2)"), {mb(m.memLibreMb), mb(u.cortarMemMb)});
    if (m.hilosApp >= 0 && m.hilosApp > u.cortarHilos)
        return llenar(traducir("sala", "el programa tiene %1 hilos (el tope es %2)"),
                      {std::to_string(m.hilosApp), std::to_string(u.cortarHilos)});
    if (m.carga >= 0 && m.carga > u.cortarCarga)
        return llenar(traducir("sala", "carga de la máquina %1 (el tope es %2)"), {num(m.carga), num(u.cortarCarga)});
    return {};
}

// --- Quién es de prueba ------------------------------------------------------------------

bool esDePrueba(const json& p) {
    return p.is_object() && p.contains("prueba") && p["prueba"].is_boolean() && p["prueba"].get<bool>();
}

json sinPrueba(const json& participantes) {
    json out = json::array();
    if (!participantes.is_array()) return out;
    for (const auto& p : participantes)
        if (!esDePrueba(p)) out.push_back(p);
    return out;
}

std::string nombreConRotulo(const std::string& nombre, bool prueba) {
    if (!prueba) return nombre;
    return llenar(traducir("sala", "%1 · prueba"), {nombre});
}

// --- El informe --------------------------------------------------------------------------

void Informe::empezar(int64_t t, int pedidos, const std::string& generador, const std::string& evento) {
    *this = Informe{};
    inicio_ = t;
    pedidos_ = pedidos;
    generador_ = generador;
    evento_ = evento;
}

void Informe::muestra(const Muestra& m) {
    if (!empezado() || terminado()) return;
    serie_.push_back(m);
}

void Informe::participantes(int64_t t, const json& deLaSala) {
    if (!empezado() || terminado() || !deLaSala.is_array()) return;
    std::map<std::string, bool> estan;
    for (const auto& p : deLaSala) {
        if (!esDePrueba(p)) continue;
        const std::string id = p.value("id", std::string());
        if (id.empty()) continue;
        Persona& g = gente_[id];
        if (g.llegoMs == 0) g.llegoMs = p.value("joinedMs", t) > 0 ? p.value("joinedMs", t) : t;
        g.nombre = p.value("name", std::string());
        const bool conectado = p.value("connected", false);
        if (g.vio && g.conectado && !conectado) ++g.cortes;
        g.conectado = conectado;
        g.vio = true;
        if (g.videoMs == 0 && conectado && p.value("mediaUp", false)) g.videoMs = t;
        estan[id] = true;
    }
    // El que ya no está en la sala se fue (salió a propósito o lo echaron): no es un
    // corte. Un corte es el que se cae y la sala le guarda el lugar (connected: false).
    for (auto& [id, g] : gente_)
        if (!estan.count(id)) g.conectado = false;
}

void Informe::hd(const std::string& id, int ms) {
    if (!empezado() || terminado() || ms < 0) return;
    gente_[id].hd.push_back(ms);
}

void Informe::terminar(int64_t t, const std::string& motivo, bool porGuardia) {
    if (!empezado() || terminado()) return;
    fin_ = t;
    motivo_ = motivo;
    porGuardia_ = porGuardia;
}

json Informe::resumen(int64_t t) const {
    int conectados = 0, conVideo = 0, cortes = 0;
    std::vector<int> hd, aVideo;
    for (const auto& [id, g] : gente_) {
        if (g.conectado) ++conectados;
        if (g.videoMs > 0) {
            ++conVideo;
            aVideo.push_back(static_cast<int>(g.videoMs - std::max(g.llegoMs, inicio_)));
        }
        cortes += g.cortes;
        hd.insert(hd.end(), g.hd.begin(), g.hd.end());
    }
    const int entraron = static_cast<int>(gente_.size());
    json picos{{"kbpsSubida", -1}, {"kbpsBajada", -1}, {"cpuApp", -1}, {"cpuGateway", -1}, {"carga", -1},
               {"hilosApp", -1}, {"memLibreMinMb", -1}};
    for (const Muestra& m : serie_) {
        const auto subir = [&](const char* k, double v) {
            if (v >= 0 && v > picos[k].get<double>()) picos[k] = v;
        };
        subir("kbpsSubida", m.kbpsSubida);
        subir("kbpsBajada", m.kbpsBajada);
        subir("cpuApp", m.cpuApp);
        subir("cpuGateway", m.cpuGateway);
        subir("carga", m.carga);
        subir("hilosApp", m.hilosApp);
        if (m.memLibreMb >= 0 && (picos["memLibreMinMb"].get<double>() < 0 || m.memLibreMb < picos["memLibreMinMb"].get<double>()))
            picos["memLibreMinMb"] = m.memLibreMb;
    }
    const int64_t hasta = fin_ > 0 ? fin_ : t;
    return json{{"evento", evento_},
                {"generador", generador_},
                {"inicioMs", inicio_},
                {"finMs", fin_},
                {"segundos", inicio_ > 0 ? std::max<int64_t>(0, (hasta - inicio_) / 1000) : 0},
                {"pedidos", pedidos_},
                {"entraron", entraron},
                {"conectados", conectados},
                {"conVideo", conVideo},
                // Los que no llegaron a tener video (o ni entraron) cuentan como fallidos.
                {"fallidos", std::max(0, pedidos_ - conVideo)},
                {"cortes", cortes},
                {"aVideoMs", {{"p50", percentil(aVideo, 50)}, {"p95", percentil(aVideo, 95)}, {"max", percentil(aVideo, 100)}}},
                {"hd", {{"cuantos", static_cast<int>(hd.size())}, {"p50", percentil(hd, 50)}, {"p95", percentil(hd, 95)}, {"max", percentil(hd, 100)}}},
                {"ultima", serie_.empty() ? json::object() : muestraJson(serie_.back())},
                {"picos", picos},
                {"motivo", motivo_}};
}

json Informe::vivo(int64_t t) const { return resumen(t); }

json Informe::final() const {
    json j = resumen(fin_ > 0 ? fin_ : inicio_);
    // La serie de a 5 s (el máximo de cada tramo; la memoria, el mínimo): alcanza para
    // ver la curva sin un archivo enorme.
    json serie = json::array();
    for (std::size_t i = 0; i < serie_.size(); i += 5) {
        Muestra m = serie_[i];
        for (std::size_t k = i + 1; k < std::min(serie_.size(), i + 5); ++k) {
            const Muestra& o = serie_[k];
            m.kbpsSubida = std::max(m.kbpsSubida, o.kbpsSubida);
            m.kbpsBajada = std::max(m.kbpsBajada, o.kbpsBajada);
            m.cpuApp = std::max(m.cpuApp, o.cpuApp);
            m.cpuGateway = std::max(m.cpuGateway, o.cpuGateway);
            m.carga = std::max(m.carga, o.carga);
            m.hilosApp = std::max(m.hilosApp, o.hilosApp);
            if (o.memLibreMb >= 0 && (m.memLibreMb < 0 || o.memLibreMb < m.memLibreMb)) m.memLibreMb = o.memLibreMb;
        }
        serie.push_back(muestraJson(m));
    }
    j["serie"] = serie;
    json gente = json::array();
    for (const auto& [id, g] : gente_)
        gente.push_back(json{{"id", id},
                             {"nombre", g.nombre},
                             {"llegoMs", g.llegoMs},
                             {"aVideoMs", g.videoMs > 0 ? g.videoMs - std::max(g.llegoMs, inicio_) : -1},
                             {"cortes", g.cortes},
                             {"hdMs", g.hd}});
    j["personas"] = gente;
    j["bien"] = bien();
    return j;
}

bool Informe::bien() const {
    const json r = resumen(fin_ > 0 ? fin_ : inicio_);
    return r["fallidos"].get<int>() == 0 && r["cortes"].get<int>() == 0 && !porGuardia_;
}

std::string Informe::nota() const {
    const json r = resumen(fin_ > 0 ? fin_ : inicio_);
    const auto kbps = [](const json& v) {
        const double k = v.get<double>();
        return k < 0 ? std::string("?") : num(k / 1000.0);
    };
    const auto pct = [](const json& v) {
        const double k = v.get<double>();
        return k < 0 ? std::string("?") : std::to_string(static_cast<int>(std::lround(k)));
    };
    std::string t = llenar(traducir("sala", "Prueba de sala: %1 de %2 con video, %3 fallidos, %4 cortes; "
                                            "subida pico %5 Mbps, bajada pico %6 Mbps; CPU pico app %7 %, gateway %8 %"),
                           {std::to_string(r["conVideo"].get<int>()), std::to_string(r["pedidos"].get<int>()),
                            std::to_string(r["fallidos"].get<int>()), std::to_string(r["cortes"].get<int>()),
                            kbps(r["picos"]["kbpsSubida"]), kbps(r["picos"]["kbpsBajada"]), pct(r["picos"]["cpuApp"]),
                            pct(r["picos"]["cpuGateway"])});
    if (r["hd"]["cuantos"].get<int>() > 0)
        t += llenar(traducir("sala", "; HD en %1 ms (p95 %2)"),
                    {std::to_string(r["hd"]["p50"].get<int>()), std::to_string(r["hd"]["p95"].get<int>())});
    if (r["picos"]["memLibreMinMb"].get<double>() >= 0)
        t += llenar(traducir("sala", "; memoria disponible mínima %1 MB"), {mb(r["picos"]["memLibreMinMb"].get<double>())});
    if (!motivo_.empty()) t += llenar(traducir("sala", "; terminó: %1"), {motivo_});
    return t + ".";
}

} // namespace ba::app::sala::prueba
