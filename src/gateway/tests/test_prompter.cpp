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
// ============================================================================

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "Prompter.hpp"

using ba::gateway::Prompter;
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

const char* kDiscurso =
    "Buenas tardes a todos.\n"
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
    // Cambiar la velocidad sin haber corrido nada no debe mover el texto.
    p.comandar(json{{"ppm", 200}});
    CHECK(p.estado()["posicion"].get<double>() == 10.0, "cambiar la velocidad deja el texto donde estaba");
    p.comandar(json{{"corriendo", false}});
    CHECK(p.estado()["posicion"].get<double>() == 10.0, "parar tampoco lo mueve");
    p.pararEnSeco();
    CHECK(p.estado()["posicion"].get<double>() == 10.0, "el boton de panico lo deja donde esta");
    CHECK(p.estado()["corriendo"].get<bool>() == false, "y parado");
}

void avanza_corriendo() {
    Prompter p;
    // Un texto largo para que no se termine durante la prueba.
    std::string largo;
    for (int i = 0; i < 2000; ++i) largo += "palabra ";
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

} // namespace

int main() {
    contar();
    cargar_deja_parado();
    cambiar_velocidad_no_mueve();
    avanza_corriendo();
    saltar_a_parrafo();
    limites();
    if (g_fallos == 0) std::printf("test_prompter: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
