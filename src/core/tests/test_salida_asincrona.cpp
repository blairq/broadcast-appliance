// test_salida_asincrona.cpp
//
// diag/SalidaAsincrona: con stderr a un archivo (como lo deja el launcher),
// lo que se escribe llega al archivo por el hilo escritor; retenida, se junta
// en memoria y no toca el archivo hasta soltarla.

#include "diag/SalidaAsincrona.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

static int g_failures = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg);         \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

static std::string leer(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main() {
    namespace fs = std::filesystem;
    const fs::path ruta = fs::temp_directory_path() / "ba-test-salida-asincrona.log";
    std::error_code ec;
    fs::remove(ruta, ec);
    std::fflush(stdout);
    std::setvbuf(stdout, nullptr, _IONBF, 0); // los FAIL, al archivo en orden
    if (std::freopen(ruta.string().c_str(), "wb", stderr) == nullptr) {
        std::printf("FAIL no se pudo abrir %s\n", ruta.string().c_str());
        return 1;
    }
    std::setvbuf(stderr, nullptr, _IONBF, 0); // como el stderr de siempre (freopen le pone buffer)
    ba::diag::desacoplarSalidaEstandar(1u << 20);
    CHECK(ba::diag::estadoSalidaEstandar().activa, "stderr a un archivo: se desacopla");

    std::fprintf(stderr, "primera\n");
    ba::diag::vaciarSalidaEstandar(2000);
    CHECK(leer(ruta).find("primera\n") != std::string::npos, "llega al archivo");

    ba::diag::retenerSalidaEstandar(true);
    std::fprintf(stderr, "retenida\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(leer(ruta).find("retenida") == std::string::npos, "retenida: no se escribe");
    CHECK(ba::diag::estadoSalidaEstandar().enCola > 0, "retenida: queda en memoria");

    ba::diag::retenerSalidaEstandar(false);
    ba::diag::vaciarSalidaEstandar(2000);
    CHECK(leer(ruta).find("retenida\n") != std::string::npos, "soltada: se escribe");

    // Más de lo que entra: se descarta lo que sobra y se avisa en el archivo.
    ba::diag::retenerSalidaEstandar(true);
    const std::string kb(1024, 'x');
    for (int i = 0; i < 1500; ++i) std::fprintf(stderr, "%s\n", kb.c_str());
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(ba::diag::estadoSalidaEstandar().perdidos > 0, "pasado el tope: se pierde (sin trabar)");
    ba::diag::retenerSalidaEstandar(false);
    ba::diag::vaciarSalidaEstandar(3000);
    CHECK(leer(ruta).find("se perdieron") != std::string::npos, "lo perdido queda anotado");

    // stdout también está desacoplado: el resultado queda en el archivo (y si falla, se muestra entero).
    std::printf(g_failures == 0 ? "OK\n" : "%d fallas\n", g_failures);
    ba::diag::vaciarSalidaEstandar(2000);
    if (g_failures != 0) std::fputs(leer(ruta).c_str(), stdout);
    return g_failures == 0 ? 0 : 1;
}
