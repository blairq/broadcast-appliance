// ============================================================================
// src/app/tests/test_evento.cpp — el guion de un evento
//
//   1. Tiempos: la duracion sale de los cues, y cada bloque y cada cue tienen
//      su offset desde el arranque.
//   2. Guardar y cargar reproduce el guion; un tipo desconocido no se pierde.
//   3. Revision previa: encuentra lo que le falta a cada tipo de cue.
//   4. Correr el guion: avanzar, retroceder, saltar y terminar; los bloques
//      vacios no frenan el recorrido.
//   5. El desvio: tarde, adelantado, y que lo pausado NO cuente como atraso.
//   6. El recorrido guarda lo que realmente paso, con su duracion real.
// ============================================================================

#include <cstdio>
#include <string>

#include "evento/Evento.hpp"

using namespace ba::evento;
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

Cue cue(Tipo t, const std::string& id, int dur, json datos = json::object()) {
    Cue c;
    c.tipo = t;
    c.id = id;
    c.duracionSeg = dur;
    c.datos = std::move(datos);
    c.texto = "algo que el operador lee";
    return c;
}

/// Un acto chico pero completo: apertura, discurso y cierre.
Evento actoDePrueba() {
    Evento e;
    e.nombre = "Acto del 25 de mayo";
    e.inicio = "20:00";

    Bloque apertura;
    apertura.id = "b1";
    apertura.nombre = "Apertura";
    apertura.cues.push_back(cue(Tipo::Escena, "c1", 30, json{{"escena", "Placa inicial"}}));
    apertura.cues.push_back(cue(Tipo::Musica, "c2", 90, json{{"archivo", "Medios/himno.wav"}}));

    Bloque discurso;
    discurso.id = "b2";
    discurso.nombre = "Palabras del intendente";
    discurso.cues.push_back(cue(Tipo::Escena, "c3", 10, json{{"escena", "Camara 1"}}));
    discurso.cues.push_back(cue(Tipo::Orador, "c4", 300, json{{"participante", "p-intendente"}}));

    Bloque cierre;
    cierre.id = "b3";
    cierre.nombre = "Cierre";
    cierre.cues.push_back(cue(Tipo::Escena, "c5", 20, json{{"escena", "Placa final"}}));

    e.bloques = {apertura, discurso, cierre};
    return e;
}

// --- 1) Tiempos -------------------------------------------------------------
void tiempos() {
    const Evento e = actoDePrueba();
    CHECK(e.cantidadDeCues() == 5, "cuenta todos los cues");
    CHECK(e.bloques[0].duracionSeg() == 120, "la apertura dura lo que suman sus cues");
    CHECK(e.duracionSeg() == 450, "el evento dura la suma de sus bloques");

    CHECK(e.offsetDeBloqueSeg(0) == 0, "el primer bloque arranca en cero");
    CHECK(e.offsetDeBloqueSeg(1) == 120, "el segundo arranca cuando termina el primero");
    CHECK(e.offsetDeBloqueSeg(2) == 430, "y el tercero despues del segundo");

    CHECK(e.offsetDeCueSeg(0, 0) == 0, "el primer cue arranca en cero");
    CHECK(e.offsetDeCueSeg(0, 1) == 30, "el segundo cue, despues del primero");
    CHECK(e.offsetDeCueSeg(1, 1) == 130, "un cue del segundo bloque cuenta lo anterior");
    CHECK(e.offsetDeCueSeg(9, 0) == -1, "un bloque que no existe no tiene offset");
    CHECK(e.offsetDeCueSeg(0, 9) == -1, "un cue que no existe tampoco");
}

