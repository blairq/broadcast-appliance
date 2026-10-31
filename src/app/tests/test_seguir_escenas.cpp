// ============================================================================
// src/app/tests/test_seguir_escenas.cpp — renombrar una escena no deja la armada ni la
// transición de entrada con el nombre viejo (escenas/SeguirEscenas.hpp)
// ============================================================================
#include "escenas/SeguirEscenas.hpp"

#include <cstdio>

using ba::app::escenas::seguirEscenas;
using json = nlohmann::json;

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
    json tr = {{"Cámara", {{"tipo", "fade"}, {"ms", 500}}}, {"Placa", {{"tipo", "cut"}, {"ms", 0}}}};
    std::vector<std::string> armadas{"Cámara", "Placa", ""};
    // Renombrar «Cámara» a «Plano general».
    auto s = seguirEscenas(tr, armadas, {{"Cámara", "Plano general"}}, {"Plano general", "Placa"});
    CHECK(!tr.contains("Cámara") && tr.contains("Plano general") && tr["Plano general"]["ms"] == 500 && s.transiciones,
          "la transición de entrada sigue a la escena renombrada (y hay que guardarla)");
    CHECK(armadas[0] == "Plano general" && s.armadas.size() == 1 && s.armadas[0] == 0,
          "la armada de esa salida toma el nombre nuevo (y se avisa solo a esa)");
    // Dos renombres seguidos antes del aviso: A → B → C.
    s = seguirEscenas(tr, armadas, {{"Plano general", "B"}, {"B", "C"}}, {"C", "Placa"});
    CHECK(tr.contains("C") && !tr.contains("B") && armadas[0] == "C", "dos renombres juntos, en orden");
    // Se borra «Placa»: su transición y la armada se van.
    s = seguirEscenas(tr, armadas, {}, {"C"});
    CHECK(!tr.contains("Placa") && armadas[1].empty() && s.transiciones && s.armadas.size() == 1,
          "lo de una escena borrada se saca");
    // Nada cambió: nada que guardar ni avisar.
    s = seguirEscenas(tr, armadas, {}, {"C"});
    CHECK(!s.transiciones && s.armadas.empty(), "sin cambios, nada que guardar");
    std::printf(fallas == 0 ? "test_seguir_escenas: OK\n" : "test_seguir_escenas: %d fallas\n", fallas);
    return fallas == 0 ? 0 : 1;
}
