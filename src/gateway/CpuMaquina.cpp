// src/gateway/CpuMaquina.cpp — la CPU de la máquina y del gateway (ver el .hpp)

#include "CpuMaquina.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/resource.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_host.h>
#else
#include <fstream>
#include <string>
#endif
#endif

namespace ba::gateway {

namespace {

/// Ocupado y total de la máquina (true si el sistema lo dijo).
bool leerMaquina(std::uint64_t& ocupado, std::uint64_t& total) {
#if defined(_WIN32)
    FILETIME ocioso, nucleo, usuario;
    if (!GetSystemTimes(&ocioso, &nucleo, &usuario)) return false;
    const auto u64 = [](const FILETIME& f) {
        return (static_cast<std::uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
    };
    total = u64(nucleo) + u64(usuario); // el tiempo de núcleo INCLUYE el ocioso
    ocupado = total - u64(ocioso);
    return true;
#elif defined(__APPLE__)
    host_cpu_load_info_data_t cpu{};
    mach_msg_type_number_t n = HOST_CPU_LOAD_INFO_COUNT;
    mach_port_t host = mach_host_self();
    const kern_return_t r = host_statistics(host, HOST_CPU_LOAD_INFO, reinterpret_cast<host_info_t>(&cpu), &n);
    mach_port_deallocate(mach_task_self(), host);
    if (r != KERN_SUCCESS) return false;
    ocupado = static_cast<std::uint64_t>(cpu.cpu_ticks[CPU_STATE_USER]) + cpu.cpu_ticks[CPU_STATE_SYSTEM] +
              cpu.cpu_ticks[CPU_STATE_NICE];
    total = ocupado + cpu.cpu_ticks[CPU_STATE_IDLE];
    return true;
#else
    std::ifstream f("/proc/stat");
    std::string cpu;
    std::uint64_t us = 0, ni = 0, sy = 0, id = 0, io = 0, irq = 0, sirq = 0, st = 0;
    if (!(f >> cpu >> us >> ni >> sy >> id >> io >> irq >> sirq) || cpu != "cpu") return false;
    f >> st; // steal (puede no estar)
    ocupado = us + ni + sy + irq + sirq + st;
    total = ocupado + id + io;
    return true;
#endif
}

/// CPU del proceso en segundos (usuario + sistema).
double cpuProceso() {
#if defined(_WIN32)
    FILETIME creacion, salida, nucleo, usuario;
    if (!GetProcessTimes(GetCurrentProcess(), &creacion, &salida, &nucleo, &usuario)) return -1;
    const auto u64 = [](const FILETIME& f) {
        return (static_cast<std::uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
    };
    return static_cast<double>(u64(nucleo) + u64(usuario)) / 1e7;
#else
    rusage ru{};
    if (getrusage(RUSAGE_SELF, &ru) != 0) return -1;
    return static_cast<double>(ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) +
           static_cast<double>(ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1e6;
#endif
}

} // namespace

CpuMaquina::Lectura CpuMaquina::medir() {
    Lectura l;
    std::uint64_t ocupado = 0, total = 0;
    const bool ok = leerMaquina(ocupado, total);
    const double proceso = cpuProceso();
    const double pared =
        std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (hay_) {
        if (ok && total > total_ && ocupado >= ocupado_)
            l.maquinaPct = std::clamp(100.0 * static_cast<double>(ocupado - ocupado_) / static_cast<double>(total - total_), 0.0, 100.0);
        const double seg = pared - paredS_;
        const unsigned nucleos = std::max(1u, std::thread::hardware_concurrency());
        if (proceso >= 0 && procesoS_ >= 0 && seg > 0)
            l.procesoPct = std::clamp(100.0 * (proceso - procesoS_) / (seg * nucleos), 0.0, 100.0);
    }
    hay_ = true;
    if (ok) {
        ocupado_ = ocupado;
        total_ = total;
    }
    procesoS_ = proceso;
    paredS_ = pared;
    return l;
}

} // namespace ba::gateway
