// ============================================================================
// src/gateway/tests/test_prompter.cpp — el teleprompter
//
//   1. Contar palabras: la cuenta tiene que ser estable (de ella sale la
//      duracion estimada y la posicion).
//   2. Cargar un texto lo deja PARADO en el principio.
//   3. Cambiar la velocidad o parar no mueve el texto de lugar.
//   4. Corriendo, la posicion avanza a las palabras por minuto que diga.
//   5. Saltar a un parrafo.
//   6. La posicion nunca se pasa del final ni va antes del principio.
//   7. Quien esta conectado: se anota al pedir el estado, se olvida solo
//      cuando deja de pedir, no se duplica al recargar la pagina, el nombre
//      vacio no pisa el que habia, y la tabla tiene tope.
// ============================================================================

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>

#include "Prompter.hpp"

using ba::gateway::Prompter;
using ba::gateway::Prompters;
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

const char* kDiscurso = "Buenas tardes a todos.\n"
                        "Gracias por acompanarnos en esta fecha.\n"
                        "\n"
                        "Hace doscientos anos, en esta misma plaza, un grupo de vecinos decidio\n"
                        "que las cosas podian ser de otra manera.\n"
                        "\n"
                        "Muchas gracias.";

void contar() {
    CHECK(Prompter::contarPalabras("") == 0, "un texto vacio no tiene palabras");
    CHECK(Prompter::contarPalabras("   \n\t ") == 0, "solo espacios tampoco");
    CHECK(Prompter::contarPalabras("hola") == 1, "una palabra");
    CHECK(Prompter::contarPalabras("  hola   mundo  ") == 2, "los espacios de mas no cuentan");
    CHECK(Prompter::contarPalabras("uno\ndos\tres") == 3, "los saltos y tabs separan igual");
}

void cargar_deja_parado() {
    Prompter p;
    p.cargar("Discurso del intendente", kDiscurso);
    const json e = p.estado();
    CHECK(e["titulo"] == "Discurso del intendente", "guarda el titulo");
    CHECK(e["palabras"].get<int>() == 32, "cuenta las palabras del discurso");
    CHECK(e["posicion"].get<double>() == 0.0, "empieza por el principio");
    CHECK(e["corriendo"].get<bool>() == false, "un texto nuevo NUNCA arranca solo");
    CHECK(e["parrafos"].size() == 3, "encuentra los tres parrafos");
    CHECK(e["parrafos"][0].get<int>() == 0, "el primero empieza en la palabra cero");

    // A 140 palabras por minuto, 32 palabras son unos 13 segundos.
    CHECK(p.duracionSeg() == 13, "estima cuanto dura leerlo");
    p.comandar(json{{"ppm", 70}});
    CHECK(p.duracionSeg() == 27, "a la mitad de velocidad, casi el doble de tiempo");
}

void cambiar_velocidad_no_mueve() {
    Prompter p;
    p.cargar("x", kDiscurso);
    p.comandar(json{{"posicion", 10.0}});
    p.comandar(json{{"corriendo", true}});
    // Cambiar la velocidad sin haber corrido nada no debe mover el texto. El
    // prompter corre con el reloj real: entre un comando y el otro pasan unos
    // ms (muchos mas con los sanitizers) y el texto avanza lo que corresponde a
    // ese tiempo. Lo que se prueba es que NO SALTE: una palabra de margen es
    // unos 300 ms a 200 ppm, y un salto seria de decenas.
    const auto cerca = [](double pos) { return std::fabs(pos - 10.0) < 1.0; };
    p.comandar(json{{"ppm", 200}});
    CHECK(cerca(p.estado()["posicion"].get<double>()), "cambiar la velocidad deja el texto donde estaba");
    p.comandar(json{{"corriendo", false}});
    const double parado = p.estado()["posicion"].get<double>();
    CHECK(cerca(parado), "parar tampoco lo mueve");
    // Parado ya no corre el reloj: desde aca la posicion tiene que ser exacta.
    p.pararEnSeco();
    CHECK(p.estado()["posicion"].get<double>() == parado, "el boton de panico lo deja donde esta");
    CHECK(p.estado()["corriendo"].get<bool>() == false, "y parado");
}

