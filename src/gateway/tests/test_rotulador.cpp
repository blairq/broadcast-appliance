// ============================================================================
// src/gateway/tests/test_rotulador.cpp — el buzon de rotulos
//
//   1. Lo que A publica es lo que ve el celular.
//   2. Un encargo sin fuente no se acepta (no hay nada que hacer con el).
//   3. La clave no se guarda ni se le devuelve a nadie.
//   4. Los encargos se entregan una sola vez.
//   5. La cola tiene tope y se recorta por lo viejo.
//   6. Sin nadie que publique, `viva` es false: el celular tiene que poder
//      decir «la app no responde» en vez de mostrar una escena fantasma.
// ============================================================================

#include <cstdio>
#include <string>

#include "Rotulador.hpp"

using ba::gateway::Rotulador;
using json = nlohmann::json;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (0)

json unaFuente(int id) {
    return json{{"escena", "Camara 1"},
                {"fuentes", json::array({json{{"id", id}, {"fuente", "Zocalo"}}})}};
}

} // namespace

int main() {
    // 1 y 6.
    {
        Rotulador r;
        const json vacio = r.estado();
        CHECK(!vacio.value("viva", true), "sin nadie que publique, no esta viva");

        r.publicar(unaFuente(7));
        const json e = r.estado();
        CHECK(e.value("escena", std::string()) == "Camara 1", "la escena que publico A");
        CHECK(e["fuentes"].size() == 1, "y sus fuentes");
        CHECK(e.value("viva", false), "recien publicada, esta viva");
    }

    // 2, 3 y 4.
    {
        Rotulador r;
        CHECK(!r.encargar(json{{"accion", "aire"}}), "un encargo sin fuente no sirve");
        CHECK(!r.encargar(json("hola")), "ni uno que no es un objeto");
        CHECK(r.pendientes() == 0, "y ninguno de los dos se encola");

        CHECK(r.encargar(json{{"id", 7}, {"accion", "aire"}, {"clave", "secreta"}}),
              "un encargo con fuente si");
        CHECK(r.pendientes() == 1, "queda esperando a que A lo venga a buscar");

        const json tomados = r.tomarEncargos();
        CHECK(tomados.size() == 1, "A se lo lleva");
        CHECK(!tomados[0].contains("clave"), "la clave no viaja de vuelta");
        CHECK(tomados[0].contains("enMs"), "pero si cuando se encargo");
        CHECK(r.pendientes() == 0, "y la cola queda vacia");
        CHECK(r.tomarEncargos().empty(), "un encargo se entrega UNA sola vez");
    }

    // 5: el tope. Si A no viene a buscarlos es que no esta, y guardar cien no
    // lo va a resucitar: lo que importa es el ultimo.
    {
        Rotulador r;
        for (int i = 0; i < 40; ++i) r.encargar(json{{"id", 1}, {"n", i}});
        CHECK(r.pendientes() == Rotulador::kMaxEncargos, "la cola no crece sin fin");
        const json t = r.tomarEncargos();
        CHECK(t.back().value("n", -1) == 39, "el ultimo encargo sobrevive siempre");
        CHECK(t.front().value("n", -1) == 40 - static_cast<int>(Rotulador::kMaxEncargos),
              "se recorta por lo viejo");
    }

    if (g_fallos == 0) std::printf("test_rotulador: todo bien\n");
    return g_fallos == 0 ? 0 : 1;
}
