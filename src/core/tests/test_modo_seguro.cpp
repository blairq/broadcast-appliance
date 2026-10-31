// ============================================================================
// src/core/tests/test_modo_seguro.cpp — el modo seguro escalonado (F1-10)
//
//   1. Sin argumentos: nada apagado.
//   2. Nivel 1: superres, CLAP, Python, agentes y modelos; NDI y ventanas no; escenas sí.
//   3. Nivel 2: además NDI y ventanas; escenas sí.
//   4. Nivel 3 (y el viejo --safe-mode): escenas vacías.
//   5. --safe-off apaga una pieza aunque el nivel no llegue.
//   6. argumentos() y leerArgumentos() van y vuelven.
// ============================================================================
#include "fault/ModoSeguro.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {
int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (0)

ba::fault::ModoSeguro leer(std::vector<std::string> a) {
    std::vector<char*> v{const_cast<char*>("ba-app")};
    for (auto& s : a) v.push_back(s.data());
    return ba::fault::leerArgumentos(static_cast<int>(v.size()), v.data());
}
} // namespace

int main() {
    using ba::fault::Pieza;
    const auto nada = leer({"--restore"});
    CHECK(!nada.activo(), "1: nada");
    for (Pieza p : ba::fault::piezas()) CHECK(!nada.apagada(p), "1: ninguna apagada");

    const auto n1 = leer({"--safe-level=1"});
    CHECK(n1.apagada(Pieza::Superres) && n1.apagada(Pieza::ClapTerceros) && n1.apagada(Pieza::PluginsPython) &&
              n1.apagada(Pieza::Agentes) && n1.apagada(Pieza::Modelos),
          "2: las de nivel 1");
    CHECK(!n1.apagada(Pieza::Ndi) && !n1.apagada(Pieza::CapturaVentana), "2: NDI y ventanas siguen");
    CHECK(!n1.escenasVacias(), "2: escenas restauradas");

    const auto n2 = leer({"--safe-level=2"});
    CHECK(n2.apagada(Pieza::Ndi) && n2.apagada(Pieza::CapturaVentana) && !n2.escenasVacias(), "3");

    CHECK(leer({"--safe-level=3"}).escenasVacias(), "4: nivel 3");
    CHECK(leer({"--safe-mode"}).escenasVacias(), "4: --safe-mode");
    CHECK(leer({"--safe-level=9"}).nivel == 3, "4: tope");

    const auto off = leer({"--safe-level=1", "--safe-off=ndi,inventada"});
    CHECK(off.apagada(Pieza::Ndi) && !off.apagada(Pieza::CapturaVentana) && off.extra.size() == 1, "5");
    const auto soloOff = leer({"--safe-off=superres"});
    CHECK(soloOff.activo() && soloOff.apagada(Pieza::Superres) && !soloOff.apagada(Pieza::Agentes), "5: sin nivel");

    const auto ida = off.argumentos();
    CHECK(ida.size() == 2 && ida[0] == "--safe-level=1" && ida[1] == "--safe-off=ndi", "6: argumentos");
    const auto vuelta = leer(ida);
    CHECK(vuelta.nivel == 1 && vuelta.extra == off.extra, "6: vuelta");
    CHECK(!off.aviso().empty(), "aviso");

    std::printf(g_fallos == 0 ? "test_modo_seguro: OK\n" : "test_modo_seguro: %d fallos\n", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
