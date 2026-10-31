// ============================================================================
// src/app/tests/test_evento.cpp — el guion de un evento
//
//   1. Tiempos: la duracion sale de los cues, y cada bloque y cada cue tienen
//      su offset desde el arranque.
//   2. Guardar y cargar reproduce el guion; un tipo desconocido no se pierde.
//   3. Revision previa: encuentra lo que le falta a cada tipo de cue.
//   4. La línea de tiempo (v2) viaja con el guion sin perderse.
//
// Correrlo (avanzar, HOLD, pasar solo, el recorrido) es de la línea: test_linea.
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
    e.fecha = "2026-05-25";
    e.inicio = "20:00";
    e.huso = "America/Argentina/Buenos_Aires";

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
    CHECK(b.fecha == "2026-05-25", "la fecha sobrevive");
    CHECK(b.huso == "America/Argentina/Buenos_Aires", "el huso sobrevive");
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

// --- 2b) Fechas y horas -----------------------------------------------------
void fechas_y_horas() {
    CHECK(fechaValida("2026-05-25"), "una fecha normal");
    CHECK(fechaValida("2024-02-29"), "el 29 de febrero de un bisiesto existe");
    CHECK(!fechaValida("2026-02-29"), "el de un ano comun no");
    CHECK(!fechaValida("2026-04-31"), "abril no tiene 31");
    CHECK(!fechaValida("2026-13-01"), "no hay mes 13");
    CHECK(!fechaValida("2026-00-10"), "ni mes 0");
    CHECK(!fechaValida("2026-05-00"), "ni dia 0");
    CHECK(!fechaValida("25/05/2026"), "el formato de casa no");
    CHECK(!fechaValida("2026-5-25"), "ni sin el cero adelante");
    CHECK(!fechaValida(""), "un vacio no es una fecha");
    CHECK(!fechaValida("2026-05-25T20:00"), "ni una fecha con cosas pegadas");
    CHECK(!fechaValida("aaaa-bb-cc"), "ni letras");

    CHECK(horaValida("20:00"), "una hora normal");
    CHECK(horaValida("00:00"), "la medianoche");
    CHECK(horaValida("23:59"), "el ultimo minuto");
    CHECK(!horaValida("24:00"), "no hay hora 24");
    CHECK(!horaValida("20:60"), "ni minuto 60");
    CHECK(!horaValida("8:00"), "ni sin el cero adelante");
    CHECK(!horaValida("8pm"), "ni en el otro idioma");
    CHECK(!horaValida(""), "un vacio no es una hora");
}

