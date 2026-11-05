// Puntualidad.hpp — ¿la máquina deja correr a tiempo a este proceso? (para los tests de tiempo
// real: test_lazo_latencia, test_lazo_invitados, test_motor_app, test_composicion…)
//
// Un test de tiempo real mide milisegundos de verdad: con la máquina cargada (otros compilando,
// otra suite en paralelo) un hilo que tenía que despertar a los 10 ms despierta a los 30 y el test
// falla sin que el programa tenga nada mal. Eso no es un rojo: es una corrida que no sirve. Acá se
// mide lo que de verdad importa —cuánto tarde despierta el sistema a un hilo que duerme 2 ms— y,
// si el test falló con la máquina impuntual, termina en SKIP (77) con el motivo; con la máquina en
// hora, un fallo es un fallo.
//
//   ba::pruebas::SondaPuntualidad sonda;      // al empezar: mide mientras corre el test
//   ...
//   return ba::pruebas::resultadoTiempoReal("test_x", fallos, &sonda);
//
// BA_TEST_TIEMPO_REAL_ESTRICTO=1: nunca SKIP (para confirmar un fallo con la máquina sola).
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <time.h>   // clock_gettime
#include <unistd.h> // sysconf; getloadavg está en <cstdlib>
#endif

namespace ba::pruebas {

/// Cuánto tarde despierta el sistema a un hilo que duerme 2 ms con plazo absoluto.
struct Puntualidad {
    int muestras = 0;
    double mediaMs = 0, p99Ms = 0, maxMs = 0;
    double cargaPorNucleo = -1; ///< carga media de 1 min por núcleo (-1: el sistema no la da)