void avanza_corriendo() {
    Prompter p;
    // Un texto largo para que no se termine durante la prueba.
    std::string largo;
    for (int i = 0; i < 2000; ++i)
        largo += "palabra ";
    p.cargar("largo", largo);
    // 300 palabras por minuto es el tope: 5 por segundo. Con 400 ms la
    // posicion tiene que haber avanzado unas 2 palabras, que alcanza para ver
    // que corre sin que la prueba tarde.
    p.comandar(json{{"ppm", 300}});
    p.comandar(json{{"corriendo", true}});
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    const double pos = p.estado()["posicion"].get<double>();
    // El estado NO adelanta la posicion: la calcula la vista. El servidor
    // guarda desde donde y desde cuando.
    CHECK(pos == 0.0, "el servidor no adelanta la posicion: manda desde donde y desde cuando");
    // Al comandar algo, si la congela.
    p.comandar(json{{"corriendo", false}});
    const double fijada = p.estado()["posicion"].get<double>();
    CHECK(fijada > 1.0, "al parar, la posicion quedo donde iba el texto");
    CHECK(fijada < 5.0, "y no mas alla de lo que dio el tiempo");
}

void saltar_a_parrafo() {
    Prompter p;
    p.cargar("x", kDiscurso);
    CHECK(p.comandar(json{{"parrafo", 2}}), "salta al tercer parrafo");
    const json e = p.estado();
    CHECK(e["posicion"].get<double>() == e["parrafos"][2].get<double>(), "queda en su primera palabra");
    CHECK(!p.comandar(json{{"parrafo", 99}}), "un parrafo que no existe no hace nada");
}

void limites() {
    Prompter p;
    p.cargar("x", kDiscurso);
    p.comandar(json{{"posicion", 9999.0}});
    CHECK(p.estado()["posicion"].get<double>() == 32.0, "no se pasa del final");
    p.comandar(json{{"posicion", -50.0}});
    CHECK(p.estado()["posicion"].get<double>() == 0.0, "ni va antes del principio");

    p.comandar(json{{"ppm", 5}});
    CHECK(p.estado()["ppm"].get<int>() == 40, "una velocidad absurda se acota");
    p.comandar(json{{"ppm", 9000}});
    CHECK(p.estado()["ppm"].get<int>() == 300, "por arriba tambien");
    p.comandar(json{{"tamano", 99}});
    CHECK(p.estado()["tamano"].get<int>() == 10, "el tamano va de 1 a 10");

    CHECK(!p.comandar(json{{"cualquiera", 1}}), "un comando desconocido no hace nada");
    CHECK(!p.comandar(json::array()), "y algo que ni es un objeto tampoco");

    const std::string largo(400, 'x');
    p.comandar(json{{"aviso", largo}});
    CHECK(p.estado()["aviso"].get<std::string>().size() == 120, "el aviso se corta: es para leerlo de reojo");
}

/// Quien esta conectado sale de quien pide el estado: no hay canal aparte.
void conectados() {
    Prompter p;
    CHECK(p.clientes().empty(), "recien arrancado no hay nadie");

    p.visto("tablet-1", "orador", "Atril 1", 0x0100007f);
    p.visto("celu-1", "control", "", 0x0200007f);
    json c = p.clientes();
    CHECK(c.size() == 2, "dos conectados");
    // Los oradores primero: es lo que hay que mirar antes de que alguien suba.
    CHECK(c[0]["vista"] == "orador" && c[0]["nombre"] == "Atril 1", "el orador va primero, con su nombre");
    CHECK(c[1]["vista"] == "control", "despues el control");
    CHECK(c[0]["ip"] == "127.0.0.1", "la ip en texto");

    // Recargar la pagina no es un aparato nuevo: el id lo guarda el navegador.
    p.visto("tablet-1", "orador", "Atril 1", 0x0100007f);
    CHECK(p.clientes().size() == 2, "el mismo id no se duplica");

    // El nombre vacio NO borra el que ya habia: la pagina solo lo manda
    // cuando alguien lo escribio.
    p.visto("tablet-1", "orador", "", 0x0100007f);
    CHECK(p.clientes()[0]["nombre"] == "Atril 1", "un nombre vacio no pisa el que habia");

    // Una vista que no se reconoce cae en "orador": la tablet es lo que no
    // puede quedar afuera de la lista.
    p.visto("raro", "cualquiera", "", 0);
    bool hayRaro = false;
    for (const auto& x : p.clientes())
        if (x["id"] == "raro") hayRaro = x["vista"] == "orador";
    CHECK(hayRaro, "una vista desconocida entra como orador");

    // Sin id no se anota: un pedido sin identidad no es un aparato.
    const size_t antes = p.clientes().size();
    p.visto("", "orador", "", 0);
    CHECK(p.clientes().size() == antes, "sin id no se anota");

    // Dejar de pedir = desaparecer. La pagina pide cada 500 ms, asi que
    // pasado kVivoMs() sin aparecer ya no esta.
    std::this_thread::sleep_for(std::chrono::milliseconds(Prompter::kVivoMs() + 200));
    CHECK(p.clientes().empty(), "el que deja de pedir se va solo");
    p.visto("tablet-1", "orador", "", 0x0100007f);
    CHECK(p.clientes().size() == 1, "y vuelve si vuelve a pedir");
    CHECK(p.clientes()[0]["nombre"] == "Atril 1", "con el nombre que tenia");

    // Tope: alguien probando URLs no hace crecer la tabla sin fin.
    Prompter q;
    for (int i = 0; i < static_cast<int>(Prompter::kMaxClientes) + 20; ++i)
        q.visto("id-" + std::to_string(i), "control", "", 0);
    CHECK(q.clientes().size() <= Prompter::kMaxClientes, "la tabla tiene tope");
}

