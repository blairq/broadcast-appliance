// test_perro_guardian.cpp
//
// El watchdog de hardware (F4-08) con un dispositivo FALSO: un archivo común hace de
// /dev/watchdog (el núcleo entiende igual las escrituras: cualquier byte le da de comer
// y la «V» antes de cerrar lo desarma). Que coma como mucho una vez por segundo, que al
// desarmar escriba la V y cierre, que el destructor desarme, y que un dispositivo que no
// está no frene nada. Y las métricas del launcher: el archivo en Runtime/.

#include "MetricasLauncher.hpp"
#include "PerroGuardian.hpp"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

static int g_failures = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg);         \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

namespace fs = std::filesystem;

static std::string leer(const fs::path& p) {
    std::ifstream in(p);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int main() {
    const fs::path dir = fs::temp_directory_path() / ("ba-perro-" + std::to_string(std::time(nullptr)));
    fs::create_directories(dir);
    const fs::path falso = dir / "watchdog";
    std::ofstream(falso).close();

    using ba::launcher::PerroGuardian;
#if defined(_WIN32)
    PerroGuardian w;
    CHECK(!w.abrir(falso.string(), 30), "Windows: no hay watchdog de hardware");
#else
    {
        PerroGuardian w;
        std::string err;
        CHECK(w.abrir(falso.string(), 30, &err) && w.abierto(), "abre el dispositivo falso (el plazo no lo toma: es un archivo)");
        w.latir(10'000);
        w.latir(10'400); // menos de un segundo: no come de nuevo
        w.latir(11'000);
        w.latir(12'500);
        CHECK(w.latidos() == 3, "come como mucho una vez por segundo");
        w.desarmar();
        CHECK(!w.abierto(), "desarmado y cerrado");
        CHECK(leer(falso) == "kkkV", "tres latidos y la V mágica");
        w.latir(20'000);
        CHECK(leer(falso) == "kkkV", "cerrado no escribe");
    }
    {
        // El destructor desarma (salir en orden no reinicia la máquina).
        std::ofstream(falso, std::ios::trunc).close();
        {
            PerroGuardian w;
            CHECK(w.abrir(falso.string(), 30), "abre otra vez");
            w.latir(1'000);
        }
        CHECK(leer(falso) == "kV", "al irse, desarma");
    }
    {
        PerroGuardian w;
        std::string err;
        CHECK(!w.abrir((dir / "no-esta").string(), 30, &err) && !err.empty(), "sin dispositivo: no abre y dice por qué");
        w.latir(5'000);
        w.desarmar(); // nada que desarmar: no revienta
    }
#endif

    // Las métricas del launcher: Runtime/metricas-launcher.prom, en el formato de Prometheus.
    ba::launcher::MetricasLauncher::falla("app", false);
    ba::launcher::MetricasLauncher::falla("gateway", true);
    ba::launcher::MetricasLauncher::falla("emisor", false); // los emisores aparte también cuentan (F3-01)
    ba::launcher::MetricasLauncher m;
    m.vuelta(dir, 1'000, 7);
    const std::string prom = leer(dir / "metricas-launcher.prom");
    CHECK(prom.find("ba_launcher_caidas_total{proceso=\"app\"} 1") != std::string::npos, "la caída de A");
    CHECK(prom.find("ba_launcher_cuelgues_total{proceso=\"gateway\"} 1") != std::string::npos, "el cuelgue de B");
    CHECK(prom.find("ba_launcher_caidas_total{proceso=\"emisor\"} 1") != std::string::npos, "la caída de un emisor");
    CHECK(prom.find("ba_launcher_perro_latidos_total 7") != std::string::npos, "los latidos del perro");
    m.vuelta(dir, 2'000, 8); // antes de los 5 s: no reescribe
    CHECK(leer(dir / "metricas-launcher.prom") == prom, "se escribe cada 5 s");

    fs::remove_all(dir);
    if (g_failures == 0) std::printf("test_perro_guardian: OK\n");
    return g_failures == 0 ? 0 : 1;
}
