// src/app/sala/MedidaMaquina.cpp — cuánto aprieta la máquina (ver el .hpp)

#include "sala/MedidaMaquina.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#elif defined(__APPLE__)
#include <libproc.h>
#include <mach/mach.h>
#include <stdlib.h>
#include <unistd.h>
#else
#include <stdlib.h>
#include <fstream>
#include <sstream>
#include <string>
#endif

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

} // namespace ba::app::sala::prueba
