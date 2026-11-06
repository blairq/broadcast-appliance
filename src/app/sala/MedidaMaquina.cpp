// src/app/sala/MedidaMaquina.cpp — cuánto aprieta la máquina (ver el .hpp)

#include "sala/MedidaMaquina.hpp"

#include <QNetworkInterface>
#include <QSysInfo>

#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#elif defined(__APPLE__)
#include <libproc.h>
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <stdlib.h>
#include <unistd.h>
#else
#include <dirent.h>
#include <stdlib.h>
#include <fstream>
#include <sstream>
#include <string>
#endif

#include <algorithm>

namespace ba::app::sala::prueba {

Medida medirMaquina() {
    Medida m;
#if defined(_WIN32)
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) m.memLibreMb = static_cast<double>(ms.ullAvailPhys) / (1024.0 * 1024.0);
    // Los hilos de este proceso: la foto de hilos del sistema (una vez por segundo como mucho).
    const HANDLE foto = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (foto != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te{};
        te.dwSize = sizeof(te);
        int n = 0;
        const DWORD yo = GetCurrentProcessId();
        if (Thread32First(foto, &te)) {
            do {
                if (te.th32OwnerProcessID == yo) ++n;
            } while (Thread32Next(foto, &te));
        }
        CloseHandle(foto);
        m.hilosApp = n;
    }
#elif defined(__APPLE__)
    vm_statistics64_data_t vm{};
    mach_msg_type_number_t n = HOST_VM_INFO64_COUNT;
    const mach_port_t host = mach_host_self();
    vm_size_t pagina = 0;
    if (host_page_size(host, &pagina) == KERN_SUCCESS &&
        host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &n) == KERN_SUCCESS) {
        const double pags = static_cast<double>(vm.free_count) + vm.inactive_count + vm.speculative_count + vm.purgeable_count;
        m.memLibreMb = pags * static_cast<double>(pagina) / (1024.0 * 1024.0);
    }
    mach_port_deallocate(mach_task_self(), host);
    double carga[1] = {0};
    if (getloadavg(carga, 1) == 1) m.carga = carga[0];
    proc_taskinfo ti{};
    if (proc_pidinfo(getpid(), PROC_PIDTASKINFO, 0, &ti, sizeof(ti)) == static_cast<int>(sizeof(ti)))
        m.hilosApp = ti.pti_threadnum;
#else
    {
        // Línea por línea: algunas no traen unidad (HugePages_Total: 0).
        std::ifstream f("/proc/meminfo");
        std::string linea;
        while (std::getline(f, linea))
            if (linea.rfind("MemAvailable:", 0) == 0) {
                m.memLibreMb = static_cast<double>(std::atoll(linea.c_str() + 13)) / 1024.0;
                break;
            }
    }
    double carga[1] = {0};
    if (getloadavg(carga, 1) == 1) m.carga = carga[0];
    {
        std::ifstream f("/proc/self/status");
        std::string linea;
        while (std::getline(f, linea))
            if (linea.rfind("Threads:", 0) == 0) {
                m.hilosApp = std::atoi(linea.c_str() + 8);
                break;
            }
    }
#endif
    return m;
}

