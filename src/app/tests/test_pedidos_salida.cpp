// ============================================================================
// src/app/tests/test_pedidos_salida.cpp — lo pedido de cada salida (emision/PedidosSalida)
//
// El caso del hallazgo: startRecording encolaba y devolvía true; si el hilo de salidas no
// podía arrancar la grabación, el motor la bajaba y el runtime no se enteraba: al-aire.json
// seguía diciendo «grabando» y tras un corte se retomaba algo que nunca grabó. Ahora lo
// pedido queda Pendiente hasta el resultado de ESA orden; si falló, deja de estar pedido.
//
// Y los de la tanda siguiente: pedir dos veces mientras está pendiente es la MISMA orden
// (antes la segunda quedaba en «Si» y la falla de la primera se tiraba: al-aire.json decía
// que grababa algo que nunca arrancó); el corte de luz marca «cerrada pero pedida» en un
// solo paso (antes había un momento en que el documento decía «no graba»); y el rearme por
// la GPU no retoma lo que cerró el corte de luz.
// ============================================================================
#include "emision/PedidosSalida.hpp"

#include <cstdio>

using ba::app::salidas::Pedidos;

namespace {
int fallas = 0;
#define CHECK(c, msg)                                                                     \
    do {                                                                                  \
        if (!(c)) {                                                                       \
            std::printf("FALLA: %s (%s, linea %d)\n", msg, #c, __LINE__);               \
            ++fallas;                                                                     \
        } else {                                                                          \
            std::printf("ok: %s\n", msg);                                                 \
        }                                                                                 \
    } while (0)
} // namespace

int main() {
    Pedidos p;
    // 1. Pedida: pendiente (cuenta como pedida: un corte ahora la retoma).
    const auto o1 = p.pedir("principal", true);
    CHECK(p.de("principal").graba == Pedidos::Estado::Pendiente && p.de("principal").grabaPedida(),
          "grabar queda pendiente y pedida hasta que el motor diga");
    // 2. El motor dice que falló: ya no está pedida (el documento de aire deja de decirlo).
    CHECK(p.resultado("principal", true, o1, false), "el resultado de la orden cambia lo pedido");
    CHECK(!p.de("principal").grabaPedida(), "una grabación que no arrancó deja de estar pedida");
    // 3. Un resultado viejo no pisa lo de ahora: se paró y se pidió de nuevo, llega el de la anterior.
    const auto o2 = p.pedir("principal", true);
    p.dejarDePedir("principal", true);
    const auto o3 = p.pedir("principal", true);
    CHECK(!p.resultado("principal", true, o2, false), "el resultado de una orden vieja no cuenta");
    CHECK(p.de("principal").graba == Pedidos::Estado::Pendiente, "sigue pendiente la última");
    CHECK(p.resultado("principal", true, o3, true) && p.de("principal").graba == Pedidos::Estado::Si,
          "la última orden que arrancó: grabando");
    // 4. Parada mientras estaba pendiente: el resultado que llega después no la revive.
    const auto o4 = p.pedir("sala-2", false);
    p.dejarDePedir("sala-2", false);
    CHECK(!p.resultado("sala-2", false, o4, true) && !p.de("sala-2").transmitePedida(),
          "parada antes del resultado: no vuelve a estar pedida");
    // 5. La por omisión es una salida más: grabar y transmitir de cada una, aparte.
    p.pedir("sala-2", true);
    CHECK(p.de("sala-2").grabaPedida() && !p.de("sala-2").transmitePedida() && p.de("principal").grabaPedida(),
          "cada salida con lo suyo");
    // 6. El corte de luz: cerrada pero pedida; al volver la energía se toma una vez.
    p.dejarDePedir("principal", true);
    p.cerrarPorEnergia("principal");
    CHECK(p.de("principal").grabaPedida(), "cerrada por el corte de luz: sigue pedida (vuelve)");
    const auto volver = p.tomarCerradasPorEnergia();
    CHECK(volver.size() == 1 && volver.front() == "principal" && p.tomarCerradasPorEnergia().empty(),
          "al volver la energía se retoma una sola vez");
    // 8. Doble pedido con la orden en camino (doble clic, el operador y el MCP a la vez): es
    //    la misma orden; si falla, deja de estar pedida aunque se haya pedido dos veces.
    {
        Pedidos q;
        const auto a = q.pedir("principal", true);
        const auto b = q.pedir("principal", true);
        CHECK(a == b, "pedir otra vez lo pendiente devuelve la misma orden");
        CHECK(q.resultado("principal", true, a, false) && !q.de("principal").grabaPedida(),
              "la falla de esa orden deja de estar pedida (no queda «Si» por el segundo pedido)");
        CHECK(!q.resultado("principal", true, b, true), "el segundo resultado de la misma orden no cambia nada");
        // Ya grabando (Si) y se pide otra vez: es otra orden, que contesta el motor.
        const auto c = q.pedir("principal", true);
        q.resultado("principal", true, c, true);
        const auto antes = q.de("principal");
        const auto d = q.pedir("principal", true);
        CHECK(d != c && q.de("principal").graba == Pedidos::Estado::Pendiente, "pedir lo que ya graba es una orden nueva");
        // Esa orden ni se pudo encolar: vuelve lo de antes (sigue grabando, no queda pendiente).
        q.devolver("principal", true, d, antes);
        CHECK(q.de("principal").graba == Pedidos::Estado::Si && q.de("principal").ordenGraba == c,
              "una orden que ni se encoló devuelve lo de antes");
    }
    // 9. El corte de luz: en UN paso deja de grabar y sigue pedida (el documento nunca dice
    //    «no graba» en el medio).
    {
        Pedidos q;
        q.resultado("principal", true, q.pedir("principal", true), true);
        q.cerrarPorEnergia("principal");
        const auto x = q.de("principal");
        CHECK(x.graba == Pedidos::Estado::No && x.cerradaPorEnergia && x.grabaPedida(),
              "cerrada por el corte de luz: no graba y sigue pedida, en un solo paso");
    }
    // 10. El rearme por la GPU durante el corte: lo demás se vuelve a pedir, lo que cerró el
    //     corte de luz sigue esperando la energía (no se retoma con la batería baja).
    {
        Pedidos q;
        q.resultado("principal", true, q.pedir("principal", true), true);
        q.resultado("sala-2", false, q.pedir("sala-2", false), true);
        q.cerrarPorEnergia("principal");
        const auto porEnergia = q.rearmar();
        CHECK(porEnergia.size() == 1 && porEnergia.front() == "principal", "el rearme dice cuáles cerró el corte de luz");
        CHECK(q.de("principal").cerradaPorEnergia && q.de("principal").graba == Pedidos::Estado::No,
              "lo cerrado por el corte de luz sigue esperando la energía (no se retoma)");
        CHECK(!q.de("sala-2").transmitePedida(), "lo demás se olvida (el rearme lo vuelve a pedir)");
        const auto volver = q.tomarCerradasPorEnergia();
        CHECK(volver.size() == 1 && volver.front() == "principal", "al volver la luz se retoma");
    }
    // 7. Los destinos: en una sola salida; sacarlos y olvidar la salida.
    p.ponerDestino("principal", "destino-1");
    p.ponerDestino("sala-2", "destino-1");
    CHECK(p.de("principal").destinos.empty() && p.de("sala-2").destinos.count("destino-1") == 1,
          "un destino sale con una sola salida");
    p.sacarDestino("todos");
    CHECK(p.de("sala-2").destinos.empty(), "«todos» los saca a todos");
    p.olvidar("sala-2");
    CHECK(!p.de("sala-2").grabaPedida(), "una salida borrada no vuelve");
    std::printf(fallas == 0 ? "test_pedidos_salida: OK\n" : "test_pedidos_salida: %d fallas\n", fallas);
    return fallas == 0 ? 0 : 1;
}
