// ============================================================================
// src/core/platform/Nucleos.cpp — ver Nucleos.hpp
//
// Antes vivía en src/motor/Prioridad.cpp (solo ba-app). Los textos del grupo de medios
// siguen en el dominio «motor» (las traducciones ya hechas valen igual); los del gateway,
// en «gateway».
// ============================================================================
#include "platform/Nucleos.hpp"

#include "i18n/I18n.hpp"

#include <algorithm>
#include <atomic>
#include <iterator>
#include <map>
#include <mutex>
#include <sstream>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <intrin.h>
#else
#include <dirent.h>
#include <sched.h>
#include <fstream>
#endif

namespace ba::platform::nucleos {

using json = nlohmann::json;
using ba::i18n::llenar;
using ba::i18n::traducir;

namespace {

#if !defined(__APPLE__)
std::string textoCpus(const std::set<int>& s) {
    // De a tramos: «0-5,12-17».
    std::string out;
    for (auto it = s.begin(); it != s.end();) {
        const int a = *it;
        int b = a;
        auto sig = std::next(it);
        while (sig != s.end() && *sig == b + 1) {
            b = *sig;
            ++sig;
        }
        out += (out.empty() ? "" : ",") + std::to_string(a) + (b != a ? "-" + std::to_string(b) : std::string());
        it = sig;
    }
    return out;
}
#endif

#if !defined(__APPLE__) && !defined(_WIN32)
std::string leerLinea(const std::string& ruta) {
    std::ifstream f(ruta);
    std::string l;
    std::getline(f, l);
    return l;
}

/// Los grupos de CPUs que comparten una L3 (cada CCD/CCX), sin repetir, en el orden en que
/// aparecen (el de la cpu0 primero).
std::vector<std::set<int>> gruposL3() {
    std::vector<std::set<int>> out;
    for (int cpu = 0;; ++cpu) {
        const std::string dir = "/sys/devices/system/cpu/cpu" + std::to_string(cpu);
        DIR* d = opendir(dir.c_str());
        if (d == nullptr) break; // no hay más CPUs
        closedir(d);
        for (int i = 0; i < 8; ++i) {
            const std::string idx = dir + "/cache/index" + std::to_string(i) + "/";
            const std::string nivel = leerLinea(idx + "level");
            if (nivel.empty()) break;
            if (nivel != "3") continue;
            const std::set<int> g = listaCpus(leerLinea(idx + "shared_cpu_list"));
            bool ya = false;
            for (const auto& x : out) ya = ya || x == g;
            if (!g.empty() && !ya) out.push_back(g);
        }
    }
    return out;
}

std::vector<std::string> interfacesDeRed() {
    std::vector<std::string> out;
    DIR* d = opendir("/sys/class/net");
    if (d == nullptr) return out;
    while (dirent* e = readdir(d)) {
        const std::string n = e->d_name;
        if (n == "." || n == ".." || n == "lo") continue;
        out.push_back(n);
    }
    closedir(d);
    return out;
}
#endif

#if !defined(__APPLE__)
/// Los procesadores lógicos con su clase y su L3, del sistema.
std::vector<Cpu> leerCpus() {
    std::vector<Cpu> out;
#if defined(_WIN32)
    ULONG largo = 0;
    GetSystemCpuSetInformation(nullptr, 0, &largo, GetCurrentProcess(), 0);
    if (largo == 0) return out;
    std::vector<char> buf(largo);
    if (!GetSystemCpuSetInformation(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buf.data()), largo, &largo,
                                    GetCurrentProcess(), 0))
        return out;
    for (ULONG off = 0; off < largo;) {
        auto* i = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buf.data() + off);
        if (i->Type == CpuSetInformation)
            out.push_back({static_cast<int>(i->CpuSet.Group) * 64 + static_cast<int>(i->CpuSet.LogicalProcessorIndex),
                           static_cast<unsigned long>(i->CpuSet.Id), static_cast<int>(i->CpuSet.EfficiencyClass),
                           static_cast<int>(i->CpuSet.LastLevelCacheIndex)});
        off += i->Size;
    }
#elif !defined(__APPLE__)
    const std::set<int> rapidos = listaCpus(leerLinea("/sys/devices/cpu_core/cpus")); // Intel híbrido
    const auto grupos = gruposL3();
    for (size_t g = 0; g < grupos.size(); ++g)
        for (int c : grupos[g])
            out.push_back({c, static_cast<unsigned long>(c), rapidos.empty() || rapidos.count(c) ? 1 : 0, static_cast<int>(g)});
#endif
    return out;
}
#endif

/// Lo que se armó (se fija una vez, antes de que haya hilos calientes).
struct Grupo {
    std::atomic<bool> listo{false};
#if defined(_WIN32)
    std::vector<ULONG> ids;
#elif !defined(__APPLE__)
    cpu_set_t cpus;
#endif
};
Grupo g_medios;
Grupo g_gateway;

