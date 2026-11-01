// ============================================================================
// src/app/tests/test_buzon_salidas.cpp — lo pedido y el buzón del hilo de salidas
//
// emision/BuzonSalidas.hpp. Lo pedido de cada salida (transmitir, grabar, la señal limpia)
// es del dueño del motor y lleva el número de la orden que lo cambió; el hilo de salidas le
// cuenta por el buzón que una orden no arrancó. Antes los dos escribían lo pedido bajo un
// candado compartido, y una falla que llegaba tarde (de una orden ya reemplazada por parar y
// volver a pedir) bajaba lo pedido después: la interfaz decía «no graba» con la grabación
// nueva arrancando.
// ============================================================================
#include "emision/BuzonSalidas.hpp"

#include <cstdio>
#include <thread>
#include <vector>

using ba::app::BuzonSalidas;
using ba::app::MensajeSalida;
using ba::app::PedidoOrden;

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
    std::uint64_t ordenes = 0;
    // 1. Pedido y no arrancó: deja de estar pedido.
    PedidoOrden g;
    const std::uint64_t o1 = g.poner(true, ordenes);
    CHECK(g.si && g.orden == o1, "pedir lo deja pedido con su orden");
    g.noArranco(o1);
    CHECK(!g.si, "la orden que no arrancó baja lo pedido");

    // 2. El caso del hallazgo: pedir, parar y volver a pedir; la falla de la PRIMERA llega
    //    tarde y no baja la segunda.
    const std::uint64_t a = g.poner(true, ordenes);
    g.poner(false, ordenes);
    const std::uint64_t b = g.poner(true, ordenes);
    g.noArranco(a);
    CHECK(g.si && g.orden == b, "una falla vieja no baja lo pedido después");
    g.noArranco(b);
    CHECK(!g.si, "la falla de la orden en curso sí lo baja");

    // 3. Parar también es una orden: una falla de lo que se paró no cambia nada.
    const std::uint64_t c = g.poner(true, ordenes);
    g.poner(false, ordenes);
    g.noArranco(c);
    CHECK(!g.si, "parado sigue parado aunque llegue la falla de antes");

    // 4. El buzón entrega en el orden en que llegó y queda vacío.
    BuzonSalidas buzon;
    buzon.poner({MensajeSalida::Tipo::NoGraba, "principal", {}, 7});
    buzon.poner({MensajeSalida::Tipo::Aac, "principal", {}, 0, 96});
    auto m = buzon.tomar();
    CHECK(m.size() == 2 && m[0].tipo == MensajeSalida::Tipo::NoGraba && m[0].orden == 7 && m[1].kbps == 96,
          "el buzón entrega en orden");
    CHECK(buzon.tomar().empty(), "después de tomar queda vacío");

    // 5. Desde varios hilos (el de salidas y el de un emisor que se adopta): no se pierde nada
    //    y cada productor conserva su orden.
    constexpr int kPorHilo = 2000;
    std::vector<std::thread> hilos;
    for (int h = 0; h < 4; ++h)
        hilos.emplace_back([&buzon, h]() {
            for (int i = 0; i < kPorHilo; ++i)
                buzon.poner({MensajeSalida::Tipo::ExtraNoArranco, "s" + std::to_string(h), {}, static_cast<std::uint64_t>(i)});
        });
    std::vector<MensajeSalida> todos;
    while (todos.size() < 4u * kPorHilo) {
        auto parte = buzon.tomar();
        todos.insert(todos.end(), parte.begin(), parte.end());
    }
    for (auto& t : hilos) t.join();
    bool enOrden = true;
    std::uint64_t ultimo[4] = {0, 0, 0, 0};
    bool primero[4] = {true, true, true, true};
    for (const auto& x : todos) {
        const int h = x.salida[1] - '0';
        if (!primero[h] && x.orden != ultimo[h] + 1) enOrden = false;
        primero[h] = false;
        ultimo[h] = x.orden;
    }
    CHECK(todos.size() == 4u * kPorHilo && enOrden, "varios hilos: llega todo y cada uno en su orden");

    std::printf("%s\n", fallas == 0 ? "TODO BIEN" : "HAY FALLAS");
    return fallas == 0 ? 0 : 1;
}