// --- 3) Revision previa -----------------------------------------------------
void revision() {
    CHECK(revisar(actoDePrueba()).empty(), "un acto completo no tiene reparos");

    // Cuando es el evento. Sin fecha corre igual, pero no se lo puede
    // convocar: es aviso, no impedimento.
    Evento sinCuando = actoDePrueba();
    sinCuando.fecha.clear();
    sinCuando.huso.clear();
    const auto rsc = revisar(sinCuando);
    CHECK(rsc.size() == 2, "avisa que falta la fecha y que falta el huso");
    for (const Reparo& r : rsc)
        CHECK(!r.grave, "no impiden correr el guion");

    // Un formato mal escrito SI es grave: una fecha que no se entiende no se
    // ignora sola, se lee mal.
    Evento malaFecha = actoDePrueba();
    malaFecha.fecha = "25/05/2026";
    const auto rf = revisar(malaFecha);
    CHECK(rf.size() == 1 && rf[0].grave, "una fecha mal escrita se rechaza");

    Evento malaHora = actoDePrueba();
    malaHora.inicio = "8pm";
    const auto rh = revisar(malaHora);
    CHECK(rh.size() == 1 && rh[0].grave, "una hora mal escrita se rechaza");

    Evento vacio;
    const auto rv = revisar(vacio);
    CHECK(rv.size() == 1 && rv[0].grave, "un evento sin bloques no se puede correr");

    Evento e = actoDePrueba();
    e.bloques[0].cues[0].datos = json::object();            // escena sin escena
    e.bloques[1].cues[1].duracionSeg = 0;                   // orador sin tiempo
    e.bloques[2].cues.push_back(cue(Tipo::Pausa, "c6", 0)); // pausa que no espera nada
    const auto rs = revisar(e);
    CHECK(rs.size() == 3, "encuentra los tres problemas");
    for (const Reparo& r : rs)
        CHECK(r.grave, "los tres impiden correr el guion");
    CHECK(rs[0].bloque == 0 && rs[0].cue == 0, "ubica el reparo donde esta");

    // El cue de audio: la accion tiene que ser una de las que se saben hacer.
    Cue mal = cue(Tipo::Audio, "ca", 1, json{{"accion", "bajar"}, {"fuente", "Default"}});
    CHECK(mal.queLeFalta().find("no existe") != std::string::npos,
          "una accion de audio inventada se avisa por nombre");
    // El cue de grafico: la FUENTE hace falta siempre, porque es donde se ve.
    // Un grafico sin fuente no esta en ninguna escena y no lo ve nadie.
    Cue g = cue(Tipo::Grafico, "cg", 0, json{{"grafico", "zocalo"}});
    CHECK(g.queLeFalta().find("fuente") != std::string::npos,
          "un grafico sin fuente de escena no se puede sacar al aire");
    Cue gOculta = cue(Tipo::Grafico, "cg2", 0, json{{"fuente", "Zócalo"}, {"accion", "ocultar"}});
    CHECK(gOculta.queLeFalta().empty(), "para ocultar alcanza con saber la fuente");
    Cue gSinCual = cue(Tipo::Grafico, "cg3", 0, json{{"fuente", "Zócalo"}});
    CHECK(!gSinCual.queLeFalta().empty(), "para mostrar hace falta decir cual grafico");
    Cue gOk = cue(Tipo::Grafico, "cg4", 0, json{{"fuente", "Zócalo"}, {"grafico", "zocalo"}});
    CHECK(gOk.queLeFalta().empty(), "con fuente y grafico esta completo");
    // El cue de carrusel: fija carrusel y lámina (la lámina, como número o como texto).
    Tipo tc{};
    CHECK(tipoDeNombre("carrusel", tc) && tc == Tipo::Carrusel && ejecutable(Tipo::Carrusel), "el tipo carrusel existe y se ejecuta");
    Cue cSin = cue(Tipo::Carrusel, "cc1", 0, json{{"lamina", 3}});
    CHECK(cSin.queLeFalta().find("carrusel") != std::string::npos, "un cue de carrusel sin carrusel avisa");
    Cue cSinLamina = cue(Tipo::Carrusel, "cc2", 0, json{{"carrusel", "Tema 1"}});
    CHECK(!cSinLamina.queLeFalta().empty(), "sin lámina avisa");
    Cue cOk = cue(Tipo::Carrusel, "cc3", 0, json{{"carrusel", "Tema 1"}, {"lamina", "3"}});
    CHECK(cOk.queLeFalta().empty() && numeroDeLamina(cOk.datos) == 3, "la lámina como texto vale");
    CHECK(numeroDeLamina(json{{"lamina", "x"}}) == 0 && numeroDeLamina(json{{"lamina", 0}}) == 0,
          "una lámina que no es un número no vale");

    // Prender o apagar un rotulo automatico no necesita fuente ni grafico: el
    // vinculo ya sabe donde se ve. Lo que necesita es decir CUAL.
    Cue gv = cue(Tipo::Grafico, "cg5", 0, json{{"accion", "prender"}});
    CHECK(gv.queLeFalta().find("vinculo") != std::string::npos,
          "prender un vinculo sin decir cual no se puede");
    Cue gvOk = cue(Tipo::Grafico, "cg6", 0, json{{"accion", "apagar"}, {"vinculo", "v1"}});
    CHECK(gvOk.queLeFalta().empty(), "con el id del vinculo alcanza, sin fuente ni grafico");

    Cue sinFuente = cue(Tipo::Audio, "cb", 1, json{{"accion", "silenciar"}});
    CHECK(!sinFuente.queLeFalta().empty(), "silenciar necesita saber que fuente");
    Cue bien = cue(Tipo::Audio, "cc", 1, json{{"accion", "volumen"}, {"fuente", "Default"}});
    CHECK(bien.queLeFalta().empty(), "con accion y fuente esta completo");

    // Parar la musica no necesita archivo: es el mismo tipo de cue.
    Cue parar = cue(Tipo::Musica, "cx", 2, json{{"accion", "parar"}});
    CHECK(parar.queLeFalta().empty(), "parar la musica no necesita archivo");
    Cue arrancar = cue(Tipo::Musica, "cy", 2);
    CHECK(!arrancar.queLeFalta().empty(), "arrancarla si");

    // Una cadena larga de automaticos se avisa, pero no impide correr.
    Evento enCadena;
    // Con el cuando puesto, para que el unico reparo sea el que se esta
    // probando y no los avisos de fecha.
    enCadena.fecha = "2026-05-25";
    enCadena.inicio = "20:00";
    enCadena.huso = "America/Argentina/Buenos_Aires";
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

// --- 4) La línea viaja con el guion ---------------------------------------------
/// El evento lleva su línea de tiempo (v2) tal cual: este archivo no la entiende,
/// pero no la puede perder al guardar (la corre la Corrida, ver test_linea).
void linea_tal_cual() {
    Evento e = actoDePrueba();
    CHECK(e.aJson().count("linea") == 0, "un evento sin línea (v1) no escribe la clave");
    e.linea = json{{"version", 2}, {"bloques", json::array()}, {"inventado", {{"x", 1}}}};
    const Evento v = Evento::deJson(e.aJson());
    CHECK(v.linea == e.linea, "la línea vuelve igual, con lo que no se entiende acá");
    json roto = e.aJson();
    roto["linea"] = "texto";
    CHECK(Evento::deJson(roto).linea.is_null(), "una línea que no es un objeto no se toma");
}

/// La lista de auto-aceptar del lobby: va y vuelve del JSON, y una entrada sin
/// ficha de rostro NO sobrevive —dejaria pasar por nombre, que es justo lo que
/// no se quiere—.
void auto_aceptar() {
    ba::evento::Evento e;
    e.nombre = "Programa";
    ba::evento::Invitado a;
    a.personaId = "pf2f1850c1b84";
    a.nombre = "Ana Perez";
    a.nota = "panelista";
    e.autoAceptar.push_back(a);
    const auto j = e.aJson();
    const ba::evento::Evento v = ba::evento::Evento::deJson(j);
    CHECK(v.autoAceptar.size() == 1, "la lista de auto-aceptar vuelve del JSON");
    CHECK(v.autoAceptar.size() == 1 && v.autoAceptar[0].personaId == "pf2f1850c1b84",
          "con su ficha de rostro");
    CHECK(v.autoAceptar.size() == 1 && v.autoAceptar[0].nota == "panelista", "y su nota");

    auto roto = j;
    roto["autoAceptar"][0].erase("personaId");
    CHECK(ba::evento::Evento::deJson(roto).autoAceptar.empty(),
          "una entrada sin ficha de rostro se descarta");
}

} // namespace

int main() {
    tiempos();
    ida_y_vuelta();
    fechas_y_horas();
    revision();
    linea_tal_cual();
    auto_aceptar();
    if (g_fallos == 0) std::printf("test_evento: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