std::vector<std::uint32_t> conHijos(const std::vector<std::uint32_t>& pids) {
    std::vector<std::uint32_t> out = pids;
    if (pids.empty()) return out;
    const auto esPadre = [&pids](std::uint32_t pp) { return std::find(pids.begin(), pids.end(), pp) != pids.end(); };
#if defined(_WIN32)
    const HANDLE foto = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (foto == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    for (BOOL ok = Process32FirstW(foto, &pe); ok; ok = Process32NextW(foto, &pe))
        if (esPadre(pe.th32ParentProcessID)) out.push_back(pe.th32ProcessID);
    CloseHandle(foto);
#elif defined(__APPLE__)
    (void)esPadre;
    for (const std::uint32_t p : pids) {
        pid_t hijos[256] = {};
        const int n = proc_listchildpids(static_cast<pid_t>(p), hijos, static_cast<int>(sizeof(hijos)));
        // Devuelve cuántos pids (en versiones viejas, bytes: lo de más queda en cero y no cuenta).
        const int k = std::clamp(n, 0, 256);
        for (int i = 0; i < k; ++i)
            if (hijos[i] > 0) out.push_back(static_cast<std::uint32_t>(hijos[i]));
    }
#else
    // /proc/<pid>/stat: el cuarto campo es el padre (después del nombre entre paréntesis).
    if (DIR* d = opendir("/proc")) {
        while (const dirent* e = readdir(d)) {
            const long pid = std::atol(e->d_name);
            if (pid <= 0) continue;
            std::ifstream f(std::string("/proc/") + e->d_name + "/stat");
            std::string linea;
            if (!std::getline(f, linea)) continue;
            const std::size_t cierra = linea.rfind(')');
            if (cierra == std::string::npos) continue;
            std::istringstream resto(linea.substr(cierra + 1));
            std::string estado;
            long padre = 0;
            if (resto >> estado >> padre && padre > 0 && esPadre(static_cast<std::uint32_t>(padre)))
                out.push_back(static_cast<std::uint32_t>(pid));
        }
        closedir(d);
    }
#endif
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

namespace {
#if defined(__APPLE__)
int sysctlEntero(const char* nombre) {
    int v = 0;
    size_t largo = sizeof(v);
    return sysctlbyname(nombre, &v, &largo, nullptr, 0) == 0 ? v : -1;
}
#endif

std::string modeloCpu() {
#if defined(_WIN32)
    wchar_t buf[256] = {};
    DWORD largo = sizeof(buf);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString",
                     RRF_RT_REG_SZ, nullptr, buf, &largo) == ERROR_SUCCESS)
        return QString::fromWCharArray(buf).trimmed().toStdString();
#elif defined(__APPLE__)
    char buf[256] = {};
    size_t largo = sizeof(buf);
    if (sysctlbyname("machdep.cpu.brand_string", buf, &largo, nullptr, 0) == 0) return buf;
#else
    std::ifstream f("/proc/cpuinfo");
    std::string linea;
    while (std::getline(f, linea))
        if (linea.rfind("model name", 0) == 0) {
            const std::size_t dos = linea.find(':');
            if (dos != std::string::npos) return QString::fromStdString(linea.substr(dos + 1)).trimmed().toStdString();
        }
#endif
    return QSysInfo::currentCpuArchitecture().toStdString();
}

int nucleosFisicos() {
#if defined(_WIN32)
    DWORD largo = 0;
    GetLogicalProcessorInformation(nullptr, &largo);
    if (largo == 0) return -1;
    std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> info(largo / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
    if (!GetLogicalProcessorInformation(info.data(), &largo)) return -1;
    int n = 0;
    for (const auto& i : info)
        if (i.Relationship == RelationProcessorCore) ++n;
    return n;
#elif defined(__APPLE__)
    return sysctlEntero("hw.physicalcpu");
#else
    // «cpu cores» de /proc/cpuinfo por zócalo (una máquina de un zócalo, lo común).
    std::ifstream f("/proc/cpuinfo");
    std::string linea;
    while (std::getline(f, linea))
        if (linea.rfind("cpu cores", 0) == 0) {
            const std::size_t dos = linea.find(':');
            if (dos != std::string::npos) return std::atoi(linea.c_str() + dos + 1);
        }
    return -1;
#endif
}
} // namespace

nlohmann::json fichaMaquina(const ba::diag::Recursos::Muestra& r) {
    using json = nlohmann::json;
    json cpu{{"modelo", modeloCpu()},
             {"nucleosLogicos", static_cast<int>(std::thread::hardware_concurrency())},
             {"nucleosFisicos", nucleosFisicos()}};
#if defined(__APPLE__)
    // Apple Silicon: los de rendimiento y los de eficiencia.
    if (const int p = sysctlEntero("hw.perflevel0.physicalcpu"); p > 0) cpu["rendimiento"] = p;
    if (const int e = sysctlEntero("hw.perflevel1.physicalcpu"); e > 0) cpu["eficiencia"] = e;
#endif
    json red = json::array();
    for (const QNetworkInterface& i : QNetworkInterface::allInterfaces()) {
        const auto f = i.flags();
        if (!f.testFlag(QNetworkInterface::IsUp) || !f.testFlag(QNetworkInterface::IsRunning) || f.testFlag(QNetworkInterface::IsLoopBack))
            continue;
        bool conIpv4 = false;
        for (const QNetworkAddressEntry& a : i.addressEntries())
            if (a.ip().protocol() == QAbstractSocket::IPv4Protocol && !a.ip().isLinkLocal()) conIpv4 = true;
        if (!conIpv4) continue;
        const auto t = i.type();
        const char* tipo = t == QNetworkInterface::Wifi ? "wifi" : t == QNetworkInterface::Ethernet ? "cable" : "otro";
        double velocidad = -1;
        for (const auto& x : r.interfaces)
            if (x.nombre == i.name().toStdString()) velocidad = x.velocidadMbps;
        red.push_back({{"nombre", i.name().toStdString()},
                       {"descripcion", i.humanReadableName().toStdString()},
                       {"tipo", tipo},
                       {"velocidadMbps", velocidad}});
        if (red.size() >= 4) break;
    }
    return json{{"nombre", QSysInfo::machineHostName().toStdString()},
                {"sistema", QSysInfo::prettyProductName().toStdString()},
                {"arquitectura", QSysInfo::currentCpuArchitecture().toStdString()},
                {"cpu", cpu},
                {"memoriaMb", r.ramTotalMb},
                {"gpu", {{"nombre", r.gpuNombre}, {"memoriaUnificada", r.memoriaUnificada}}},
                {"red", red}};
}

} // namespace ba::app::sala::prueba
