// src/core/diag/SalidaAsincrona.cpp — ver SalidaAsincrona.hpp
#include "diag/SalidaAsincrona.hpp"
#include "i18n/I18n.hpp"
#include "platform/Hilo.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ba::diag {

namespace {

std::intptr_t g_original = -1; // ver salidaOriginal()

struct Cola {
    std::mutex mu;
    std::condition_variable hay, vacia;
    std::deque<std::string> trozos;
    std::size_t bytes = 0, max = 0, perdidos = 0, perdidosTotal = 0, escritos = 0;
    bool escribiendo = false;
    bool activa = false;
    bool retenida = false;
    std::chrono::steady_clock::time_point desde; // de la escritura en curso
};

Cola& cola() {
    static Cola* c = new Cola(); // vive lo que el proceso (los hilos no se detienen)
    return *c;
}

void encolar(const char* d, std::size_t n) {
    Cola& c = cola();
    {
        std::lock_guard<std::mutex> l(c.mu);
        if (c.bytes + n > c.max) {
            c.perdidos += n; // el disco no da abasto: se pierde, pero nadie se traba
            c.perdidosTotal += n;
            return;
        }
        c.trozos.emplace_back(d, n);
        c.bytes += n;
    }
    c.hay.notify_one();
}

/// El escritor: de la cola al archivo, con `escribir` (puede trabarse: es su hilo).
template <class Escribir>
void escritor(Escribir escribir) {
    Cola& c = cola();
    for (;;) {
        std::string t;
        std::size_t perdidos = 0;
        {
            std::unique_lock<std::mutex> l(c.mu);
            c.hay.wait(l, [&] { return !c.retenida && (!c.trozos.empty() || c.perdidos > 0); });
            perdidos = c.perdidos;
            c.perdidos = 0;
            if (!c.trozos.empty()) {
                t = std::move(c.trozos.front());
                c.trozos.pop_front();
                c.bytes -= t.size();
            }
            c.escribiendo = true;
            c.desde = std::chrono::steady_clock::now();
        }
        if (perdidos > 0) {
            const std::string aviso =
                "\n[" +
                i18n::llenar(i18n::traducir("registro", "registro: el disco no daba abasto; se perdieron %1 bytes de salida"),
                             {std::to_string(perdidos)}) +
                "]\n";
            escribir(aviso.data(), aviso.size());
        }
        if (!t.empty()) escribir(t.data(), t.size());
        {
            std::lock_guard<std::mutex> l(c.mu);
            c.escribiendo = false;
            c.escritos += t.size();
            if (c.trozos.empty()) c.vacia.notify_all();
        }
    }
}

} // namespace

void vaciarSalidaEstandar(int ms) {
    Cola& c = cola();
    std::fflush(stdout);
    std::fflush(stderr);
    if (!c.activa || c.retenida) return;
    // Lo que todavía está en la tubería lo pasa el lector en un momento.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::unique_lock<std::mutex> l(c.mu);
    c.vacia.wait_for(l, std::chrono::milliseconds(ms), [&] { return c.trozos.empty() && !c.escribiendo; });
}

void retenerSalidaEstandar(bool si) {
    Cola& c = cola();
    {
        std::lock_guard<std::mutex> l(c.mu);
        if (c.retenida == si) return;
        c.retenida = si;
    }
    c.hay.notify_all();
}

EstadoSalida estadoSalidaEstandar() {
    Cola& c = cola();
    std::lock_guard<std::mutex> l(c.mu);
    EstadoSalida e;
    e.activa = c.activa;
    e.retenida = c.retenida;
    e.enCola = c.bytes;
    e.perdidos = c.perdidosTotal;
    e.escritos = c.escritos;
    if (c.escribiendo)
        e.escribiendoMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - c.desde).count();
    return e;
}

std::intptr_t salidaOriginal() {
    if (g_original != -1) return g_original;
#if defined(_WIN32)
    return reinterpret_cast<std::intptr_t>(GetStdHandle(STD_ERROR_HANDLE));
#else
    return 2;
#endif
}

#if defined(_WIN32)

void desacoplarSalidaEstandar(std::size_t maxBytes) {
    Cola& c = cola();
    if (c.activa) return;
    HANDLE original = GetStdHandle(STD_ERROR_HANDLE);
    if (original == nullptr || original == INVALID_HANDLE_VALUE || GetFileType(original) != FILE_TYPE_DISK) return;
    HANDLE destino = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), original, GetCurrentProcess(), &destino, 0, FALSE, DUPLICATE_SAME_ACCESS)) return;
    HANDLE leer = nullptr, escribir = nullptr;
    if (!CreatePipe(&leer, &escribir, nullptr, 1u << 20)) {
        CloseHandle(destino);
        return;
    }
    std::fflush(stdout);
    std::fflush(stderr);
    const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(escribir), _O_WRONLY | _O_BINARY);
    if (fd < 0 || _dup2(fd, 1) != 0 || _dup2(fd, 2) != 0) {
        CloseHandle(leer);
        CloseHandle(destino);
        return;
    }
    SetStdHandle(STD_OUTPUT_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(1)));
    SetStdHandle(STD_ERROR_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(2)));
    c.max = maxBytes;
    c.activa = true;
    g_original = reinterpret_cast<std::intptr_t>(destino);
    std::thread([leer]() {
        ba::platform::nombrarHilo("salida-leer");
        char buf[16384];
        DWORD n = 0;
        while (ReadFile(leer, buf, sizeof(buf), &n, nullptr) && n > 0) encolar(buf, n);
    }).detach();
    std::thread([destino]() {
        ba::platform::nombrarHilo("salida-escribir");
        escritor([destino](const char* d, std::size_t n) {
            DWORD w = 0;
            WriteFile(destino, d, static_cast<DWORD>(n), &w, nullptr);
        });
    }).detach();
}

#else

void desacoplarSalidaEstandar(std::size_t maxBytes) {
    Cola& c = cola();
    if (c.activa) return;
    struct stat st {};
    if (fstat(2, &st) != 0 || !S_ISREG(st.st_mode)) return; // una consola o una tubería: como está
    const int destino = dup(2);
    int p[2];
    if (destino < 0 || pipe(p) != 0) {
        if (destino >= 0) close(destino);
        return;
    }
#if defined(F_SETPIPE_SZ)
    fcntl(p[1], F_SETPIPE_SZ, 1 << 20);
#endif
    std::fflush(stdout);
    std::fflush(stderr);
    if (dup2(p[1], 1) < 0 || dup2(p[1], 2) < 0) {
        close(p[0]);
        close(p[1]);
        close(destino);
        return;
    }
    close(p[1]);
    fcntl(p[0], F_SETFD, FD_CLOEXEC);
    fcntl(destino, F_SETFD, FD_CLOEXEC);
    c.max = maxBytes;
    c.activa = true;
    g_original = destino;
    const int leer = p[0];
    std::thread([leer]() {
        ba::platform::nombrarHilo("salida-leer");
        char buf[16384];
        for (;;) {
            const ssize_t n = read(leer, buf, sizeof(buf));
            if (n <= 0) break;
            encolar(buf, static_cast<std::size_t>(n));
        }
    }).detach();
    std::thread([destino]() {
        ba::platform::nombrarHilo("salida-escribir");
        escritor([destino](const char* d, std::size_t n) {
            while (n > 0) {
                const ssize_t w = write(destino, d, n);
                if (w <= 0) return;
                d += w;
                n -= static_cast<std::size_t>(w);
            }
        });
    }).detach();
}

#endif

} // namespace ba::diag
