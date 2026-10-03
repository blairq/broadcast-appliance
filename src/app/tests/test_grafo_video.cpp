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
        if (c.value("hacia", "") == "out") pelcoOut = c.value("desde", "") == "traer";
    CHECK(pelcoOut, "la escena de Pelco la alimenta el compositor: su filtro lo trae (`traer`)");

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

    // Controladores: UNA corrección de color para tres cámaras de marcas distintas.
    {
        GrafoVideo k;
        for (const char* c : {"Sony", "Pelco", "Panasonic"}) k.asegurarEntrada(c);
        std::string e;
        const std::string sony = k.agregar("gst", "Sony", "", json{{"elemento", "videobalance"}, {"propiedades", {{"saturation", 1.2}}}}, &e);
        const std::string ctl = k.controlarDesde(sony, &e);
        CHECK(!ctl.empty() && k.nodo(ctl)->tipo == "control" && k.nodo(ctl)->grupo.empty(), "controlarDesde hace un controlador del estudio");
        CHECK(k.controlador(sony) != nullptr && k.controlador(sony)->id == ctl, "la tarjeta queda manejada por él");
        const std::string pelco = k.agregarControlada(ctl, "Pelco", "", &e);
        const std::string pana = k.agregarControlada(ctl, "Panasonic", "", &e);
        CHECK(!pelco.empty() && !pana.empty() && k.controlador(pana)->id == ctl, "agregarControlada en las otras dos");
        CHECK(k.nodo(pelco)->etiqueta == k.nodo(sony)->etiqueta, "la controlada se llama como la original (sin «(todas)»)");
        // Cambiar el controlador cambia las tres.
        CHECK(k.ajustar(ctl, json{{"params", {{"propiedades", {{"saturation", 0.8}, {"hue", 0.1}}}}}}, &e), "ajustar el controlador");
        const auto sat = [&](const std::string& fuente) {
            const json pf = k.paraFiltro(fuente);
            for (const auto& n : pf["nodos"])
                if (n.value("tipo", "") == "gst") return n["params"]["propiedades"].value("saturation", -1.0);
            return -1.0;
        };
        CHECK(sat("Sony") == 0.8 && sat("Pelco") == 0.8 && sat("Panasonic") == 0.8, "las tres cámaras corren con lo del controlador");
        // Lo propio de una cámara (la Pelco tira a verde: un poco de matiz).
        CHECK(k.ajustar(pelco, json{{"local", {{"propiedades", {{"hue", -0.05}}}}}}, &e), "ajustar lo local");
        double hue = 0;
        const json pfPelco = k.paraFiltro("Pelco");
        for (const auto& n : pfPelco["nodos"])
            if (n.value("tipo", "") == "gst") hue = n["params"]["propiedades"].value("hue", 0.0);
        CHECK(hue == -0.05 && sat("Pelco") == 0.8, "lo local pisa solo lo suyo");
        CHECK(!k.paraFiltro("Pelco").dump().empty() && k.paraFiltro("Pelco").dump().find("\"local\"") == std::string::npos,
              "el filtro no recibe `local`");
        // El cable de control no es de video: no se mete en el pipeline.
        CHECK(k.paraFiltro("Sony")["cables"].size() == 2, "el filtro de Sony: in -> tarjeta -> out (sin el control)");
        // Tipos de puerto: control solo a control, y del mismo tipo de tarjeta.
        const std::string croma = k.agregar("croma", "Sony", "", json::object(), &e);
        CHECK(k.conectar(ctl, "ctl", croma, "ctl", &e).empty(), "un controlador de videobalance no maneja un croma");
        CHECK(k.conectar(ctl, "ctl", croma, "in", &e).empty(), "un cable de control no va a una entrada de video");
        CHECK(k.conectar(GrafoVideo::idEntrada("Sony"), "out", sony, "ctl", &e).empty(), "un cable de video no va a un control");
        // Quitar una tarjeta manejada la puentea igual (el cable de control no cuenta).
        CHECK(k.quitar(pana, &e) && hay(k, GrafoVideo::idEntrada("Panasonic"), GrafoVideo::idEscena("Panasonic")),
              "quitar una tarjeta manejada la puentea");
        // Soltar: se queda con lo que usaba.
        CHECK(k.soltar(pelco, &e) && k.controlador(pelco) == nullptr &&
                  k.nodo(pelco)->params["propiedades"].value("saturation", 0.0) == 0.8,
              "soltar deja la tarjeta con los valores que usaba");
        // Guardar y leer.
        const GrafoVideo r = GrafoVideo::desde(k.aJson());
        CHECK(r.controlador(sony) != nullptr && r.nodo(ctl)->params["params"]["propiedades"].value("saturation", 0.0) == 0.8,
              "el controlador y su cable se guardan");
    }

    // Lo que cruza entradas: tomas, el compositor y traer.
    {
        GrafoVideo k;
        for (const char* c : {"Sony", "Pelco", "Panasonic"}) k.asegurarEntrada(c);
        std::string e;
        const std::string comp = k.agregar("compositor", "", "", json::object(), &e);
        const std::string croma = k.agregar("croma", "Sony", "", json::object(), &e);
        k.conectar(croma, "out", comp, "", &e);                               // in_0: el croma de Sony
        k.conectar(GrafoVideo::idEntrada("Pelco"), "out", comp, "", &e);       // in_1: la Pelco tal cual
        k.conectar(GrafoVideo::idEntrada("Panasonic"), "out", comp, "", &e);   // in_2
        const json pf = k.paraFiltro("Sony");
        bool toma = false;
        for (const auto& n : pf["nodos"])
            if (n.value("tipo", "") == "toma" && n["params"].value("nombre", "") == croma) toma = true;
        CHECK(toma, "Sony publica la toma de su croma (lo que sale hacia el compositor)");
        bool tomaPelco = false;
        const json pfPelco = k.paraFiltro("Pelco");
        for (const auto& c : pfPelco["cables"])
            if (c.value("desde", "") == "in" && c.value("hacia", "").rfind("toma:", 0) == 0) tomaPelco = true;
        CHECK(tomaPelco, "Pelco publica su entrada tal cual");
        const json cfg = k.configCompositor(comp);
        CHECK(cfg["capas"].size() == 3 && cfg["capas"][0].value("toma", "") == croma && cfg["capas"][1].value("fuente", "") == "Pelco",
              "el compositor: tres capas, en orden, con su toma y su entrada");
        CHECK(cfg["capas"][2].value("x", -1.0) == 0.0 && cfg["capas"][2].value("y", -1.0) == 0.5 &&
                  cfg["capas"][1].value("x", -1.0) == 0.5 && cfg["capas"][0].value("ancho", 0.0) == 0.5,
              "sin posiciones: mosaico de 2x2");
        k.ajustar(comp, json{{"capas", {{"in_1", {{"x", 0.7}, {"y", 0.05}, {"ancho", 0.25}, {"alto", 0.25}, {"orden", 9}}}}}}, &e);
        const json cfg2 = k.configCompositor(comp);
        CHECK(cfg2["capas"][2].value("fuente", "") == "Pelco" && cfg2["capas"][2].value("x", 0.0) == 0.7,
              "una capa con posición y orden: arriba de todo, donde se pidió");
        // Volver al mosaico: null saca la posición.
        k.ajustar(comp, json{{"capas", {{"in_1", nullptr}}}}, &e);
        CHECK(k.configCompositor(comp)["capas"][1].value("x", -1.0) == 0.5, "null en una capa la vuelve al mosaico");
        // La escena de Panasonic recibe el compositor: su filtro lo trae.
        k.conectar(comp, "out", GrafoVideo::idEscena("Panasonic"), "in", &e);
        bool traer = false;
        const json pfPana = k.paraFiltro("Panasonic");
        for (const auto& n : pfPana["nodos"])
            if (n.value("tipo", "") == "traer" && n["params"].value("compositor", "") == comp &&
                n["params"].value("config", "").find("\"capas\"") != std::string::npos)
                traer = true;
        CHECK(traer, "la escena alimentada por el compositor: `traer` con su configuración");
        CHECK(!k.trivial("Panasonic"), "Panasonic ya no es trivial");
    }
    // Quitar una entrada: sus nodos y cables se van.
    {
        GrafoVideo k;
        k.asegurarEntrada("A");
        k.asegurarEntrada("B");
        k.quitarEntrada("A");
        CHECK(k.entradas().size() == 1 && k.nodo(GrafoVideo::idEntrada("A")) == nullptr && k.cables().size() == 1,
              "quitarEntrada saca el sub-grafo");
    }
    // Rostros: un filtro más (la imagen pasa), con lo que Personas necesita.
    {
        GrafoVideo k;
        k.asegurarEntrada("Cam");
        std::string e;
        const std::string r = k.agregar("rostros", "Cam", "", json{{"uso", "puerta"}, {"auto", true}}, &e);
        const json pf = k.paraFiltro("Cam");
        bool ok = false;
        for (const auto& n : pf["nodos"])
            if (n.value("tipo", "") == "rostros")
                ok = n["params"].value("uso", "") == "puerta" && n["params"].value("cada", 0) == 500 && n["params"].value("auto", false);
        CHECK(!r.empty() && ok && pf["cables"].size() == 2, "rostros: tarjeta intercalada, con uso, cada y auto");
        CHECK(k.quitar(r, &e) && k.trivial("Cam"), "rostros: al quitarla, la entrada vuelve a como vino");
    }
    // El retardo es un filtro más.
    {
        GrafoVideo k;
        k.asegurarEntrada("Cam");
        std::string e;
        const std::string r = k.agregar("retardo", "Cam", "", json{{"cuadros", 3}}, &e);
        CHECK(!r.empty() && k.paraFiltro("Cam")["nodos"].size() == 3, "retardo: una tarjeta intercalada");
    }

    std::printf(fallos == 0 ? "test_grafo_video: OK\n" : "test_grafo_video: %d fallos\n", fallos);
    return fallos == 0 ? 0 : 1;
}