// --- 2) Guardar y cargar ----------------------------------------------------
void ida_y_vuelta() {
    const Evento a = actoDePrueba();
    const Evento b = Evento::deJson(a.aJson());
    CHECK(b.nombre == a.nombre, "el nombre sobrevive");
    CHECK(b.inicio == "20:00", "la hora de arranque sobrevive");
    CHECK(b.bloques.size() == 3, "los bloques sobreviven");
    CHECK(b.duracionSeg() == a.duracionSeg(), "los tiempos dan lo mismo");
    CHECK(b.bloques[1].cues[1].tipo == Tipo::Orador, "el tipo de cue sobrevive");
    CHECK(b.bloques[1].cues[1].datos.value("participante", "") == "p-intendente",
          "los datos propios del cue sobreviven");

    // Un evento escrito por una version mas nueva: el cue que no se entiende
    // queda a la vista como nota en vez de desaparecer sin avisar.
    json j = a.aJson();
    j["bloques"][0]["cues"][0]["tipo"] = "holograma";
    const Evento c = Evento::deJson(j);
    CHECK(c.bloques[0].cues.size() == 2, "el cue desconocido no se pierde");
    CHECK(c.bloques[0].cues[0].tipo == Tipo::Nota, "queda como nota, que no ejecuta nada");

    // Basura: no explota, devuelve un evento vacio.
    CHECK(Evento::deJson(json::array()).bloques.empty(), "una lista no es un evento");
    CHECK(Evento::deJson(json("hola")).nombre.empty(), "un texto tampoco");
}

// --- 3) Revision previa -----------------------------------------------------
void revision() {
    CHECK(revisar(actoDePrueba()).empty(), "un acto completo no tiene reparos");

    Evento vacio;
    const auto rv = revisar(vacio);
    CHECK(rv.size() == 1 && rv[0].grave, "un evento sin bloques no se puede correr");

    Evento e = actoDePrueba();
    e.bloques[0].cues[0].datos = json::object();      // escena sin escena
    e.bloques[1].cues[1].duracionSeg = 0;             // orador sin tiempo
    e.bloques[2].cues.push_back(cue(Tipo::Pausa, "c6", 0)); // pausa que no espera nada
    const auto rs = revisar(e);
    CHECK(rs.size() == 3, "encuentra los tres problemas");
    for (const Reparo& r : rs) CHECK(r.grave, "los tres impiden correr el guion");
    CHECK(rs[0].bloque == 0 && rs[0].cue == 0, "ubica el reparo donde esta");

    // Parar la musica no necesita archivo: es el mismo tipo de cue.
    Cue parar = cue(Tipo::Musica, "cx", 2, json{{"accion", "parar"}});
    CHECK(parar.queLeFalta().empty(), "parar la musica no necesita archivo");
    Cue arrancar = cue(Tipo::Musica, "cy", 2);
    CHECK(!arrancar.queLeFalta().empty(), "arrancarla si");

    // Una cadena larga de automaticos se avisa, pero no impide correr.
    Evento enCadena;
    Bloque b;
    b.nombre = "Seguidilla";
    for (int i = 0; i < 8; ++i) {
        Cue c = cue(Tipo::Escena, "a" + std::to_string(i), 5, json{{"escena", "X"}});
        c.automatico = true;
        b.cues.push_back(c);
    }
    enCadena.bloques.push_back(b);
    const auto rc = revisar(enCadena);
    CHECK(rc.size() == 1 && !rc[0].grave, "la cadena larga se avisa, no frena");
}

