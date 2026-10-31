// ============================================================================
// src/core/tests/test_caidas_pilas.cpp — una caída deja las pilas de TODOS los hilos (F1-12)
//
// El test se lanza a sí mismo como hijo («hijo <modo> <archivo>»), con el
// archivo de pilas en BA_CAIDA_CANAL como lo hace el launcher (Volcados). El
// hijo nombra tres hilos, los deja esperando y se cae; el padre lee el archivo:
//
//   1. Un SIGSEGV: el hijo muere por SIGSEGV (el manejador no cambia eso).
//   2. El archivo empieza con «BA-PILAS 1», dice la señal y termina con «FIN».
//   3. Están las pilas de los tres hilos con su nombre y la del que se cayó.
//   4. La lista de imágenes trae la del ejecutable.
//   5. SIGQUIT (lo que manda el launcher a un colgado): también deja las pilas.
//   6. La atribución (fault::atribuir) culpa a lo que no es del sistema.
//
// Fuera de POSIX no hace nada (en Windows el volcado lo escribe el launcher).
// ============================================================================
#include "fault/ModoSeguro.hpp"

#include <cstdio>

#if defined(_WIN32)
int main() {
    std::puts("test_caidas_pilas: solo POSIX");
    return 0;
}
#else

#include "diag/CaidaCompartida.hpp"
#include "diag/Caidas.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <pthread.h>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (0)

std::atomic<int> g_listos{0};

void nombrar(const char* n) {
#if defined(__APPLE__)
    pthread_setname_np(n);
#else
    pthread_setname_np(pthread_self(), n);
#endif
}

[[noreturn]] void esperar(const char* n) {
    nombrar(n);
    ++g_listos;
    for (;;) std::this_thread::sleep_for(std::chrono::seconds(1));
}

int hijo(const std::string& modo) {
    ba::diag::instalarManejadorDeCaidas("T");
    std::vector<std::thread> hilos;
    for (const char* n : {"ba-motor", "ba-ndi-rx", "superres-0"}) hilos.emplace_back([n] { esperar(n); });
    while (g_listos.load() < 3) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    nombrar("principal");
    if (modo == "quit") {
        raise(SIGQUIT);
    } else {
        volatile int* nada = nullptr;
        *nada = 1; // la caída
    }
    return 0;
}

std::string leer(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

int correr(const char* yo, const char* modo, const std::filesystem::path& archivo) {
    const pid_t pid = fork();
    if (pid == 0) {
        setenv(ba::diag::caida::kVariable, archivo.c_str(), 1);
        execl(yo, yo, "hijo", modo, static_cast<char*>(nullptr));
        _exit(127);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFSIGNALED(st) ? WTERMSIG(st) : -1;
}

void revisar(const std::string& t, const char* senal) {
    CHECK(t.rfind("BA-PILAS 1\n", 0) == 0, "cabecera");
    CHECK(t.find(std::string("senal ") + senal) != std::string::npos, "la señal");
    CHECK(t.find("\nFIN\n") != std::string::npos, "termina con FIN");
    CHECK(t.find("== cayo ") != std::string::npos, "la pila del que se cayó");
    CHECK(t.find("principal") != std::string::npos, "el nombre del que se cayó");
    for (const char* n : {"ba-motor", "ba-ndi-rx", "superres-0"})
        CHECK(t.find(std::string(" ") + n + "\n#00 ") != std::string::npos, n);
    CHECK(t.find("== imagenes\nimg ") != std::string::npos, "las imágenes");
    CHECK(t.find("test_caidas_pilas") != std::string::npos, "el ejecutable entre las imágenes");
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "hijo") return hijo(argv[2]);
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("ba-test-pilas-" + std::to_string(getpid()));
    fs::create_directories(dir);

    // 1-4
    const fs::path a = dir / "T.pila";
    CHECK(correr(argv[0], "segv", a) == SIGSEGV, "muere por SIGSEGV");
    const std::string t = leer(a);
    revisar(t, "11");
    {
        // La pila del que se cayó empieza donde se cayó (main), no en el manejador.
        const std::size_t c = t.find("== cayo ");
        const std::size_t l = c == std::string::npos ? c : t.find('\n', c);
        const std::string primero = l == std::string::npos ? std::string() : t.substr(l + 1, t.find('\n', l + 1) - l - 1);
        CHECK(primero.rfind("#00 ", 0) == 0 && primero.find(" main+") != std::string::npos, "#00 es donde se cayó");
    }
    if (g_fallos > 0) std::printf("%s\n", t.c_str());

    // 5
    const fs::path b = dir / "Q.pila";
    CHECK(correr(argv[0], "quit", b) == SIGQUIT, "muere por SIGQUIT");
    revisar(leer(b), std::to_string(SIGQUIT).c_str());

    // 6
    using ba::fault::Marco;
    const auto at = ba::fault::atribuir({{"libsystem_kernel.dylib", "__pthread_kill"},
                                         {"ba-app", "ba::diag::(anonymous namespace)::manejar(int, __siginfo*, void*)"},
                                         {"libgstbasuperres.dylib", "gst_basuperres_transform"},
                                         {"libgstbase-1.0.0.dylib", "x"}});
    CHECK(at.modulo == "libgstbasuperres.dylib", "culpa al primero que no es del sistema");
    CHECK(at.pieza == ba::fault::Pieza::Superres, "superres");
    const auto ndi = ba::fault::atribuir({{"libc.so.6", "abort"}, {"ba-app", "ba::app::ndi::Receptor::leer()"}});
    CHECK(ndi.modulo == "ba-app" && ndi.pieza == ba::fault::Pieza::Ndi, "NDI por el símbolo");
    const auto hilo = ba::fault::atribuir({{"libc.so.6", "memcpy"}, {"ba-app", "algo()"}}, "ba-ndi-rx");
    CHECK(hilo.pieza == ba::fault::Pieza::Ndi, "NDI por el nombre del hilo");
    CHECK(!ba::fault::atribuir({{"ba-app", "ba::app::Studio::x()"}}).pieza, "sin pieza");

    std::error_code ec;
    fs::remove_all(dir, ec);
    std::printf(g_fallos == 0 ? "test_caidas_pilas: OK\n" : "test_caidas_pilas: %d fallos\n", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}

#endif
