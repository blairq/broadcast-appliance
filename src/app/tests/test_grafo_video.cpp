// test_grafo_video.cpp — el grafo de video del estudio (video/GrafoVideo), sin GPU
//
// Entradas como sub-grafos, tarjetas intercaladas o como rama, puertos a
// pedido del compositor, cables entre entradas, ciclos en todo el grafo, la
// migración del formato de antes (un grafo por entrada) y lo que recibe el
// filtro de cada entrada.

#include "video/GrafoVideo.hpp"

#include <clocale>
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

bool hay(const GrafoVideo& g, const std::string& d, const std::string& h, const std::string& entrada = "") {
    for (const auto& c : g.cables())
        if (c.desde == d && c.hacia == h && (entrada.empty() || c.entrada == entrada)) return true;
    return false;
}
} // namespace

int main() {
    GrafoVideo g;
    const std::string sony = "Sony PTZ", pelco = "Pelco", pana = "Panasonic";
    CHECK(g.asegurarEntrada(sony) && !g.asegurarEntrada(sony), "asegurarEntrada crea una vez");
    g.asegurarEntrada(pelco);
    g.asegurarEntrada(pana);
    const std::string es = GrafoVideo::idEntrada(sony), ss = GrafoVideo::idEscena(sony);
    CHECK(es == "sony-ptz.entrada" && ss == "sony-ptz.escena", "ids legibles por entrada");
    CHECK(hay(g, es, ss) && g.trivial(sony), "de fábrica: entrada -> escena");
    CHECK(g.entradas().size() == 3, "tres entradas, tres sub-grafos");
    CHECK(g.nodo(GrafoVideo::idEntrada(pelco))->y > g.nodo(es)->y, "cada entrada en su franja");

    std::string e;
    const std::string col = g.agregar("color", sony, "", json::object(), &e);
    CHECK(col == "sony-ptz.color" && hay(g, es, col) && hay(g, col, ss) && !hay(g, es, ss), "color se intercala antes de la escena");
    const std::string cro = g.agregar("croma", sony, col, json{{"similitud", 0.5}}, &e);
    CHECK(hay(g, col, cro) && hay(g, cro, ss) && g.nodo(cro)->params.value("similitud", 0.0) == 0.5, "croma después de color");
    const std::string sal = g.agregar("salida", sony, es, json{{"nombre", "Sony limpia"}}, &e);
    CHECK(hay(g, es, sal) && hay(g, es, col), "la salida es una rama");
    CHECK(g.nodo(sal)->grupo == sony && !g.trivial(sony), "las tarjetas son del sub-grafo de su entrada");
    CHECK(g.agregar("obs", sony, "", json::object(), &e).empty(), "un filtro de OBS sin `filtro`: error");
    const std::string ob = g.agregar("obs", pelco, "", json{{"filtro", "color_filter_v2"}, {"ajustes", {{"saturation", 0.2}}}}, &e);
    CHECK(!ob.empty() && hay(g, GrafoVideo::idEntrada(pelco), ob), "una tarjeta de OBS en Pelco");
    CHECK(g.agregar("color", "No existe", "", json::object(), &e).empty(), "en una entrada que no está: error");
    CHECK(g.agregar("color", sony, ss, json::object(), &e).empty(), "de la escena no se cuelga nada");

    // Compositor (del estudio) con entradas a pedido, de dos cámaras.
    const std::string comp = g.agregar("compositor", "", "", json::object(), &e);
    CHECK(comp == "estudio.compositor" && g.nodo(comp)->grupo.empty(), "el compositor es del estudio");
    CHECK(g.puertosEntrada(*g.nodo(comp)).size() == 1 && g.puertosEntrada(*g.nodo(comp))[0].pedido, "un puerto libre a pedido");
    CHECK(g.conectar(cro, "", comp, "", &e) == "in_0", "primera entrada: in_0");
    CHECK(g.conectar(GrafoVideo::idEntrada(pelco), "", comp, "", &e) == "in_1", "segunda: in_1 (entre entradas distintas)");
    CHECK(g.puertosEntrada(*g.nodo(comp)).size() == 3, "dos conectados y uno libre");
    CHECK(g.conectar(GrafoVideo::idEntrada(pana), "", comp, "in_1", &e) == "in_1" &&
              !hay(g, GrafoVideo::idEntrada(pelco), comp) && hay(g, GrafoVideo::idEntrada(pana), comp, "in_1"),
          "conectar a un puerto ocupado lo reemplaza");
    CHECK(g.conectar(comp, "", GrafoVideo::idEscena(pelco), "", &e) == "in" && hay(g, comp, GrafoVideo::idEscena(pelco)),
          "el compositor puede alimentar la escena de otra entrada");
    CHECK(!g.trivial(pelco), "con algo de afuera en su escena no es trivial");

    // Ciclos en todo el grafo, aunque crucen entradas.
    CHECK(g.conectar(comp, "", col, "", &e).empty() && !e.empty(), "un ciclo que cruza entradas se rechaza");
    CHECK(g.conectar(ss, "", comp, "", &e).empty(), "la escena no tiene salida");
    CHECK(g.conectar(cro, "", es, "", &e).empty(), "a la entrada no le llega nada");

    // Quitar: un filtro se puentea; un compositor solo suelta sus cables.
    CHECK(g.quitar(col, &e) && hay(g, es, cro) && g.nodo(col) == nullptr, "quitar un filtro une lo de antes con lo de después");
    CHECK(!g.quitar(es, &e) && !g.quitar(ss, &e), "entrada y escena no se quitan");

    // Ajustar: los ajustes de OBS y las capas del compositor se mezclan.
    CHECK(g.ajustar(ob, json{{"ajustes", {{"gamma", 0.1}}}}, &e) &&
              g.nodo(ob)->params["ajustes"].value("saturation", 0.0) == 0.2 && g.nodo(ob)->params["ajustes"].value("gamma", 0.0) == 0.1,
          "los ajustes de OBS se mezclan clave por clave");
    CHECK(g.ajustar(comp, json{{"capas", {{"in_0", {{"x", 0.5}}}}}}, &e) &&
              g.ajustar(comp, json{{"capas", {{"in_0", {{"ancho", 0.5}}}}}}, &e) &&
              g.nodo(comp)->params["capas"]["in_0"].value("x", 0.0) == 0.5,
          "las capas del compositor se mezclan por puerto");
    CHECK(!g.ajustar(cro, json{{"suavidad", "mucha"}}, &e), "un parámetro de otro tipo: error");

    // Lo que recibe el filtro de cada entrada.
    const json fs = g.paraFiltro(sony);
    bool tieneComp = false, tieneSalida = false;
    for (const auto& n : fs["nodos"]) {
        if (n.value("tipo", "") == "compositor") tieneComp = true;
        if (n.value("id", "") == sal) tieneSalida = true;
    }
    CHECK(!tieneComp && tieneSalida, "el filtro de Sony: sus tarjetas y su salida, sin el compositor");
    bool aOut = false;
    for (const auto& c : fs["cables"])
        if (c.value("desde", "") == cro && c.value("hacia", "") == "out") aOut = true;
    CHECK(aOut, "el filtro de Sony: su croma llega a `out`");
    const json fp = g.paraFiltro(pelco);
    bool pelcoOut = false;
    for (const auto& c : fp["cables"])
        if (c.value("hacia", "") == "out") pelcoOut = true;
    CHECK(!pelcoOut, "la escena de Pelco la alimenta el compositor (el estudio), no su filtro");

    // Describir: el sub-grafo, con los de afuera marcados.
    const json d = g.describir(sony, true);
    bool compExterno = false;
    for (const auto& n : d["nodos"])
        if (n.value("id", "") == comp) compExterno = n.value("externo", false);
    CHECK(compExterno, "en el sub-grafo de Sony el compositor se ve como externo");

    // Guardar y leer.
    const GrafoVideo h = GrafoVideo::desde(g.aJson());
    CHECK(h.aJson() == g.aJson(), "guardar y leer da lo mismo");

    // Migración del formato de antes.
    const json viejo = {{"Cámara 1",
                         {{"version", 1},
                          {"nodos",
                           {{{"id", "in"}, {"tipo", "in"}, {"x", 0}, {"y", 0}},
                            {{"id", "out"}, {"tipo", "out"}, {"x", 440}, {"y", 0}},
                            {{"id", "croma"}, {"tipo", "croma"}, {"x", 220}, {"y", 0}, {"params", {{"similitud", 0.6}}}},
                            {{"id", "salida"}, {"tipo", "salida"}, {"x", 220}, {"y", 140}, {"params", {{"nombre", "C1 limpia"}}}}}},
                          {"cables",
                           {{{"desde", "in"}, {"hacia", "croma"}},
                            {{"desde", "croma"}, {"hacia", "out"}},
                            {{"desde", "in"}, {"hacia", "salida"}}}}}}};
    const GrafoVideo m = GrafoVideo::desde(viejo);
    const std::string me = GrafoVideo::idEntrada("Cámara 1"), mss = GrafoVideo::idEscena("Cámara 1");
    CHECK(me == "camara-1.entrada", "el id pasa las tildes a su letra");
    CHECK(hay(m, me, "camara-1.croma") && hay(m, "camara-1.croma", mss) && hay(m, me, "camara-1.salida"), "migra nodos y cables");
    CHECK(m.nodo("camara-1.croma")->params.value("similitud", 0.0) == 0.6 && m.salidas().size() == 1, "migra los parámetros");

    // Con un locale como el de la app (no el «C» de los tests): los acentos
    // no rompen el UTF-8 del id (se caía al guardar el json).
    if (std::setlocale(LC_ALL, "es_AR.ISO8859-1") != nullptr || std::setlocale(LC_ALL, "en_US.ISO8859-1") != nullptr ||
        std::setlocale(LC_ALL, "") != nullptr) {
        GrafoVideo z;
        z.asegurarEntrada("Camara Cámara FaceTime HD");
        bool ok = true;
        try {
            (void)z.aJson().dump();
        } catch (...) {
            ok = false;
        }
        CHECK(ok && GrafoVideo::idEntrada("Camara Cámara FaceTime HD") == "camara-camara-facetime-hd.entrada",
              "con el locale de la app, el id sigue siendo ASCII");
        std::setlocale(LC_ALL, "C");
    }

    std::printf(fallos == 0 ? "test_grafo_video: OK\n" : "test_grafo_video: %d fallos\n", fallos);
    return fallos == 0 ? 0 : 1;
}
