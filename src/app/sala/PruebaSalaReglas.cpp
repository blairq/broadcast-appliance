// src/app/sala/PruebaSalaReglas.cpp — la prueba de sala: las reglas puras (ver el .hpp)

#include "sala/PruebaSalaReglas.hpp"

#include "i18n/I18nTexto.hpp"
#include "sala/CapacidadReglas.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <map>

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
                {"cpuGenerador", m.cpuGenerador},
                {"cpuMaquina", m.cpuMaquina},
                {"redEntraMbps", m.redEntraMbps},
                {"redSaleMbps", m.redSaleMbps},
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

std::string puedeArrancarRepartido(const Medida& m, const Umbrales& u, int n, int nLocal) {
    // La base (cupo, memoria, carga) es la de siempre con el generador afuera; después se
    // suma la parte de los que genera esta máquina.
    if (std::string no = puedeArrancar(m, u, n, false); !no.empty()) return no;
    nLocal = std::clamp(nLocal, 0, n);
    const double necesita = (n + nLocal) * u.mbPorParticipante * 0.5;
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

// --- El probador -------------------------------------------------------------------------

std::string leerDecodificar(const std::string& modo, int pct, Decodificar& d) {
    if (modo != "nada" && modo != "muestra" && modo != "todo")
        return traducir("sala", "decodificar es «nada», «muestra» o «todo»");
    if (modo == "muestra" && (pct < kMuestraMinPct || pct > kMuestraMaxPct))
        return llenar(traducir("sala", "la muestra va del %1 al %2 % de lo que vuelve"),
                      {std::to_string(kMuestraMinPct), std::to_string(kMuestraMaxPct)});
    d.modo = modo;
    if (modo == "muestra") d.pct = pct;
    return {};
}

double pctEfectivo(const Decodificar& d, int n) {
    if (d.modo == "nada") return 0;
    if (d.modo == "todo" || n <= kMuestraMinima) return 100;
    // hinchada reparte parejo: de cada 100 videos que recibe, `pct` se decodifican; con
    // pocos, la muestra sube para que haya al menos kMuestraMinima (redondeado a medio %).
    const double minimo = 100.0 * kMuestraMinima / static_cast<double>(n);
    return std::min(100.0, std::ceil(std::max<double>(d.pct, minimo) * 2.0) / 2.0);
}

std::string argDecodificar(const Decodificar& d, int n) {
    const double p = pctEfectivo(d, n);
    if (p <= 0) return "nada";
    if (p >= 100) return "todo";
    return std::fmod(p, 1.0) == 0 ? std::to_string(static_cast<int>(p)) : num(p);
}

json aJson(const Decodificar& d, int n) {
    return json{{"modo", d.modo}, {"pct", d.modo == "muestra" ? d.pct : (d.modo == "todo" ? 100 : 0)}, {"pctEfectivo", pctEfectivo(d, n)}};
}

std::string puedeRepartir(int n, int otras, bool estaGenera) {
    if (otras < 0 || otras > kMaxOtrasMaquinas)
        return llenar(traducir("sala", "se puede repartir con hasta %1 máquinas más"), {std::to_string(kMaxOtrasMaquinas)});
    if (otras == 0 && !estaGenera) return traducir("sala", "si esta máquina no genera, hace falta al menos otra que genere");
    if (n < otras + (estaGenera ? 1 : 0)) return traducir("sala", "hay más máquinas que participantes");
    return {};
}

Reparto repartir(int n, int otras, bool estaGenera) {
    Reparto r;
    r.n = std::max(0, n);
    r.otras = std::clamp(otras, 0, kMaxOtrasMaquinas);
    r.estaGenera = estaGenera || r.otras == 0;
    r.maquinas = r.otras + (r.estaGenera ? 1 : 0);
    r.porMaquina = (r.n + r.maquinas - 1) / r.maquinas;
    if (r.estaGenera) {
        r.local = r.porMaquina;
        r.procesosLocales = (r.local + kPorProcesoMax - 1) / kPorProcesoMax;
        r.porProcesoLocal = r.procesosLocales > 0 ? (r.local + r.procesosLocales - 1) / r.procesosLocales : 0;
    }
    if (r.otras > 0) {
        r.procesosPorOtra = (r.porMaquina + kPorProcesoMax - 1) / kPorProcesoMax;
        r.porProcesoOtra = r.procesosPorOtra > 0 ? (r.porMaquina + r.procesosPorOtra - 1) / r.procesosPorOtra : 0;
    }
    r.trabajadores = r.procesosLocales + r.otras * r.procesosPorOtra;
    return r;
}

std::string comandoTrabajador(const Reparto& r, const std::string& host, int puerto, const std::string& clave, bool windows) {
    const std::string args = " trabajar --coordinador " + host + ":" + std::to_string(puerto) + " --por-proceso " +
                             std::to_string(std::max(1, r.porProcesoOtra));
    if (windows) return "$env:HINCHADA_CLAVE=\"" + clave + "\"; .\\hinchada.exe" + args;
    return "HINCHADA_CLAVE=" + clave + " ./hinchada" + args;
}

int trabajadoresSumados(const std::string& consola, int* otras) {
    // Las notas del coordinador (hinchada, Coordinacion.cpp): el nombre va entre « ».
    const auto contar = [&consola](const std::string& marca, std::map<std::string, int>& por) {
        for (std::size_t i = consola.find(marca); i != std::string::npos; i = consola.find(marca, i + marca.size())) {
            const std::size_t a = i + marca.size();
            const std::size_t b = consola.find("»", a);
            if (b == std::string::npos) break;
            ++por[consola.substr(a, b - a)];
        }
    };
    std::map<std::string, int> entraron, salieron;
    contar("se sumó el trabajador «", entraron);
    contar("se cortó el trabajador «", salieron);
    int total = 0, deOtras = 0;
    for (const auto& [nombre, k] : entraron) {
        const auto it = salieron.find(nombre);
        const int quedan = std::max(0, k - (it == salieron.end() ? 0 : it->second));
        total += quedan;
        if (nombre.rfind(kPrefijoLocal, 0) != 0) deOtras += quedan;
    }
    if (otras != nullptr) *otras = deOtras;
    return total;
}

namespace {
constexpr const char* kPrefijoGenerador = "BAGEN1.";
}

std::string armarCodigoGenerador(const CodigoGenerador& c) {
    const json j{{"c", c.coordinador}, {"k", c.clave}, {"n", c.procesos}, {"p", c.porProceso}, {"m", c.maquina}};
    return std::string(kPrefijoGenerador) + capacidad::base64url(j.dump(-1, ' ', false, json::error_handler_t::replace));
}

bool leerCodigoGenerador(const std::string& texto, CodigoGenerador& c) {
    std::string t;
    for (char ch : texto)
        if (!std::isspace(static_cast<unsigned char>(ch))) t.push_back(ch);
    const std::string pre = kPrefijoGenerador;
    if (t.size() <= pre.size() || t.compare(0, pre.size(), pre) != 0) return false;
    std::string crudo;
    if (!capacidad::desdeBase64url(t.substr(pre.size()), crudo)) return false;
    const json j = json::parse(crudo, nullptr, false);
    if (!j.is_object()) return false;
    CodigoGenerador x;
    x.coordinador = j.value("c", std::string());
    x.clave = j.value("k", std::string());
    x.procesos = j.value("n", 0);
    x.porProceso = j.value("p", 0);
    x.maquina = j.value("m", std::string());
    const std::size_t dos = x.coordinador.rfind(':');
    if (dos == std::string::npos || dos == 0 || std::atoi(x.coordinador.c_str() + dos + 1) <= 0) return false;
    if (x.clave.size() < 12) return false;
    if (x.procesos < 1 || x.procesos > 8 || x.porProceso < 1 || x.porProceso > 500) return false;
    c = std::move(x);
    return true;
}

std::map<std::string, double> generadorasDeMuestra(const json& linea) {
    std::map<std::string, double> out;
    if (!linea.is_object() || !linea.contains("maquinas") || !linea["maquinas"].is_array()) return out;
    for (const json& m : linea["maquinas"]) {
        if (!m.is_object() || m.value("rol", std::string()) == "sonda") continue;
        bool medida = false;
        if (m.contains("procesos") && m["procesos"].is_array())
            for (const json& pr : m["procesos"]) {
                const std::string n = pr.is_object() ? pr.value("nombre", std::string()) : std::string();
                if (n == "ba-gateway" || n == "ba-app") medida = true;
            }
        const double cpu = m.value("cpuPct", -1.0);
        const std::string nombre = m.value("maquina", std::string());
        if (medida || cpu < 0 || nombre.empty()) continue;
        out[nombre] = std::max(cpu, out.count(nombre) ? out[nombre] : -1.0);
    }
    return out;
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

void Informe::probador(const json& p) {
    if (!empezado() || terminado() || !p.is_object()) return;
    for (const auto& [k, v] : p.items()) probador_[k] = v;
}

void Informe::generadoras(const std::map<std::string, double>& cpuPorMaquina) {
    if (!empezado() || terminado()) return;
    for (const auto& [maquina, cpu] : cpuPorMaquina) {
        if (cpu < 0) continue;
        Generadora& g = generadoras_[maquina];
        g.suma += cpu;
        ++g.n;
        g.pico = std::max(g.pico, cpu);
    }
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
    json picos{{"kbpsSubida", -1}, {"kbpsBajada", -1}, {"cpuApp", -1}, {"cpuGateway", -1}, {"cpuGenerador", -1}, {"cpuMaquina", -1},
               {"redEntraMbps", -1}, {"redSaleMbps", -1}, {"carga", -1},
               {"hilosApp", -1}, {"memLibreMinMb", -1}};
    // El promedio de la CPU (lo que se compara entre corridas: el pico es un instante).
    double sumApp = 0, sumGw = 0, sumGen = 0;
    int nApp = 0, nGw = 0, nGen = 0;
    for (const Muestra& m : serie_) {
        const auto subir = [&](const char* k, double v) {
            if (v >= 0 && v > picos[k].get<double>()) picos[k] = v;
        };
        subir("kbpsSubida", m.kbpsSubida);
        subir("kbpsBajada", m.kbpsBajada);
        subir("cpuApp", m.cpuApp);
        subir("cpuGateway", m.cpuGateway);
        subir("cpuGenerador", m.cpuGenerador);
        subir("cpuMaquina", m.cpuMaquina);
        subir("redEntraMbps", m.redEntraMbps);
        subir("redSaleMbps", m.redSaleMbps);
        subir("carga", m.carga);
        if (m.cpuApp >= 0) sumApp += m.cpuApp, ++nApp;
        if (m.cpuGateway >= 0) sumGw += m.cpuGateway, ++nGw;
        if (m.cpuGenerador >= 0) sumGen += m.cpuGenerador, ++nGen;
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
                {"promedios",
                 {{"cpuApp", nApp > 0 ? sumApp / nApp : -1.0},
                  {"cpuGateway", nGw > 0 ? sumGw / nGw : -1.0},
                  {"cpuGenerador", nGen > 0 ? sumGen / nGen : -1.0}}},
                {"probador", probador_},
                {"generadoras", [this]() {
                     json g = json::array();
                     for (const auto& [maquina, x] : generadoras_)
                         g.push_back({{"maquina", maquina}, {"cpuProm", x.n > 0 ? x.suma / x.n : -1.0}, {"cpuPico", x.pico}});
                     return g;
                 }()},
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
            m.cpuGenerador = std::max(m.cpuGenerador, o.cpuGenerador);
            m.cpuMaquina = std::max(m.cpuMaquina, o.cpuMaquina);
            m.redEntraMbps = std::max(m.redEntraMbps, o.redEntraMbps);
            m.redSaleMbps = std::max(m.redSaleMbps, o.redSaleMbps);
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
    // Cómo se generó: cuánto decodificó el probador, su CPU (la de esta máquina) y las
    // máquinas que se sumaron.
    if (probador_.contains("decodificar") && probador_["decodificar"].is_object()) {
        const json& d = probador_["decodificar"];
        const std::string modo = d.value("modo", std::string());
        const std::string cual = modo == "nada"  ? traducir("sala", "nada (solo envía)")
                                 : modo == "todo" ? traducir("sala", "todo")
                                                  : llenar(traducir("sala", "una muestra (%1 %)"),
                                                           {pct(json(d.value("pctEfectivo", 0.0)))});
        t += llenar(traducir("sala", "; el probador decodificó %1"), {cual});
    }
    if (r["picos"]["cpuGenerador"].get<double>() >= 0)
        t += llenar(traducir("sala", "; CPU del probador en esta máquina %1 % (pico %2 %)"),
                    {pct(r["promedios"]["cpuGenerador"]), pct(r["picos"]["cpuGenerador"])});
    for (const auto& [maquina, g] : generadoras_)
        if (g.n > 0)
            t += llenar(traducir("sala", "; CPU de la generadora %1: %2 % (pico %3 %)"),
                        {maquina, pct(json(g.suma / g.n)), pct(json(g.pico))});
    if (probador_.contains("maquinas") && probador_["maquinas"].is_object() && probador_["maquinas"].value("otras", 0) > 0) {
        const json& mq = probador_["maquinas"];
        const std::string sumadas = std::to_string(mq.value("sumadas", 0)), otras = std::to_string(mq.value("otras", 0));
        t += mq.value("estaGenera", true) ? llenar(traducir("sala", "; generaron esta máquina y %1 de %2 más"), {sumadas, otras})
                                          : llenar(traducir("sala", "; generaron %1 de %2 máquinas más (esta solo coordinó)"), {sumadas, otras});
    }
    if (r["hd"]["cuantos"].get<int>() > 0)
        t += llenar(traducir("sala", "; HD en %1 ms (p95 %2)"),
                    {std::to_string(r["hd"]["p50"].get<int>()), std::to_string(r["hd"]["p95"].get<int>())});
    if (r["picos"]["memLibreMinMb"].get<double>() >= 0)
        t += llenar(traducir("sala", "; memoria disponible mínima %1 MB"), {mb(r["picos"]["memLibreMinMb"].get<double>())});
    if (!motivo_.empty()) t += llenar(traducir("sala", "; terminó: %1"), {motivo_});
    return t + ".";
}

} // namespace ba::app::sala::prueba
