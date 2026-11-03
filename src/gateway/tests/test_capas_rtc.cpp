// test_capas_rtc.cpp — el reparto de las sesiones en las capas de hilos (rtc/CapasHilos.hpp;
// F2 de docs/planes/PLAN-GATEWAY-FRAGMENTOS.md)
//
//   - Los tamaños automáticos: el 5900X (6 núcleos por CCD, 12 físicos) da 4 de red y 3 de
//     trabajo; la M1 Pro (4 por grupo, 8 de rendimiento), 2 y 2; sin saber los físicos, la
//     red como antes (bloque/3, 1..4); sin saber nada, 1 y 1. Los pedidos se respetan (hasta 16).
//   - 200 invitados en los 4 hilos de red del 5900X: parejos (50 cada uno), también con
//     pesos mezclados y gente que se va y vuelve.
//   - Al entrar, cada sesión va al par menos cargado; las cargas quedan parejas.
//   - Una sesión no se muda: subir su peso (720p) solo cambia adónde van las próximas.
//   - Soltar devuelve la carga; R/T = 1 y R/T = 3 reparten lo mismo en total.
//   - Sin cruzar (gateway.capasCruzadas = false): el trabajo va con su red (T = R).
//   - Varios hilos tomando y soltando a la vez: al final todo en cero.
#include "rtc/CapasHilos.hpp"

#include <algorithm>
#include <cstdio>
#include <thread>
#include <vector>

namespace ca = ba::gateway::capas;

static int fallas = 0;
#define CHECK(c, msg)                                                                                                  \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::printf("FALLA (línea %d): %s\n", __LINE__, msg);                                                      \
            ++fallas;                                                                                                  \
        }                                                                                                              \
    } while (0)

