// ============================================================================
// src/motor/Prioridad.cpp — ver Prioridad.hpp
// ============================================================================
#include "Prioridad.hpp"

#include "Reloj.hpp" // registrar
#include "i18n/I18n.hpp"
#include "ajustes/Ajustes.hpp"
#include "platform/Energia.hpp"

#include <gst/base/gstaggregator.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <pthread/qos.h>
#include <sys/sysctl.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <dirent.h>
#include <fstream>
#include <set>
#include <sstream>
#endif

namespace ba::motor {

using json = nlohmann::json;
using ba::i18n::llenar;
using ba::i18n::traducir;

namespace {

std::atomic<bool> g_activa{false}; ///< procesador.prioridad (lo fija prioridadDelProceso)
std::once_flag g_unaVez;
std::mutex g_topoMu;
json g_topo = json::object(); ///< lo que se supo del procesador y lo que se aplicó

#if !defined(__APPLE__) && !defined(_WIN32)
/// «0-5,12-17» → {0..5, 12..17}.
std::set<int> listaCpus(const std::string& s) {
    std::set<int> out;
    std::stringstream ss(s);
    std::string parte;
    while (std::getline(ss, parte, ',')) {
        const auto g = parte.find('-');
        try {
            const int a = std::stoi(parte.substr(0, g));
            const int b = g == std::string::npos ? a : std::stoi(parte.substr(g + 1));
            for (int i = a; i <= b; ++i) out.insert(i);
        } catch (...) {
        }
    }
    return out;
}

std::string leerLinea(const std::string& ruta) {
    std::ifstream f(ruta);
    std::string l;
    std::getline(f, l);
    return l;
}

std::string textoCpus(const std::set<int>& s) {
    std::string out;
    for (int c : s) out += (out.empty() ? "" : ",") + std::to_string(c);
    return out;
}

/// Los grupos de CPUs que comparten una L3 (cada CCD/CCX), sin repetir.
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
#endif

/// La topología: clases de núcleos y grupos de L3. Sin aplicar nada.
json leerTopologia() {
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
    if (largo > 0 && GetLogicalProcessorInformationEx(RelationAll, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data()), &largo)) {
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

// EL GRUPO DE MEDIOS: los hilos de medios (las entradas, el cruce a cada capa, los
// mezcladores, el bucle del motor) en UN mismo grupo de núcleos, y que no se muevan de ahí:
// lo que se pasan entre ellos (los bloques de audio de 10 ms, los encargos a la GPU) queda
// en la misma caché L3. AMD con varios CCD: el CCD más grande (el primero si son iguales), y
// lo demás del proceso (la interfaz, el MCP, los trabajadores, los codificadores) va al
// otro: no le quita núcleos a nadie (fijar el proceso entero sí). Intel híbrido: los núcleos
// de rendimiento (lo demás queda libre: en los de eficiencia, la interfaz iría lenta). Un
// solo L3 y núcleos iguales: nada. macOS no deja elegir núcleos.
#if defined(_WIN32)
std::vector<ULONG> g_idsMedios; // los CPU Sets del grupo (se fija antes de que haya hilos de medios)
#elif !defined(__APPLE__)
cpu_set_t g_cpusMedios;
#endif
std::atomic<bool> g_grupoMedios{false};

std::string armarGrupoMedios() {
#if defined(_WIN32)
    // CPU Sets: una preferencia blanda, como recomiendan Microsoft e Intel (la afinidad dura
    // le impide al sistema acomodar y saltear núcleos estacionados). Cada uno trae su clase
    // de eficiencia (la más alta es la más rápida) y el índice de su caché L3.
    ULONG largo = 0;
    GetSystemCpuSetInformation(nullptr, 0, &largo, GetCurrentProcess(), 0);
    std::vector<char> buf(largo);
    if (largo == 0 || !GetSystemCpuSetInformation(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buf.data()), largo, &largo, GetCurrentProcess(), 0))
        return llenar(traducir("motor", "GetSystemCpuSetInformation falló (%1)"), {std::to_string(GetLastError())});
    struct Uno { ULONG id; int clase; int l3; };
    std::vector<Uno> todos;
    for (ULONG off = 0; off < largo;) {
        auto* i = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buf.data() + off);
        if (i->Type == CpuSetInformation) todos.push_back({i->CpuSet.Id, i->CpuSet.EfficiencyClass, i->CpuSet.LastLevelCacheIndex});
        off += i->Size;
    }
    if (todos.empty()) return traducir("motor", "sin CPU Sets");
    int maxClase = 0;
    for (const auto& u : todos) maxClase = std::max(maxClase, u.clase);
    const bool hibrido = std::any_of(todos.begin(), todos.end(), [&](const Uno& u) { return u.clase != maxClase; });
    std::map<int, std::vector<ULONG>> porL3; // de los más rápidos
    for (const auto& u : todos)
        if (u.clase == maxClase) porL3[u.l3].push_back(u.id);
    const std::vector<ULONG>* elegido = nullptr;
    for (const auto& [l3, ids] : porL3)
        if (elegido == nullptr || ids.size() > elegido->size()) elegido = &ids;
    if (elegido->size() == todos.size()) return traducir("motor", "un solo grupo de núcleos: no hace falta");
    g_idsMedios = *elegido;
    std::vector<ULONG> resto;
    for (const auto& u : todos)
        if (std::find(elegido->begin(), elegido->end(), u.id) == elegido->end()) resto.push_back(u.id);
    std::string lo = llenar(hibrido ? traducir("motor", "%1 procesadores lógicos (los de rendimiento)")
                                    : traducir("motor", "%1 procesadores lógicos (un CCD)"),
                            {std::to_string(elegido->size())});
    // Lo demás, al otro CCD (solo si el corte es por L3: en un híbrido serían los de eficiencia).
    if (!hibrido && !resto.empty()) {
        if (SetProcessDefaultCpuSets(GetCurrentProcess(), resto.data(), static_cast<ULONG>(resto.size())))
            lo += "; " + llenar(traducir("motor", "lo demás, en los otros %1"), {std::to_string(resto.size())});
        else
            lo += "; " + llenar(traducir("motor", "SetProcessDefaultCpuSets falló (%1)"), {std::to_string(GetLastError())});
    }
    g_grupoMedios = true;
    return lo;
#elif defined(__APPLE__)
    return traducir("motor", "macOS no deja elegir núcleos");
#else
    std::set<int> base;
    const std::string core = leerLinea("/sys/devices/cpu_core/cpus");
    const bool hibrido = !core.empty();
    if (hibrido) base = listaCpus(core);
    const auto grupos = gruposL3();
    std::set<int> elegido, todas;
    for (const auto& g : grupos) {
        todas.insert(g.begin(), g.end());
        std::set<int> enBase;
        for (int c : g)
            if (!hibrido || base.count(c)) enBase.insert(c);
        if (enBase.size() > elegido.size()) elegido = enBase;
    }
    if (elegido.empty() || elegido.size() == todas.size()) return traducir("motor", "un solo grupo de núcleos: no hace falta");
    CPU_ZERO(&g_cpusMedios);
    for (int c : elegido) CPU_SET(c, &g_cpusMedios);
    g_grupoMedios = true;
    // En Linux la afinidad es dura: solo los hilos de medios (lo demás queda libre).
    return llenar(hibrido ? traducir("motor", "%1 procesadores lógicos (los de rendimiento: %2)")
                          : traducir("motor", "%1 procesadores lógicos (un L3: %2)"),
                  {std::to_string(elegido.size()), textoCpus(elegido)});
#endif
}

std::string resumen(const json& topo) {
    std::string s;
    for (const auto& c : topo["clases"])
        s += (s.empty() ? "" : " + ") + llenar(traducir("motor", "%1 de %2"), {std::to_string(c.value("nucleos", c.value("logicos", 0))),
                                                                              c.value("clase", std::string("?"))});
    if (topo["grupos_l3"].size() > 1)
        s += (s.empty() ? "" : "; ") + llenar(traducir("motor", "%1 grupos de L3 (CCD)"), {std::to_string(topo["grupos_l3"].size())});
    return s.empty() ? traducir("motor", "sin datos de la topología") : s;
}

GstBusSyncReply alMensaje(GstBus*, GstMessage* m, gpointer) {
    if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_STREAM_STATUS) {
        GstStreamStatusType t;
        GstElement* dueno = nullptr;
        gst_message_parse_stream_status(m, &t, &dueno);
        // Corre en el hilo nuevo. El de un aggregator (glvideomixer, d3d12compositor,
        // audiomixer) es el que junta las capas o las entradas a su hora: el que marca el ritmo.
        if (t == GST_STREAM_STATUS_TYPE_ENTER)
            prioridadDelHilo(dueno != nullptr && GST_IS_AGGREGATOR(dueno) ? ClaseHilo::Critico : ClaseHilo::Medios);
    }
    return GST_BUS_PASS;
}

} // namespace

