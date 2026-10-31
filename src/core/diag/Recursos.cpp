// ============================================================================
// src/core/diag/Recursos.cpp — ver Recursos.hpp
// ============================================================================
#include "diag/Recursos.hpp"
#include "platform/Hilo.hpp"
#include "platform/Termica.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
// winsock2 antes que windows.h (iphlpapi la necesita y windows.h trae la vieja).
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <windows.h>
#include <dxgi.h>
#include <pdh.h>
#include <psapi.h>
#include <tlhelp32.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <net/if.h>
#include <net/route.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <unistd.h>
#elif defined(__linux__)
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ba::diag {
namespace {

using json = nlohmann::json;
constexpr double kD = Recursos::kDesconocido;

std::int64_t ahoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

/// Un número para el JSON: null si no se sabe (más claro que un -1 para quien lo lee).
json num(double v, int decimales = 1) {
    if (v < 0 || !std::isfinite(v)) return nullptr;
    const double f = std::pow(10.0, decimales);
    return std::round(v * f) / f;
}

// --- Lo que se lee de cada sistema (contadores acumulados) ------------------------

struct Contador {
    std::uint64_t entrada = 0, salida = 0; ///< bytes desde que prendió la interfaz
    double velocidadMbps = kD;
};
struct Crudo {
    bool cpuOk = false;
    std::uint64_t cpuOcupado = 0, cpuTotal = 0; ///< en «ticks» (la unidad da igual: se divide)
    double ramUsadaMb = kD, ramTotalMb = kD;
    double gpu = kD, vramUsadaMb = kD, vramTotalMb = kD;
    std::string gpuNombre;
    bool unificada = false;
    bool redOk = false;
    std::map<std::string, Contador> red; ///< solo las físicas (sin loopback ni túneles)
    double discoEscritos = kD;           ///< bytes acumulados
    double discoTasa = kD;               ///< bytes/s, si el sistema ya lo da como tasa (Windows)
};
struct CrudoProceso {
    bool ok = false;
    double cpuSeg = 0;
    double rssMb = kD;
};

#if defined(_WIN32)

std::string utf8(const wchar_t* w) {
    if (w == nullptr || *w == 0) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}
std::uint64_t u64(const FILETIME& f) { return (static_cast<std::uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime; }
std::string minusculas(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/// pdh.dll, iphlpapi.dll y dxgi.dll se CARGAN al medir (no se importan): ba_core
/// no suma dependencias y, si falta alguna (una edición recortada), lo suyo queda
/// desconocido en vez de que el programa no arranque.
class Sistema {
    using Abrir = PDH_STATUS(WINAPI*)(LPCWSTR, DWORD_PTR, PDH_HQUERY*);
    using Agregar = PDH_STATUS(WINAPI*)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);
    using Juntar = PDH_STATUS(WINAPI*)(PDH_HQUERY);
    using Leer = PDH_STATUS(WINAPI*)(PDH_HCOUNTER, DWORD, LPDWORD, LPDWORD, PPDH_FMT_COUNTERVALUE_ITEM_W);
    using Cerrar = PDH_STATUS(WINAPI*)(PDH_HQUERY);
    using TablaIf = DWORD(WINAPI*)(PMIB_IF_TABLE2*);
    using LiberarTabla = VOID(WINAPI*)(PVOID);
    using CrearFabrica = HRESULT(WINAPI*)(REFIID, void**);

   public:
    Sistema() {
        if ((pdh_ = LoadLibraryW(L"pdh.dll")) != nullptr) {
            const auto f = [this](const char* n) { return reinterpret_cast<void*>(GetProcAddress(pdh_, n)); };
            abrir_ = reinterpret_cast<Abrir>(f("PdhOpenQueryW"));
            agregar_ = reinterpret_cast<Agregar>(f("PdhAddEnglishCounterW"));
            juntar_ = reinterpret_cast<Juntar>(f("PdhCollectQueryData"));
            leer_ = reinterpret_cast<Leer>(f("PdhGetFormattedCounterArrayW"));
            cerrar_ = reinterpret_cast<Cerrar>(f("PdhCloseQuery"));
            if (abrir_ && agregar_ && juntar_ && leer_ && cerrar_ && abrir_(nullptr, 0, &q_) == ERROR_SUCCESS) {
                // Los mismos contadores que el Administrador de tareas.
                if (agregar_(q_, L"\\GPU Engine(*)\\Utilization Percentage", 0, &motores_) != ERROR_SUCCESS) motores_ = nullptr;
                if (agregar_(q_, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &memoria_) != ERROR_SUCCESS) memoria_ = nullptr;
                if (agregar_(q_, L"\\PhysicalDisk(*)\\Disk Write Bytes/sec", 0, &disco_) != ERROR_SUCCESS) disco_ = nullptr;
                juntar_(q_); // la primera vuelta no da tasas
                pdhOk_ = true;
            }
        }
        if ((iphlp_ = LoadLibraryW(L"iphlpapi.dll")) != nullptr) {
            tabla_ = reinterpret_cast<TablaIf>(reinterpret_cast<void*>(GetProcAddress(iphlp_, "GetIfTable2")));
            liberar_ = reinterpret_cast<LiberarTabla>(reinterpret_cast<void*>(GetProcAddress(iphlp_, "FreeMibTable")));
        }
        placa();
    }
    ~Sistema() {
        if (q_ != nullptr && cerrar_ != nullptr) cerrar_(q_);
        if (pdh_ != nullptr) FreeLibrary(pdh_);
        if (iphlp_ != nullptr) FreeLibrary(iphlp_);
    }
    Sistema(const Sistema&) = delete;
    Sistema& operator=(const Sistema&) = delete;

    void leer(Crudo& c) {
        FILETIME idle, kernel, user;
        if (GetSystemTimes(&idle, &kernel, &user)) {
            // El tiempo de núcleo del sistema INCLUYE el ocioso.
            c.cpuTotal = u64(kernel) + u64(user);
            c.cpuOcupado = c.cpuTotal - u64(idle);
            c.cpuOk = true;
        }
        MEMORYSTATUSEX m{};
        m.dwLength = sizeof(m);
        if (GlobalMemoryStatusEx(&m)) {
            c.ramTotalMb = static_cast<double>(m.ullTotalPhys) / 1048576.0;
            c.ramUsadaMb = static_cast<double>(m.ullTotalPhys - m.ullAvailPhys) / 1048576.0;
        }
        gpuYDisco(c);
        red(c);
    }

    CrudoProceso proceso(std::uint32_t pid) {
        CrudoProceso p;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (h == nullptr) return p;
        FILETIME cr, sa, k, u;
        if (GetProcessTimes(h, &cr, &sa, &k, &u)) {
            p.cpuSeg = static_cast<double>(u64(k) + u64(u)) / 1e7;
            p.ok = true;
        }
        PROCESS_MEMORY_COUNTERS pmc{};
        if (K32GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) p.rssMb = static_cast<double>(pmc.WorkingSetSize) / 1048576.0;
        // Un proceso que ya terminó todavía se abre: si salió, no cuenta.
        DWORD salida = 0;
        if (GetExitCodeProcess(h, &salida) && salida != STILL_ACTIVE) p.ok = false;
        CloseHandle(h);
        return p;
    }

   private:
    /// La placa que se mide: la de hardware con más memoria propia (la que dibuja
    /// y codifica en una máquina con integrada y dedicada), por DXGI.
    void placa() {
        HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
        if (dxgi == nullptr) return;
        const auto crear = reinterpret_cast<CrearFabrica>(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory1")));
        IDXGIFactory1* fab = nullptr;
        if (crear != nullptr && SUCCEEDED(crear(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&fab))) && fab != nullptr) {
            IDXGIAdapter1* a = nullptr;
            SIZE_T mejor = 0;
            for (UINT i = 0; fab->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
                DXGI_ADAPTER_DESC1 d{};
                if (SUCCEEDED(a->GetDesc1(&d)) && (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
                    (luid_.empty() || d.DedicatedVideoMemory > mejor)) {
                    mejor = d.DedicatedVideoMemory;
                    char l[64];
                    std::snprintf(l, sizeof(l), "luid_0x%08lx_0x%08lx", static_cast<unsigned long>(d.AdapterLuid.HighPart),
                                  static_cast<unsigned long>(d.AdapterLuid.LowPart));
                    luid_ = l;
                    nombre_ = utf8(d.Description);
                    vramTotalMb_ = static_cast<double>(d.DedicatedVideoMemory) / 1048576.0;
                }
                a->Release();
            }
            fab->Release();
        }
        FreeLibrary(dxgi);
    }

    /// Los valores de un contador con comodín: {instancia, valor}.
    std::vector<std::pair<std::string, double>> valores(PDH_HCOUNTER c) {
        std::vector<std::pair<std::string, double>> out;
        if (c == nullptr) return out;
        DWORD bytes = 0, n = 0;
        leer_(c, PDH_FMT_DOUBLE, &bytes, &n, nullptr);
        if (bytes == 0) return out;
        std::vector<unsigned char> buf(bytes);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
        if (leer_(c, PDH_FMT_DOUBLE, &bytes, &n, items) != ERROR_SUCCESS) return out;
        out.reserve(n);
        for (DWORD i = 0; i < n; ++i)
            // PDH_CSTATUS_VALID_DATA (0) o PDH_CSTATUS_NEW_DATA (1): lo demás es una instancia que no sirve.
            if (items[i].FmtValue.CStatus == 0 || items[i].FmtValue.CStatus == 1)
                out.emplace_back(minusculas(utf8(items[i].szName)), items[i].FmtValue.doubleValue);
        return out;
    }

    void gpuYDisco(Crudo& c) {
        c.gpuNombre = nombre_;
        c.vramTotalMb = vramTotalMb_ > 0 ? vramTotalMb_ : kD;
        if (!pdhOk_ || juntar_(q_) != ERROR_SUCCESS) return;
        // «pid_1234_luid_0x…_0x…_phys_0_eng_3_engtype_3D»: cada motor de cada placa,
        // sumado entre procesos; la placa está tan ocupada como su motor más cargado
        // (así lo cuenta el Administrador de tareas).
        if (motores_ != nullptr) {
            std::map<std::string, double> porMotor;
            for (const auto& [nom, v] : valores(motores_)) {
                const auto p = nom.find("luid_");
                if (p == std::string::npos) continue;
                if (!luid_.empty() && nom.compare(p, luid_.size(), luid_) != 0) continue;
                porMotor[nom.substr(p)] += v;
            }
            if (!porMotor.empty()) {
                double m = 0;
                for (const auto& [k, v] : porMotor) m = std::max(m, v);
                c.gpu = std::min(100.0, m);
            }
        }
        if (memoria_ != nullptr) {
            double usada = -1;
            for (const auto& [nom, v] : valores(memoria_))
                if (luid_.empty() || nom.rfind(luid_, 0) == 0) usada = std::max(usada, 0.0) + v;
            if (usada >= 0) c.vramUsadaMb = usada / 1048576.0;
        }
        if (disco_ != nullptr)
            for (const auto& [nom, v] : valores(disco_))
                if (nom == "_total") c.discoTasa = v;
    }

    void red(Crudo& c) {
        if (tabla_ == nullptr || liberar_ == nullptr) return;
        PMIB_IF_TABLE2 t = nullptr;
        if (tabla_(&t) != NO_ERROR || t == nullptr) return;
        for (ULONG i = 0; i < t->NumEntries; ++i) {
            const MIB_IF_ROW2& r = t->Table[i];
            // Solo las placas de verdad: ni loopback, ni túneles, ni las capas de filtro
            // (cada placa aparece varias veces, una por filtro de Windows).
            if (!r.InterfaceAndOperStatusFlags.HardwareInterface || r.InterfaceAndOperStatusFlags.FilterInterface) continue;
            if (r.Type == IF_TYPE_SOFTWARE_LOOPBACK || r.OperStatus != IfOperStatusUp) continue;
            std::string nom = utf8(r.Alias);
            if (nom.empty()) nom = "if" + std::to_string(r.InterfaceIndex);
            Contador k;
            k.entrada = r.InOctets;
            k.salida = r.OutOctets;
            const ULONG64 vel = std::max(r.ReceiveLinkSpeed, r.TransmitLinkSpeed);
            if (vel > 0 && vel != static_cast<ULONG64>(-1)) k.velocidadMbps = static_cast<double>(vel) / 1e6;
            c.red[nom] = k;
        }
        c.redOk = true;
        liberar_(t);
    }

    HMODULE pdh_ = nullptr, iphlp_ = nullptr;
    Abrir abrir_ = nullptr;
    Agregar agregar_ = nullptr;
    Juntar juntar_ = nullptr;
    Leer leer_ = nullptr;
    Cerrar cerrar_ = nullptr;
    PDH_HQUERY q_ = nullptr;
    PDH_HCOUNTER motores_ = nullptr, memoria_ = nullptr, disco_ = nullptr;
    bool pdhOk_ = false;
    TablaIf tabla_ = nullptr;
    LiberarTabla liberar_ = nullptr;
    std::string luid_, nombre_;
    double vramTotalMb_ = kD;
};

#elif defined(__APPLE__)

/// Un número de un diccionario de IOKit (-1 si no está).
double numeroCf(CFDictionaryRef d, const char* clave) {
    if (d == nullptr) return kD;
    CFStringRef k = CFStringCreateWithCString(kCFAllocatorDefault, clave, kCFStringEncodingUTF8);
    const void* v = CFDictionaryGetValue(d, k);
    CFRelease(k);
    if (v == nullptr || CFGetTypeID(v) != CFNumberGetTypeID()) return kD;
    std::int64_t x = 0;
    CFNumberGetValue(static_cast<CFNumberRef>(v), kCFNumberSInt64Type, &x);
    return static_cast<double>(x);
}
std::string textoCf(CFTypeRef v) {
    if (v == nullptr) return {};
    if (CFGetTypeID(v) == CFStringGetTypeID()) {
        char buf[256] = {0};
        if (CFStringGetCString(static_cast<CFStringRef>(v), buf, sizeof(buf), kCFStringEncodingUTF8)) return buf;
    } else if (CFGetTypeID(v) == CFDataGetTypeID()) { // en las Intel, «model» viene en bytes
        const auto* p = reinterpret_cast<const char*>(CFDataGetBytePtr(static_cast<CFDataRef>(v)));
        return std::string(p, strnlen(p, static_cast<size_t>(CFDataGetLength(static_cast<CFDataRef>(v)))));
    }
    return {};
}

class Sistema {
   public:
    Sistema() : host_(mach_host_self()) {
        mach_timebase_info(&base_);
        if (base_.denom == 0) base_ = {1, 1};
        std::int64_t mem = 0;
        size_t len = sizeof(mem);
        if (sysctlbyname("hw.memsize", &mem, &len, nullptr, 0) == 0) ramTotal_ = static_cast<double>(mem);
        vm_size_t pag = 0;
        pagina_ = host_page_size(host_, &pag) == KERN_SUCCESS ? static_cast<double>(pag) : 16384.0;
    }
    ~Sistema() { mach_port_deallocate(mach_task_self(), host_); }
    Sistema(const Sistema&) = delete;
    Sistema& operator=(const Sistema&) = delete;

    void leer(Crudo& c) {
        host_cpu_load_info_data_t cpu{};
        mach_msg_type_number_t n = HOST_CPU_LOAD_INFO_COUNT;
        if (host_statistics(host_, HOST_CPU_LOAD_INFO, reinterpret_cast<host_info_t>(&cpu), &n) == KERN_SUCCESS) {
            c.cpuOcupado = static_cast<std::uint64_t>(cpu.cpu_ticks[CPU_STATE_USER]) + cpu.cpu_ticks[CPU_STATE_SYSTEM] +
                           cpu.cpu_ticks[CPU_STATE_NICE];
            c.cpuTotal = c.cpuOcupado + cpu.cpu_ticks[CPU_STATE_IDLE];
            c.cpuOk = true;
        }
        // La «memoria usada» del Monitor de Actividad: la de las apps (sin lo
        // purgable), la fija y la comprimida. El caché de archivos no cuenta: se libera solo.
        vm_statistics64_data_t vm{};
        n = HOST_VM_INFO64_COUNT;
        if (ramTotal_ > 0 &&
            host_statistics64(host_, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &n) == KERN_SUCCESS) {
            const double pags = static_cast<double>(vm.internal_page_count) - vm.purgeable_count + vm.wire_count +
                                vm.compressor_page_count;
            c.ramTotalMb = ramTotal_ / 1048576.0;
            c.ramUsadaMb = std::max(0.0, pags * pagina_ / 1048576.0);
        }
        gpu(c);
        red(c);
        disco(c);
    }

    CrudoProceso proceso(std::uint32_t pid) {
        CrudoProceso p;
        rusage_info_v2 ri{};
        if (proc_pid_rusage(static_cast<int>(pid), RUSAGE_INFO_V2, reinterpret_cast<rusage_info_t*>(&ri)) != 0) return p;
        // Los tiempos vienen en unidades de mach_absolute_time (en Apple Silicon no son ns).
        const double ns = static_cast<double>(ri.ri_user_time + ri.ri_system_time) * base_.numer / base_.denom;
        p.cpuSeg = ns / 1e9;
        p.rssMb = static_cast<double>(ri.ri_resident_size) / 1048576.0;
        p.ok = true;
        return p;
    }

   private:
    /// IOAccelerator → «PerformanceStatistics» (lo que muestra el Monitor de Actividad).
    void gpu(Crudo& c) {
        io_iterator_t it = 0;
        // 0 = el puerto por defecto (kIOMainPortDefault es de macOS 12; kIOMasterPortDefault, obsoleto).
        if (IOServiceGetMatchingServices(0, IOServiceMatching("IOAccelerator"), &it) != KERN_SUCCESS) return;
        while (io_object_t s = IOIteratorNext(it)) {
            auto* est = static_cast<CFDictionaryRef>(
                IORegistryEntryCreateCFProperty(s, CFSTR("PerformanceStatistics"), kCFAllocatorDefault, 0));
            if (est != nullptr && CFGetTypeID(est) == CFDictionaryGetTypeID()) {
                const double uso = numeroCf(est, "Device Utilization %");
                if (uso >= 0) c.gpu = std::max(c.gpu, std::min(100.0, uso));
                // Placa dedicada (Intel con AMD): memoria propia con usada y libre.
                const double usada = numeroCf(est, "vramUsedBytes"), libre = numeroCf(est, "vramFreeBytes");
                if (usada >= 0 && libre >= 0) {
                    c.vramUsadaMb = usada / 1048576.0;
                    c.vramTotalMb = (usada + libre) / 1048576.0;
                } else {
                    // Apple Silicon: memoria unificada; lo que la GPU tiene en uso, sin un total propio.
                    const double enUso = numeroCf(est, "In use system memory");
                    if (enUso >= 0) {
                        c.vramUsadaMb = std::max(c.vramUsadaMb, 0.0) + enUso / 1048576.0;
                        c.unificada = true;
                    }
                }
                if (c.gpuNombre.empty()) {
                    CFTypeRef m = IORegistryEntryCreateCFProperty(s, CFSTR("model"), kCFAllocatorDefault, 0);
                    c.gpuNombre = textoCf(m);
                    if (m != nullptr) CFRelease(m);
                }
            }
            if (est != nullptr) CFRelease(est);
            IOObjectRelease(s);
        }
        IOObjectRelease(it);
    }

    /// Los contadores de 64 bits de cada interfaz (NET_RT_IFLIST2: los de
    /// getifaddrs son de 32 y dan la vuelta a los 4 GB).
    void red(Crudo& c) {
        int mib[] = {CTL_NET, PF_ROUTE, 0, 0, NET_RT_IFLIST2, 0};
        size_t len = 0;
        if (sysctl(mib, 6, nullptr, &len, nullptr, 0) != 0 || len == 0) return;
        std::vector<char> buf(len);
        if (sysctl(mib, 6, buf.data(), &len, nullptr, 0) != 0) return;
        for (size_t p = 0; p + sizeof(if_msghdr) <= len;) {
            const auto* ifm = reinterpret_cast<const if_msghdr*>(buf.data() + p);
            if (ifm->ifm_msglen == 0) break;
            if (ifm->ifm_type == RTM_IFINFO2) {
                const auto* m2 = reinterpret_cast<const if_msghdr2*>(ifm);
                char nom[IF_NAMESIZE] = {0};
                // Las físicas (Ethernet, Wi-Fi, Thunderbolt) son «en*»: los túneles
                // (utun, VPN) y los puentes repiten el mismo tráfico.
                if (if_indextoname(m2->ifm_index, nom) != nullptr && std::strncmp(nom, "en", 2) == 0 &&
                    (m2->ifm_flags & IFF_UP) != 0 && (m2->ifm_flags & IFF_LOOPBACK) == 0) {
                    Contador k;
                    k.entrada = m2->ifm_data.ifi_ibytes;
                    k.salida = m2->ifm_data.ifi_obytes;
                    if (m2->ifm_data.ifi_baudrate > 0) k.velocidadMbps = static_cast<double>(m2->ifm_data.ifi_baudrate) / 1e6;
                    c.red[nom] = k;
                }
            }
            p += ifm->ifm_msglen;
        }
        c.redOk = true;
    }

    /// Lo escrito en cada disco (IOBlockStorageDriver → «Statistics»).
    void disco(Crudo& c) {
        io_iterator_t it = 0;
        if (IOServiceGetMatchingServices(0, IOServiceMatching("IOBlockStorageDriver"), &it) != KERN_SUCCESS) return;
        double total = 0;
        bool alguno = false;
        while (io_object_t s = IOIteratorNext(it)) {
            auto* est = static_cast<CFDictionaryRef>(IORegistryEntryCreateCFProperty(s, CFSTR("Statistics"), kCFAllocatorDefault, 0));
            if (est != nullptr && CFGetTypeID(est) == CFDictionaryGetTypeID()) {
                const double b = numeroCf(est, "Bytes (Write)");
                if (b >= 0) {
                    total += b;
                    alguno = true;
                }
            }
            if (est != nullptr) CFRelease(est);
            IOObjectRelease(s);
        }
        IOObjectRelease(it);
        if (alguno) c.discoEscritos = total;
    }

    mach_port_t host_;
    mach_timebase_info_data_t base_{1, 1};
    double ramTotal_ = 0, pagina_ = 16384;
};

#elif defined(__linux__)

std::string leerTodo(const std::string& ruta) {
    std::ifstream f(ruta);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
double leerNumero(const std::string& ruta) {
    std::ifstream f(ruta);
    double v = kD;
    if (!(f >> v)) return kD;
    return v;
}
bool existe(const std::string& ruta) {
    struct stat st {};
    return ::stat(ruta.c_str(), &st) == 0;
}

class Sistema {
   public:
    Sistema() {
        tick_ = static_cast<double>(sysconf(_SC_CLK_TCK));
        pagina_ = static_cast<double>(sysconf(_SC_PAGESIZE));
    }

    void leer(Crudo& c) {
        {
            std::ifstream f("/proc/stat");
            std::string cpu;
            std::uint64_t us = 0, ni = 0, sy = 0, id = 0, io = 0, irq = 0, sirq = 0, st = 0;
            if (f >> cpu >> us >> ni >> sy >> id >> io >> irq >> sirq && cpu == "cpu") {
                f >> st; // steal (puede no estar)
                c.cpuOcupado = us + ni + sy + irq + sirq + st;
                c.cpuTotal = c.cpuOcupado + id + io;
                c.cpuOk = true;
            }
        }
        {
            std::ifstream f("/proc/meminfo");
            std::string k;
            double v = 0, total = -1, disp = -1;
            std::string unidad;
            while (f >> k >> v >> unidad) {
                if (k == "MemTotal:") total = v;
                else if (k == "MemAvailable:") disp = v;
                if (total >= 0 && disp >= 0) break;
            }
            if (total > 0 && disp >= 0) {
                c.ramTotalMb = total / 1024.0;
                c.ramUsadaMb = (total - disp) / 1024.0;
            }
        }
        gpu(c);
        red(c);
        disco(c);
    }

    CrudoProceso proceso(std::uint32_t pid) {
        CrudoProceso p;
        const std::string base = "/proc/" + std::to_string(pid);
        std::ifstream st(base + "/stat");
        std::string linea;
        if (!std::getline(st, linea)) return p;
        // Después del «(nombre)» viene el estado; utime y stime son los campos 14 y 15.
        const auto cierre = linea.rfind(')');
        if (cierre == std::string::npos || cierre + 2 > linea.size()) return p;
        std::istringstream ls(linea.substr(cierre + 2));
        std::string x;
        for (int i = 3; i <= 13 && ls >> x; ++i) {
        }
        unsigned long long ut = 0, stt = 0;
        if (!(ls >> ut >> stt)) return p;
        p.cpuSeg = static_cast<double>(ut + stt) / tick_;
        std::ifstream sm(base + "/statm");
        long paginas = 0, rss = 0;
        if (sm >> paginas >> rss) p.rssMb = static_cast<double>(rss) * pagina_ / 1048576.0;
        p.ok = true;
        return p;
    }

   private:
    /// amdgpu: gpu_busy_percent y mem_info_vram_*. Intel (i915) y NVIDIA (el
    /// driver propietario) no lo publican en /sys: queda desconocido.
    void gpu(Crudo& c) {
        DIR* d = opendir("/sys/class/drm");
        if (d == nullptr) return;
        while (dirent* e = readdir(d)) {
            const std::string n = e->d_name;
            // card0, card1… (no card0-DP-1, que son los conectores)
            if (n.rfind("card", 0) != 0 || n.size() <= 4 ||
                !std::all_of(n.begin() + 4, n.end(), [](char ch) { return ch >= '0' && ch <= '9'; }))
                continue;
            const std::string dev = "/sys/class/drm/" + n + "/device/";
            const double uso = leerNumero(dev + "gpu_busy_percent");
            if (uso < 0) continue;
            if (uso >= c.gpu) {
                c.gpu = std::min(100.0, uso);
                const double usada = leerNumero(dev + "mem_info_vram_used");
                const double total = leerNumero(dev + "mem_info_vram_total");
                if (usada >= 0 && total > 0) {
                    c.vramUsadaMb = usada / 1048576.0;
                    c.vramTotalMb = total / 1048576.0;
                }
                c.gpuNombre = n;
            }
        }
        closedir(d);
    }

    void red(Crudo& c) {
        std::ifstream f("/proc/net/dev");
        if (!f) return;
        std::string linea;
        while (std::getline(f, linea)) {
            const auto dos = linea.find(':');
            if (dos == std::string::npos) continue;
            std::string nom = linea.substr(0, dos);
            nom.erase(0, nom.find_first_not_of(' '));
            // Solo las físicas (tienen «device»): ni lo, ni puentes de Docker, ni túneles.
            if (nom.empty() || nom == "lo" || !existe("/sys/class/net/" + nom + "/device")) continue;
            std::istringstream ls(linea.substr(dos + 1));
            std::uint64_t v[9] = {0};
            for (auto& x : v) ls >> x;
            Contador k;
            k.entrada = v[0];
            k.salida = v[8];
            const double vel = leerNumero("/sys/class/net/" + nom + "/speed"); // Mb/s; -1 si no se sabe
            if (vel > 0) k.velocidadMbps = vel;
            if (leerTodo("/sys/class/net/" + nom + "/operstate").rfind("down", 0) == 0) continue;
            c.red[nom] = k;
        }
        c.redOk = true;
    }

    /// Sectores escritos de los discos enteros (no particiones, ni loop, ni los
    /// que son otra vista del mismo disco: dm-*, md*).
    void disco(Crudo& c) {
        std::ifstream f("/proc/diskstats");
        if (!f) return;
        std::string linea;
        double total = 0;
        bool alguno = false;
        while (std::getline(f, linea)) {
            std::istringstream ls(linea);
            unsigned long long mayor = 0, menor = 0;
            std::string nom;
            unsigned long long v[7] = {0};
            if (!(ls >> mayor >> menor >> nom)) continue;
            for (auto& x : v) ls >> x;
            if (nom.rfind("loop", 0) == 0 || nom.rfind("ram", 0) == 0 || nom.rfind("zram", 0) == 0 ||
                nom.rfind("dm-", 0) == 0 || nom.rfind("md", 0) == 0 || !existe("/sys/block/" + nom))
                continue;
            total += static_cast<double>(v[6]) * 512.0; // sectores escritos (siempre de 512)
            alguno = true;
        }
        if (alguno) c.discoEscritos = total;
    }

    double tick_ = 100, pagina_ = 4096;
};

#else

class Sistema {
   public:
    void leer(Crudo&) {}
    CrudoProceso proceso(std::uint32_t) { return {}; }
};

#endif

// --- El historial ----------------------------------------------------------------

/// Las series que se guardan (en este orden en cada punto).
const std::vector<std::string>& kSeries() {
    static const std::vector<std::string> s{"cpu", "gpu", "ram", "vram", "red", "red_entrada", "red_salida", "motor", "disco"};
    return s;
}
constexpr std::size_t kNSeries = 9;

struct Punto {
    std::int64_t t = 0;
    std::array<float, kNSeries> v{};
};

std::array<float, kNSeries> valoresDe(const Recursos::Muestra& m) {
    return {static_cast<float>(m.cpu),          static_cast<float>(m.gpu),           static_cast<float>(m.ramPct()),
            static_cast<float>(m.vramPct()),    static_cast<float>(m.redPct),        static_cast<float>(m.redEntradaMbps),
            static_cast<float>(m.redSalidaMbps), static_cast<float>(m.perdidosPct),  static_cast<float>(m.discoEscrituraMBs)};
}

int indiceDe(const std::string& clave) {
    const auto& s = kSeries();
    const auto it = std::find(s.begin(), s.end(), clave);
    return it == s.end() ? -1 : static_cast<int>(it - s.begin());
}

struct Seguimiento {
    int sobreAviso = 0, sobreCritico = 0, bajoAviso = 0, bajoCritico = 0;
    Recursos::Estado estado = Recursos::Estado::Desconocido;
    std::int64_t desde = 0;
};

/// Cuánto hace que está en su estado; los de aviso y crítico, contando los
/// segundos que tuvo que sostenerse para llegar (si no, recién llegado decía «hace 0 s»).
std::int64_t sostenido(const Seguimiento& sg, std::int64_t t, const Recursos::Umbrales& u) {
    if (sg.desde <= 0 || t < sg.desde) return 0;
    const bool alto = sg.estado == Recursos::Estado::Aviso || sg.estado == Recursos::Estado::Critico;
    return (t - sg.desde) / 1000 + (alto ? std::max(1, u.sostenidoS) - 1 : 0);
}

} // namespace

// --- Recursos --------------------------------------------------------------------

struct Recursos::Impl {
    // Lo que se lee del sistema: solo un hilo a la vez (el que mide o un test).
    std::mutex medirMu;
    Sistema sistema;
    Crudo previo;
    bool hayPrevio = false;
    std::chrono::steady_clock::time_point tPrevio;
    std::map<std::uint32_t, double> cpuPrevia; ///< pid → segundos de CPU de la vuelta anterior

    // Lo compartido (corto: nunca se mide con esto tomado).
    mutable std::mutex mu;
    std::vector<std::pair<std::string, std::uint32_t>> quienes;
    Motor motor;
    Umbrales umbrales;
    std::vector<Punto> anillo;
    std::size_t cabeza = 0, cuantos = 0;
    Muestra ultima;
    std::map<std::string, Seguimiento> seg;

    // El hilo.
    std::thread hilo;
    std::mutex hiloMu;
    std::condition_variable cv;
    bool parar = false;
    std::atomic<bool> andando{false};

    /// El punto `atras` (0 = el último).
    const Punto& punto(std::size_t atras) const {
        return anillo[(cabeza + anillo.size() - 1 - atras) % anillo.size()];
    }
    double promedio(int idx, std::size_t n) const {
        double s = 0;
        int k = 0;
        for (std::size_t i = 0; i < std::min(n, cuantos); ++i) {
            const float v = punto(i).v[static_cast<std::size_t>(idx)];
            if (v >= 0) {
                s += v;
                ++k;
            }
        }
        return k > 0 ? s / k : kD;
    }
    double maximo(int idx, std::size_t n) const {
        double m = kD;
        for (std::size_t i = 0; i < std::min(n, cuantos); ++i) m = std::max<double>(m, punto(i).v[static_cast<std::size_t>(idx)]);
        return m;
    }
};

const std::vector<std::string>& Recursos::claves() {
    static const std::vector<std::string> c{"cpu", "gpu", "ram", "vram", "red", "motor"};
    return c;
}

const char* Recursos::nombre(Estado e) {
    switch (e) {
        case Estado::Normal: return "normal";
        case Estado::Aviso: return "aviso";
        case Estado::Critico: return "critico";
        default: return "desconocido";
    }
}

Recursos::Recursos(int historialS) : d_(std::make_unique<Impl>()) {
    d_->anillo.resize(static_cast<std::size_t>(std::max(10, historialS)));
    // El launcher (si nos lanzó él) se mide como uno más.
    const auto [ppid, pnom] = padre();
    if (ppid != 0 && pnom.rfind("BroadcastAppliance", 0) == 0) d_->quienes.emplace_back("launcher", ppid);
}

Recursos::~Recursos() { parar(); }

void Recursos::arrancar(std::function<void()> alMedir, int periodoMs) {
    if (d_->andando.exchange(true)) return;
    {
        std::lock_guard<std::mutex> l(d_->hiloMu);
        d_->parar = false;
    }
    d_->hilo = std::thread([this, alMedir = std::move(alMedir), periodoMs]() {
        ba::platform::nombrarHilo("recursos");
        medir(); // la primera vuelta solo anota los contadores
        for (;;) {
            {
                std::unique_lock<std::mutex> l(d_->hiloMu);
                if (d_->cv.wait_for(l, std::chrono::milliseconds(std::max(100, periodoMs)), [this] { return d_->parar; }))
                    return;
            }
            agregar(medir());
            if (alMedir) alMedir();
        }
    });
}

void Recursos::parar() {
    {
        std::lock_guard<std::mutex> l(d_->hiloMu);
        d_->parar = true;
    }
    d_->cv.notify_all();
    if (d_->hilo.joinable()) d_->hilo.join();
    d_->andando = false;
}

void Recursos::procesos(std::vector<std::pair<std::string, std::uint32_t>> quienes) {
    std::lock_guard<std::mutex> l(d_->mu);
    // El launcher lo encuentra el constructor: se conserva si quien llama no lo nombra.
    for (const auto& q : d_->quienes)
        if (q.first == "launcher" &&
            std::none_of(quienes.begin(), quienes.end(), [](const auto& x) { return x.first == "launcher"; }))
            quienes.push_back(q);
    d_->quienes = std::move(quienes);
}

void Recursos::motor(const Motor& m) {
    std::lock_guard<std::mutex> l(d_->mu);
    d_->motor = m;
}

void Recursos::umbrales(const Umbrales& u) {
    std::lock_guard<std::mutex> l(d_->mu);
    d_->umbrales = u;
}

Recursos::Umbrales Recursos::umbrales() const {
    std::lock_guard<std::mutex> l(d_->mu);
    return d_->umbrales;
}

Recursos::Muestra Recursos::medir() {
    std::lock_guard<std::mutex> lm(d_->medirMu);
    Muestra m;
    m.t = ahoraMs();
    const unsigned hw = std::thread::hardware_concurrency();
    m.nucleos = hw > 0 ? static_cast<int>(hw) : 1;
    std::vector<std::pair<std::string, std::uint32_t>> quienes;
    {
        std::lock_guard<std::mutex> l(d_->mu);
        quienes = d_->quienes;
        m.fps = d_->motor.fps;
        m.perdidosPct = d_->motor.perdidosPct;
        m.salidaKbps = d_->motor.salidaKbps;
    }
    quienes.insert(quienes.begin(), std::make_pair(std::string("app"), pidPropio()));

    Crudo c;
    d_->sistema.leer(c);
    const auto t = std::chrono::steady_clock::now();
    const double dt = d_->hayPrevio ? std::chrono::duration<double>(t - d_->tPrevio).count() : 0;
    const Crudo& p = d_->previo;
    const bool comparable = d_->hayPrevio && dt > 0.05;

    if (comparable && c.cpuOk && p.cpuOk && c.cpuTotal > p.cpuTotal)
        m.cpu = std::clamp(static_cast<double>(c.cpuOcupado - std::min(c.cpuOcupado, p.cpuOcupado)) /
                               static_cast<double>(c.cpuTotal - p.cpuTotal) * 100.0,
                           0.0, 100.0);
    m.ramUsadaMb = c.ramUsadaMb;
    m.ramTotalMb = c.ramTotalMb;
    m.gpu = c.gpu;
    m.vramUsadaMb = c.vramUsadaMb;
    m.vramTotalMb = c.vramTotalMb;
    m.gpuNombre = c.gpuNombre;
    m.memoriaUnificada = c.unificada;

    if (comparable && c.redOk && p.redOk) {
        double ent = 0, sal = 0, pct = kD;
        for (const auto& [nom, k] : c.red) {
            const auto a = p.red.find(nom);
            if (a == p.red.end()) continue;
            // Un contador que vuelve atrás (la interfaz se reinició): esta vuelta no cuenta.
            const double e = k.entrada >= a->second.entrada ? static_cast<double>(k.entrada - a->second.entrada) * 8 / dt / 1e6 : 0;
            const double s = k.salida >= a->second.salida ? static_cast<double>(k.salida - a->second.salida) * 8 / dt / 1e6 : 0;
            ent += e;
            sal += s;
            if (k.velocidadMbps > 0) pct = std::max(pct, std::min(100.0, std::max(e, s) / k.velocidadMbps * 100.0));
            if (e > 0.001 || s > 0.001 || k.velocidadMbps > 0) m.interfaces.push_back({nom, e, s, k.velocidadMbps});
        }
        m.redEntradaMbps = ent;
        m.redSalidaMbps = sal;
        m.redPct = pct;
        std::sort(m.interfaces.begin(), m.interfaces.end(), [](const Interfaz& x, const Interfaz& y) {
            return x.entradaMbps + x.salidaMbps > y.entradaMbps + y.salidaMbps;
        });
    }
    if (c.discoTasa >= 0) m.discoEscrituraMBs = c.discoTasa / 1048576.0;
    else if (comparable && c.discoEscritos >= 0 && p.discoEscritos >= 0)
        m.discoEscrituraMBs = std::max(0.0, c.discoEscritos - p.discoEscritos) / dt / 1048576.0;

    std::map<std::uint32_t, double> cpuAhora;
    for (const auto& [nom, pid] : quienes) {
        if (pid == 0) continue;
        Proceso pr;
        pr.nombre = nom;
        pr.pid = pid;
        const CrudoProceso cp = d_->sistema.proceso(pid);
        if (!cp.ok) {
            m.procesos.push_back(pr); // está pedido pero no anda: se lo muestra desconocido
            continue;
        }
        pr.rssMb = cp.rssMb;
        cpuAhora[pid] = cp.cpuSeg;
        const auto a = d_->cpuPrevia.find(pid);
        if (comparable && a != d_->cpuPrevia.end() && cp.cpuSeg >= a->second) {
            pr.nucleos = (cp.cpuSeg - a->second) / dt;
            pr.cpu = std::min(100.0, pr.nucleos / m.nucleos * 100.0);
        }
        m.procesos.push_back(pr);
    }
    // La térmica (F4-05): barata en los tres sistemas (un estado, unos archivos de /sys).
    const platform::EstadoTermico te = platform::leerTermica();
    m.termica = platform::nombre(te.nivel);
    m.temperaturaC = te.temperaturaC;
    m.limiteFrecuenciaPct = te.limiteFrecuenciaPct;
    m.termicaFuente = te.fuente;
    d_->cpuPrevia = std::move(cpuAhora);
    d_->previo = std::move(c);
    d_->tPrevio = t;
    d_->hayPrevio = true;
    return m;
}

void Recursos::agregar(const Muestra& m) {
    std::lock_guard<std::mutex> l(d_->mu);
    Punto p;
    p.t = m.t;
    p.v = valoresDe(m);
    d_->anillo[d_->cabeza] = p;
    d_->cabeza = (d_->cabeza + 1) % d_->anillo.size();
    d_->cuantos = std::min(d_->cuantos + 1, d_->anillo.size());
    d_->ultima = m;

    // Los estados: para subir hay que SOSTENERSE `sostenidoS` muestras seguidas por
    // encima del umbral, y para bajar, otras tantas por debajo (sin eso, un valor
    // que bailotea en el borde cambiaba de estado a cada segundo).
    const Umbrales& u = d_->umbrales;
    const int s = std::max(1, u.sostenidoS);
    for (const std::string& k : claves()) {
        Seguimiento& sg = d_->seg[k];
        const double v = p.v[static_cast<std::size_t>(indiceDe(k))];
        Estado nuevo = sg.estado;
        if (v < 0) {
            sg = Seguimiento{};
            nuevo = Estado::Desconocido;
        } else {
            const double a = k == "motor" ? u.motorAviso : u.aviso;
            const double c = k == "motor" ? u.motorCritico : u.critico;
            sg.sobreAviso = v >= a ? sg.sobreAviso + 1 : 0;
            sg.sobreCritico = v >= c ? sg.sobreCritico + 1 : 0;
            sg.bajoAviso = v < a ? sg.bajoAviso + 1 : 0;
            sg.bajoCritico = v < c ? sg.bajoCritico + 1 : 0;
            const Estado sube = sg.sobreCritico >= s ? Estado::Critico : sg.sobreAviso >= s ? Estado::Aviso : Estado::Normal;
            if (nuevo == Estado::Desconocido) nuevo = Estado::Normal;
            if (sube > nuevo) {
                nuevo = sube;
            } else {
                if (nuevo == Estado::Critico && sg.bajoCritico >= s) nuevo = Estado::Aviso;
                if (nuevo == Estado::Aviso && sg.bajoAviso >= s) nuevo = Estado::Normal;
            }
        }
        if (nuevo != sg.estado) {
            sg.estado = nuevo;
            sg.desde = m.t;
        }
    }
}

Recursos::Muestra Recursos::ultima() const {
    std::lock_guard<std::mutex> l(d_->mu);
    return d_->ultima;
}

Recursos::Estado Recursos::estado(const std::string& clave) const {
    std::lock_guard<std::mutex> l(d_->mu);
    const auto it = d_->seg.find(clave);
    return it == d_->seg.end() ? Estado::Desconocido : it->second.estado;
}

std::vector<double> Recursos::serie(const std::string& clave, int segundos) const {
    std::vector<double> out;
    const int idx = indiceDe(clave);
    if (idx < 0) return out;
    std::lock_guard<std::mutex> l(d_->mu);
    const std::size_t n = std::min<std::size_t>(d_->cuantos, static_cast<std::size_t>(std::max(0, segundos)));
    out.reserve(n);
    for (std::size_t i = n; i-- > 0;) out.push_back(d_->punto(i).v[static_cast<std::size_t>(idx)]);
    return out;
}

double Recursos::pico(const std::string& clave) const {
    const int idx = indiceDe(clave);
    if (idx < 0) return kD;
    std::lock_guard<std::mutex> l(d_->mu);
    return d_->maximo(idx, d_->cuantos);
}

int Recursos::muestras() const {
    std::lock_guard<std::mutex> l(d_->mu);
    return static_cast<int>(d_->cuantos);
}

int Recursos::capacidad() const {
    std::lock_guard<std::mutex> l(d_->mu);
    return static_cast<int>(d_->anillo.size());
}

json Recursos::niveles() const {
    std::lock_guard<std::mutex> l(d_->mu);
    json out = json::object();
    const std::int64_t t = d_->ultima.t;
    for (const std::string& k : claves()) {
        const auto it = d_->seg.find(k);
        const Seguimiento sg = it == d_->seg.end() ? Seguimiento{} : it->second;
        out[k] = {{"estado", nombre(sg.estado)},
                  {"valor", d_->cuantos > 0 ? num(d_->punto(0).v[static_cast<std::size_t>(indiceDe(k))]) : json(nullptr)},
                  {"sostenido_s", sostenido(sg, t, d_->umbrales)}};
    }
    return out;
}

json Recursos::informe(int historialS, int pasoS) const {
    std::lock_guard<std::mutex> l(d_->mu);
    const Muestra& m = d_->ultima;
    const Umbrales& u = d_->umbrales;
    const std::size_t todo = d_->cuantos;
    constexpr std::size_t k10 = 10;
    const auto estadoDe = [&](const std::string& k) {
        const auto it = d_->seg.find(k);
        return it == d_->seg.end() ? Seguimiento{} : it->second;
    };
    // Lo común de cada recurso con estado: pico, promedio, estado y margen.
    const auto comun = [&](const std::string& k, nlohmann::json j) {
        const int idx = indiceDe(k);
        const Seguimiento sg = estadoDe(k);
        const double prom = d_->promedio(idx, k10);
        j["pico"] = num(d_->maximo(idx, todo));
        j["promedio_10s"] = num(prom);
        j["estado"] = nombre(sg.estado);
        j["sostenido_s"] = sostenido(sg, m.t, u);
        if (k != "motor") j["margen"] = prom >= 0 ? num(std::max(0.0, 100.0 - prom)) : nlohmann::json(nullptr);
        return j;
    };

    nlohmann::json out;
    out["t"] = m.t;
    out["periodo_s"] = 1;
    out["muestras"] = todo;
    out["umbrales"] = {{"aviso_pct", u.aviso},
                       {"critico_pct", u.critico},
                       {"motor_aviso_pct", u.motorAviso},
                       {"motor_critico_pct", u.motorCritico},
                       {"sostenido_s", u.sostenidoS}};
    out["cpu"] = comun("cpu", {{"pct", num(m.cpu)}, {"nucleos", m.nucleos}});
    out["ram"] = comun("ram", {{"pct", num(m.ramPct())}, {"usada_mb", num(m.ramUsadaMb, 0)}, {"total_mb", num(m.ramTotalMb, 0)}});
    out["gpu"] = comun("gpu", {{"pct", num(m.gpu)}, {"nombre", m.gpuNombre}});
    out["vram"] = comun("vram", {{"pct", num(m.vramPct())},
                                 {"usada_mb", num(m.vramUsadaMb, 0)},
                                 {"total_mb", num(m.vramTotalMb, 0)},
                                 {"memoria_unificada", m.memoriaUnificada}});
    nlohmann::json ifs = nlohmann::json::array();
    for (const Interfaz& i : m.interfaces)
        ifs.push_back({{"nombre", i.nombre},
                       {"entrada_mbps", num(i.entradaMbps, 2)},
                       {"salida_mbps", num(i.salidaMbps, 2)},
                       {"velocidad_mbps", num(i.velocidadMbps, 0)}});
    out["red"] = comun("red", {{"pct", num(m.redPct)},
                               {"entrada_mbps", num(m.redEntradaMbps, 2)},
                               {"salida_mbps", num(m.redSalidaMbps, 2)},
                               {"pico_entrada_mbps", num(d_->maximo(indiceDe("red_entrada"), todo), 2)},
                               {"pico_salida_mbps", num(d_->maximo(indiceDe("red_salida"), todo), 2)},
                               {"interfaces", ifs}});
    out["disco"] = {{"escritura_mb_s", num(m.discoEscrituraMBs, 2)}, {"pico", num(d_->maximo(indiceDe("disco"), todo), 2)}};
    out["motor"] = comun("motor", {{"fps", num(m.fps)}, {"perdidos_pct", num(m.perdidosPct, 2)}, {"salida_kbps", num(m.salidaKbps, 0)}});
    nlohmann::json procs = nlohmann::json::array();
    for (const Proceso& p : m.procesos)
        procs.push_back({{"nombre", p.nombre},
                         {"pid", p.pid},
                         {"cpu_pct", num(p.cpu)},
                         {"nucleos", num(p.nucleos, 2)},
                         {"rss_mb", num(p.rssMb, 0)}});
    out["procesos"] = procs;
    out["termica"] = {{"nivel", m.termica},
                      {"temperatura_c", num(m.temperaturaC, 1)},
                      {"limite_frecuencia_pct", num(m.limiteFrecuenciaPct, 0)},
                      {"fuente", m.termicaFuente}};

    // El peor estado y lo que menos margen tiene (lo que va a explotar primero).
    Estado peor = Estado::Desconocido;
    for (const std::string& k : claves()) peor = std::max(peor, estadoDe(k).estado);
    out["estado"] = nombre(peor);
    double margen = kD;
    std::string limita;
    for (const std::string& k : {std::string("cpu"), std::string("gpu"), std::string("ram"), std::string("vram"), std::string("red")}) {
        const double prom = d_->promedio(indiceDe(k), k10);
        if (prom < 0) continue;
        const double mg = std::max(0.0, 100.0 - prom);
        if (margen < 0 || mg < margen) {
            margen = mg;
            limita = k;
        }
    }
    out["margen"] = {{"pct", num(margen)}, {"limita", limita.empty() ? nlohmann::json(nullptr) : nlohmann::json(limita)}};

    if (historialS > 0 && todo > 0) {
        const std::size_t n = std::min<std::size_t>(todo, static_cast<std::size_t>(historialS));
        const std::size_t paso = static_cast<std::size_t>(std::max(1, pasoS));
        nlohmann::json filas = nlohmann::json::array();
        // Grupos de `paso` segundos desde el más nuevo; cada uno da su máximo (para
        // ver venir el límite importa el pico, no el promedio). Después, del más
        // viejo al más nuevo.
        for (std::size_t ini = 0; ini < n; ini += paso) {
            const std::size_t fin = std::min(n, ini + paso);
            std::array<double, kNSeries> mx;
            mx.fill(kD);
            for (std::size_t i = ini; i < fin; ++i)
                for (std::size_t s = 0; s < kNSeries; ++s) mx[s] = std::max<double>(mx[s], d_->punto(i).v[s]);
            nlohmann::json fila = nlohmann::json::array({(m.t - d_->punto(ini).t) / 1000});
            for (std::size_t s = 0; s < kNSeries; ++s) fila.push_back(num(mx[s], s == 5 || s == 6 || s == 8 ? 2 : 1));
            filas.push_back(std::move(fila));
        }
        std::reverse(filas.begin(), filas.end());
        nlohmann::json cols = nlohmann::json::array({"hace_s"});
        for (const std::string& s : kSeries()) cols.push_back(s);
        out["historial"] = {{"paso_s", paso}, {"columnas", cols}, {"filas", filas}};
    }
    return out;
}

std::uint32_t Recursos::pidPropio() {
#if defined(_WIN32)
    return GetCurrentProcessId();
#elif defined(__APPLE__) || defined(__linux__)
    return static_cast<std::uint32_t>(getpid());
#else
    return 0;
#endif
}

std::pair<std::uint32_t, std::string> Recursos::padre() {
#if defined(_WIN32)
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return {0, {}};
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    const DWORD yo = GetCurrentProcessId();
    DWORD ppid = 0;
    std::map<DWORD, std::string> nombres;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (pe.th32ProcessID == yo) ppid = pe.th32ParentProcessID;
        nombres[pe.th32ProcessID] = utf8(pe.szExeFile);
    }
    CloseHandle(snap);
    const auto it = nombres.find(ppid);
    return {ppid, it == nombres.end() ? std::string() : it->second};
#elif defined(__APPLE__)
    const pid_t pp = getppid();
    char nom[2 * MAXCOMLEN + 1] = {0};
    proc_name(pp, nom, sizeof(nom));
    return {static_cast<std::uint32_t>(pp), nom};
#elif defined(__linux__)
    const pid_t pp = getppid();
    std::string nom;
    std::ifstream("/proc/" + std::to_string(pp) + "/comm") >> nom; // truncado a 15: «BroadcastApplia»
    if (nom.rfind("BroadcastApplia", 0) == 0) nom = "BroadcastAppliance";
    return {static_cast<std::uint32_t>(pp), nom};
#else
    return {0, {}};
#endif
}

} // namespace ba::diag
