// ============================================================================
// src/motor/Memoria.cpp — ver Memoria.hpp
// ============================================================================
#include "Memoria.hpp"

#include <chrono>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <malloc.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <malloc/malloc.h>
#else
#include <malloc.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#endif

namespace ba::motor::memoria {

std::int64_t privada() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        return static_cast<std::int64_t>(pmc.PrivateUsage);
    return -1;
#elif defined(__APPLE__)
    task_vm_info_data_t info{};
    mach_msg_type_number_t n = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &n) == KERN_SUCCESS)
        return static_cast<std::int64_t>(info.phys_footprint);
    return -1;
#else
    FILE* f = std::fopen("/proc/self/status", "r");
    if (f == nullptr) return -1;
    char linea[256];
    std::int64_t kb = -1;
    while (std::fgets(linea, sizeof linea, f) != nullptr)
        if (std::strncmp(linea, "RssAnon:", 8) == 0) {
            kb = std::strtoll(linea + 8, nullptr, 10);
            break;
        }
    std::fclose(f);
    return kb < 0 ? -1 : kb * 1024;
#endif
}

Compactada compactar() {
    Compactada c;
    c.antes = privada();
    const auto t0 = std::chrono::steady_clock::now();
#if defined(_WIN32)
    // Todos los montones del proceso (el del CRT, el del proceso, los de las bibliotecas).
    std::vector<HANDLE> montones(64);
    DWORD n = GetProcessHeaps(static_cast<DWORD>(montones.size()), montones.data());
    if (n > montones.size()) {
        montones.resize(n);
        n = GetProcessHeaps(static_cast<DWORD>(montones.size()), montones.data());
    }
    for (DWORD i = 0; i < n && i < montones.size(); ++i) HeapCompact(montones[i], 0);
    _heapmin();
#elif defined(__APPLE__)
    malloc_zone_pressure_relief(nullptr, 0);
#else
    malloc_trim(0);
#endif
    c.us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
    c.despues = privada();
    return c;
}

} // namespace ba::motor::memoria
