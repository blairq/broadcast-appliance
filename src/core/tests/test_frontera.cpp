// test_frontera.cpp
//
// diag/Frontera: la misma frontera es la misma referencia, los contadores no pierden nada
// con varios hilos a la vez, las tasas salen de la diferencia entre lecturas, los máximos
// se vuelven a cero al leer y MedirPase suma trabajo.

#include "diag/Frontera.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg);         \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

static const nlohmann::json* buscar(const nlohmann::json& j, const std::string& id) {
    for (const auto& f : j["fronteras"])
        if (f.value("id", "") == id) return &f;
    return nullptr;
}

int main() {
    using namespace ba::diag;
    Frontera& a = frontera("prueba.a", 3, "a → b");
    CHECK(&frontera("prueba.a") == &a, "misma frontera, misma referencia");
    CHECK(a.numero() == 3 && a.descripcion() == "a → b", "número y descripción de la primera vez");

    // Varios hilos a la vez: nada se pierde.
    constexpr int kHilos = 8, kPases = 20000;
    std::vector<std::thread> hs;
    for (int h = 0; h < kHilos; ++h)
        hs.emplace_back([&a] {
            for (int i = 0; i < kPases; ++i) {
                a.pase(100, 1000, i % 2 ? 2000 : 0);
                if (i % 10 == 0) a.despertada();
            }
        });
    for (auto& t : hs) t.join();
    Frontera::Lectura l = a.leer();
    CHECK(l.pases == kHilos * kPases, "pases");
    CHECK(l.bytes == 100ULL * kHilos * kPases, "bytes");
    CHECK(l.despertadas == kHilos * kPases / 10, "despertadas");
    CHECK(l.trabajoNs == 1000ULL * kHilos * kPases && l.trabajoN == l.pases, "trabajo");
    CHECK(l.esperaN == kHilos * kPases / 2 && l.esperaNs == 2000ULL * l.esperaN, "espera (solo las medidas)");
    CHECK(l.trabajoMaxNs == 1000 && l.esperaMaxNs == 2000, "máximos");
    CHECK(a.leer().trabajoMaxNs == 0, "el máximo vuelve a 0 al leer");

    // Lote: el trabajo es de la tanda; el máximo, por pase.
    Frontera& b = frontera("prueba.b", 0, "lote");
    b.lote(10, 1000, 50000, 7000);
    l = b.leer();
    CHECK(l.pases == 10 && l.trabajoN == 10 && l.trabajoMaxNs == 5000 && l.esperaN == 1, "lote");

    // MedirPase suma un pase con su tiempo.
    Frontera& c = frontera("prueba.c", 1, "medida");
    {
        MedirPase m(c, 64);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    l = c.leer();
    CHECK(l.pases == 1 && l.bytes == 64 && l.trabajoNs >= 1000000, "MedirPase");

    // Tasas: la primera lectura no tiene ventana; la segunda sí.
    nlohmann::json j0 = fronterasJson(0);
    CHECK(j0.contains("fronteras") && j0["fronteras"].size() >= 3, "json con las fronteras");
    CHECK(j0["fronteras"][0].value("id", "") == "prueba.c", "ordenadas por número de la tabla");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    for (int i = 0; i < 100; ++i) a.pase(1024, 3000);
    a.despertada();
    nlohmann::json j1 = fronterasJson(0);
    const nlohmann::json* fa = buscar(j1, "prueba.a");
    CHECK(fa != nullptr, "está prueba.a");
    if (fa) {
        CHECK(fa->value("pases_s", 0.0) > 100 && fa->value("pases_s", 0.0) < 1000, "pases/s de la ventana");
        CHECK((*fa)["pases_por_despertada"].get<double>() == 100.0, "pases por despertada");
        CHECK((*fa)["trabajo_us_pase"].get<double>() == 3.0, "trabajo por pase en µs");
    }
    // Pedida antes de la ventana: la misma de antes.
    CHECK(fronterasJson(60000) == j1, "dentro de la ventana repite la última");

    if (g_failures) std::printf("%d fallas\n", g_failures);
    else std::printf("test_frontera: ok\n");
    return g_failures ? 1 : 0;
}
