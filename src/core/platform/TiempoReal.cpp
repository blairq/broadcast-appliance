// src/core/platform/TiempoReal.cpp — ver TiempoReal.hpp
//
// Un solo archivo para los tres sistemas, como Hilo.cpp: es una llamada por
// sistema y lo que hay alrededor (contar, recordar qué se hizo en este hilo)
// es lo mismo.

#include "platform/TiempoReal.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#else
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace ba::platform {

namespace {

std::atomic<int> g_hilos{0}, g_fallidos{0};
std::atomic<const char*> g_modo{""};

/// Lo que se le hizo a ESTE hilo (para deshacerlo al soltarlo).
struct EnEsteHilo {
    bool subido = false;
#if defined(_WIN32)
    HANDLE mmcss = nullptr;
#elif !defined(__APPLE__)
    bool fifo = false;
#endif
    // Un hilo que termina subido (un std::thread propio que no lo soltó) deja de contar.
    ~EnEsteHilo() {
        if (subido) g_hilos.fetch_sub(1);
    }
};
thread_local EnEsteHilo t_hilo;

#if defined(_WIN32)
// avrt.dll se busca al llamar (es de Windows desde Vista, pero así no se enlaza
// y un Windows recortado sin el servicio MMCSS sigue andando).
using PonerMm = HANDLE(WINAPI*)(LPCWSTR, LPDWORD);
using PrioridadMm = BOOL(WINAPI*)(HANDLE, int);
using RevertirMm = BOOL(WINAPI*)(HANDLE);
struct Avrt {
    PonerMm poner = nullptr;
    PrioridadMm prioridad = nullptr;
    RevertirMm revertir = nullptr;
};
const Avrt& avrt() {
    static const Avrt a = [] {
        Avrt x;
        if (HMODULE m = LoadLibraryW(L"avrt.dll")) {
            x.poner = reinterpret_cast<PonerMm>(reinterpret_cast<void*>(GetProcAddress(m, "AvSetMmThreadCharacteristicsW")));
            x.prioridad = reinterpret_cast<PrioridadMm>(reinterpret_cast<void*>(GetProcAddress(m, "AvSetMmThreadPriority")));
            x.revertir = reinterpret_cast<RevertirMm>(reinterpret_cast<void*>(GetProcAddress(m, "AvRevertMmThreadCharacteristics")));
        }
        return x;
    }();
    return a;
}
constexpr int kAvrtPrioridadAlta = 1; // AVRT_PRIORITY_HIGH (avrt.h)
#elif defined(__APPLE__)
std::uint32_t msATics(double ms) {
    static const mach_timebase_info_data_t base = [] {
        mach_timebase_info_data_t b{};
        mach_timebase_info(&b);
        return b;
    }();
    return static_cast<std::uint32_t>(ms * 1e6 * base.denom / base.numer);
}
#endif

} // namespace

bool tiempoRealAudio(int periodoMs) {
    if (t_hilo.subido) return true;
    periodoMs = std::clamp(periodoMs, 1, 100);
    const char* modo = nullptr;
#if defined(_WIN32)
    const Avrt& a = avrt();
    if (a.poner != nullptr) {
        DWORD tarea = 0;
        if (HANDLE h = a.poner(L"Pro Audio", &tarea)) {
            if (a.prioridad != nullptr) a.prioridad(h, kAvrtPrioridadAlta);
            t_hilo.mmcss = h;
            modo = "mmcss";
        }
    }
#elif defined(__APPLE__)
    // El período es el bloque; el cómputo, lo que de verdad tarda un bloque de
    // mezcla con margen (2 ms de 10); el plazo, la mitad del período. Con
    // preemptible el kernel puede cortarlo si se pasa (y lo baja si abusa).
    thread_time_constraint_policy_data_t p{};
    p.period = msATics(periodoMs);
    p.computation = msATics(std::max(0.5, periodoMs * 0.2));
    p.constraint = msATics(periodoMs * 0.5);
    p.preemptible = TRUE;
    if (thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
                          reinterpret_cast<thread_policy_t>(&p), THREAD_TIME_CONSTRAINT_POLICY_COUNT) == KERN_SUCCESS)
        modo = "time-constraint";
#else
    // SCHED_FIFO solo si el límite lo deja (limits.conf, el grupo «audio»):
    // sin permisos el pedido falla igual y no hay nada que pedirle a nadie.
    rlimit rt{};
    if (getrlimit(RLIMIT_RTPRIO, &rt) == 0 && rt.rlim_cur > 0) {
        sched_param sp{};
        const rlim_t tope = rt.rlim_cur == RLIM_INFINITY ? 20 : std::min<rlim_t>(rt.rlim_cur, 20);
        sp.sched_priority = static_cast<int>(tope);
        if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) == 0) {
            t_hilo.fifo = true;
            modo = "sched-fifo";
        }
    }
    if (modo == nullptr && setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), -10) == 0) modo = "nice";
#endif
    if (modo == nullptr) {
        g_fallidos.fetch_add(1);
        return false;
    }
    t_hilo.subido = true;
    g_hilos.fetch_add(1);
    g_modo.store(modo);
    return true;
}

void soltarTiempoReal() {
    if (!t_hilo.subido) return;
#if defined(_WIN32)
    if (t_hilo.mmcss != nullptr && avrt().revertir != nullptr) avrt().revertir(t_hilo.mmcss);
    t_hilo.mmcss = nullptr;
#elif defined(__APPLE__)
    thread_standard_policy_data_t p{};
    thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_STANDARD_POLICY, reinterpret_cast<thread_policy_t>(&p),
                      THREAD_STANDARD_POLICY_COUNT);
#else
    if (t_hilo.fifo) {
        sched_param sp{};
        pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp);
        t_hilo.fifo = false;
    } else {
        setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 0);
    }
#endif
    t_hilo.subido = false;
    g_hilos.fetch_sub(1);
}

EstadoTiempoReal estadoTiempoReal() {
    EstadoTiempoReal e;
    e.hilos = g_hilos.load();
    e.fallidos = g_fallidos.load();
    e.modo = g_modo.load();
    return e;
}

} // namespace ba::platform
