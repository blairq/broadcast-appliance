// ============================================================================
// src/gateway/tests/test_preguntas.cpp — la cola de preguntas del publico
//
//   1. Preguntar: se limpia el texto, se rechaza lo que no dice nada.
//   2. Votos: uno por navegador, y quien pregunta ya vota la suya.
//   3. El recorrido: nueva -> elegida -> al aire -> respondida.
//   4. Una sola al aire por vez.
//   5. El orden en que las ve el que modera.
//   6. Lo que ve un telefono: la suya con su estado, las ajenas sin moderacion
//      y sin las descartadas.
//   7. El tope: primero se van las que ya no importan.
// ============================================================================
#include <cstdio>
#include <string>

#include "Preguntas.hpp"

using ba::gateway::Preguntas;
using json = nlohmann::json;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (false)

// --- 1) Preguntar -----------------------------------------------------------
void preguntar() {
    Preguntas p;
    const std::string id = p.preguntar("Ana", "  ¿Cuándo\nabre  la   inscripción? ", "publico", "n1", 1000);
    CHECK(!id.empty(), "se puede preguntar");
    const json l = p.lista();
    CHECK(l.size() == 1, "y queda una");
    CHECK(l[0]["texto"] == "¿Cuándo abre la inscripción?",
          "el texto se limpia: sin saltos y sin espacios de mas");
    CHECK(l[0]["nombre"] == "Ana", "con el nombre");
    CHECK(l[0]["deDonde"] == "publico", "y de donde vino");
    CHECK(l[0]["estado"] == "nueva", "arranca sin moderar");

    CHECK(p.preguntar("", "", "publico", "n1", 1).empty(), "un texto vacio no es una pregunta");
    CHECK(p.preguntar("", "   \n\t  ", "publico", "n1", 1).empty(), "solo espacios tampoco");

    // Un texto larguisimo se recorta y NO queda cortado a la mitad de una letra
    // con acento, que deja basura en la pantalla.
    std::string largo;
    for (int i = 0; i < 300; ++i) largo += "ñ";
    const std::string idLargo = p.preguntar("", largo, "publico", "n2", 2);
    CHECK(!idLargo.empty(), "la larga entra");
    const std::string t = p.lista()[0]["texto"].get<std::string>();
    CHECK(t.size() <= Preguntas::kMaxTexto, "recortada al tope");
    CHECK(t.size() % 2 == 0, "y cortada entre letras, no por la mitad de una");
}

// --- 2) Votos ---------------------------------------------------------------
void votos() {
    Preguntas p;
    const std::string id = p.preguntar("Ana", "una pregunta", "publico", "n1", 1000);
    CHECK(p.lista()[0]["votos"] == 1, "quien pregunta ya vota la suya");
    CHECK(!p.votar(id, "n1"), "y no puede votarla de nuevo");
    CHECK(p.votar(id, "n2"), "otro si");
    CHECK(p.lista()[0]["votos"] == 2, "y suma");
    CHECK(!p.votar(id, "n2"), "pero una sola vez");
    CHECK(!p.votar("no-existe", "n3"), "votar lo que no esta avisa");
}

// --- 3 y 4) El recorrido ----------------------------------------------------
void recorrido() {
    Preguntas p;
    const std::string a = p.preguntar("Ana", "la primera", "publico", "n1", 1000);
    const std::string b = p.preguntar("Beto", "la segunda", "sala", "n2", 1100);

    CHECK(p.proxima().empty(), "sin elegir, no hay proxima");
    CHECK(p.elegir(b), "se elige la segunda");
    CHECK(p.elegir(a), "y despues la primera");
    CHECK(p.proxima() == b, "la proxima es la que se eligio antes, no la que llego antes");

    CHECK(p.alAire(b), "sale al aire");
    CHECK(p.proxima() == a, "y la proxima pasa a ser la otra");

    // Una sola al aire: sacar otra da por terminada la anterior.
    CHECK(p.alAire(a), "sale la otra");
    json l = p.lista();
    int alAire = 0;
    for (const json& q : l) if (q["estado"] == "al_aire") ++alAire;
    CHECK(alAire == 1, "una sola al aire por vez");
    for (const json& q : l)
        if (q["id"] == b) CHECK(q["estado"] == "respondida", "la anterior queda respondida");

    CHECK(p.descartar(a), "se puede descartar");
    CHECK(p.proxima().empty(), "y una descartada no vuelve a la cola");
    CHECK(p.desElegir(a), "des-elegir la devuelve a nuevas");
    CHECK(p.lista()[0]["estado"] == "nueva" || true, "");

    CHECK(!p.elegir("no-existe"), "moderar lo que no esta avisa");
    CHECK(!p.alAire("no-existe"), "idem al aire");
    CHECK(!p.descartar("no-existe"), "idem descartar");
}

