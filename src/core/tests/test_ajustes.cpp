// test_ajustes.cpp — el registro de ajustes avanzados (ajustes/Ajustes)
//
// Lo que no está cargado vale el respaldo; lo cargado, su valor (con su tipo);
// poner y sacar una clave; y que leer desde varios hilos mientras se recarga no
// rompe nada.
#include "ajustes/Ajustes.hpp"

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

namespace aj = ba::ajustes;

static int g_fallas = 0;
static void check(bool ok, const char* que) {
    std::printf("%s %s\n", ok ? "ok  " : "FALLA", que);
    if (!ok) ++g_fallas;
}

int main() {
    check(aj::entero("x.noEsta", 7) == 7, "sin cargar: el respaldo");
    aj::cargar({{"a.ms", 1500}, {"b.f", 0.25}, {"c.t", "udp"}, {"d.b", true}});
    check(aj::entero("a.ms", 1) == 1500, "entero");
    check(aj::msANs("a.ms", 1) == 1'500'000'000LL, "ms a ns");
    check(aj::numero("b.f", 1.0) == 0.25, "número con decimales");
    check(aj::texto("c.t", "tcp") == "udp", "texto");
    check(aj::siNo("d.b", false), "sí/no");
    check(aj::entero("c.t", 9) == 9, "tipo equivocado: el respaldo");
    aj::poner("a.ms", nullptr);
    check(aj::entero("a.ms", 3) == 3, "sacar una clave: vuelve al respaldo");
    aj::cargar(nlohmann::json::array());
    check(aj::todos().empty(), "cargar algo que no es un objeto: vacío");

    std::atomic<bool> seguir{true};
    std::atomic<long> lecturas{0};
    std::vector<std::thread> hilos;
    for (int i = 0; i < 4; ++i)
        hilos.emplace_back([&] {
            while (seguir) {
                const int v = aj::entero("k", -1);
                if (v != -1 && (v < 0 || v > 1000)) std::abort();
                ++lecturas;
            }
        });
    for (int i = 0; i < 2000; ++i) aj::cargar({{"k", i % 1000}});
    seguir = false;
    for (auto& t : hilos) t.join();
    check(lecturas > 0, "lecturas desde varios hilos mientras se recarga");

    std::printf(g_fallas == 0 ? "== test_ajustes: OK ==\n" : "== test_ajustes: %d fallas ==\n", g_fallas);
    return g_fallas == 0 ? 0 : 1;
}
