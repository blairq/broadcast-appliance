// test_relanzador.cpp — cuándo relanzar un hijo que se cayó (platform/Relanzador.hpp, G-13/G-16)
//
//   1. Duplicando con tope.
//   2. Uno que muere enseguida (300 ms) NO olvida sus caídas: la espera crece (antes ba-web
//      volvía a 0,5 s apenas vivía 250 ms y se relanzaba cada ~0,75 s para siempre).
//   3. Andando sano `estableMs`, se olvidan.
//   4. Cortacircuito: N seguidas sin un rato estable y se espera mucho; uno estable lo cierra.

#include "platform/Relanzador.hpp"

#include <cstdio>

static int g_failures = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg);         \
            ++g_failures;                                                      \
        } else {                                                               \
            std::printf("ok: %s\n", msg);                                      \
        }                                                                      \
    } while (0)

using ba::platform::PoliticaRelanzar;
using ba::platform::Relanzador;

int main() {
    const auto dup = PoliticaRelanzar::duplicando(500, []() { return std::uint64_t{8000}; });
    CHECK(dup(0) == 500 && dup(1) == 1000 && dup(4) == 8000 && dup(9) == 8000, "duplicando con tope");

    // Uno que muere a los 300 ms, vigilado cada 250 ms.
    {
        Relanzador r(PoliticaRelanzar{.esperaMs = dup, .estableMs = 10000});
        std::uint64_t t = 0, ultima = 0;
        for (int i = 0; i < 6; ++i) {
            r.lanzado(t);
            r.vigilar(t + 250, true); // vivo y contestando, pero hace nada
            t += 300;
            ultima = r.caida(t);
            t += ultima;
        }
        CHECK(ultima == 8000 && r.consecutivas() == 6, "muere enseguida: la espera crece (no vuelve a 0,5 s)");
    }
    // Andando estable, se olvida.
    {
        Relanzador r(PoliticaRelanzar{.esperaMs = dup, .estableMs = 10000});
        r.caida(0);
        r.caida(1000);
        r.lanzado(5000);
        CHECK(!r.vigilar(9000, true), "sano pero poco rato: todavía no");
        CHECK(!r.vigilar(20000, false), "un rato largo pero sin contestar: no");
        CHECK(r.vigilar(20000, true) && r.consecutivas() == 0, "sano y estable: se olvidan");
        CHECK(r.caida(30000) == 500, "y la próxima espera lo mínimo");
    }
    // Cortacircuito.
    {
        Relanzador r(PoliticaRelanzar{.esperaMs = dup, .estableMs = 10000, .cortarTras = 3, .esperaCortadoMs = 300000});
        r.caida(0);
        r.caida(1);
        CHECK(!r.cortado(), "dos seguidas: todavía insiste");
        CHECK(r.caida(2) == 300000 && r.cortado(), "la tercera abre el cortacircuito: espera larga");
        CHECK(r.caidas() == 3, "cuenta las caídas (métricas)");
        r.lanzado(400000);
        CHECK(r.vigilar(420000, true) && !r.cortado(), "un rato estable lo cierra");
    }
    std::printf(g_failures == 0 ? "test_relanzador: OK\n" : "test_relanzador: %d FALLAS\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
