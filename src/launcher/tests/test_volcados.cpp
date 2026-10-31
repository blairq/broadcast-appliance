// ============================================================================
// src/launcher/tests/test_volcados.cpp — de la caída al .json con la atribución (F1-12)
//
//   1. leerPilas: la pila del que se cayó, con los símbolos legibles, y la
//      atribución (módulo y pieza) aunque el símbolo venga sin demangle.
//   2. Una caída de verdad (POSIX): el test se lanza a sí mismo con el canal que
//      da Volcados, el hijo crea un hilo «ba-ndi-rx» y se cae en él; el launcher
//      (tomarCaida) deja A-<fecha>-caida.pila y su .json con hilo, módulo y
//      pieza «ndi», y hayCaida vuelve a false.
//   3. Un cuelgue (porCuelgue): SIGQUIT, pilas de todos los hilos, tipo «colgado».
//   4. causaDeCodigo: 139 es un error de memoria.
// ============================================================================
#include "Volcados.hpp"

#include <cstdio>
#include <string>

namespace {
int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (0)
} // namespace

#if !defined(_WIN32)

#include "diag/Caidas.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <pthread.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace ba::app::ndi {
// Un símbolo con «ndi» en el espacio de nombres, como los de NdiSource.
[[gnu::noinline]] void recibir(volatile int* p) { *p = 1; }
} // namespace ba::app::ndi

namespace {

namespace fs = std::filesystem;
std::atomic<bool> g_listo{false};

int hijo(const std::string& modo) {
    ba::diag::instalarManejadorDeCaidas("A");
    std::thread t([modo] {
#if defined(__APPLE__)
        pthread_setname_np("ba-ndi-rx");
#else
        pthread_setname_np(pthread_self(), "ba-ndi-rx");
#endif
        g_listo = true;
        if (modo == "caida") ba::app::ndi::recibir(nullptr);
        for (;;) std::this_thread::sleep_for(std::chrono::seconds(1));
    });
    while (!g_listo) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    for (;;) std::this_thread::sleep_for(std::chrono::seconds(1)); // espera la caída o el SIGQUIT
}

pid_t lanzar(const char* yo, const char* modo, const std::map<std::string, std::string>& env) {
    const pid_t pid = fork();
    if (pid == 0) {
        for (const auto& [k, v] : env) setenv(k.c_str(), v.c_str(), 1);
        execl(yo, yo, "hijo", modo, static_cast<char*>(nullptr));
        _exit(127);
    }
    return pid;
}

nlohmann::json leerJson(const fs::path& p) {
    std::ifstream f(p);
    return nlohmann::json::parse(f, nullptr, false);
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "hijo") return hijo(argv[2]);
    using ba::launcher::Volcados;

    // 1
    {
        const auto p = Volcados::leerPilas("BA-PILAS 1\nproceso A\nsenal 11 SIGSEGV\n== cayo 7 gst-src\n"
                                           "#00 0x1 libsystem_kernel.dylib+0x1 __pthread_kill+0x8\n"
                                           "#01 0x2 ba-app+0x2 _ZN2ba4diag12_GLOBAL__N_17manejarEiP9__siginfoPv+0x10\n"
                                           "#02 0x3 libgstbasuperres.so+0x3 gst_sr_chain+0x4\n"
                                           "== hilo 8 otro\n#00 0x9 libnoidea.so+0x9 x+0x1\n== imagenes\nFIN\n");
        CHECK(p.senal == 11, "señal");
        CHECK(p.hilo == "gst-src", "hilo");
        CHECK(p.marcos.size() == 3, "solo los marcos del que se cayó");
        CHECK(p.marcos[1].simbolo.find("ba::diag::") != std::string::npos, "demangle");
        CHECK(p.completa, "FIN");
        CHECK(p.atribucion.modulo == "libgstbasuperres.so", "módulo");
        CHECK(p.atribucion.pieza == ba::fault::Pieza::Superres, "pieza");
    }

    const fs::path logs = fs::temp_directory_path() / ("ba-test-volcados-" + std::to_string(getpid()));
    std::error_code ec;
    fs::remove_all(logs, ec);
    fs::create_directories(logs);
    { std::ofstream(logs / "app.log") << "linea uno\nlinea dos\n"; }

    // 2
    {
        Volcados v(logs);
        const auto env = v.canalPara("A", logs / "app.log");
        CHECK(!env.empty(), "el canal");
        const pid_t pid = lanzar(argv[0], "caida", env);
        v.lanzado("A", static_cast<std::uint32_t>(pid));
        int st = 0;
        waitpid(pid, &st, 0);
        CHECK(WIFSIGNALED(st) && WTERMSIG(st) == SIGSEGV, "el hijo murió por SIGSEGV");
        CHECK(v.hayCaida("A"), "hay caída");
        const auto c = v.tomarCaida("A");
        CHECK(c.has_value(), "tomarCaida");
        if (c) {
            CHECK(c->dmp.extension() == ".pila" && fs::exists(c->dmp), "el .pila con la hora");
            CHECK(c->atribucion.pieza == ba::fault::Pieza::Ndi, "atribuida a NDI");
            const auto j = leerJson(c->json);
            CHECK(j.value("tipo", "") == "caída", "tipo");
            CHECK(j.value("hilo", "") == "ba-ndi-rx", "el nombre del hilo");
            CHECK(j.value("pieza", "") == "ndi", "pieza en el json");
            CHECK(j.value("codigo", 0) == 11, "código");
            CHECK(j["ultimas_lineas"].size() == 2, "las últimas líneas del registro");
            if (g_fallos > 0) std::printf("%s\n", j.dump(1).c_str());
        }
        CHECK(!v.hayCaida("A"), "tomada, ya no hay");
    }

    // 3
    {
        Volcados v(logs);
        const auto env = v.canalPara("B", logs / "gateway.log");
        const pid_t pid = lanzar(argv[0], "colgado", env);
        std::this_thread::sleep_for(std::chrono::milliseconds(300)); // que instale el manejador
        const auto c = v.porCuelgue("B", static_cast<std::uint32_t>(pid), logs / "gateway.log");
        int st = 0;
        waitpid(pid, &st, 0);
        CHECK(WIFSIGNALED(st) && WTERMSIG(st) == SIGQUIT, "terminó por SIGQUIT");
        const auto j = leerJson(c.json);
        CHECK(j.value("tipo", "") == "colgado", "colgado");
        CHECK(fs::exists(c.dmp), "con pilas");
        std::ifstream f(c.dmp);
        const std::string t((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        CHECK(t.find("ba-ndi-rx\n#00 ") != std::string::npos, "la pila del otro hilo, con su nombre");
    }

    // 4
    CHECK(ba::launcher::causaDeCodigo(139).find("SIGSEGV") != std::string::npos, "139");

    fs::remove_all(logs, ec);
    std::printf(g_fallos == 0 ? "test_volcados: OK\n" : "test_volcados: %d fallos\n", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}

#else

int main() {
    CHECK(ba::launcher::causaDeCodigo(static_cast<int>(0xC0000005u)).find("0xC0000005") != std::string::npos, "AV");
    std::printf(g_fallos == 0 ? "test_volcados: OK\n" : "test_volcados: %d fallos\n", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}

#endif