    /// Impuntual: así no se pueden medir milisegundos. Despierta en hora, en reposo, con
    /// décimas de ms (media ~0,05 ms, p99 < 0,5 ms en la Mac y en Linux); compilando con todos
    /// los núcleos, la media pasa del ms y el p99 de varios ms.
    bool impuntual() const {
        return (muestras > 0 && (mediaMs > 0.5 || p99Ms > 3.0)) || cargaPorNucleo > 1.25;
    }
    std::string texto() const {
        char b[200];
        std::snprintf(b, sizeof b, "despierta tarde %.2f ms de media, p99 %.2f ms, máx %.2f ms (%d muestras); carga %s",
                      mediaMs, p99Ms, maxMs, muestras,
                      cargaPorNucleo < 0 ? "?" : (std::to_string(cargaPorNucleo).substr(0, 4) + " por núcleo").c_str());
        return b;
    }
};

inline int nucleosEnLinea() {
#if defined(_WIN32)
    return static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
#else
    const long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? static_cast<int>(n) : 1;
#endif
}

inline double cargaPorNucleo() {
#if defined(__APPLE__) || defined(__linux__)
    double l[1] = {0};
    if (getloadavg(l, 1) == 1) return l[0] / nucleosEnLinea();
#endif
    return -1;
}

inline Puntualidad resumir(std::vector<double> atrasosMs) {
    Puntualidad p;
    p.cargaPorNucleo = cargaPorNucleo();
    p.muestras = static_cast<int>(atrasosMs.size());
    if (atrasosMs.empty()) return p;
    std::sort(atrasosMs.begin(), atrasosMs.end());
    double suma = 0;
    for (double a : atrasosMs) suma += a;
    p.mediaMs = suma / static_cast<double>(atrasosMs.size());
    p.p99Ms = atrasosMs[atrasosMs.size() * 99 / 100];
    p.maxMs = atrasosMs.back();
    return p;
}

/// Mide ahora, bloqueando `ventana` (de a 2 ms).
inline Puntualidad medirPuntualidad(std::chrono::milliseconds ventana = std::chrono::milliseconds(1000)) {
    using Reloj = std::chrono::steady_clock;
    std::vector<double> atrasos;
    const auto fin = Reloj::now() + ventana;
    auto plazo = Reloj::now();
    while (Reloj::now() < fin) {
        plazo += std::chrono::milliseconds(2);
        std::this_thread::sleep_until(plazo);
        const auto ahora = Reloj::now();
        atrasos.push_back(std::chrono::duration<double, std::milli>(ahora - plazo).count());
        if (ahora - plazo > std::chrono::milliseconds(2)) plazo = ahora; // sin ráfagas para alcanzar
    }
    return resumir(std::move(atrasos));
}

/// El tiempo de CPU que usó este hilo hasta ahora, en ms: para medir cuánto CUESTA algo (una
/// copia, una cuenta) sin contar lo que el hilo esperó a que le tocara la CPU. En Windows la
/// resolución es la del reloj del sistema (~15,6 ms): medir tandas largas.
inline double cpuDelHiloMs() {
#if defined(_WIN32)
    FILETIME c, e, k, u;
    if (!GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u)) return 0;
    const auto a64 = [](FILETIME f) { return (static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    return static_cast<double>(a64(k) + a64(u)) / 1e4; // de a 100 ns
#else
    timespec t{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return static_cast<double>(t.tv_sec) * 1e3 + static_cast<double>(t.tv_nsec) / 1e6;
#endif
}

/// Mide en un hilo propio mientras corre el test (de a 5 ms: 200 despertadas por segundo).
class SondaPuntualidad {
public:
    SondaPuntualidad() : hilo_([this] { correr(); }) {}
    ~SondaPuntualidad() { detener(); }
    SondaPuntualidad(const SondaPuntualidad&) = delete;
    SondaPuntualidad& operator=(const SondaPuntualidad&) = delete;

    void detener() {
        fin_.store(true);
        if (hilo_.joinable()) hilo_.join();
    }
    Puntualidad foto() {
        std::lock_guard<std::mutex> g(m_);
        return resumir(atrasos_);
    }

private:
    void correr() {
        using Reloj = std::chrono::steady_clock;
        auto plazo = Reloj::now();
        while (!fin_.load()) {
            plazo += std::chrono::milliseconds(5);
            std::this_thread::sleep_until(plazo);
            const auto ahora = Reloj::now();
            {
                std::lock_guard<std::mutex> g(m_);
                atrasos_.push_back(std::chrono::duration<double, std::milli>(ahora - plazo).count());
            }
            if (ahora - plazo > std::chrono::milliseconds(5)) plazo = ahora;
        }
    }
    std::atomic<bool> fin_{false};
    std::mutex m_;
    std::vector<double> atrasos_;
    std::thread hilo_;
};

/// Corre bajo Wine (el Windows de Docker: Wine sobre Rosetta, sin GPU ni prioridades de tiempo
/// real de verdad). ntdll de Wine exporta wine_get_version; el de Windows, no.
inline bool bajoWine() {
#if defined(_WIN32)
    HMODULE n = GetModuleHandleW(L"ntdll.dll");
    return n != nullptr && GetProcAddress(n, "wine_get_version") != nullptr;
#else
    return false;
#endif
}

/// GStreamer GL bajo Wine: la ventana oculta de WGL que arma para cada contexto aborta el
/// proceso («gstglwindow_win32.c: window_proc: assertion failed: (window_win32->internal_win_id
/// == hWnd)»: la ventana recibe mensajes en un orden que en Windows no se da). Visto el
/// 2026-11-05 en test_composicion (glvideomixer) y test_sondas (glupload). Es del emulador (en
/// Windows, GL anda); lo que usa GL se saltea bajo Wine, con el motivo. Devuelve true si hay
/// que saltearlo.
inline bool sinGlPorWine(const char* que) {
    if (!bajoWine()) return false;
    std::printf("se saltea %s: bajo Wine, GStreamer GL aborta al crear su ventana de WGL (window_proc: "
                "internal_win_id == hWnd; es del emulador, en Windows anda)\n",
                que);
    std::fflush(stdout);
    return true;
}

/// Corre en un x86_64 emulado (el Linux de Docker en una Mac ARM: Rosetta, que se presenta como
/// «VirtualApple» en /proc/cpuinfo). Ahí el tiempo real no se puede medir: todo pasa por el
/// traductor.
inline bool bajoRosetta() {
#if defined(__linux__)
    std::FILE* f = std::fopen("/proc/cpuinfo", "r");
    if (f == nullptr) return false;
    char linea[256];
    bool si = false;
    while (!si && std::fgets(linea, sizeof linea, f) != nullptr) si = std::strstr(linea, "VirtualApple") != nullptr;
    std::fclose(f);
    return si;
#else
    return false;
#endif
}

inline bool tiempoRealEstricto() {
    const char* e = std::getenv("BA_TEST_TIEMPO_REAL_ESTRICTO");
    return e != nullptr && *e != '\0' && *e != '0';
}

/// El código de salida de un test de tiempo real: 0 sin fallos; con fallos y la máquina
/// impuntual (durante el test, según la sonda, o ahora, medida 1 s), SKIP (77) con el motivo;
/// si no, 1. Lleva SKIP_RETURN_CODE 77 en su add_test.
inline int resultadoTiempoReal(const char* test, int fallos, SondaPuntualidad* sonda = nullptr) {
    if (fallos == 0) return 0;
    Puntualidad durante;
    if (sonda != nullptr) {
        sonda->detener();
        durante = sonda->foto();
    }
    const Puntualidad ahora = medirPuntualidad();
    std::printf("%s: %d fallo(s). Puntualidad de la máquina: durante el test %s; ahora %s\n", test, fallos,
                sonda != nullptr ? durante.texto().c_str() : "(sin sonda)", ahora.texto().c_str());
    if ((bajoRosetta() || bajoWine()) && !tiempoRealEstricto()) {
        std::printf("SKIP (máquina cargada): %s mide tiempo real y corre bajo un emulador (%s): ahí no se puede "
                    "medir. En una máquina de verdad se exige.\n",
                    test, bajoWine() ? "Wine" : "Rosetta: el Linux x86_64 de Docker en una Mac ARM");
        std::fflush(stdout);
        return 77;
    }
    if ((durante.impuntual() || ahora.impuntual()) && !tiempoRealEstricto()) {
        // «SKIP (máquina cargada)»: tools/ci/tests-con-cache.py no lo anota como pasado (la
        // próxima vez corre de nuevo).
        std::printf("SKIP (máquina cargada): %s mide tiempo real y la máquina estaba cargada (no despertaba a tiempo): la corrida no "
                    "sirve. Repetir con la máquina sola (o BA_TEST_TIEMPO_REAL_ESTRICTO=1 para que falle).\n",
                    test);
        std::fflush(stdout);
        return 77;
    }
    return 1;
}

/// Para un test con chequeos comunes y otros de TIEMPO (milisegundos de reloj de pared: una
/// demora, un despertar): un chequeo común que falla es un fallo siempre; los de tiempo, solo
/// con la máquina en hora (con la máquina cargada, SKIP con el motivo; resultadoTiempoReal).
inline int resultadoConTiempos(const char* test, int fallos, int fallosDeTiempo, SondaPuntualidad* sonda = nullptr) {
    if (fallos > 0) return 1;
    return resultadoTiempoReal(test, fallosDeTiempo, sonda);
}

} // namespace ba::pruebas