// --- 4) Correr el guion -----------------------------------------------------
void correr() {
    Guion g(actoDePrueba());
    CHECK(g.estado() == Guion::Estado::Parado, "arranca parado");
    CHECK(g.cueActual() == nullptr, "parado no esta en ningun cue");

    g.arrancar(1000);
    CHECK(g.estado() == Guion::Estado::Corriendo, "arrancado corre");
    CHECK(g.cueActual() != nullptr && g.cueActual()->id == "c1", "empieza por el primer cue");
    CHECK(g.cueSiguiente() != nullptr && g.cueSiguiente()->id == "c2", "y sabe cual sigue");

    CHECK(g.avanzar(2000) && g.cueActual()->id == "c2", "avanza dentro del bloque");
    CHECK(g.avanzar(3000) && g.cueActual()->id == "c3", "y cruza al bloque siguiente");
    CHECK(g.cueSiguiente()->id == "c4", "el siguiente cruza de bloque tambien");

    CHECK(g.retroceder(4000) && g.cueActual()->id == "c2", "retrocede cruzando de bloque");
    CHECK(g.irA({2, 0}, 5000) && g.cueActual()->id == "c5", "salta a donde le digan");
    CHECK(!g.irA({9, 0}, 5000), "no salta a un bloque que no existe");

    CHECK(!g.avanzar(6000), "despues del ultimo cue no hay mas");
    CHECK(g.estado() == Guion::Estado::Terminado, "y el guion termina");

    // Un bloque vacio en el medio no frena el recorrido.
    Evento e = actoDePrueba();
    Bloque hueco;
    hueco.nombre = "Todavia sin armar";
    e.bloques.insert(e.bloques.begin() + 1, hueco);
    Guion g2(e);
    g2.arrancar(0);
    g2.avanzar(0);
    CHECK(g2.avanzar(0) && g2.cueActual()->id == "c3", "salta el bloque vacio");

    // Un evento sin ningun cue termina apenas arranca, no se queda colgado.
    Guion g3(Evento{});
    g3.arrancar(0);
    CHECK(g3.estado() == Guion::Estado::Terminado, "sin cues, termina enseguida");
}

// --- 5) El desvio -----------------------------------------------------------
void desvio() {
    Guion g(actoDePrueba());
    g.arrancar(0);
    CHECK(g.desvioSeg(0) == 0, "recien arrancado no hay desvio");

    // El primer cue dura 30 s previstos. Si se pasa al segundo a los 45 s,
    // vamos 15 s tarde.
    g.avanzar(45000);
    CHECK(g.desvioSeg(45000) == 15, "pasarse de largo es ir tarde");
    CHECK(g.enCueSeg(50000) == 5, "cuenta los segundos en el cue");
    CHECK(g.restanteSeg(50000) == 85, "y lo que falta segun lo previsto");

    // Si en cambio se adelanta, el desvio es negativo.
    Guion g2(actoDePrueba());
    g2.arrancar(0);
    g2.avanzar(20000);
    CHECK(g2.desvioSeg(20000) == -10, "cortar antes es ir adelantado");

    // Lo pausado no cuenta: el operador paro a proposito.
    Guion g3(actoDePrueba());
    g3.arrancar(0);
    g3.avanzar(30000); // puntual: 30 s previstos, 30 s reales
    CHECK(g3.desvioSeg(30000) == 0, "puntual");
    g3.pausar(40000);
    CHECK(g3.estado() == Guion::Estado::Pausado, "queda pausado");
    CHECK(g3.desvioSeg(100000) == 10, "mientras esta pausado el desvio no corre");
    g3.reanudar(100000);
    CHECK(g3.desvioSeg(100000) == 10, "al reanudar sigue el desvio que habia");
    CHECK(g3.enCueSeg(110000) == 20, "y los segundos en el cue descuentan la pausa");
}

// --- 6) El recorrido --------------------------------------------------------
void recorrido() {
    Guion g(actoDePrueba());
    g.arrancar(0);
    g.avanzar(45000);
    g.avanzar(120000);

    const auto& r = g.recorrido();
    CHECK(r.size() == 3, "un renglon por cue ejecutado");
    CHECK(r[0].previstoSeg == 30 && r[0].realSeg() == 45, "el primero: 30 previstos, 45 reales");
    CHECK(r[1].previstoSeg == 90 && r[1].realSeg() == 75, "el segundo salio mas corto");
    CHECK(r[2].hastaMs == 0, "el que esta corriendo todavia no tiene fin");
    const Posicion segundo{0, 1}; // fuera del CHECK: la coma de las llaves confunde al preprocesador
    CHECK(r[1].pos == segundo, "cada renglon sabe de que cue habla");
}

} // namespace

int main() {
    tiempos();
    ida_y_vuelta();
    revision();
    correr();
    desvio();
    recorrido();
    if (g_fallos == 0) std::printf("test_evento: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
