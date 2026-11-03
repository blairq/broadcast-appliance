// ============================================================================
// src/gateway/tests/test_cps_segun_carga.cpp — los cps de la capa baja según la carga
// (CpsSegunCarga.hpp)
//
//   1. carga(): la mayor de lo que se sabe; los perdidos cuentan como 100 o 0; sin nada, -1.
//   2. El controlador baja un escalón con la carga alta SOSTENIDA, espera entre cambios, no
//      pasa del último; entre los umbrales no se mueve (histéresis); sube con la carga baja
//      sostenida más tiempo; sin medida vuelve de a poco; apagado vuelve a 0 enseguida.
//   3. repartir(): primero la mitad menos importante (las celdas más chicas), después todos;
//      orden estable; los niveles 15 → 10 → 7,5.
// ============================================================================

#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "CpsSegunCarga.hpp"

using namespace ba::gateway;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (0)

void carga() {
    MedidaCarga m;
    CHECK(ControlCps::carga(m, 0, 5) < 0, "sin medidas: no se sabe");
    m.motorPct = 62;
    CHECK(ControlCps::carga(m, 0, 5) == 62, "el motor de la placa");
    m.perdidosPct = 1;
    CHECK(ControlCps::carga(m, 0, 5) == 62, "pocos perdidos: no suman");
    m.perdidosPct = 12;
    CHECK(ControlCps::carga(m, 0, 5) == 100, "muchos perdidos: saturado");
    CHECK(ControlCps::carga(m, 0, 0) == 62, "perdidos apagado (tope 0)");
    MedidaCarga d;
    d.decodificarMs = 15;
    CHECK(ControlCps::carga(d, 0, 5) < 0, "el tiempo de decodificar sin presupuesto no cuenta");
    CHECK(ControlCps::carga(d, 30, 5) == 50, "con presupuesto de 30 ms, 15 ms es el 50 %");
    d.motorPct = 130;
    CHECK(ControlCps::carga(d, 0, 5) == 100, "nunca más de 100");
}

void histeresis() {
    ControlCps c;
    AjustesCps a; // 85 / 70, sostener 2 s, recuperar 10 s, espera 3 s, 15/10/7,5 en 2 partes
    c.ajustes(a);
    const MedidaCarga m;
    CHECK(c.escalonMax() == 4, "dos niveles por debajo de 15, de a dos partes: 4 escalones");
    std::int64_t t = 1000;
    CHECK(!c.medir(90, m, t), "alta, pero recién empieza");
    CHECK(!c.medir(90, m, t + 1000), "un segundo: todavía no");
    CHECK(c.medir(90, m, t + 2000) && c.escalon() == 1, "sostenida 2 s: baja un escalón");
    CHECK(!c.medir(95, m, t + 4000), "sigue alta pero no pasó la espera entre cambios (3 s)");
    CHECK(c.medir(95, m, t + 5000) && c.escalon() == 2, "pasada la espera: otro escalón");
    // Entre los umbrales: quieto.
    for (int i = 0; i < 30; ++i) CHECK(!c.medir(78, m, t + 6000 + i * 1000), "entre 70 y 85 no se mueve");
    CHECK(c.escalon() == 2, "y quedó donde estaba");
    // Carga baja, sostenida: sube de a uno, despacio.
    t = 100000;
    CHECK(!c.medir(50, m, t), "baja, recién empieza");
    CHECK(!c.medir(50, m, t + 9000), "9 s: todavía no (recuperar es prudente)");
    CHECK(c.medir(50, m, t + 10000) && c.escalon() == 1, "10 s: sube un escalón");
    CHECK(!c.medir(50, m, t + 15000), "el siguiente, otros 10 s después");
    CHECK(c.medir(50, m, t + 20000) && c.escalon() == 0, "y vuelve a 15 para todos");
    CHECK(!c.medir(10, m, t + 40000) && c.escalon() == 0, "no baja de 0");
    // Un pico de un segundo no hace nada.
    t = 200000;
    CHECK(!c.medir(99, m, t) && !c.medir(60, m, t + 1000) && !c.medir(99, m, t + 2000), "un pico suelto no cuenta");
    CHECK(c.escalon() == 0, "sigue en 0");
    // Hasta el fondo y no más.
    t = 300000;
    for (int i = 0; i <= 40; ++i) c.medir(100, m, t + i * 1000);
    CHECK(c.escalon() == c.escalonMax(), "saturado mucho tiempo: el último escalón");
    // Sin medida: cuenta como baja (no se degrada a ciegas).
    t = 400000;
    for (int i = 0; i <= 60; ++i) c.medir(-1, m, t + i * 1000);
    CHECK(c.escalon() == 0, "sin medida, vuelve de a poco a 15");
    // Apagado: a 0 en el acto.
    t = 500000;
    for (int i = 0; i <= 10; ++i) c.medir(100, m, t + i * 1000);
    CHECK(c.escalon() > 0, "bajó de nuevo");
    a.activo = false;
    c.ajustes(a);
    CHECK(c.medir(100, m, t + 20000) && c.escalon() == 0, "apagado: todos a 15");
    const auto e = c.estado();
    CHECK(e["decisiones"].is_array() && !e["decisiones"].empty() && e["decisiones"].back()["motivo"] == "apagado",
          "las decisiones quedan en el estado");
}