int main() {
    // Tamaños.
    ca::Tamanos t = ca::resolver(0, 0, 6, 12);
    CHECK(t.red == 4 && t.trabajo == 3, "automático en el 5900X: 4 de red (12 físicos / 3), 3 de trabajo");
    t = ca::resolver(0, 0, 4, 8);
    CHECK(t.red == 2 && t.trabajo == 2, "automático en la M1 Pro: 2 y 2");
    t = ca::resolver(0, 0, 4, 4);
    CHECK(t.red == 2, "con 4 núcleos, 2 de red (el mínimo)");
    t = ca::resolver(0, 0, 1, 1);
    CHECK(t.red == 1, "con un núcleo, no más hilos de red que núcleos");
    t = ca::resolver(0, 0, 32, 64);
    CHECK(t.red == 8 && t.trabajo == 6, "automático con topes (8 de red y 6 de trabajo)");
    t = ca::resolver(0, 0, 6);
    CHECK(t.red == 2 && t.trabajo == 3, "sin saber los físicos, la red como antes (bloque/3)");
    t = ca::resolver(0, 0, 0);
    CHECK(t.red == 1 && t.trabajo == 1, "sin saber los núcleos: 1 y 1");
    t = ca::resolver(0, 0, 32);
    CHECK(t.red == 4 && t.trabajo == 6, "sin los físicos, con topes (4 y 6)");
    CHECK(ca::nucleosFisicos() >= 0, "los físicos de esta máquina (0 si no se sabe)");

    // 200 invitados en los 4 de red del 5900X: parejos, también con pesos y gente que va y viene.
    {
        ca::Reparto r(ca::resolver(0, 0, 6, 12));
        CHECK(r.tamanos().red == 4, "el reparto del 5900X tiene 4 de red");
        std::vector<ca::Lugar> ls;
        for (int i = 0; i < 200; ++i) ls.push_back(r.tomar());
        for (int i = 0; i < 4; ++i) CHECK(r.sesionesRed(i) == 50 && r.cargaRed(i) == 50, "200 en 4: 50 cada uno");
        // Se van los pares (100) y entran 100 nuevos, uno de cada 5 en alta.
        for (size_t i = 0; i < ls.size(); i += 2) r.soltar(ls[i]);
        for (int i = 0; i < 100; ++i) ls.push_back(r.tomar(i % 5 == 0 ? ca::kPesoAlta : ca::kPesoNormal));
        int menor = 1 << 30, mayor = 0;
        for (int i = 0; i < 4; ++i) {
            menor = std::min(menor, r.cargaRed(i));
            mayor = std::max(mayor, r.cargaRed(i));
        }
        CHECK(mayor - menor <= ca::kPesoAlta, "después de ir y venir, las cargas siguen parejas (a un 720p de diferencia)");
        for (auto& l : ls) r.soltar(l);
        for (int i = 0; i < 4; ++i) CHECK(r.cargaRed(i) == 0 && r.sesionesRed(i) == 0, "al irse todos, en cero");
    }
    t = ca::resolver(3, 5, 4);
    CHECK(t.red == 3 && t.trabajo == 5, "lo pedido se respeta");
    t = ca::resolver(99, 99, 4);
    CHECK(t.red == 16 && t.trabajo == 16, "lo pedido, hasta 16");

    // Reparto parejo.
    {
        ca::Reparto r(ca::Tamanos{2, 3});
        std::vector<ca::Lugar> ls;
        for (int i = 0; i < 12; ++i) ls.push_back(r.tomar());
        CHECK(r.cargaRed(0) == 6 && r.cargaRed(1) == 6, "12 sesiones: 6 y 6 en la red");
        CHECK(r.cargaTrabajo(0) == 4 && r.cargaTrabajo(1) == 4 && r.cargaTrabajo(2) == 4, "4, 4 y 4 en el trabajo");
        // La primera sube a 720p: no se muda, pero las próximas van a la otra red.
        const ca::Lugar antes = ls[0];
        r.cambiarPeso(ls[0], ca::kPesoAlta);
        CHECK(ls[0].red == antes.red && ls[0].trabajo == antes.trabajo, "subir el peso no muda la sesión");
        CHECK(r.cargaRed(antes.red) == 6 + ca::kPesoAlta - 1, "su red ahora pesa más");
        const ca::Lugar nueva = r.tomar();
        CHECK(nueva.red != antes.red, "la próxima va a la red menos cargada");
        ls.push_back(nueva);
        r.cambiarPeso(ls[0], ca::kPesoNormal);
        CHECK(r.cargaRed(antes.red) == 6, "bajar el peso devuelve la carga");
        int total = 0;
        for (auto& l : ls) {
            r.soltar(l);
            CHECK(!l.valido(), "soltar deja el lugar vacío");
        }
        total = r.cargaRed(0) + r.cargaRed(1) + r.cargaTrabajo(0) + r.cargaTrabajo(1) + r.cargaTrabajo(2);
        CHECK(total == 0 && r.sesionesRed(0) == 0 && r.sesionesRed(1) == 0, "soltar todo deja todo en cero");
        ca::Lugar vacio;
        r.soltar(vacio); // no pasa nada
        CHECK(r.cargaRed(0) == 0, "soltar un lugar vacío no cambia nada");
    }

    // R/T = 1 y R/T = 3: la misma carga total, repartida.
    {
        ca::Reparto uno(ca::Tamanos{1, 1}), tres(ca::Tamanos{3, 3});
        for (int i = 0; i < 9; ++i) {
            uno.tomar(i % 3 == 0 ? ca::kPesoAlta : ca::kPesoNormal);
            tres.tomar(i % 3 == 0 ? ca::kPesoAlta : ca::kPesoNormal);
        }
        const int totalUno = uno.cargaRed(0), totalTres = tres.cargaRed(0) + tres.cargaRed(1) + tres.cargaRed(2);
        CHECK(totalUno == totalTres, "R = 1 y R = 3 suman la misma carga");
        int maxTres = 0, minTres = 1 << 30;
        for (int i = 0; i < 3; ++i) {
            maxTres = std::max(maxTres, tres.cargaRed(i));
            minTres = std::min(minTres, tres.cargaRed(i));
        }
        CHECK(maxTres - minTres <= ca::kPesoAlta, "con R = 3 la carga queda pareja (dentro de una sesión de 720p)");
        const auto e = tres.estado();
        CHECK(e["red"].size() == 3 && e["trabajo"].size() == 3 && e["red"][0].value("hilo", "") == "ba-rtc-net/1",
              "estado: las capas con su hilo");
    }

    // Sin cruzar: el trabajo de cada sesión es el de su red.
    {
        ca::Reparto r(ca::Tamanos{2, 5}, false);
        CHECK(r.tamanos().trabajo == 2 && !r.cruzado(), "sin cruzar: T = R");
        for (int i = 0; i < 6; ++i) {
            const ca::Lugar l = r.tomar();
            CHECK(l.trabajo == l.red, "sin cruzar: trabajo == red");
        }
    }

    // Varios hilos a la vez.
    {
        ca::Reparto r(ca::Tamanos{3, 2});
        std::vector<std::thread> hs;
        for (int k = 0; k < 8; ++k)
            hs.emplace_back([&r] {
                for (int i = 0; i < 2000; ++i) {
                    ca::Lugar l = r.tomar(1 + i % 4);
                    r.cambiarPeso(l, 1 + (i + 1) % 4);
                    r.soltar(l);
                }
            });
        for (auto& h : hs) h.join();
        CHECK(r.cargaRed(0) + r.cargaRed(1) + r.cargaRed(2) == 0 && r.cargaTrabajo(0) + r.cargaTrabajo(1) == 0,
              "varios hilos tomando y soltando: todo en cero");
    }

    std::printf(fallas ? "test_capas_rtc: %d fallas\n" : "test_capas_rtc: todo ok\n", fallas);
    return fallas ? 1 : 0;
}
