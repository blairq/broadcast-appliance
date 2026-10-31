// Las salidas guardadas (PLAN-SALIDAS, etapa 2; modelo, sin motor): Config/salidas.json
// con la por omisión siempre, el tope, crear/borrar/editar, el `salida` de los destinos y
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
        CHECK(!salidas::agregar(l, {"con espacio", "", "", 0}).empty(), "id con espacio: no");
        CHECK(!salidas::agregar(l, {"principal", "", "", 0}).empty(), "repetir la por omisión: no");
        CHECK(salidas::agregar(l, {"streaming2", "YouTube 2", "", 0}).empty(), "agregar una");
        CHECK(l.salidas.back().color == salidas::colorDeFabrica(1) && l.salidas.back().orden == 1, "con su color y al final");
        CHECK(!salidas::agregar(l, {"tercera", "", "", 0}).empty(), "pasar el tope: no");
        CHECK(salidas::editar(l, "streaming2", {{"nombre", "YT"}, {"color", "#00FF00"}}).empty() && l.buscar("streaming2")->nombre == "YT",
              "editar nombre y color");
        CHECK(!salidas::editar(l, "streaming2", {{"color", "verde"}}).empty(), "un color que no es #RRGGBB: no");
        CHECK(!salidas::quitar(l, "principal").empty() && !salidas::quitar(l, "").empty(), "la por omisión no se borra");
        CHECK(salidas::quitar(l, "streaming2").empty() && l.salidas.size() == 1, "borrar la otra");
        CHECK(!salidas::quitar(l, "streaming2").empty(), "borrar una que no está: no");
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
