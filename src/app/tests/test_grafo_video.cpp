// test_grafo_video.cpp — el grafo de tarjetas de video (video/GrafoVideo), sin GPU
//
// Agregar intercalando o como rama, quitar uniendo, conectar sin ciclos y con
// una sola cosa por entrada, lo guardado que no cierra, y lo que recibe el
// filtro.

#include "video/GrafoVideo.hpp"

#include <cstdio>

using ba::app::video::GrafoVideo;
using json = nlohmann::json;

namespace {
int fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++fallos;                                                                                        \
        }                                                                                                    \
    } while (0)

bool hay(const GrafoVideo& g, const std::string& d, const std::string& h) {
    for (const auto& c : g.cables())
        if (c.desde == d && c.hacia == h) return true;
    return false;
}
} // namespace

int main() {
    GrafoVideo g;
    CHECK(g.trivial() && hay(g, "in", "out"), "de fábrica: in -> out");

    std::string e;
    const std::string col = g.agregar("color", "", json::object(), &e);
    CHECK(col == "color" && hay(g, "in", "color") && hay(g, "color", "out") && !hay(g, "in", "out"),
          "color se intercala antes de la escena");
    const std::string cro = g.agregar("croma", "color", json{{"similitud", 0.5}}, &e);
    CHECK(hay(g, "color", "croma") && hay(g, "croma", "out") && g.nodo("croma")->params.value("similitud", 0.0) == 0.5,
          "croma después de color, con su parámetro");
    const std::string sal = g.agregar("salida", "in", json{{"nombre", "Cam · limpia"}}, &e);
    CHECK(hay(g, "in", "salida") && hay(g, "in", "color"), "la salida es una rama: no corta la cadena");
    CHECK(g.salidas().size() == 1 && g.salidas()[0] == "Cam · limpia", "salidas() la nombra");
    CHECK(g.agregar("sepia", "", json::object(), &e).empty() && !e.empty(), "un tipo que no existe: error");
    CHECK(g.agregar("color", "out", json::object(), &e).empty(), "no se cuelga nada de la escena");

    CHECK(!g.conectar("croma", "color", &e), "un ciclo se rechaza");
    CHECK(!g.conectar("out", "color", &e) && !g.conectar("color", "in", &e), "out no tiene salida, in no tiene entrada");
    CHECK(g.conectar("in", "out", &e) && hay(g, "in", "out") && !hay(g, "croma", "out"),
          "conectar a una entrada ocupada reemplaza el cable");
    CHECK(g.conectar("croma", "out", &e) && !hay(g, "in", "out"), "y de vuelta");

    CHECK(g.quitar("color", &e) && hay(g, "in", "croma") && g.nodo("color") == nullptr, "quitar une lo de antes con lo de después");
    CHECK(!g.quitar("in", &e) && !g.quitar("out", &e), "in y out no se quitan");

    CHECK(g.ajustar("croma", json{{"suavidad", 0.2}, {"etiqueta", "Fondo verde"}}, &e) &&
              g.nodo("croma")->etiqueta == "Fondo verde",
          "ajustar parámetros y etiqueta");
    CHECK(!g.ajustar("croma", json{{"inventado", 1}}, &e), "un parámetro que no tiene: error");
    CHECK(!g.ajustar("croma", json{{"suavidad", "mucha"}}, &e), "un parámetro de otro tipo: error");

    // Guardar y leer.
    const GrafoVideo h = GrafoVideo::desde(g.aJson());
    CHECK(h.aJson() == g.aJson(), "guardar y leer da lo mismo");
    // Lo que no cierra se descarta.
    json roto = g.aJson();
    roto["nodos"].push_back({{"id", "x"}, {"tipo", "sepia"}});
    roto["cables"].push_back({{"desde", "croma"}, {"hacia", "croma"}});
    roto["cables"].push_back({{"desde", "fantasma"}, {"hacia", "out"}});
    const GrafoVideo r = GrafoVideo::desde(roto);
    CHECK(r.nodo("x") == nullptr && !hay(r, "croma", "croma") && hay(r, "croma", "out"), "lo guardado que no cierra se descarta");
    CHECK(GrafoVideo::desde(json{{"nodos", json::array()}}).nodo("in") != nullptr, "siempre quedan in y out");

    const json f = g.paraFiltro();
    CHECK(f["nodos"].size() == g.nodos().size() && f["cables"].size() == g.cables().size() && !f["nodos"][0].contains("x"),
          "lo que recibe el filtro: nodos y cables, sin posiciones");
    CHECK(GrafoVideo::catalogo().size() == 4, "el catálogo: color, croma, recorte, salida");
    (void)cro;
    (void)sal;
    std::printf(fallos == 0 ? "test_grafo_video: OK\n" : "test_grafo_video: %d fallos\n", fallos);
    return fallos == 0 ? 0 : 1;
}