#if !defined(__APPLE__)
void fijar(Grupo& g, const std::vector<Cpu>& cpus, const std::set<int>& logicos) {
#if defined(_WIN32)
    g.ids.clear();
    for (const Cpu& c : cpus)
        if (logicos.count(c.logico)) g.ids.push_back(static_cast<ULONG>(c.id));
#elif !defined(__APPLE__)
    (void)cpus;
    CPU_ZERO(&g.cpus);
    for (int c : logicos) CPU_SET(c, &g.cpus);
#endif
    g.listo = true;
}
#endif

void alGrupo(Grupo& g) {
    if (!g.listo.load(std::memory_order_acquire)) return;
#if defined(_WIN32)
    SetThreadSelectedCpuSets(GetCurrentThread(), g.ids.data(), static_cast<ULONG>(g.ids.size()));
#elif !defined(__APPLE__)
    sched_setaffinity(0, sizeof g.cpus, &g.cpus);
#endif
}

} // namespace

std::set<int> listaCpus(const std::string& s) {
    std::set<int> out;
    std::stringstream ss(s);
    std::string parte;
    while (std::getline(ss, parte, ',')) {
        const auto g = parte.find('-');
        try {
            const int a = std::stoi(parte.substr(0, g));
            const int b = g == std::string::npos ? a : std::stoi(parte.substr(g + 1));
            for (int i = a; i <= b && i - a < 4096; ++i) out.insert(i);
        } catch (...) {
        }
    }
    return out;
}

std::set<int> cpusDeInterrupciones(const std::string& texto, const std::vector<std::string>& interfaces) {
    std::istringstream in(texto);
    std::string linea;
    if (!std::getline(in, linea)) return {};
    // La cabecera: «CPU0 CPU1 …»: cuántas columnas de cuentas hay.
    int columnas = 0;
    {
        std::istringstream h(linea);
        std::string t;
        while (h >> t)
            if (t.rfind("CPU", 0) == 0) ++columnas;
    }
    if (columnas == 0) return {};
    std::vector<unsigned long long> suma(static_cast<size_t>(columnas), 0);
    unsigned long long total = 0;
    while (std::getline(in, linea)) {
        std::istringstream l(linea);
        std::string irq;
        if (!(l >> irq) || irq.empty() || irq.back() != ':') continue;
        std::vector<unsigned long long> cuentas;
        for (int i = 0; i < columnas; ++i) {
            unsigned long long v = 0;
            if (!(l >> v)) break;
            cuentas.push_back(v);
        }
        if (static_cast<int>(cuentas.size()) != columnas) continue;
        std::string resto;
        std::getline(l, resto);
        bool deRed = false;
        for (const auto& nombre : interfaces) {
            if (nombre.empty()) continue;
            // «enp3s0-TxRx-0», «eth0», «igc 0000:05:00.0 enp5s0»: el nombre entero, como palabra o prefijo.
            for (size_t p = resto.find(nombre); p != std::string::npos; p = resto.find(nombre, p + 1)) {
                const bool antesOk = p == 0 || resto[p - 1] == ' ' || resto[p - 1] == '\t';
                const size_t fin = p + nombre.size();
                const bool despuesOk = fin == resto.size() || resto[fin] == '-' || resto[fin] == ' ' || resto[fin] == '\n';
                if (antesOk && despuesOk) {
                    deRed = true;
                    break;
                }
            }
            if (deRed) break;
        }
        if (!deRed) continue;
        for (int i = 0; i < columnas; ++i) {
            suma[static_cast<size_t>(i)] += cuentas[static_cast<size_t>(i)];
            total += cuentas[static_cast<size_t>(i)];
        }
    }
    std::set<int> out;
    if (total == 0) return out;
    for (int i = 0; i < columnas; ++i)
        if (suma[static_cast<size_t>(i)] * 5 >= total) out.insert(i); // ≥ 20 %
    return out;
}