void prioridadDelHilo(ClaseHilo c) {
    // Al grupo de medios (todas las clases: son los hilos del motor y sus pipelines).
    if (g_grupoMedios.load(std::memory_order_relaxed)) {
#if defined(_WIN32)
        SetThreadSelectedCpuSets(GetCurrentThread(), g_idsMedios.data(), static_cast<ULONG>(g_idsMedios.size()));
#elif !defined(__APPLE__)
        sched_setaffinity(0, sizeof g_cpusMedios, &g_cpusMedios);
#endif
    }
    if (!g_activa.load(std::memory_order_relaxed)) return;
#if defined(__APPLE__)
    // En macOS solo los Critico suben (QoS interactiva: prefieren los núcleos de
    // rendimiento). Medido en la M1 Pro (2026-10-20, contadores del kernel, misma versión):
    // con QoS interactiva en TODOS los de streaming, ~23,8 mil cambios de contexto por
    // segundo y la CPU variando de 62 a 84 %; sin ella, ~18,9 mil y 59 % estable. Se
    // desplazan entre ellos y esperan a la cola de GL de GStreamer, que queda más abajo (le
    // mandan con dispatch_async y esperan con una condición: macOS no le sube la QoS).
    if (c == ClaseHilo::Critico) pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#elif defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), c == ClaseHilo::Critico  ? THREAD_PRIORITY_HIGHEST
                                          : c == ClaseHilo::Medios ? THREAD_PRIORITY_ABOVE_NORMAL
                                                                   : THREAD_PRIORITY_NORMAL);
