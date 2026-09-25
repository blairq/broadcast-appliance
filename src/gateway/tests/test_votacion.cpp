// ============================================================================
// src/gateway/tests/test_votacion.cpp — la urna
//
//   1. Abrir: hace falta pregunta y al menos dos opciones.
//   2. Encuesta: uno por telefono, y volver a votar cambia el voto.
//   3. Formal: hace falta un codigo del padron; el telefono NO alcanza.
//   4. El quorum: «votaron 12 de 40», con el denominador del padron.
//   5. Secreta no es anonima: se sabe QUIEN voto, no QUE voto.
//   6. En vivo: el resultado no se ve hasta cerrar, salvo que se deje.
//   7. Abrir otra no arrastra los votos de la anterior.
// ============================================================================
#include <cstdio>
#include <map>
#include <string>

#include "Votacion.hpp"

using ba::gateway::Votacion;
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

const std::vector<std::string> kSiNo = {"A favor", "En contra"};

int votosDe(const json& resultado, const char* opcion) {
    for (const json& o : resultado)
        if (o["opcion"] == opcion) return o["votos"].get<int>();
    return -1;
}

// --- 1) Abrir ---------------------------------------------------------------
void abrir() {
    Votacion v;
    CHECK(v.estado()["hubo"] == false, "antes de abrir ninguna, no hay nada que mirar");
    CHECK(!v.abrir("", kSiNo, Votacion::Modo::Encuesta, false, {}, 0, 1), "sin pregunta no abre");
    CHECK(!v.abrir("¿Aprobamos?", {"Solo una"}, Votacion::Modo::Encuesta, false, {}, 0, 1),
          "con una sola opcion no hay nada que decidir");
    CHECK(!v.abrir("¿Aprobamos?", {}, Votacion::Modo::Encuesta, false, {}, 0, 1),
          "sin opciones tampoco");
    CHECK(v.abrir("¿Aprobamos el balance?", kSiNo, Votacion::Modo::Encuesta, false, {}, 0, 1000),
          "con pregunta y dos opciones, si");
    CHECK(v.hayAbierta(), "queda abierta");
    // Las opciones que terminan en acento llegan enteras: «Si» proyectado como
    // «S» fue un bug de verdad.
    Votacion a;
    a.abrir("¿Aprobamos el café?", {"Sí", "No", "Abstención"}, Votacion::Modo::Encuesta, false, {}, 0, 1);
    const json e = a.estado();
    CHECK(e["pregunta"] == "¿Aprobamos el café?", "la pregunta con acento al final llega entera");
    CHECK(e["resultado"][0]["opcion"] == "Sí", "«Sí» sigue siendo «Sí»");
    CHECK(e["resultado"][2]["opcion"] == "Abstención", "y «Abstención» tampoco se corta");
    CHECK(v.cerrar(), "y se cierra");
    CHECK(!v.hayAbierta(), "queda cerrada");
    CHECK(!v.cerrar(), "cerrar la que ya esta cerrada avisa");
}

// --- 2) Encuesta ------------------------------------------------------------
void encuesta() {
    Votacion v;
    v.abrir("¿De que tema hablamos?", {"Uno", "Dos"}, Votacion::Modo::Encuesta, false, {}, 0, 1000);
    CHECK(v.votar("tel-1", "", 0).empty(), "vota uno");
    CHECK(v.votar("tel-2", "", 1).empty(), "vota otro");
    json e = v.estado();
    CHECK(e["votaron"] == 2, "votaron dos");
    CHECK(votosDe(e["resultado"], "Uno") == 1 && votosDe(e["resultado"], "Dos") == 1,
          "uno por lado");

    // Volver a votar CAMBIA, no suma: el que se equivoco de boton tiene que
    // poder arreglarlo.
    CHECK(v.votar("tel-1", "", 1).empty(), "puede cambiar");
    e = v.estado();
    CHECK(e["votaron"] == 2, "siguen siendo dos votantes");
    CHECK(votosDe(e["resultado"], "Dos") == 2, "y ahora los dos votaron lo mismo");

    CHECK(!v.votar("tel-3", "", 9).empty(), "una opcion que no existe se rechaza");
    CHECK(!v.votar("", "", 0).empty(), "sin telefono no se puede contar");
    v.cerrar();
    CHECK(!v.votar("tel-9", "", 0).empty(), "cerrada no se vota mas");
}

// --- 3, 4 y 5) Formal -------------------------------------------------------
void formal() {
    Votacion v;
    const std::map<std::string, std::string> padron{
        {"A1B2", "José Pérez"}, {"C3D4", "Luis Gómez"}, {"E5F6", "Marta Ruiz"}};
    // Cuarenta con derecho a voto, tres presentes: el denominador es el del
    // padron, no el de los que estan.
    v.abrir("¿Aprobamos el balance?", kSiNo, Votacion::Modo::Formal, false, padron, 40, 1000);

    // EL TELEFONO NO ALCANZA: se borra el sitio y se vota de nuevo.
    CHECK(!v.votar("tel-1", "", 0).empty(), "sin codigo no vota");
    CHECK(!v.votar("tel-1", "ZZZZ", 0).empty(), "con un codigo que no esta en el padron, tampoco");
    CHECK(v.votar("tel-1", "A1B2", 0).empty(), "con el suyo, si");

    // El mismo codigo desde OTRO telefono sigue siendo un solo voto.
    CHECK(v.votar("tel-99", "A1B2", 1).empty(), "puede cambiarlo desde otro telefono");
    json e = v.estado();
    CHECK(e["votaron"] == 1, "pero sigue siendo UN voto");

    v.votar("tel-2", "C3D4", 0);
    e = v.estado();
    CHECK(e["votaron"] == 2, "votaron dos");
    CHECK(e["podian"] == 40, "de cuarenta que podian: sin el denominador, dos a favor parece unanime");
    CHECK(e["faltanVotar"].size() == 1, "y falta uno de los presentes");
    CHECK(e["faltanVotar"][0] == "Marta Ruiz", "con nombre y todo");
    CHECK(e["quienes"].size() == 2, "se sabe quienes votaron");
    CHECK(e["quienes"][0].contains("opcion"), "y en una abierta, que voto cada uno");

    // SECRETA: se sabe QUIEN voto (hace falta para el quorum), no QUE voto.
    Votacion s;
    s.abrir("¿Aprobamos?", kSiNo, Votacion::Modo::Formal, true, padron, 40, 1000);
    s.votar("tel-1", "A1B2", 0);
    const json se = s.estado();
    CHECK(se["secreta"] == true, "es secreta");
    CHECK(se["quienes"].size() == 1, "se sabe que Jose voto");
    CHECK(!se["quienes"][0].contains("opcion"), "pero no que voto");
    CHECK(votosDe(se["resultado"], "A favor") == 1, "el recuento igual sale");
}

