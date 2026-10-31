// test_calidad_calor.cpp — sala/CalidadPorCalor con una sala falsa
//
// El paso «invitados» de la política térmica (F4-05): con la máquina muy caliente se
// le pide 720p a cada persona adentro y a las que entran; al aliviarse, la calidad
// completa a esas mismas (no a las que se fueron); una que se fue y vuelve se baja de
// nuevo.

#include "sala/CalidadPorCalor.hpp"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {
int fallos = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__); \
            ++fallos;                                                        \
        } else {                                                             \
            std::printf("ok: %s\n", msg);                                    \
        }                                                                    \
    } while (0)
} // namespace

int main() {
    using Pedido = std::pair<std::string, std::string>;
    std::vector<std::string> adentro{"ana", "beto"};
    std::vector<Pedido> pedidos;
    ba::app::sala::CalidadPorCalor c([&](const std::string& id, const std::string& que) { pedidos.emplace_back(id, que); },
                                     [&]() { return adentro; });

    c.revisar();
    CHECK(pedidos.empty(), "sin calor, la sala cambia y no se pide nada");
    CHECK(c.bajar(true) && c.baja(), "el gancho atiende el paso");
    CHECK((pedidos == std::vector<Pedido>{{"ana", "calidad-720p"}, {"beto", "calidad-720p"}}), "a los de adentro, 720p");
    pedidos.clear();
    c.revisar();
    CHECK(pedidos.empty(), "a cada uno una sola vez");
    adentro = {"ana", "carla"}; // se fue beto, entró carla
    c.revisar();
    CHECK((pedidos == std::vector<Pedido>{{"carla", "calidad-720p"}}), "la que entra también baja");
    pedidos.clear();
    adentro = {"ana", "carla", "beto"}; // beto vuelve
    c.revisar();
    CHECK((pedidos == std::vector<Pedido>{{"beto", "calidad-720p"}}), "el que vuelve se baja de nuevo");
    pedidos.clear();
    adentro = {"carla", "beto"}; // se fue ana
    CHECK(c.bajar(false) && !c.baja(), "se alivia");
    CHECK((pedidos == std::vector<Pedido>{{"beto", "calidad-max"}, {"carla", "calidad-max"}}),
          "la calidad completa a los que siguen (no a la que se fue)");
    pedidos.clear();
    adentro = {"dani"};
    c.revisar();
    CHECK(pedidos.empty(), "ya fresca, los que entran no se tocan");

    if (fallos == 0) std::printf("== test_calidad_calor: OK ==\n");
    return fallos == 0 ? 0 : 1;
}
