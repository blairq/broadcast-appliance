// Las salidas guardadas (PLAN-SALIDAS, etapa 2; modelo, sin motor): Config/salidas.json
// con la por omisión siempre, el tope, crear/borrar/editar, la mezcla y el codificador de
// cada una, el `salida` de los destinos (y qué pasa con ellos al borrar su salida) y
// Runtime/al-aire.json por salida, que un archivo viejo se siga leyendo igual y que lo
// nuevo lo entienda un arranque viejo.
#include "emision/SalidasConfig.hpp"

#include <cstdio>

using namespace ba::app;
using json = nlohmann::json;

static int fallas = 0;
#define CHECK(c, m)                        \
    do {                                   \
        if (!(c)) {                        \
            std::printf("FALLA: %s\n", m); \
            ++fallas;                      \
        } else {                           \
            std::printf("ok: %s\n", m);    \
        }                                  \
    } while (0)

int main() {
    // --- salidas.json ---
    {
        const salidas::Lista l = salidas::leer(json());
        CHECK(l.salidas.size() == 1 && l.salidas[0].id == salidas::kPorOmision, "sin archivo: solo la por omisión");
        CHECK(l.tope == salidas::kTopeDeFabrica, "sin archivo: el tope de fábrica");
        CHECK(l.salidas[0].color == salidas::colorDeFabrica(0) && l.salidas[0].nombre == "principal", "la por omisión con nombre y color");
    }
    {
        // Un archivo que no nombra a la por omisión, con una repetida, una rota y desordenado.
        const json j = {{"tope", 3},
                        {"salidas", {{{"id", "b"}, {"nombre", "Pantalla"}, {"orden", 2}},
                                     {{"id", "a"}, {"color", "#123456"}, {"orden", 1}},
                                     {{"id", "b"}, {"nombre", "otra"}},
                                     {{"id", "mal:id"}},
                                     "nada"}}};
        const salidas::Lista l = salidas::leer(j);
        CHECK(l.tope == 3, "el tope del archivo");
        CHECK(l.salidas.size() == 3, "la por omisión se suma; la repetida y la rota no");
        CHECK(l.salidas[0].id == "principal" && l.salidas[1].id == "a" && l.salidas[2].id == "b", "la por omisión primera y las demás por orden");
        CHECK(l.salidas[1].color == "#123456" && l.salidas[2].color == salidas::colorDeFabrica(2), "el color elegido o el de su lugar");
        CHECK(l.salidas[2].nombre == "Pantalla" && l.salidas[1].nombre == "a", "el nombre (o el id)");
        // Ida y vuelta.
        const salidas::Lista otra = salidas::leer(salidas::aJson(l));
        CHECK(salidas::aJson(otra) == salidas::aJson(l), "guardar y volver a leer da lo mismo");
    }
    {
        salidas::Lista l = salidas::leer(json{{"tope", 2}});
        CHECK(!salidas::agregar(l, {"con espacio", "", "", 0, {}, {}}).empty(), "id con espacio: no");
        CHECK(!salidas::agregar(l, {"principal", "", "", 0, {}, {}}).empty(), "repetir la por omisión: no");
        CHECK(salidas::agregar(l, {"streaming2", "YouTube 2", "", 0, {}, {}}).empty(), "agregar una");
        CHECK(l.salidas.back().color == salidas::colorDeFabrica(1) && l.salidas.back().orden == 1, "con su color y al final");
        CHECK(!salidas::agregar(l, {"tercera", "", "", 0, {}, {}}).empty(), "pasar el tope: no");
        CHECK(salidas::editar(l, "streaming2", {{"nombre", "YT"}, {"color", "#00FF00"}}).empty() && l.buscar("streaming2")->nombre == "YT",
              "editar nombre y color");
        CHECK(!salidas::editar(l, "streaming2", {{"color", "verde"}}).empty(), "un color que no es #RRGGBB: no");
        CHECK(!salidas::quitar(l, "principal").empty() && !salidas::quitar(l, "").empty(), "la por omisión no se borra");
        CHECK(salidas::quitar(l, "streaming2").empty() && l.salidas.size() == 1, "borrar la otra");
        CHECK(!salidas::quitar(l, "streaming2").empty(), "borrar una que no está: no");
    }
    // --- La mezcla y el codificador de cada una ---
    {
        salidas::Lista l = salidas::leer(json{{"tope", 3}});
        CHECK(salidas::agregar(l, {"tv", "TV", "", 0, {}, {}}).empty(), "agregar la TV");
        CHECK(salidas::ponerMando(l, "tv", "Mic 1", {0.5, false}), "anotar el fader de una fuente en la TV");
        CHECK(!salidas::ponerMando(l, "tv", "Mic 1", {0.5, false}), "lo mismo otra vez: nada que guardar");
        CHECK(salidas::ponerMando(l, "tv", "Mic 1", {0.5, true}), "el mute");
        CHECK(!salidas::ponerMando(l, "principal", "Mic 1", {0.1, false}) && !salidas::ponerMando(l, "", "Mic 1", {0.1, false}),
              "la por omisión no (lo suyo va en la escena)");
        CHECK(!salidas::ponerMando(l, "nadie", "Mic 1", {0.1, false}), "una salida que no está: no");
        // El VCA de los grupos de su mezcla: el suyo (1 = sin anotar).
        CHECK(salidas::ponerGrupo(l, "tv", "Invitado (audio)", 0.5) && !salidas::ponerGrupo(l, "tv", "Invitado (audio)", 0.5),
              "el VCA de un grupo en la TV (otra vez igual: nada)");
        CHECK(salidas::ponerGrupo(l, "tv", "Mic", 2.0) && salidas::ponerGrupo(l, "tv", "Mic", 1.0) &&
                  l.buscar("tv")->grupos.count("Mic") == 0,
              "en 1 se saca");
        CHECK(!salidas::ponerGrupo(l, "principal", "Mic", 0.5), "la por omisión no (lo suyo va en la configuración)");
        CHECK(salidas::leer(salidas::aJson(l)).buscar("tv")->grupos.at("Invitado (audio)") == 0.5, "el VCA vuelve al leer");
        CHECK(salidas::editar(l, "tv", {{"codificador", "x264"}}).empty() && l.buscar("tv")->codificador == "x264",
              "el codificador de la TV");
        CHECK(!salidas::editar(l, "tv", {{"codificador", "h265-raro"}}).empty() && l.buscar("tv")->codificador == "x264",
              "un codificador que no existe: no, queda el de antes");
        CHECK(!salidas::editar(l, "principal", {{"codificador", "x264"}}).empty(), "la por omisión usa el de la configuración");
        const json j = salidas::aJson(l);
        const salidas::Lista otra = salidas::leer(j);
        const salidas::Salida* tv = otra.buscar("tv");
        CHECK(tv != nullptr && tv->codificador == "x264" && tv->mezcla.count("Mic 1") == 1 &&
                  tv->mezcla.at("Mic 1") == (salidas::Mando{0.5, true}),
              "la mezcla y el codificador vuelven al leer");
        CHECK(!j["salidas"][0].contains("mezcla") && !j["salidas"][0].contains("codificador"), "la por omisión no los lleva");
        CHECK(salidas::editar(l, "tv", {{"codificador", ""}}).empty() && l.buscar("tv")->codificador.empty(),
              "vacío: vuelve al de la configuración");
        // Un archivo tocado a mano: un volumen que no es número no entra; uno enorme, al tope.
        const salidas::Lista rara = salidas::leer(
            json{{"salidas", {{{"id", "tv"}, {"mezcla", {{"a", {{"volumen", "alto"}}}, {"b", {{"volumen", 99}, {"mute", true}}}}}}}}});
        CHECK(rara.buscar("tv")->mezcla.count("a") == 0 && rara.buscar("tv")->mezcla.at("b").volumen == 4.0,
              "mezcla rota: lo que no es número no entra; lo enorme, al tope");
    }
    // --- Los destinos de una salida que se borra: una decisión, nunca basura ---
    {
        salidas::Lista l = salidas::leer(json{{"tope", 4}});
        CHECK(salidas::agregar(l, {"tv", "TV", "", 0, {}, {}}).empty() && salidas::agregar(l, {"sala", "Sala", "", 0, {}, {}}).empty(),
              "dos salidas más");
        const json todos = json::array({json{{"id", "d1"}, {"salida", "tv"}}, json{{"id", "d2"}, {"salida", "tv"}},
                                        json{{"id", "d3"}}, json{{"id", "d4"}, {"salida", "sala"}}});
        CHECK(salidas::destinosDe(todos, "tv") == 2 && salidas::destinosDe(todos, "principal") == 1, "cuántos tiene cada una");
        json d = todos;
        std::vector<std::string> borrados;
        CHECK(!salidas::destinosDeBorrada(d, l, "tv", "", &borrados).empty() && d == todos,
              "con destinos y sin decir qué hacer: no, y no toca nada");
        CHECK(!salidas::destinosDeBorrada(d, l, "tv", "tv", &borrados).empty() && d == todos, "a sí misma: no");
        CHECK(!salidas::destinosDeBorrada(d, l, "tv", "nadie", &borrados).empty() && d == todos, "a una que no existe: no");
        CHECK(salidas::destinosDeBorrada(d, l, "tv", "sala", &borrados).empty() && salidas::destinosDe(d, "sala") == 3 &&
                  salidas::destinosDe(d, "tv") == 0 && borrados.empty() && d.size() == 4,
              "pasan a otra salida");
        d = todos;
        CHECK(salidas::destinosDeBorrada(d, l, "tv", "principal", &borrados).empty() && salidas::destinosDe(d, "principal") == 3 &&
                  !d[0].contains("salida"),
              "a la por omisión: sin campo");
        d = todos;
        CHECK(salidas::destinosDeBorrada(d, l, "tv", "borrar", &borrados).empty() && d.size() == 2 && borrados.size() == 2 &&
                  borrados[0] == "d1" && borrados[1] == "d2",
              "borrados (con sus ids, para sacar las claves)");
        d = todos;
        CHECK(salidas::destinosDeBorrada(d, l, "nada-que-ver", "", &borrados).empty() && d == todos, "sin destinos: nada que decidir");
    }
    // --- destinos.json ---
    {
        CHECK(salidas::salidaDeDestino(json{{"id", "d1"}}) == "principal", "destino sin salida: la por omisión");
        CHECK(salidas::salidaDeDestino(json{{"id", "d1"}, {"salida", ""}}) == "principal", "destino con salida vacía: la por omisión");
        CHECK(salidas::salidaDeDestino(json{{"id", "d1"}, {"salida", "b"}}) == "b", "destino de otra salida");
    }
    // --- al-aire.json ---
    {
        // El de antes (sin salidas): todo es de la por omisión.
        const json viejo = {{"streaming", true}, {"recording", false}, {"escena", "Plano"}, {"cerrado", false}};
        const salidas::AlAire a = salidas::leerAlAire(viejo);
        CHECK(!a.cerrado, "viejo: no se cerró");
        CHECK(a.de("").escena == "Plano" && a.de("principal").transmitiendo && !a.de("principal").grabando, "viejo: la por omisión");
        CHECK(a.de("b").escena.empty() && !a.de("b").grabando, "viejo: otra salida, nada");
        CHECK(salidas::leerAlAire(json{{"escena", "x"}}).cerrado, "viejísimo (sin «cerrado»): cierre ordenado");
        CHECK(salidas::leerAlAire(json("roto")).salidas.empty(), "roto: nada");
    }
    {
        salidas::AlAire a;
        a.cerrado = false;
        a.salidas["principal"] = {"Plano", true, true};
        a.salidas["b"] = {"Pantalla", false, true};
        const json j = salidas::aJson(a);
        // Lo que lee un arranque de antes de las salidas: los campos sueltos.
        CHECK(j.value("escena", std::string()) == "Plano" && j.value("streaming", false) && j.value("recording", false) &&
                  !j.value("cerrado", true),
              "nuevo: los campos de siempre son de la por omisión");
        const salidas::AlAire b = salidas::leerAlAire(j);
        CHECK(b.de("b").escena == "Pantalla" && b.de("b").grabando && !b.de("b").transmitiendo, "nuevo: vuelve cada salida como estaba");
        CHECK(b.de("principal").escena == "Plano" && b.de("principal").transmitiendo, "nuevo: la por omisión también");
    }
    {
        // F1-04: cada salida con su transmisión y sus destinos (vuelven todas tras un corte).
        salidas::AlAire a;
        a.cerrado = false;
        a.salidas["principal"] = {"Plano", true, false, {"respaldo"}};
        a.salidas["b"] = {"Pantalla", true, true, {"youtube-b", "twitch-b"}};
        const salidas::AlAire b = salidas::leerAlAire(salidas::aJson(a));
        CHECK(b.de("b").transmitiendo && b.de("b").grabando, "otra salida: transmite y graba");
        CHECK((b.de("b").destinos == std::vector<std::string>{"youtube-b", "twitch-b"}), "otra salida: sus destinos");
        CHECK((b.de("principal").destinos == std::vector<std::string>{"respaldo"}), "la por omisión: sus destinos");
        CHECK(salidas::leerAlAire(json{{"salidas", {{"b", {{"streaming", true}}}}}}).de("b").destinos.empty(),
              "sin `destinos` (de antes): ninguno");
    }
    std::printf(fallas == 0 ? "TODO OK\n" : "%d FALLAS\n", fallas);
    return fallas == 0 ? 0 : 1;
}
