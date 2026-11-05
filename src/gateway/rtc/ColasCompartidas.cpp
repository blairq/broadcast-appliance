// ============================================================================
// src/gateway/rtc/ColasCompartidas.cpp — ver ColasCompartidas.hpp (la topología; las colas de
// libwebrtc en sí son webrtc/ColasRtc.cpp)
// ============================================================================
#include "ColasCompartidas.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ba::gateway::colas {

// ---------------------------------------------------------------------------- topología

namespace {

#if defined(__linux__)
/// «0-5,12-17» → {0..5, 12..17}.
std::set<int> listaCpus(const std::string& s) {
    std::set<int> r;
    std::stringstream ss(s);
    std::string parte;
    while (std::getline(ss, parte, ',')) {
        const auto guion = parte.find('-');
        try {
            if (guion == std::string::npos) {
                if (!parte.empty()) r.insert(std::stoi(parte));
            } else {
                const int a = std::stoi(parte.substr(0, guion)), b = std::stoi(parte.substr(guion + 1));
                for (int i = a; i <= b && i - a < 4096; ++i) r.insert(i);
            }
        } catch (...) {
        }
    }
    return r;
}
std::string leerLinea(const std::string& ruta) {
    std::ifstream f(ruta);
    std::string l;
    std::getline(f, l);
    return l;
}
#endif

} // namespace

int nucleosDeUnBloque() {
#if defined(__APPLE__)
    // Apple M: los de rendimiento comparten la L2 de a grupos (4 en la M1 Pro).
    int n = 0;
    size_t t = sizeof(n);
    if (sysctlbyname("hw.perflevel0.cpusperl2", &n, &t, nullptr, 0) == 0 && n > 0) return n;
    t = sizeof(n);
    if (sysctlbyname("hw.perflevel0.physicalcpu", &n, &t, nullptr, 0) == 0 && n > 0) return n;
    return 0;
#elif defined(_WIN32)
    DWORD largo = 0;
    GetLogicalProcessorInformationEx(RelationAll, nullptr, &largo);
    if (largo == 0) return 0;
    std::vector<unsigned char> buf(largo);
    auto* base = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data());
    if (!GetLogicalProcessorInformationEx(RelationAll, base, &largo)) return 0;
    // La L3 del procesador 0 (grupo 0, bit 0) y los núcleos (no hilos SMT) que caen en ella.
    KAFFINITY l3 = 0;
    for (DWORD o = 0; o < largo;) {
        auto* i = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data() + o);
        if (i->Relationship == RelationCache && i->Cache.Level == 3 && i->Cache.GroupMask.Group == 0 &&
            (i->Cache.GroupMask.Mask & 1) != 0)
            l3 = i->Cache.GroupMask.Mask;
        o += i->Size;
    }
    int nucleos = 0;
    for (DWORD o = 0; o < largo;) {
        auto* i = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data() + o);
        if (i->Relationship == RelationProcessorCore && i->Processor.GroupCount > 0 && i->Processor.GroupMask[0].Group == 0 &&
            (l3 == 0 || (i->Processor.GroupMask[0].Mask & l3) != 0))
            ++nucleos;
        o += i->Size;
    }
    return nucleos;
#elif defined(__linux__)
    const std::string base = "/sys/devices/system/cpu/";
    std::set<int> bloque;
    for (int i = 3; i >= 2 && bloque.empty(); --i) // la L3; si no hay, la L2
        for (int idx = 0; idx < 8; ++idx) {
            const std::string d = base + "cpu0/cache/index" + std::to_string(idx) + "/";
            if (leerLinea(d + "level") == std::to_string(i)) {
                bloque = listaCpus(leerLinea(d + "shared_cpu_list"));
                break;
            }
        }
    if (bloque.empty()) return 0;
    // Un núcleo físico por grupo de hermanos SMT: el primero de su lista.
    int nucleos = 0;
    for (int c : bloque) {
        const std::set<int> hermanos = listaCpus(leerLinea(base + "cpu" + std::to_string(c) + "/topology/thread_siblings_list"));
        if (hermanos.empty() || *hermanos.begin() == c) ++nucleos;
    }
    return nucleos;
#else
    return 0;
#endif
}

Opciones resolver(const Opciones& o) {
    Opciones r = o;
    if (r.hilosPesadas <= 0) {
        int n = nucleosDeUnBloque();
        if (n <= 0) n = static_cast<int>(std::max(2u, std::thread::hardware_concurrency() / 2));
        r.hilosPesadas = std::clamp(n, 2, 6);
    }
    r.hilosPesadas = std::clamp(r.hilosPesadas, 1, 64);
    if (r.hilosLivianas <= 0) r.hilosLivianas = r.hilosPesadas == 1 ? 1 : 2;
    r.hilosLivianas = std::clamp(r.hilosLivianas, 1, 64);
    return r;
}

bool esPesada(std::string_view nombre) {
    // Las de decodificar video: una por stream de video que llega (VideoReceiveStream2);
    // cada tarea decodifica un cuadro (ms por software o con el MFT de Windows).
    return nombre == "DecodingQueue";
}

} // namespace ba::gateway::colas
