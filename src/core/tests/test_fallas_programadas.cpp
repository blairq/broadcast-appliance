// test_fallas_programadas.cpp — el registro de fallas del banco de caos (F1-14).
//
// Con un reloj de mentira: una falla programada a los N ms no sale antes, sale una
// vez, la toma solo su salida, limpiar borra todo y lo inválido se rechaza.
#include "fault/FallasProgramadas.hpp"

#include <cstdint>
#include <cstdio>

using ba::fault::FallasProgramadas;

namespace {
int g_fallas = 0;
#define CHECK(c, m)                                                   \
    do {                                                              \
        if (!(c)) {                                                   \
            std::printf("FALLO: %s (linea %d)\n", m, __LINE__);       \
            ++g_fallas;                                               \
        } else {                                                      \
            std::printf("ok: %s\n", m);                               \
        }                                                             \
    } while (0)
std::int64_t g_ahora = 1000;
std::int64_t reloj() { return g_ahora; }
} // namespace

int main() {
    FallasProgramadas f;
    f.ponerReloj(&reloj);
    CHECK(!f.hay() && f.tomar("grabacion").empty(), "vacío: nada que romper");
    CHECK(!f.programar("grabacion", "nada", 0).empty(), "pieza inválida: error");
    CHECK(!f.programar("otra", "sumidero", 0).empty(), "salida inválida: error");
    CHECK(f.programar("grabacion", "sumidero", 3000).empty(), "programar el sumidero de la grabación a los 3 s");
    CHECK(f.programar("transmision", "codificador", 0).empty(), "y el codificador de la transmisión ya");
    CHECK(f.hay(), "hay programadas");
    CHECK(f.tomar("grabacion").empty(), "la de la grabación todavía no");
    CHECK(f.tomar("transmision") == "codificador", "la de la transmisión sí");
    CHECK(f.tomar("transmision").empty(), "una sola vez");
    g_ahora += 2999;
    CHECK(f.tomar("grabacion").empty(), "a los 2,999 s, no");
    g_ahora += 1;
    CHECK(f.tomar("grabacion") == "sumidero", "a los 3 s, sí");
    CHECK(!f.hay(), "no queda nada");
    const auto e = f.estado();
    CHECK(e["programadas"].empty() && e["disparadas"].size() == 2, "el estado cuenta las disparadas");
    f.programar("grabacion", "codificador", 10);
    f.limpiar();
    CHECK(!f.hay() && f.estado()["disparadas"].empty(), "limpiar borra todo");
    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