void repartir() {
    const std::vector<double> niveles{15, 10, 7.5};
    std::vector<CandidatoCps> c;
    for (int i = 0; i < 6; ++i) c.push_back({"p" + std::to_string(i), (i + 1) * 100}); // p0 la más chica
    auto r = ControlCps::repartir(c, 0, niveles, 2);
    for (const auto& [k, v] : r) CHECK(v == 15, "escalón 0: todos a 15");
    r = ControlCps::repartir(c, 1, niveles, 2);
    CHECK(r["p0"] == 10 && r["p1"] == 10 && r["p2"] == 10, "escalón 1: la mitad más chica a 10");
    CHECK(r["p3"] == 15 && r["p4"] == 15 && r["p5"] == 15, "la mitad más grande sigue en 15");
    r = ControlCps::repartir(c, 2, niveles, 2);
    for (const auto& [k, v] : r) CHECK(v == 10, "escalón 2: todos a 10");
    r = ControlCps::repartir(c, 3, niveles, 2);
    CHECK(r["p0"] == 7.5 && r["p2"] == 7.5 && r["p3"] == 10 && r["p5"] == 10, "escalón 3: la mitad chica a 7,5");
    r = ControlCps::repartir(c, 4, niveles, 2);
    for (const auto& [k, v] : r) CHECK(v == 7.5, "escalón 4: todos a 7,5");
    r = ControlCps::repartir(c, 9, niveles, 2);
    for (const auto& [k, v] : r) CHECK(v == 7.5, "más allá del último: el último");
    // Las tarjetas de la interfaz (alto 0) son las menos importantes.
    c.push_back({"tarjeta", 0});
    r = ControlCps::repartir(c, 1, niveles, 2);
    CHECK(r["tarjeta"] == 10 && r["p5"] == 15, "la tarjeta baja antes que la celda más grande");
    // Orden estable a igual alto (por id): no saltan de mitad entre vueltas.
    std::vector<CandidatoCps> iguales{{"b", 100}, {"a", 100}, {"d", 100}, {"c", 100}};
    r = ControlCps::repartir(iguales, 1, niveles, 2);
    CHECK(r["a"] == 10 && r["b"] == 10 && r["c"] == 15 && r["d"] == 15, "a igual alto, por id");
    CHECK(ControlCps::repartir({}, 3, niveles, 2).empty(), "sin candidatos, nada");
}

} // namespace

int main() {
    carga();
    histeresis();
    repartir();
    if (g_fallos == 0) std::printf("test_cps_segun_carga: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