Reparto repartir(const std::vector<Cpu>& cpus, const std::set<int>& evitar) {
    Reparto r;
    if (cpus.empty()) return r;
    int maxClase = cpus.front().clase;
    for (const Cpu& c : cpus) maxClase = std::max(maxClase, c.clase);
    r.hibrido = std::any_of(cpus.begin(), cpus.end(), [&](const Cpu& c) { return c.clase != maxClase; });
    std::map<int, std::set<int>> porL3; // de los más rápidos, por índice de L3
    for (const Cpu& c : cpus)
        if (c.clase == maxClase) porL3[c.l3].insert(c.logico);
    // Medios: el grupo más grande (el primero si son iguales), como siempre hizo ba-app.
    const std::set<int>* medios = nullptr;
    int l3Medios = 0;
    for (const auto& [l3, g] : porL3)
        if (medios == nullptr || g.size() > medios->size()) {
            medios = &g;
            l3Medios = l3;
        }
    if (medios == nullptr || medios->size() == cpus.size()) return r; // un solo grupo: nada
    r.medios = *medios;
    if (r.hibrido) return r; // el otro grupo serían los núcleos de eficiencia: el gateway queda libre
    // Gateway: el más grande de los OTROS bloques de L3.
    const std::set<int>* otro = nullptr;
    for (const auto& [l3, g] : porL3)
        if (l3 != l3Medios && (otro == nullptr || g.size() > otro->size())) otro = &g;
    if (otro == nullptr) return r;
    std::set<int> gw;
    for (int c : *otro)
        if (!evitar.count(c)) gw.insert(c);
    r.gateway = gw.size() >= 2 ? gw : *otro;
    return r;
}

json topologia() {
    json clases = json::array(), l3 = json::array();
#if defined(__APPLE__)
    int niveles = 0;
    size_t t = sizeof niveles;
    if (sysctlbyname("hw.nperflevels", &niveles, &t, nullptr, 0) == 0)
        for (int i = 0; i < niveles; ++i) {
            const std::string b = "hw.perflevel" + std::to_string(i) + ".";
            int fisicos = 0, logicos = 0;
            char nombre[64] = {0};
            size_t tn = sizeof nombre, tf = sizeof fisicos, tl = sizeof logicos;
            sysctlbyname((b + "name").c_str(), nombre, &tn, nullptr, 0);
            sysctlbyname((b + "physicalcpu").c_str(), &fisicos, &tf, nullptr, 0);
            sysctlbyname((b + "logicalcpu").c_str(), &logicos, &tl, nullptr, 0);
            const std::string n = nombre;
            clases.push_back({{"clase", n == "Performance" ? "rendimiento" : n == "Efficiency" ? "eficiencia" : n},
                              {"nucleos", fisicos},
                              {"logicos", logicos}});
        }
#elif defined(_WIN32)
    DWORD largo = 0;
    GetLogicalProcessorInformationEx(RelationAll, nullptr, &largo);
    std::vector<char> buf(largo);
    if (largo > 0 &&
        GetLogicalProcessorInformationEx(RelationAll, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data()), &largo)) {
        std::map<int, std::pair<int, int>> porClase; // clase de eficiencia → núcleos, lógicos
        for (DWORD off = 0; off < largo;) {
            auto* i = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data() + off);
            if (i->Relationship == RelationProcessorCore) {
                auto& c = porClase[i->Processor.EfficiencyClass];
                ++c.first;
                c.second += static_cast<int>(__popcnt64(static_cast<unsigned long long>(i->Processor.GroupMask[0].Mask)));
            } else if (i->Relationship == RelationCache && i->Cache.Level == 3) {
                const unsigned long long m = static_cast<unsigned long long>(i->Cache.GroupMask.Mask);
                l3.push_back({{"logicos", static_cast<int>(__popcnt64(m))}, {"mascara", m}, {"grupo", i->Cache.GroupMask.Group}});
            }
            off += i->Size;
        }
        // Windows numera la eficiencia al revés: la clase más alta es la más rápida.
        const int maxClase = porClase.empty() ? 0 : porClase.rbegin()->first;
        for (auto it = porClase.rbegin(); it != porClase.rend(); ++it)
            clases.push_back({{"clase", porClase.size() == 1 ? "todos" : it->first == maxClase ? "rendimiento" : "eficiencia"},
                              {"nucleos", it->second.first},
                              {"logicos", it->second.second}});
    }
#else
    const std::string core = leerLinea("/sys/devices/cpu_core/cpus"), atom = leerLinea("/sys/devices/cpu_atom/cpus");
    if (!core.empty() || !atom.empty()) { // Intel híbrido
        if (!core.empty()) clases.push_back({{"clase", "rendimiento"}, {"logicos", listaCpus(core).size()}, {"cpus", core}});
        if (!atom.empty()) clases.push_back({{"clase", "eficiencia"}, {"logicos", listaCpus(atom).size()}, {"cpus", atom}});
    }
    for (const auto& g : gruposL3()) l3.push_back({{"logicos", g.size()}, {"cpus", textoCpus(g)}});
#endif
    return {{"clases", clases}, {"grupos_l3", l3}};
}

std::string resumen(const json& topo) {
    std::string s;
    if (topo.contains("clases"))
        for (const auto& c : topo["clases"])
            s += (s.empty() ? "" : " + ") + llenar(traducir("motor", "%1 de %2"), {std::to_string(c.value("nucleos", c.value("logicos", 0))),
                                                                                  c.value("clase", std::string("?"))});
    if (topo.contains("grupos_l3") && topo["grupos_l3"].size() > 1)
        s += (s.empty() ? "" : "; ") + llenar(traducir("motor", "%1 grupos de L3 (CCD)"), {std::to_string(topo["grupos_l3"].size())});
    return s.empty() ? traducir("motor", "sin datos de la topología") : s;
}

