// ============================================================================
// src/app/comun/UsoCpu.hpp — cuánto CPU usa el proceso (porcentaje de toda la máquina)
//
// Cada `medir()` da el uso desde la medición anterior: tiempo de CPU del
// proceso dividido el tiempo de reloj y la cantidad de núcleos.
// ============================================================================
#pragma once

#include <chrono>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/resource.h>
#endif

namespace ba::app {

class UsoCpu {
   public:
    UsoCpu() { medir(); }
    double medir() {
        const double cpu = cpuSegundos();
        const auto ahora = std::chrono::steady_clock::now();
        const double pared = std::chrono::duration<double>(ahora - ultimaPared_).count();
        double pct = 0;
        if (ultimoCpu_ >= 0 && pared > 0) {
            const unsigned n = std::thread::hardware_concurrency();
            pct = (cpu - ultimoCpu_) / pared / (n > 0 ? n : 1) * 100.0;
        }
        ultimoCpu_ = cpu;
        ultimaPared_ = ahora;
        return pct < 0 ? 0 : pct;
    }

   private:
    static double cpuSegundos() {
#ifdef _WIN32
        FILETIME c, e, k, u;
        if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return 0;
        auto aSeg = [](const FILETIME& f) {
            return static_cast<double>((static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime) / 1e7;
        };
        return aSeg(k) + aSeg(u);
#else
        rusage r{};
        getrusage(RUSAGE_SELF, &r);
        return static_cast<double>(r.ru_utime.tv_sec + r.ru_stime.tv_sec) +
               static_cast<double>(r.ru_utime.tv_usec + r.ru_stime.tv_usec) / 1e6;
#endif
    }
    double ultimoCpu_ = -1;
    std::chrono::steady_clock::time_point ultimaPared_ = std::chrono::steady_clock::now();
};

} // namespace ba::app