#ifdef THREAD_POWER_THROTTLING_CURRENT_VERSION
    // Sin EcoQoS: el ahorro de energía es lo que en un Intel híbrido lo manda a los núcleos E.
    THREAD_POWER_THROTTLING_STATE s{};
    s.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    s.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    s.StateMask = 0;
    SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &s, sizeof s);
#endif
#else
    if (c != ClaseHilo::Motor &&
        setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), c == ClaseHilo::Critico ? -10 : -5) != 0) {
        static std::atomic<bool> avisado{false};
        if (!avisado.exchange(true))
            registrar(Nivel::Info, traducir("motor", "procesador: los hilos del video y el audio no pueden subir de prioridad "
                                                     "(hace falta CAP_SYS_NICE o RLIMIT_NICE)"));
    }
#endif
}

void prioridadEnPipeline(GstElement* pipeline) {
    if ((!g_activa.load() && !g_grupoMedios.load()) || pipeline == nullptr || !GST_IS_PIPELINE(pipeline)) return;
    GstBus* bus = gst_element_get_bus(pipeline);
    if (bus == nullptr) return;
    gst_bus_set_sync_handler(bus, alMensaje, nullptr, nullptr);
    gst_object_unref(bus);
}

void prioridadDelProceso() {
    std::call_once(g_unaVez, []() {
        const bool activa = ajustes::siNo("procesador.prioridad", true);
        g_activa = activa;
        json topo = leerTopologia();
        if (activa) {
            // Sin App Nap (macOS) ni EcoQoS (Windows): el ahorro por proceso, el mismo que
            // usan el gateway y los gráficos (platform/Energia).
            ba::platform::sinAhorroDeEnergia();
#if defined(_WIN32)
            SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
#endif
        }
        topo["prioridad"] = activa;
        topo["grupo_medios"] = ajustes::siNo("procesador.mediosJuntos", true) ? armarGrupoMedios() : std::string("apagado");
        registrar(Nivel::Info, llenar(activa ? traducir("motor", "procesador: %1; prioridad del video y el audio: sí; video y "
                                                                 "audio juntos: %2")
                                             : traducir("motor", "procesador: %1; prioridad del video y el audio: no; video y "
                                                                 "audio juntos: %2"),
                                      {resumen(topo), topo["grupo_medios"].get<std::string>()}));
        std::lock_guard<std::mutex> l(g_topoMu);
        g_topo = std::move(topo);
    });
}

json topologiaCpu() {
    std::lock_guard<std::mutex> l(g_topoMu);
    return g_topo;
}

} // namespace ba::motor