std::string armarGrupoMedios() {
#if defined(__APPLE__)
    return traducir("motor", "macOS no deja elegir núcleos");
#else
    const std::vector<Cpu> cpus = leerCpus();
#if defined(_WIN32)
    if (cpus.empty()) return llenar(traducir("motor", "GetSystemCpuSetInformation falló (%1)"), {std::to_string(GetLastError())});
#endif
    const Reparto r = repartir(cpus, {});
    if (r.medios.empty()) return traducir("motor", "un solo grupo de núcleos: no hace falta");
    fijar(g_medios, cpus, r.medios);
#if defined(_WIN32)
    // CPU Sets: una preferencia blanda, como recomiendan Microsoft e Intel (la afinidad dura
    // le impide al sistema acomodar y saltear núcleos estacionados).
    std::string lo = llenar(r.hibrido ? traducir("motor", "%1 procesadores lógicos (los de rendimiento)")
                                      : traducir("motor", "%1 procesadores lógicos (un CCD)"),
                            {std::to_string(r.medios.size())});
    // Lo demás, al otro CCD (solo si el corte es por L3: en un híbrido serían los de eficiencia).
    if (!r.hibrido) {
        std::vector<ULONG> resto;
        for (const Cpu& c : cpus)
            if (!r.medios.count(c.logico)) resto.push_back(static_cast<ULONG>(c.id));
        if (!resto.empty()) {
            if (SetProcessDefaultCpuSets(GetCurrentProcess(), resto.data(), static_cast<ULONG>(resto.size())))
                lo += "; " + llenar(traducir("motor", "lo demás, en los otros %1"), {std::to_string(resto.size())});
            else
                lo += "; " + llenar(traducir("motor", "SetProcessDefaultCpuSets falló (%1)"), {std::to_string(GetLastError())});
        }
    }
    return lo;
#else
    // En Linux la afinidad es dura: solo los hilos de medios (lo demás queda libre).
    return llenar(r.hibrido ? traducir("motor", "%1 procesadores lógicos (los de rendimiento: %2)")
                            : traducir("motor", "%1 procesadores lógicos (un L3: %2)"),
                  {std::to_string(r.medios.size()), textoCpus(r.medios)});
#endif
#endif
}

void alGrupoMedios() { alGrupo(g_medios); }
bool hayGrupoMedios() { return g_medios.listo.load(std::memory_order_relaxed); }

std::string armarGrupoGateway(const std::string& evitarTexto) {
#if defined(__APPLE__)
    (void)evitarTexto;
    return traducir("gateway", "macOS no deja elegir núcleos");
#else
    const std::vector<Cpu> cpus = leerCpus();
    if (cpus.empty()) return traducir("gateway", "sin datos de los núcleos");
    std::set<int> evitar;
    if (evitarTexto == "auto") {
#if defined(_WIN32)
        evitar = {0, 1}; // sin RSS, Windows atiende la placa de red en el 0 (y su hermano por SMT)
#else
        std::ifstream f("/proc/interrupts");
        const std::string texto((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        evitar = cpusDeInterrupciones(texto, interfacesDeRed());
#endif
    } else {
        evitar = listaCpus(evitarTexto);
    }
    const Reparto r = repartir(cpus, evitar);
    if (r.gateway.empty())
        return r.hibrido ? traducir("gateway", "núcleos de dos clases: los hilos quedan libres")
                         : traducir("gateway", "un solo grupo de núcleos: no hace falta");
    fijar(g_gateway, cpus, r.gateway);
    std::string lo = llenar(traducir("gateway", "%1 procesadores lógicos (%2), el bloque de caché que no usan los medios de la app"),
                            {std::to_string(r.gateway.size()), textoCpus(r.gateway)});
    if (!evitar.empty())
        lo += "; " + llenar(traducir("gateway", "sin %1 (interrupciones de la placa de red)"), {textoCpus(evitar)});
#if defined(_WIN32)
    if (!SetProcessDefaultCpuSets(GetCurrentProcess(), g_gateway.ids.data(), static_cast<ULONG>(g_gateway.ids.size())))
        lo += "; " + llenar(traducir("gateway", "SetProcessDefaultCpuSets falló (%1)"), {std::to_string(GetLastError())});
#else
    alGrupo(g_gateway); // el principal: los hilos que cree después lo heredan
#endif
    return lo;
#endif
}

void alGrupoGateway() { alGrupo(g_gateway); }
bool hayGrupoGateway() { return g_gateway.listo.load(std::memory_order_relaxed); }

} // namespace ba::platform::nucleos
