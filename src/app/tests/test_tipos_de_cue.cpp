// ============================================================================
// src/app/tests/test_tipos_de_cue.cpp — una sola lista de tipos de cue (E-MCP27)
//
// El formulario de la página y `guion_cue` (su esquema y lo que avisa que un tipo no usa)
// salen de evento/TiposDeCue. Esto cuida que esa lista diga lo que el programa hace:
//
//   1. Están todos los tipos de evento::Tipo (y solo esos), en su orden.
//   2. Cada tipo acepta lo que su ejecución lee (CorridaViewModel::ejecutarCue): el
//      teleprompter, en qué prompter; la pantalla, tipo y pie; el gráfico, sus valores y el
//      vínculo; el carrusel, carrusel y lámina.
//   3. Los campos del formulario son parte de lo que acepta, sin repetidos.
// ============================================================================
#include <algorithm>
#include <cstdio>
#include <set>
#include <string>

#include "evento/Evento.hpp"
#include "evento/TiposDeCue.hpp"

namespace {
int g_fallos = 0;
#define CHECK(cond, msg)                                                                 \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);             \
            ++g_fallos;                                                                  \
        }                                                                                \
    } while (0)

bool acepta(const std::string& tipo, const std::string& campo) {
    const auto m = ba::evento::camposPorTipo();
    const auto it = m.find(tipo);
    return it != m.end() && std::find(it->second.begin(), it->second.end(), campo) != it->second.end();
}
} // namespace

int main() {
    using ba::evento::Tipo;
    // 1.
    const Tipo todos[] = {Tipo::Escena, Tipo::Musica, Tipo::Video,    Tipo::Teleprompter, Tipo::Orador, Tipo::Grafico,
                          Tipo::Carrusel, Tipo::Pantalla, Tipo::Audio, Tipo::Pausa,        Tipo::Espera, Tipo::Nota};
    const auto nombres = ba::evento::nombresDeTipos();
    CHECK(nombres.size() == std::size(todos), "un tipo de cue por cada evento::Tipo");
    for (std::size_t i = 0; i < std::size(todos) && i < nombres.size(); ++i)
        CHECK(nombres[i] == ba::evento::nombreDeTipo(todos[i]), "en el orden de evento::Tipo");

    // 2.
    CHECK(acepta("teleprompter", "prompter") && acepta("teleprompter", "discurso") && acepta("teleprompter", "arrancar"),
          "teleprompter: prompter, discurso, arrancar");
    CHECK(acepta("pantalla", "tipo") && acepta("pantalla", "pie") && acepta("pantalla", "contenido"), "pantalla: tipo y pie");
    CHECK(acepta("grafico", "valores") && acepta("grafico", "vinculo") && acepta("grafico", "seVa"), "gráfico: valores, vínculo");
    CHECK(acepta("carrusel", "carrusel") && acepta("carrusel", "lamina"), "carrusel: carrusel y lámina");
    CHECK(acepta("escena", "transicion") && acepta("escena", "ms"), "escena: su transición");
    CHECK(!acepta("nota", "escena"), "una nota no usa nada");

    // 3.
    for (const auto& t : ba::evento::tiposDeCue()) {
        std::set<std::string> vistos;
        for (const auto& c : t.campos) {
            CHECK(acepta(t.tipo, c), "lo del formulario se acepta");
            CHECK(vistos.insert(c).second, "sin repetidos");
        }
        for (const auto& c : t.avanzados) CHECK(vistos.insert(c).second, "sin repetidos entre formulario y avanzados");
    }

    if (g_fallos == 0) std::printf("test_tipos_de_cue: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