// Varios teleprompters, cada uno con su texto (Prompters).
void varios() {
    Prompters ps;
    CHECK(Prompters::idValido("") == "principal", "sin id: el principal");
    CHECK(Prompters::idValido("atril-2") == "atril-2", "minúsculas, números y guiones");
    CHECK(Prompters::idValido("Atril").empty() && Prompters::idValido("a/b").empty() && Prompters::idValido(std::string(33, 'a')).empty(),
          "ni mayúsculas, ni barras, ni más de 32");
    const auto a = ps.de(""), b = ps.de("invitado");
    CHECK(a && b && a != b && ps.de("principal") == a, "el principal y otro, distintos; el mismo id da el mismo");
    a->cargar("Presidencia", "uno dos tres");
    b->cargar("Invitado", "cuatro cinco");
    CHECK(a->estado()["titulo"] == "Presidencia" && b->estado()["titulo"] == "Invitado", "cada uno con su texto");
    const auto l = ps.lista();
    CHECK(l.size() == 2 && l[0]["id"] == "principal" && l[1]["titulo"] == "Invitado" && l[1]["palabras"] == 2, "la lista, el principal primero");
    CHECK(!ps.quitar("principal") && ps.quitar("invitado") && ps.lista().size() == 1, "el principal no se saca; los otros sí");
    CHECK(b->estado()["titulo"] == "Invitado", "el que se sacó sigue vivo mientras alguien lo usa");
    CHECK(!ps.de("No Vale"), "un id que no vale no crea nada");
    for (int i = 0; i < static_cast<int>(Prompters::kMax) + 5; ++i) ps.de("p" + std::to_string(i));
    CHECK(ps.lista().size() <= Prompters::kMax, "tienen tope");
}

// Lo del presentador (la línea de tiempo): va a todos, no mueve el texto y lo sabe el
// que se abre después.
void presentador() {
    Prompters ps;
    auto a = ps.de("principal");
    a->cargar("Discurso", "uno dos tres");
    const auto version = a->estado()["version"];
    ps.comandarTodos(json{{"presentador", {{"alAire", {{"nombre", "Apertura"}, {"hastaMs", 1000}}}, {"proximo", {{"nombre", "Himno"}}}}}});
    CHECK(a->estado()["presentador"]["proximo"]["nombre"] == "Himno", "llega al que hay");
    CHECK(a->estado()["version"] == version, "y no cuenta como un cambio del texto");
    CHECK(ps.de("atril-2")->estado()["presentador"]["alAire"]["nombre"] == "Apertura", "el que se abre después ya lo sabe");
    ps.comandarTodos(json{{"presentador", nullptr}});
    CHECK(a->estado()["presentador"].is_null() && ps.de("atril-2")->estado()["presentador"].is_null(), "sin línea, nada");
}

} // namespace

int main() {
    contar();
    cargar_deja_parado();
    cambiar_velocidad_no_mueve();
    avanza_corriendo();
    saltar_a_parrafo();
    limites();
    conectados();
    varios();
    presentador();
    if (g_fallos == 0) std::printf("test_prompter: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
