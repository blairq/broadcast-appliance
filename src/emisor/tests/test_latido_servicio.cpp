// src/emisor/tests/test_latido_servicio.cpp — el latido del emisor es del bucle, no del proceso
//
// LatidoServicio sin relojes: el bucle que da vueltas late; el que se traba deja de latir
// a los vueltaMaxMs; lo largo declarado (arrancar, aplicar, cerrar) late hasta
// ocupadoMaxMs desde que empezó o desde que avanzó, y la vuelta siguiente lo cierra.

#include "LatidoServicio.hpp"

#include <cstdio>

namespace {
int fallos = 0;
#define CHECK(c, m)                                                                                          \
    do {                                                                                                     \
        if (c)                                                                                               \
            std::printf("ok: %s\n", m);                                                                      \
        else {                                                                                               \
            std::printf("FALLO: %s (%s, linea %d)\n", m, #c, __LINE__);                                      \
            ++fallos;                                                                                        \
        }                                                                                                    \
    } while (0)
} // namespace

int main() {
    using ba::emisor::LatidoServicio;
    {
        LatidoServicio l(3000, 15000);
        l.vuelta(1000);
        CHECK(l.late(1100), "el bucle dio una vuelta hace 100 ms: late");
        CHECK(l.late(3999), "a los 2,999 s sin vuelta: todavía late");
        CHECK(!l.late(4000), "a los 3 s sin vuelta: el bucle está trabado, no late");
        l.vuelta(5000);
        CHECK(l.late(5100), "vuelve a dar vueltas: late de nuevo");
    }
    {
        LatidoServicio l(3000, 15000);
        l.ocupado(0); // arrancando, antes de la primera vuelta
        CHECK(l.late(10000), "arrancando (ocupado) a los 10 s: late");
        CHECK(!l.late(15000), "arrancando sin avanzar 15 s: no late");
        l.vuelta(16000);
        CHECK(l.late(16100), "la primera vuelta cierra el arranque");
        CHECK(!l.late(19000), "y desde ahí rige la vuelta (3 s)");
    }
    {
        LatidoServicio l(3000, 15000);
        l.vuelta(0);
        l.ocupado(100); // aplicar un pedido que para una grabación
        CHECK(l.late(8000), "aplicando 8 s (cerrar el archivo): late");
        l.ocupado(12000); // cerrar: el pasaje a MP4 avanzó
        CHECK(l.late(20000), "lo largo que avanza sigue latiendo");
        CHECK(!l.late(27000), "lo largo que no avanza 15 s: no late");
        l.vuelta(27100);
        CHECK(l.late(27200) && !l.late(30100), "la vuelta siguiente lo cierra: vuelve a regir la vuelta");
    }
    std::printf("%s\n", fallos ? "HAY FALLOS" : "todo bien");
    return fallos ? 1 : 0;
}
