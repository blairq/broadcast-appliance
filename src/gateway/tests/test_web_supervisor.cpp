// ============================================================================
// src/gateway/tests/test_web_supervisor.cpp — el supervisor de ba-web (BaWeb.hpp)
//
// Con el ba-web de verdad (solo POSIX: SIGSTOP):
//   1. Arranca y contesta su sondeo de vida (/_ba/vivo, sin el gateway).
//   2. Frenado (SIGSTOP a él y a sus workers, como el banco de caos «stop_web»): el
//      sistema le sigue aceptando conexiones, pero no contesta; el supervisor lo nota en
//      unos segundos, lo mata y lo relanza, y el nuevo contesta.
//   3. Muerto (kill -9): se relanza como siempre.
// ============================================================================
#include "BaWeb.hpp"
#include "Log.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

#if !defined(_WIN32)
#include <signal.h>
#include <unistd.h>
#endif

namespace {

int g_fallas = 0;

#define CHECK(cond, que)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FALLA: %s (%s, linea %d)\n", que, #cond, __LINE__); \
            ++g_fallas;                                                   \
        }                                                                 \
    } while (0)

/// Espera hasta `topeMs` a que `cond` sea cierta; los ms que tardó, o -1.
template <class F>
long long esperar(F cond, int topeMs) {
    const auto t0 = std::chrono::steady_clock::now();
    for (;;) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        if (cond()) return ms;
        if (ms > topeMs) return -1;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

} // namespace

int main() {
#if defined(_WIN32) || !defined(BA_TEST_BA_WEB)
    std::printf("SKIP: con SIGSTOP y el ba-web compilado (POSIX)\n");
    return 77;
#else
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("ba_web_supervisor_" + std::to_string(::getpid()));
    fs::create_directories(dir);
    ba::gateway::Log log;
    ba::gateway::WebSupervisor::Cfg c;
    c.exe = BA_TEST_BA_WEB;
    c.logPath = (dir / "ba-web.log").string();
    c.dir = dir.string();
    c.puerto = static_cast<uint16_t>(42000 + ::getpid() % 2000);
    c.upstream = static_cast<uint16_t>(c.puerto + 1); // nadie: el sondeo no lo usa
    c.procesos = 2;
    {
        ba::gateway::WebSupervisor sup(c, log);
        CHECK(sup.start(), "arranca");
        const long long listo = esperar([&]() { return ba::gateway::WebSupervisor::contesta(c.puerto, 500); }, 5000);
        std::printf("  contesta a los %lld ms de lanzarlo\n", listo);
        CHECK(listo >= 0, "1: contesta su sondeo de vida");

        // 2. Frenado: SIGSTOP a su grupo (el master y los workers).
        const std::uint32_t pid = sup.pid();
        CHECK(pid != 0 && ::kill(-static_cast<pid_t>(pid), SIGSTOP) == 0, "2: se lo frena");
        CHECK(!ba::gateway::WebSupervisor::contesta(c.puerto, 500), "2: frenado no contesta");
        const long long vuelve = esperar(
            [&]() { return sup.reinicios() >= 1 && sup.pid() != pid && ba::gateway::WebSupervisor::contesta(c.puerto, 500); },
            20000);
        std::printf("  frenado: relanzado y contestando a los %lld ms (reinicios=%d)\n", vuelve, sup.reinicios());
        CHECK(vuelve >= 0 && vuelve < 15000, "2: el frenado se mata y se relanza, y el nuevo contesta");
        CHECK(::kill(static_cast<pid_t>(pid), 0) != 0, "2: el frenado ya no existe");

        // 3. Muerto: se relanza como siempre.
        const std::uint32_t pid2 = sup.pid();
        const int antes = sup.reinicios();
        ::kill(-static_cast<pid_t>(pid2), SIGKILL);
        const long long vuelve2 = esperar(
            [&]() { return sup.reinicios() > antes && ba::gateway::WebSupervisor::contesta(c.puerto, 500); }, 15000);
        std::printf("  muerto: relanzado y contestando a los %lld ms\n", vuelve2);
        CHECK(vuelve2 >= 0, "3: el muerto se relanza");
        sup.stop();
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    if (g_fallas == 0) std::printf("test_web_supervisor: OK\n");
    return g_fallas == 0 ? 0 : 1;
#endif
}
