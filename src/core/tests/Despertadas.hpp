// Despertadas.hpp — cuántas veces el sistema sacó de dormir a este proceso (para los tests
// que miden sondeos: test_mirada, test_retorno_ritmo)
#pragma once

#if defined(_WIN32)
#else
#include <sys/resource.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <libproc.h>
#endif
#endif

namespace ba::pruebas {

/// Las despertadas del proceso hasta ahora: en macOS las que cuenta el sistema (las «idle wake
/// ups» del Monitor de Actividad: temporizadores que lo sacan de dormir); en Linux, los cambios
/// de contexto voluntarios (cada despertada de un sleep es uno). En Windows no se mide (0).
inline long despertadasDelProceso() {
#if defined(__APPLE__)
    rusage_info_v2 r{};
    if (proc_pid_rusage(getpid(), RUSAGE_INFO_V2, reinterpret_cast<rusage_info_t*>(&r)) != 0) return 0;
    return static_cast<long>(r.ri_pkg_idle_wkups + r.ri_interrupt_wkups);
#elif defined(_WIN32)
    return 0;
#else
    rusage r{};
    getrusage(RUSAGE_SELF, &r);
    return r.ru_nvcsw;
#endif
}

} // namespace ba::pruebas