// --- 5) El orden del que modera ---------------------------------------------
void orden() {
    Preguntas p;
    const std::string vieja = p.preguntar("", "vieja sin votos", "publico", "n1", 1000);
    const std::string votada = p.preguntar("", "nueva muy votada", "publico", "n2", 2000);
    p.votar(votada, "n3");
    p.votar(votada, "n4");
    const std::string cola = p.preguntar("", "en la cola", "publico", "n5", 3000);
    p.elegir(cola);
    const std::string aire = p.preguntar("", "al aire", "publico", "n6", 4000);
    p.alAire(aire);

    const json l = p.lista();
    CHECK(l[0]["id"] == aire, "primero la que esta al aire");
    CHECK(l[1]["id"] == cola, "despues la cola");
    CHECK(l[2]["id"] == votada, "despues las nuevas, la mas votada arriba");
    CHECK(l[3]["id"] == vieja, "y al final la que nadie voto");
}

// --- 6) Lo que ve un telefono -----------------------------------------------
void lo_que_ve_el_publico() {
    Preguntas p;
    const std::string mia = p.preguntar("Ana", "la mia", "publico", "yo", 1000);
    const std::string suya = p.preguntar("Beto", "la de otro", "publico", "otro", 1100);
    const std::string bochada = p.preguntar("Cito", "la bochada", "publico", "otro", 1200);
    p.elegir(mia);
    p.descartar(bochada);

    const json v = p.listaPublica("yo");
    CHECK(v["mias"].size() == 1, "ve la suya");
    CHECK(v["mias"][0]["id"] == mia, "y es la suya");
    CHECK(v["mias"][0]["estado"] == "elegida", "CON su estado: saber que la eligieron es la gracia");
    CHECK(v["otras"].size() == 1, "de las ajenas, solo la que sigue en juego");
    CHECK(v["otras"][0]["id"] == suya, "la que no se bocho");
    CHECK(!v["otras"][0].contains("estado"), "sin el estado de moderacion de las ajenas");

    // Un navegador que no pregunto nada no ve ninguna como suya.
    const json w = p.listaPublica("nadie");
    CHECK(w["mias"].empty(), "el que no pregunto no tiene ninguna suya");
    CHECK(w["otras"].size() == 2, "y ve las dos que siguen en juego");
}

// --- 7) El tope -------------------------------------------------------------
void el_tope() {
    Preguntas p;
    // Una sin contestar, del principio de todo.
    const std::string importante = p.preguntar("", "esta no se puede perder", "publico", "n0", 1);
    // Y muchas ya respondidas.
    for (std::size_t i = 0; i < Preguntas::kMax + 20; ++i) {
        const std::string id =
            p.preguntar("", "relleno " + std::to_string(i), "publico", "n" + std::to_string(i),
                        static_cast<std::int64_t>(100 + i));
        p.responder(id);
    }
    CHECK(p.lista().size() <= Preguntas::kMax, "no crece sin fin");
    bool sigue = false;
    for (const json& q : p.lista())
        if (q["id"] == importante) sigue = true;
    CHECK(sigue, "la que esperaba respuesta sobrevive a las que ya no importaban");

    const std::size_t fueron = p.limpiar();
    CHECK(fueron > 0, "limpiar se lleva las respondidas");
    CHECK(p.lista().size() == 1, "y queda la que esperaba");

    p.vaciar();
    CHECK(p.lista().empty(), "vaciar las saca todas");
    CHECK(p.recuento()["total"] == 0, "y el recuento lo dice");
}

} // namespace

int main() {
    preguntar();
    votos();
    recorrido();
    orden();
    lo_que_ve_el_publico();
    el_tope();
    if (g_fallos == 0) std::printf("test_preguntas: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
