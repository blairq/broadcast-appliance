// ============================================================================
// src/core/diag/Caidas.cpp — ver Caidas.hpp
// ============================================================================
#include "diag/Caidas.hpp"
#include "diag/SalidaAsincrona.hpp"

#if !defined(_WIN32)

#include <csignal>
#include <cstring>
#include <execinfo.h>
#include <initializer_list>
#include <unistd.h>

namespace ba::diag {
namespace {

const char* g_quien = "proceso";
int g_fd = STDERR_FILENO; // el archivo de Logs/ (no la cola: el proceso muere antes)

/// `write` no falla parcialmente en la practica para estos tamaños, pero se
/// escribe en bucle igual: en un manejador de señal no hay segunda oportunidad.
void escribir(const char* s) {
    if (s == nullptr) return;
    std::size_t n = std::strlen(s);
    while (n > 0) {
        const ssize_t w = write(g_fd, s, n);
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
    backtrace_symbols_fd(marcos, n, g_fd);
    escribir("==== fin de la pila ====\n");

    // Se muere como se habria muerto: mismo codigo de salida, mismo
    // comportamiento del launcher. Esto solo deja la prueba.
    signal(sig, SIG_DFL);
    raise(sig);
}

} // namespace

void instalarManejadorDeCaidas(const char* quien) {
    if (quien != nullptr) g_quien = quien;
    g_fd = static_cast<int>(salidaOriginal());

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

#include "diag/CaidaCompartida.hpp"
#include "platform/Paths.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdlib.h>

namespace ba::diag {
namespace {

// Todo lo que el manejador usa se prepara al instalar: cayéndose no se pide
// memoria ni se carga nada (con el heap roto, eso puede no volver).
const char* g_quien = "proceso";
caida::Pedido* g_pedido = nullptr;
HANDLE g_pide = nullptr, g_hecho = nullptr;
wchar_t g_dirVolcados[MAX_PATH] = {};
using MiniDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                 PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
MiniDumpFn g_volcar = nullptr;
std::atomic<DWORD> g_hiloCayendo{0};

void escribir(const char* s) {
    DWORD w = 0;
    WriteFile(reinterpret_cast<HANDLE>(salidaOriginal()), s, static_cast<DWORD>(std::strlen(s)), &w, nullptr);
}

void moduloDe(const void* dir, char* nombre, std::size_t n, unsigned long long* desp) {
    HMODULE m = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(dir), &m)) {
        char ruta[MAX_PATH] = {};
        GetModuleFileNameA(m, ruta, MAX_PATH);
        const char* b = std::strrchr(ruta, '\\');
        strncpy_s(nombre, n, b != nullptr ? b + 1 : ruta, _TRUNCATE);
        *desp = reinterpret_cast<std::uintptr_t>(dir) - reinterpret_cast<std::uintptr_t>(m);
    } else {
        strncpy_s(nombre, n, "?", _TRUNCATE);
        *desp = reinterpret_cast<std::uintptr_t>(dir);
    }
}

void volcarmeSolo(EXCEPTION_POINTERS* ep) {
    if (g_volcar == nullptr || g_dirVolcados[0] == 0) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t ruta[MAX_PATH + 96];
    swprintf_s(ruta, L"%s\\%S-%04u%02u%02u-%02u%02u%02u-caida.dmp", g_dirVolcados, g_quien, t.wYear, t.wMonth, t.wDay,
               t.wHour, t.wMinute, t.wSecond);
    HANDLE f = CreateFileW(ruta, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), ep, FALSE};
    const BOOL ok = g_volcar(GetCurrentProcess(), GetCurrentProcessId(), f, static_cast<MINIDUMP_TYPE>(caida::kTipoVolcado),
                             &mei, nullptr, nullptr);
    CloseHandle(f);
    escribir(ok ? "(volcado propio en Logs/dumps)\n" : "(no se pudo escribir el volcado)\n");
}

void reportar(EXCEPTION_POINTERS* ep, const char* tipo) {
    const DWORD yo = GetCurrentThreadId();
    DWORD nadie = 0;
    if (!g_hiloCayendo.compare_exchange_strong(nadie, yo)) {
        if (nadie == yo) return; // el mismo hilo otra vez (abort después de reportar): ya está
        Sleep(60000);            // otro hilo ya lo está reportando: que termine él
        return;
    }
    const DWORD codigo = ep->ExceptionRecord->ExceptionCode;
    void* dir = ep->ExceptionRecord->ExceptionAddress;
    char mod[160];
    unsigned long long desp = 0;
    moduloDe(dir, mod, sizeof mod, &desp);
    char linea[640];
    std::snprintf(linea, sizeof linea, "\n==== SE CAYO %s: %s (%s, 0x%08lX) en %s+0x%llX, hilo %lu ====\n", g_quien, tipo,
                  caida::nombreCodigo(codigo), codigo, mod, desp, yo);
    escribir(linea);
    bool hecho = false;
    if (g_pedido != nullptr && g_pide != nullptr && g_hecho != nullptr) {
        g_pedido->hilo = yo;
        g_pedido->codigo = codigo;
        g_pedido->punteros = reinterpret_cast<std::uint64_t>(ep);
        g_pedido->direccion = reinterpret_cast<std::uint64_t>(dir);
        strncpy_s(g_pedido->tipo, sizeof g_pedido->tipo, tipo, _TRUNCATE);
        g_pedido->version = caida::kVersion;
        MemoryBarrier();
        g_pedido->magic = caida::kMagic;
        SetEvent(g_pide);
        hecho = WaitForSingleObject(g_hecho, 60000) == WAIT_OBJECT_0;
        escribir(hecho ? "(el launcher escribió el volcado en Logs/dumps)\n" : "(el launcher no respondió: se vuelca solo)\n");
    }
    if (!hecho) volcarmeSolo(ep);
}

void reportarAca(const char* tipo) {
    CONTEXT ctx{};
    RtlCaptureContext(&ctx);
    EXCEPTION_RECORD er{};
    er.ExceptionCode = caida::kCodigoAbort;
    er.ExceptionAddress = reinterpret_cast<PVOID>(ctx.Rip);
    EXCEPTION_POINTERS ep{&er, &ctx};
    reportar(&ep, tipo);
}

// La corrupción del heap no llega al filtro de «sin atrapar» (Windows la
// manda directo a WER): se la ve antes, como excepción vectorizada.
LONG WINAPI vectorizada(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode == 0xC0000374) reportar(ep, "corrupción del heap");
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI sinAtrapar(EXCEPTION_POINTERS* ep) {
    reportar(ep, "excepción sin atrapar");
    return EXCEPTION_CONTINUE_SEARCH; // y sigue como siempre (WER, código de salida)
}

void alAbortar(int) { reportarAca("abort"); }

void llamadaPura() {
    reportarAca("llamada a una función virtual pura");
    std::abort();
}

void parametroInvalido(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, std::uintptr_t) {
    reportarAca("parámetro inválido a la biblioteca de C");
    std::abort();
}

} // namespace

void instalarManejadorDeCaidas(const char* quien) {
    static bool instalado = false;
    if (instalado) return;
    instalado = true;
    if (quien != nullptr) g_quien = quien;

    // El canal que dejó el launcher (si no hay launcher, no está).
    wchar_t canal[256] = {};
    wchar_t var[64] = {};
    MultiByteToWideChar(CP_UTF8, 0, caida::kVariable, -1, var, 64);
    if (GetEnvironmentVariableW(var, canal, 256) > 0) {
        if (HANDLE m = OpenFileMappingW(FILE_MAP_WRITE, FALSE, canal)) {
            g_pedido = static_cast<caida::Pedido*>(MapViewOfFile(m, FILE_MAP_WRITE, 0, 0, sizeof(caida::Pedido)));
            g_pide = OpenEventW(EVENT_MODIFY_STATE, FALSE, caida::nombrePide(canal).c_str());
            g_hecho = OpenEventW(SYNCHRONIZE, FALSE, caida::nombreHecho(canal).c_str());
        }
        // Que no lo hereden los procesos que lance la app (plugins, herramientas).
        SetEnvironmentVariableW(var, nullptr);
    }
    // Para volcarse solo: dbghelp (del sistema) cargada ya, y la carpeta.
    if (HMODULE d = LoadLibraryW(L"dbghelp.dll"))
        g_volcar = reinterpret_cast<MiniDumpFn>(reinterpret_cast<void*>(GetProcAddress(d, "MiniDumpWriteDump")));
    std::error_code ec;
    const std::filesystem::path dir = ba::platform::dataRoot() / "Logs" / "dumps";
    std::filesystem::create_directories(dir, ec);
    wcsncpy_s(g_dirVolcados, dir.wstring().c_str(), _TRUNCATE);

    AddVectoredExceptionHandler(1, vectorizada);
    SetUnhandledExceptionFilter(sinAtrapar);
    signal(SIGABRT, alAbortar);
    _set_abort_behavior(0, _WRITE_ABORT_MSG); // sin el cuadro de diálogo; el informe de fallas sigue
    _set_purecall_handler(llamadaPura);
    _set_invalid_parameter_handler(parametroInvalido);
}

} // namespace ba::diag

#endif