// --- 6) En vivo -------------------------------------------------------------
void en_vivo() {
    Votacion v;
    v.abrir("¿Aprobamos?", kSiNo, Votacion::Modo::Formal, false, {{"A1", "Ana"}}, 1, 1000);
    v.votar("tel-1", "A1", 0);
    json p = v.estadoPublico("tel-1", "A1");
    CHECK(p["hay"] == true && p["abierta"] == true, "hay una abierta");
    CHECK(p["miVoto"] == 0, "y el telefono ve lo que voto");
    CHECK(!p.contains("resultado"),
          "pero NO el resultado: ver que va perdiendo cambia lo que vota el que falta");

    v.setMostrarEnVivo(true);
    p = v.estadoPublico("tel-1", "A1");
    CHECK(p.contains("resultado"), "salvo que se deje a proposito");

    v.setMostrarEnVivo(false);
    v.cerrar();
    p = v.estadoPublico("tel-1", "A1");
    CHECK(p.contains("resultado"), "cerrada, el resultado se ve siempre");

    // Un telefono sin codigo ve la pregunta pero sabe que no puede votar.
    const json ajeno = v.estadoPublico("tel-2", "");
    CHECK(ajeno["puedeVotar"] == false, "el que no esta en el padron lo sabe antes de intentar");
}

// --- 7) Una por vez ---------------------------------------------------------
void una_por_vez() {
    Votacion v;
    v.abrir("¿Primera?", kSiNo, Votacion::Modo::Encuesta, false, {}, 0, 1000);
    v.votar("tel-1", "", 0);
    v.votar("tel-2", "", 0);
    CHECK(v.estado()["votaron"] == 2, "dos votos en la primera");

    v.abrir("¿Segunda?", {"Sí", "No", "Abstención"}, Votacion::Modo::Encuesta, false, {}, 0, 2000);
    const json e = v.estado();
    CHECK(e["pregunta"] == "¿Segunda?", "abrir otra reemplaza a la anterior");
    CHECK(e["votaron"] == 0, "y NO arrastra los votos: son de otra pregunta");
    CHECK(e["resultado"].size() == 3, "con sus propias opciones");
}

// --- 8) El historial, que es lo que hace posible el acta ---------------------
void historial() {
    Votacion v;
    CHECK(v.historial().empty(), "sin ninguna cerrada, no hay historial");

    v.abrir("¿Punto uno?", kSiNo, Votacion::Modo::Encuesta, false, {}, 0, 1000);
    v.votar("tel-1", "", 0);
    CHECK(v.historial().empty(), "una abierta todavia no esta en el historial");
    v.cerrar();
    CHECK(v.historial().size() == 1, "al cerrar, entra");
    CHECK(v.historial()[0]["pregunta"] == "¿Punto uno?", "con su pregunta");
    CHECK(votosDe(v.historial()[0]["resultado"], "A favor") == 1, "y su resultado");

    // Una asamblea vota varios puntos seguidos: el acta los lleva a TODOS.
    v.abrir("¿Punto dos?", kSiNo, Votacion::Modo::Encuesta, false, {}, 0, 2000);
    v.votar("tel-1", "", 1);
    v.cerrar();
    CHECK(v.historial().size() == 2, "dos puntos votados, dos en el historial");
    CHECK(v.historial()[1]["pregunta"] == "¿Punto dos?", "en orden");
    CHECK(votosDe(v.historial()[1]["resultado"], "En contra") == 1, "cada una con lo suyo");

    // Reemplazar una SIN cerrarla igual la archiva: para el acta importa que se
    // voto, no que alguien se haya acordado de apretar cerrar.
    v.abrir("¿Punto tres?", kSiNo, Votacion::Modo::Encuesta, false, {}, 0, 3000);
    v.votar("tel-1", "", 0);
    v.abrir("¿Punto cuatro?", kSiNo, Votacion::Modo::Encuesta, false, {}, 0, 4000);
    CHECK(v.historial().size() == 3, "la que se reemplazo sin cerrar igual quedo");
    CHECK(v.historial()[2]["pregunta"] == "¿Punto tres?", "y es la que era");

    v.olvidarHistorial();
    CHECK(v.historial().empty(), "se puede vaciar para el evento siguiente");
}

} // namespace

int main() {
    abrir();
    encuesta();
    formal();
    en_vivo();
    una_por_vez();
    historial();
    if (g_fallos == 0) std::printf("test_votacion: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
