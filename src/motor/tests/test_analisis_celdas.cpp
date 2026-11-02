// test_analisis_celdas.cpp — AnalisisCeldas: los cuadros nuevos de cada celda de un mosaico
//
//   - una celda que cambia en cada cuadro del mosaico va al ritmo del mosaico; una que
//     cambia uno sí y uno no, a la mitad; una quieta, a 0;
//   - los intervalos entre cuadros nuevos y los huecos (contra lo que manda su capa);
//   - el rótulo de abajo de la celda (que no cambia con la cara) no cuenta;
//   - los cuadros que el emisor publicó y no se leyeron (saltos del número de cuadro).

#include "AnalisisCeldas.hpp"

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

using ba::motor::AnalisisCeldas;

namespace {
int fallos = 0;
void check(bool ok, const std::string& que) {
    std::printf("%s: %s\n", ok ? "ok" : "FALLA", que.c_str());
    if (!ok) ++fallos;
}

constexpr int W = 200, H = 100;

/// Pinta el rectángulo (en píxeles) de un gris `v`.
void pintar(std::vector<std::uint8_t>& px, int x, int y, int w, int h, std::uint8_t v) {
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i)
            for (int k = 0; k < 4; ++k) px[(static_cast<std::size_t>(j) * W + i) * 4 + k] = v;
}
} // namespace

int main() {
    {
        // Tres celdas de 100x50 arriba (a, b) y una abajo (c), a 30 cps de mosaico, 3 s.
        AnalisisCeldas a({{"a", "baja", 0.0, 0.0, 0.5, 0.5, 15},
                          {"b", "baja", 0.5, 0.0, 0.5, 0.5, 15},
                          {"c", "alta", 0.0, 0.5, 0.5, 0.5, 30}});
        std::vector<std::uint8_t> px(static_cast<std::size_t>(W) * H * 4, 0);
        const std::int64_t periodo = 33'333'333;
        for (int n = 0; n < 91; ++n) {
            pintar(px, 0, 0, 100, 50, static_cast<std::uint8_t>(n));        // a: cada cuadro
            pintar(px, 100, 0, 100, 50, static_cast<std::uint8_t>(n / 2));  // b: uno sí, uno no
            // c: quieta, salvo el rótulo (el 25 % de abajo de la celda): no es un cuadro nuevo
            pintar(px, 0, 90, 100, 10, static_cast<std::uint8_t>(n));
            a.cuadro(px.data(), W, H, W * 4, n * periodo, static_cast<std::uint64_t>(n + 1));
        }
        const auto r = a.resumen();
        const auto& c = r["celdas"];
        std::printf("%s\n", r.dump().c_str());
        check(r["cuadros"] == 91 && std::abs(r["cps"].get<double>() - 30.0) < 0.1, "el mosaico: 91 cuadros a 30 cps");
        check(r["perdidos"] == 0, "sin saltos de número: nada perdido");
        check(std::abs(c[0]["cps"].get<double>() - 30.0) < 0.1, "la que cambia en cada cuadro: 30 cps");
        check(std::abs(c[1]["cps"].get<double>() - 15.0) < 0.5, "la que cambia uno sí y uno no: 15 cps");
        check(c[2]["cps"].get<double>() == 0.0 && c[2]["nuevos"] == 0, "la quieta (solo cambia su rótulo): 0");
        check(std::abs(c[1]["intervalo_ms"]["p50"].get<double>() - 66.7) < 0.2, "intervalo de la de 15: 66,7 ms");
        check(c[1]["huecos"] == 0 && !c[1]["por_debajo"].get<bool>(), "la de 15 contra una capa de 15: sin huecos, no está por debajo");
        check(c[2]["por_debajo"].get<bool>(), "la quieta contra una capa de 30: por debajo");
        check(std::abs(r["resumen"]["por_debajo_pct"].get<double>() - 33.3) < 0.1, "una de tres por debajo: 33,3 %");
    }
    {
        // El emisor publica 1, 2, 5, 6 (3 y 4 no se leyeron) y se lee dos veces el 6.
        AnalisisCeldas a({{"x", "baja", 0, 0, 1, 1, 15}});
        std::vector<std::uint8_t> px(static_cast<std::size_t>(W) * H * 4, 0);
        const std::uint64_t idx[] = {1, 2, 5, 6, 6};
        for (int n = 0; n < 5; ++n) {
            pintar(px, 0, 0, W, H, static_cast<std::uint8_t>(idx[n]));
            a.cuadro(px.data(), W, H, W * 4, n * 40'000'000LL, idx[n]);
        }
        const auto r = a.resumen();
        check(r["perdidos"] == 2, "dos cuadros publicados que no se leyeron");
        check(r["repetidos"] == 1, "uno leído dos veces");
        check(r["celdas"][0]["nuevos"] == 3 && r["celdas"][0]["repetidos"] == 1, "la celda: 3 nuevos y 1 repetido");
    }
    {
        // Huecos: una de 15 cps a la que le faltan cuadros (intervalos de 133 ms).
        AnalisisCeldas a({{"x", "baja", 0, 0, 1, 1, 15}});
        std::vector<std::uint8_t> px(static_cast<std::size_t>(W) * H * 4, 0);
        const std::int64_t ts[] = {0, 66, 133, 266, 333, 466};
        for (int n = 0; n < 6; ++n) {
            pintar(px, 0, 0, W, H, static_cast<std::uint8_t>(n + 1));
            a.cuadro(px.data(), W, H, W * 4, ts[n] * 1'000'000LL);
        }
        const auto r = a.resumen();
        check(r["celdas"][0]["huecos"] == 2, "dos intervalos de 133 ms contra 66,7: dos huecos");
        check(r["celdas"][0]["intervalo_ms"]["max"].get<double>() == 133.0, "el máximo: 133 ms");
    }
    {
        // Sin cuadros: el resumen no se rompe.
        AnalisisCeldas a({{"x", "", 0, 0, 1, 1, 0}});
        const auto r = a.resumen();
        check(r["cuadros"] == 0 && r["celdas"][0]["cps"] == 0.0 && r["celdas"][0]["intervalo_ms"].is_null(), "sin cuadros: ceros");
    }
    std::printf("%s\n", fallos == 0 ? "TODO OK" : "HAY FALLAS");
    return fallos == 0 ? 0 : 1;
}
