// ============================================================================
// src/core/diag/Caidas.cpp — ver Caidas.hpp
// ============================================================================
#include "diag/Caidas.hpp"

#if !defined(_WIN32)

#include <csignal>
#include <cstring>
#include <execinfo.h>
#include <initializer_list>
#include <unistd.h>

namespace ba::diag {
namespace {

const char* g_quien = "proceso";

/// `write` no falla parcialmente en la practica para estos tamaños, pero se
/// escribe en bucle igual: en un manejador de señal no hay segunda oportunidad.
void escribir(const char* s) {
    if (s == nullptr) return;
    std::size_t n = std::strlen(s);
    while (n > 0) {
        const ssize_t w = write(STDERR_FILENO, s, n);
        if (w <= 0) return;
        s += w;
        n -= static_cast<std::size_t>(w);
    }
}

const char* nombreDeSenal(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV (acceso a memoria invalida)";
        case SIGABRT: return "SIGABRT (abort: una comprobacion interna fallo)";
        case SIGBUS: return "SIGBUS (acceso mal alineado o archivo mapeado que se corto)";
        case SIGILL: return "SIGILL (instruccion invalida)";
        case SIGFPE: return "SIGFPE (division por cero u overflow)";
        default: return "señal desconocida";
    }
}

void manejar(int sig) {
    escribir("\n==== SE CAYO ");
    escribir(g_quien);
    escribir(": ");
    escribir(nombreDeSenal(sig));
    escribir(" ====\n");
    // Los nombres salen como los guarda el enlazador. Se dice como leerlos:
    // quien encuentre esto en el log a las ocho de la noche no tiene por que
    // acordarse de c++filt.
    escribir("(los nombres se leen pasando el log por: c++filt)\n");

    void* marcos[64];
    const int n = backtrace(marcos, 64);
    // `backtrace_symbols_fd` escribe sin asignar memoria, que es justo lo que
    // hace falta aca (`backtrace_symbols`, la otra, hace malloc y se cuelga).
    backtrace_symbols_fd(marcos, n, STDERR_FILENO);
    escribir("==== fin de la pila ====\n");

    // Se muere como se habria muerto: mismo codigo de salida, mismo
    // comportamiento del launcher. Esto solo deja la prueba.
    signal(sig, SIG_DFL);
    raise(sig);
}

} // namespace

void instalarManejadorDeCaidas(const char* quien) {
    if (quien != nullptr) g_quien = quien;

    // Se llama UNA VEZ ahora, con todo sano: la primera llamada a `backtrace`
    // carga libgcc y eso si asigna memoria. Haciendolo aca, el manejador ya la
    // encuentra cargada y no tiene que asignar nada.
    void* calentar[4];
    (void)backtrace(calentar, 4);

    for (const int sig : {SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE})
        signal(sig, manejar);
}

} // namespace ba::diag

#else // _WIN32

namespace ba::diag {
void instalarManejadorDeCaidas(const char*) {}
} // namespace ba::diag

#endif
