// ============================================================================
// src/app/tests/test_linea.cpp — la línea de tiempo v2 (modelo y resolvedor)
//
//   1. JSON ida y vuelta sin pérdida, con las claves del plan.
//   2. Migración v1 → v2: pasos, piezas, desde, continuar, pausa, espera.
//   3. Pronóstico: front, end y back time; estados; over/under (reglas 1-4).
//   4. Hard starts: avisar, esperar y cortar, con gap y overlap.
//   5. Continuar, pre y post espera, condiciones conocidas y sin conocer (5).
//   6. Holds, salteados y el próximo movido a mano.
//   7. Estado en t por pista (prioridad, el más nuevo, vidas) y próximos eventos.
//   8. Varias salidas: anclas, momentos compartidos, conflictos de PTZ y gráficos.
//   9. Revisión previa.
//  10. Load to time y retoma tras un corte (vuelve en HOLD y pide descripción).
//  11. La fachada del guion de cues: proyectar y reconciliar (lo no tocado vuelve exacto).
//  12. Editar en lote (todo o nada, lo emitido protegido) y el diff.
//  13. La vista compacta (linea_ver) y el ajuste simple (linea_ajustar).
//  14. La corrida: GO, lo que entra solo, el aviso, HOLD, pausa, saltar, ir a con
//      reposición, adlibs, insertar del estante y la retoma tras un corte.
//
// Todo sin motor y con el reloj inyectado: ningún tiempo real.
// ============================================================================

#include <algorithm>
#include <cstdio>
#include <string>

#include "evento/Evento.hpp"
#include "evento/Corrida.hpp"
#include "evento/Linea.hpp"
#include "evento/LineaEdicion.hpp"

using namespace ba::evento::linea;
using json = nlohmann::json;
namespace v1 = ba::evento;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (0)

constexpr int64_t S = 1000;
constexpr int64_t M = 60 * S;
constexpr int64_t H = 60 * M;
constexpr int64_t T0 = 19 * H; // el evento arranca a las 19:00 (medianoche = 0)

Paso paso(const std::string& id, int seg, const std::string& salida = "") {
    Paso p;
    p.id = id;
    p.nombre = "Paso " + id;
    p.salida = salida;
    p.fin.duracionSeg = seg;
    p.duracionPrevistaSeg = seg;
    return p;
}

Pieza pieza(const std::string& id, const std::string& pista, const std::string& tipo, json datos = json::object(),
            int64_t desdeMs = 0, Vida vida = Vida::Paso) {
    Pieza z;
    z.id = id;
    z.pista = pista;
    z.tipo = tipo;
    z.datos = std::move(datos);
    z.desde.ms = desdeMs;
    z.vida = vida;
    return z;
}

Pista pista(const std::string& id, const std::string& tipo, const std::string& salida = "") {
    Pista p;
    p.id = id;
    p.tipo = tipo;
    p.salida = salida;
    p.nombre = id;
    return p;
}

Linea lineaBase() {
    Linea l;
    l.salidas = {"principal"};
    l.pistas = {pista("escena", "escena", "principal"), pista("graficos", "graficos", "principal"),
                pista("audio", "audio", "principal"),   pista("dsk", "dsk", "principal"),
                pista("notas", "notas"),                pista("asistente", "asistente")};
    return l;
}

Bloque bloque(const std::string& id, std::vector<Paso> pasos) {
    Bloque b;
    b.id = id;
    b.nombre = "Bloque " + id;
    b.pasos = std::move(pasos);
    return b;
}

/// Tres pasos de un minuto en dos bloques: b1 [p1 p2], b2 [p3].
Linea tresPasos() {
    Linea l = lineaBase();
    l.bloques = {bloque("b1", {paso("p1", 60), paso("p2", 60)}), bloque("b2", {paso("p3", 60)})};
    return l;
}

Entrada entrada(int64_t ahora) {
    Entrada e;
    e.ahoraMs = ahora;
    e.inicioPlaneadoMs = T0;
    e.medianocheMs = 0;
    return e;
}

const PasoResuelto& pr(const Resolucion& r, const std::string& id, const std::string& salida = "principal") {
    static PasoResuelto vacio;
    const SalidaResuelta* s = r.salida(salida);
    const PasoResuelto* p = s ? s->paso(id) : nullptr;
    if (!p) {
        std::printf("FALLO: no está el paso %s en %s\n", id.c_str(), salida.c_str());
        ++g_fallos;
        return vacio;
    }
    return *p;
}

std::string enPista(const Resolucion& r, int64_t t, const std::string& pista) {
    for (const Activa& a : r.estadoEn(t))
        if (a.pista == pista) return a.pieza.pieza;
    return {};
}

// --- 1. JSON --------------------------------------------------------------------

void testJson() {
    Linea l = lineaBase();
    l.salidas.push_back("sala");
    Paso a = paso("a", 30);
    a.guion = "Buenas noches";
    a.notaOperador = "mirar al conductor";
    a.etiquetas = {"apertura", "himno"};
    a.transicion = json{{"tipo", "fundido"}, {"ms", 500}};
    a.continuar = Continuar::Con;
    a.preEsperaMs = 250;
    a.postEsperaMs = 1500;
    a.piezas.push_back(pieza("z1", "escena", "escena", json{{"escena", "Cámara 1"}}));
    Pieza z2 = pieza("z2", "graficos", "grafico", json{{"fuente", "Zócalo"}, {"grafico", "nombre"}});
    z2.desde.modo = Ancla::Modo::AntesDelFin;
    z2.desde.ms = 10 * S;
    z2.dura.modo = Dura::Modo::Ms;
    z2.dura.ms = 8 * S;
    z2.prioridad = 3;
    z2.siSePerdio = SiSePerdio::Preguntar;
    a.piezas.push_back(z2);
    Pieza z3 = pieza("z3", "audio", "musica", json{{"archivo", "Medios/a.wav"}}, 0, Vida::Evento);
    z3.desde.modo = Ancla::Modo::Cuando;
    z3.desde.cuando = "bus:oradores/al_aire == 'banca-12'";
    z3.dura.modo = Dura::Modo::AlTerminar;
    a.piezas.push_back(z3);
    Pieza z4 = pieza("z4", "notas", "nota", json::object(), 0, Vida::HastaQueSeSaque);
    z4.desde.modo = Ancla::Modo::Manual;
    z4.texto = "aplausos";
    a.piezas.push_back(z4);

    Paso b = paso("b", 0);
    b.inicio.modo = ModoInicio::Hora;
    b.inicio.hora = "19:30";
    b.inicio.politica = Politica::Cortar;
    b.fin.modo = ModoFin::Abierto;
    Paso c = paso("c", 0, "sala");
    c.inicio.modo = ModoInicio::Ancla;
    c.inicio.salida = "principal";
    c.inicio.paso = "b";
    c.inicio.masMs = 2000;
    c.fin.modo = ModoFin::Mientras;
    c.fin.mientras = "bus:votacion/abierta";
    Paso d = paso("d", 0, "*");
    d.inicio.modo = ModoInicio::Cuando;
    d.inicio.condicion = "medio:z3.termino";
    d.fin.modo = ModoFin::AlTerminar;
    Paso e = paso("e", 10);
    e.inicio.modo = ModoInicio::Tras;
    e.inicio.de = "a";
    Paso f = paso("f", 10);
    f.inicio.modo = ModoInicio::Con;
    f.inicio.masMs = 700;
    Bloque b1 = bloque("b1", {a, b, c});
    b1.presupuestoSeg = 120;
    b1.nota = "la apertura";
    Bloque b2 = bloque("b2", {d, e, f});
    b2.inicio = Inicio{};
    b2.inicio->modo = ModoInicio::Hora;
    b2.inicio->hora = "20:00";
    b2.inicio->politica = Politica::Esperar;
    b2.plantilla = "punto";
    b2.origen = "asamblea/orden-del-dia";
    l.bloques = {b1, b2};
    ItemEstante it;
    it.id = "aplauso";
    it.tecla = "F1";
    it.boton = "Aplausos";
    it.color = "#ff0";
    it.pieza = pieza("adlib1", "audio", "musica", json{{"archivo", "Medios/aplausos.wav"}});
    l.estante.push_back(it);
    ItemEstante it2;
    it2.id = "corte";
    it2.paso = paso("corte", 20);
    l.estante.push_back(it2);
    Disparador di;
    di.id = "d1";
    di.cuando = "tecla:F5";
    di.hacer = json{{"herramienta", "qr_mostrar"}};
    di.activo = Vida::Bloque;
    l.disparadores.push_back(di);
    Plantilla pl;
    pl.id = "entrevista";
    pl.nombre = "Entrevista";
    pl.pasos = {paso("t1", 60)};
    l.plantillas.push_back(pl);

    const json j = l.aJson();
    CHECK(j["version"] == 2, "la línea se guarda como versión 2");
    const Linea l2 = Linea::deJson(j);
    CHECK(l2.aJson() == j, "JSON ida y vuelta sin pérdida");
    CHECK(l2.bloques[0].pasos[1].inicio.politica == Politica::Cortar, "la política del hard start vuelve");
    CHECK(l2.bloques[0].pasos[0].piezas[1].siSePerdio == SiSePerdio::Preguntar, "el siSePerdio vuelve");
    CHECK(!l2.bloques[0].pasos[0].piezas[0].siSePerdio, "sin siSePerdio no se inventa uno");
    CHECK(l2.estante[0].pieza && l2.estante[1].paso, "el estante guarda piezas y pasos");
    CHECK(l2.bloques[1].inicio && l2.bloques[1].inicio->hora == "20:00", "el inicio del bloque vuelve");

    // Las claves son las del plan, escritas a mano.
    const json escrito = json::parse(R"({
        "version": 2, "salidas": ["principal"],
        "pistas": [{"id":"g","tipo":"graficos","salida":"principal"},{"id":"n","tipo":"notas","salida":null}],
        "bloques": [{"id":"b","nombre":"B","pasos":[
            {"id":"x","inicio":{"modo":"hora","hora":"19:30","politica":"esperar"},
             "fin":{"abierto":true},"continuar":"sigue",
             "piezas":[{"id":"q","pista":"g","tipo":"grafico","desde":{"antesDelFin":10000},
                        "dura":{"ms":5000},"vida":"bloque","siSePerdio":"ejecutar"}]},
            {"id":"y","inicio":{"modo":"con","de":"anterior","masMs":300},"fin":{"mientras":"bus:votacion/abierta"}},
            {"id":"w","inicio":{"modo":"inventado"}}]}]})");
    const Linea l3 = Linea::deJson(escrito);
    const Paso& x = l3.bloques[0].pasos[0];
    CHECK(x.inicio.modo == ModoInicio::Hora && x.inicio.politica == Politica::Esperar, "inicio hora/esperar");
    CHECK(x.fin.modo == ModoFin::Abierto && x.continuar == Continuar::Sigue, "fin abierto y continuar sigue");
    CHECK(x.piezas[0].desde.modo == Ancla::Modo::AntesDelFin && x.piezas[0].desde.ms == 10000, "desde antesDelFin");
    CHECK(x.piezas[0].dura.modo == Dura::Modo::Ms && x.piezas[0].vida == Vida::Bloque, "dura y vida");
    CHECK(l3.bloques[0].pasos[1].inicio.masMs == 300 && l3.bloques[0].pasos[1].fin.modo == ModoFin::Mientras, "con y mientras");
    CHECK(l3.bloques[0].pasos[2].inicio.modo == ModoInicio::Go, "un modo desconocido queda en go");
    CHECK(l3.pistas[1].salida.empty(), "una pista global tiene salida null");
    CHECK(Linea::deJson(json("cualquier cosa")).bloques.empty(), "basura no rompe la carga");
}

// --- 2. Migración -----------------------------------------------------------------

v1::Cue cue(v1::Tipo t, const std::string& id, int dur, json datos = json::object(), bool automatico = false) {
    v1::Cue c;
    c.tipo = t;
    c.id = id;
    c.duracionSeg = dur;
    c.datos = std::move(datos);
    c.texto = "texto de " + id;
    c.automatico = automatico;
    return c;
}

void testMigracion() {
    v1::Evento e;
    e.nombre = "Acto";
    v1::Bloque a;
    a.id = "b1";
    a.nombre = "Apertura";
    a.nota = "nota del bloque";
    a.cues = {cue(v1::Tipo::Escena, "c1", 30, json{{"escena", "Placa"}}, true),
              cue(v1::Tipo::Musica, "c2", 90, json{{"archivo", "Medios/himno.wav"}}),
              cue(v1::Tipo::Grafico, "c3", 10, json{{"fuente", "Z"}, {"grafico", "g"}, {"seVa", "al-avanzar"}}, true),
              cue(v1::Tipo::Pausa, "c4", 5),
              cue(v1::Tipo::Grafico, "c5", 0,
                  json{{"fuente", "Z"}, {"grafico", "g"}, {"seVa", "sola"}, {"valores", {{"salidaEn", 4000}}}}),
              cue(v1::Tipo::Espera, "c6", 0, json::object(), true)};
    v1::Bloque b;
    b.id = "b2";
    b.nombre = "Cierre";
    b.cues = {cue(v1::Tipo::Musica, "c7", 20, json{{"archivo", "Medios/cierre.wav"}}),
              cue(v1::Tipo::Orador, "c8", 300, json{{"participante", "p-ana"}})};
    e.bloques = {a, b};

    const Linea l = migrar(e);
    CHECK(l.salidas == std::vector<std::string>{"principal"}, "una salida: principal");
    CHECK(l.bloques.size() == 2 && l.bloques[0].nota == "nota del bloque", "los bloques quedan igual");
    const auto& p0 = l.bloques[0].pasos;
    CHECK(p0.size() == 3, "escena, pausa y espera abren pasos");
    CHECK(p0[0].id == "c1" && p0[0].piezas.size() == 3, "el paso de la escena lleva escena, música y gráfico");
    CHECK(p0[0].piezas[0].tipo == "escena" && p0[0].piezas[0].pista == "escena-principal", "la escena en su pista");
    CHECK(p0[0].piezas[1].desde.modo == Ancla::Modo::Ms && p0[0].piezas[1].desde.ms == 30 * S,
          "la música encadenada entra a la suma de lo de antes");
    CHECK(p0[0].piezas[1].vida == Vida::Evento, "la música sigue hasta que otra la reemplace");
    CHECK(p0[0].piezas[2].desde.modo == Ancla::Modo::Manual, "un cue que esperaba el clic queda manual");
    CHECK(p0[0].piezas[2].vida == Vida::AlAvanzar, "seVa al-avanzar → vida alAvanzar");
    CHECK(p0[0].duracionPrevistaSeg == 130 && p0[0].fin.duracionSeg == 130, "el paso dura lo que sus cues");
    CHECK(p0[0].continuar == Continuar::Sigue, "el último cue encadenaba: continuar sigue");
    // La pausa sigue sola hasta lo que tiene adentro (c5 a los 5 s); el paso
    // siguiente lo decide el último cue, c5, que era manual.
    CHECK(p0[1].id == "c4" && p0[1].continuar == Continuar::No && p0[1].fin.duracionSeg == 5,
          "la pausa es un paso con su duración");
    CHECK(p0[1].piezas.size() == 1 && p0[1].piezas[0].desde.ms == 5 * S, "lo que sigue a la pausa va a tiempo");
    CHECK(p0[1].piezas[0].dura.modo == Dura::Modo::Ms && p0[1].piezas[0].dura.ms == 4000, "seVa sola: dura salidaEn");
    CHECK(p0[2].id == "c6" && p0[2].inicio.modo == ModoInicio::Go && p0[2].notaOperador == "texto de c6",
          "la espera es un paso con GO y su texto");
    CHECK(p0[2].continuar == Continuar::No && p0[2].fin.modo == ModoFin::Abierto,
          "la espera nunca se va sola, aunque diga automática (como en el v1): es abierta");
    const auto& p1 = l.bloques[1].pasos;
    CHECK(p1.size() == 2 && p1[0].id == "b2-inicio", "un bloque que arranca con música abre un paso sin escena");
    CHECK(p1[0].piezas[0].id == "c7" && p1[0].piezas[0].desde.ms == 0, "la música del arranque entra con el paso");
    CHECK(p1[1].id == "c8" && p1[1].piezas[0].tipo == "orador" && p1[1].piezas[0].pista == "sala", "el orador abre paso");
    int totalV1 = e.duracionSeg(), totalV2 = 0;
    for (const Bloque& bb : l.bloques)
        for (const Paso& pp : bb.pasos)
            totalV2 += pp.duracionPrevistaSeg;
    CHECK(totalV1 == totalV2, "la duración total no cambia");
    bool hayAudio = false;
    for (const Pista& pi : l.pistas)
        hayAudio = hayAudio || (pi.id == "audio-principal" && pi.salida == "principal");
    CHECK(hayAudio && l.pista("escena-principal") && l.pista("sala") && !l.pista("pantallas"), "solo las pistas que se usan");
    CHECK(Linea::deJson(l.aJson()).aJson() == l.aJson(), "lo migrado se guarda y se carga igual");
    CHECK(revisar(l, {}).size() < 50, "lo migrado se puede revisar");

    // Una pausa sola al final de su paso sigue sola, como en el v1.
    v1::Evento e2;
    v1::Bloque c;
    c.id = "b";
    c.cues = {cue(v1::Tipo::Escena, "x", 10, json{{"escena", "E"}}), cue(v1::Tipo::Pausa, "y", 3)};
    e2.bloques = {c};
    const Linea l2 = migrar(e2);
    CHECK(l2.bloques[0].pasos.size() == 2 && l2.bloques[0].pasos[1].continuar == Continuar::Sigue &&
              l2.bloques[0].pasos[1].piezas.empty(),
          "pausa: paso sin escena, con duración y continuar sigue");
}

// --- 3. Pronóstico básico ---------------------------------------------------------------

void testPronostico() {
    const Linea l = tresPasos();
    // Antes de arrancar: todo a partir de la hora prevista (regla 1).
    Resolucion r = resolver(l, entrada(T0 - 10 * M));
    CHECK(pr(r, "p1").frenteMs == T0 && pr(r, "p1").finMs == T0 + 60 * S, "p1 a la hora prevista");
    CHECK(pr(r, "p2").frenteMs == T0 + 60 * S && pr(r, "p3").frenteMs == T0 + 120 * S, "los GO se suponen al terminar el anterior");
    CHECK(pr(r, "p1").estado == EstadoPaso::Proximo && pr(r, "p2").estado == EstadoPaso::Futuro, "próximo y futuro");
    CHECK(r.salida("principal")->proximo == "p1" && r.salida("principal")->alAire.empty(), "nada al aire todavía");
    CHECK(r.salida("principal")->finPlaneadoMs == T0 + 180 * S && r.salida("principal")->overUnderMs == 0, "en hora");
    CHECK(pr(r, "p3").backMs == T0 + 120 * S && pr(r, "p1").backMs == T0, "back times hasta el fin planeado");
    CHECK(!pr(r, "p1").tarde && !pr(r, "p1").estimado && !pr(r, "p1").automatico, "a tiempo, no estimado, con GO");

    // Al aire y pasado de tiempo: empuja todo (regla 2) y se ve el over (regla 4).
    Linea l2 = l;
    l2.bloques[0].presupuestoSeg = 100;
    Entrada e = entrada(T0 + 90 * S);
    e.instancias = {{"principal", "p1", T0, 0, ComoEntro::Go}};
    r = resolver(l2, e);
    CHECK(pr(r, "p1").estado == EstadoPaso::AlAire && pr(r, "p1").finMs == T0 + 90 * S, "el que se pasa termina ahora");
    CHECK(pr(r, "p2").estado == EstadoPaso::Proximo && pr(r, "p2").frenteMs == T0 + 90 * S, "el próximo, empujado");
    CHECK(pr(r, "p3").frenteMs == T0 + 150 * S, "todo lo que viene, empujado");
    CHECK(r.salida("principal")->overUnderMs == 30 * S, "vamos +0:30");
    CHECK(pr(r, "p2").tarde, "p2 arranca después de su back time");
    const auto& bs = r.salida("principal")->bloques;
    CHECK(bs.size() == 2 && bs[0].overUnderMs == 50 * S && bs[0].presupuestoMs == 100 * S, "b1 contra su presupuesto");
    CHECK(bs[1].overUnderMs == 0 && bs[1].presupuestoMs == 60 * S, "b2 contra lo previsto");

    // Un paso cerrado y el siguiente al aire.
    e = entrada(T0 + 70 * S);
    e.instancias = {{"principal", "p1", T0, T0 + 50 * S, ComoEntro::Go}, {"principal", "p2", T0 + 50 * S, 0, ComoEntro::Go}};
    r = resolver(l, e);
    CHECK(pr(r, "p1").estado == EstadoPaso::Pasado && pr(r, "p1").finMs == T0 + 50 * S, "lo emitido con su hora real");
    CHECK(pr(r, "p2").finMs == T0 + 110 * S && pr(r, "p3").frenteMs == T0 + 110 * S, "adelantados");
    CHECK(r.salida("principal")->overUnderMs == -10 * S, "vamos -0:10");

    // Abierto: cuenta la prevista pero es un supuesto, y lo que sigue también.
    Linea l3 = tresPasos();
    l3.bloques[0].pasos[0].fin.modo = ModoFin::Abierto;
    r = resolver(l3, entrada(T0));
    CHECK(pr(r, "p1").estimado && pr(r, "p2").estimado && pr(r, "p1").finMs == T0 + 60 * S, "abierto: estimado");
}

// --- 4. Hard starts ---------------------------------------------------------------------

void testHardStarts() {
    Linea l = lineaBase();
    Paso p2 = paso("p2", 60);
    p2.inicio.modo = ModoInicio::Hora;
    p2.inicio.hora = "19:05";
    l.bloques = {bloque("b1", {paso("p1", 60), p2})};
    // Sobra: el hard start no se mueve y el anterior queda hasta la hora (gap).
    Resolucion r = resolver(l, entrada(T0));
    const auto& hs = r.salida("principal")->hardStarts;
    CHECK(hs.size() == 1 && hs[0].gapMs == 4 * M, "gap de 4 minutos");
    CHECK(pr(r, "p2").frenteMs == T0 + 5 * M && pr(r, "p1").finMs == T0 + 5 * M, "el hard start no se mueve");
    CHECK(!pr(r, "p2").automatico && pr(r, "p2").como == ComoEntro::Go, "avisar: el GO lo da la persona");
    Entrada ef = entrada(T0);
    ef.finPlaneadoMs = T0 + 30 * M;
    r = resolver(l, ef);
    CHECK(pr(r, "p1").backMs == T0 + 4 * M && pr(r, "p2").backMs == T0 + 5 * M, "el back time llega al hard start");

    // Falta: overlap, y avisar/esperar entran cuando se llega.
    l.bloques[0].pasos[0] = paso("p1", 600);
    r = resolver(l, entrada(T0));
    CHECK(r.salida("principal")->hardStarts[0].gapMs == -5 * M, "overlap de 5 minutos");
    CHECK(pr(r, "p2").frenteMs == T0 + 10 * M, "avisar: entra cuando se llega");
    CHECK(pr(r, "p1").tarde && pr(r, "p1").backMs == T0 - 5 * M, "p1 no llega: su back time es antes del arranque");
    l.bloques[0].pasos[1].inicio.politica = Politica::Esperar;
    r = resolver(l, entrada(T0));
    CHECK(pr(r, "p2").automatico && pr(r, "p2").como == ComoEntro::Hora, "esperar: entra solo");

    // Cortar: entra a la hora pase lo que pase; lo que no llega no entra.
    Paso p3 = paso("p3", 60);
    p3.inicio.modo = ModoInicio::Hora;
    p3.inicio.hora = "19:05";
    p3.inicio.politica = Politica::Cortar;
    l.bloques = {bloque("b1", {paso("p1", 600), paso("p2", 60), p3})};
    r = resolver(l, entrada(T0));
    CHECK(pr(r, "p3").frenteMs == T0 + 5 * M && pr(r, "p3").automatico, "cortar: a la hora exacta");
    CHECK(pr(r, "p2").cortadoPorHora, "lo que no llega queda cortado");
    CHECK(pr(r, "p1").finMs == T0 + 5 * M, "el que está en el medio se corta a la hora");
    CHECK(r.salida("principal")->hardStarts[0].gapMs == -6 * M, "overlap contra lo previsto");

    // Una hora antes del arranque es del día siguiente (cruza la medianoche).
    l.bloques[0].pasos[2].inicio.hora = "18:00";
    r = resolver(l, entrada(T0));
    CHECK(r.salida("principal")->hardStarts[0].horaMs == 18 * H + 24 * H, "18:00 con arranque 19:00 es mañana");

    // El hard start del bloque vale para su primer paso.
    Linea lb = tresPasos();
    lb.bloques[1].inicio = Inicio{};
    lb.bloques[1].inicio->modo = ModoInicio::Hora;
    lb.bloques[1].inicio->hora = "19:10";
    r = resolver(lb, entrada(T0));
    CHECK(pr(r, "p3").frenteMs == T0 + 10 * M && r.salida("principal")->hardStarts.size() == 1, "hard start del bloque");
}

// --- 5. Continuar, esperas y condiciones ---------------------------------------------------

void testContinuarYCondiciones() {
    Linea l = tresPasos();
    l.bloques[0].pasos[0].continuar = Continuar::Sigue;
    l.bloques[0].pasos[0].postEsperaMs = 2 * S;
    l.bloques[0].pasos[1].preEsperaMs = 1 * S;
    Resolucion r = resolver(l, entrada(T0));
    CHECK(pr(r, "p2").automatico && pr(r, "p2").como == ComoEntro::Auto, "sigue: el siguiente entra solo");
    CHECK(pr(r, "p2").frenteMs == T0 + 63 * S, "fin + post espera + pre espera");
    CHECK(pr(r, "p1").finMs == T0 + 63 * S, "un paso por vez: el anterior sale cuando entra el siguiente");
    CHECK(!pr(r, "p3").automatico, "p2 no encadena: p3 espera el GO");

    l.bloques[0].pasos[0].continuar = Continuar::Con;
    l.bloques[0].pasos[0].postEsperaMs = 10 * S;
    l.bloques[0].pasos[1].preEsperaMs = 0;
    r = resolver(l, entrada(T0));
    CHECK(pr(r, "p2").frenteMs == T0 + 10 * S && pr(r, "p1").finMs == T0 + 10 * S, "con: arranca con el anterior más la espera");

    // Tras un paso con nombre.
    Linea lt = tresPasos();
    lt.bloques[1].pasos[0].inicio.modo = ModoInicio::Tras;
    lt.bloques[1].pasos[0].inicio.de = "p1";
    r = resolver(lt, entrada(T0));
    CHECK(pr(r, "p3").frenteMs == T0 + 60 * S && pr(r, "p3").automatico, "tras p1");

    // Cuando: sin saber, se usa lo previsto y queda estimado (regla 5).
    Linea lc = tresPasos();
    lc.bloques[0].pasos[1].inicio.modo = ModoInicio::Cuando;
    lc.bloques[0].pasos[1].inicio.condicion = "bus:votacion/cerrada";
    Entrada e = entrada(T0 + 10 * S);
    e.instancias = {{"principal", "p1", T0, 0, ComoEntro::Go}};
    r = resolver(lc, e);
    CHECK(pr(r, "p2").estimado && pr(r, "p2").frenteMs == T0 + 60 * S && pr(r, "p2").automatico, "cuando sin saber: estimado");
    CHECK(pr(r, "p3").estimado, "lo que sigue a un estimado también lo es");
    e.condiciones["bus:votacion/cerrada"] = {true, T0 + 30 * S};
    e.ahoraMs = T0 + 30 * S;
    r = resolver(lc, e);
    CHECK(!pr(r, "p2").estimado && pr(r, "p2").frenteMs == T0 + 30 * S, "cuando se cumplió: a esa hora");
    CHECK(pr(r, "p1").finMs == T0 + 30 * S, "y el anterior sale ahí");

    // Fin al terminar el medio y mientras, con lo que se sabe.
    Linea lm = tresPasos();
    lm.bloques[0].pasos[0].fin.modo = ModoFin::AlTerminar;
    lm.bloques[0].pasos[0].piezas.push_back(pieza("zv", "audio", "video", json{{"archivo", "v.mp4"}}));
    e = entrada(T0 + 5 * S);
    e.instancias = {{"principal", "p1", T0, 0, ComoEntro::Go}};
    r = resolver(lm, e);
    CHECK(pr(r, "p1").estimado, "al terminar el medio, sin saber cuándo: estimado");
    e.condiciones["medio:zv.termino"] = {true, T0 + 42 * S};
    e.ahoraMs = T0 + 42 * S;
    r = resolver(lm, e);
    CHECK(!pr(r, "p1").estimado && pr(r, "p1").finMs == T0 + 42 * S, "el medio terminó: fin conocido");
    Linea lw = tresPasos();
    lw.bloques[0].pasos[0].fin.modo = ModoFin::Mientras;
    lw.bloques[0].pasos[0].fin.mientras = "bus:votacion/abierta";
    e = entrada(T0 + 80 * S);
    e.instancias = {{"principal", "p1", T0, 0, ComoEntro::Go}};
    e.condiciones["bus:votacion/abierta"] = {false, T0 + 75 * S};
    r = resolver(lw, e);
    CHECK(!pr(r, "p1").estimado && pr(r, "p1").finMs == T0 + 80 * S, "mientras: terminó (y está al aire hasta ahora)");
}

// --- 6. Holds, salteados y próximo --------------------------------------------------------

void testHoldsYSaltos() {
    Linea l = tresPasos();
    l.bloques[0].pasos[0].continuar = Continuar::Sigue;
    Entrada e = entrada(T0 + 90 * S);
    e.instancias = {{"principal", "p1", T0, 0, ComoEntro::Go}};
    Resolucion r = resolver(l, e);
    CHECK(pr(r, "p2").automatico, "sin hold, p2 entra solo");
    bool entra = std::any_of(r.proximosEventos.begin(), r.proximosEventos.end(), [](const ProximoEvento& p) {
        return p.que == ProximoEvento::Que::EntraPaso && p.paso == "p2";
    });
    CHECK(entra, "y figura en lo que entra solo");
    e.holds = {{"principal", "p1"}};
    r = resolver(l, e);
    CHECK(pr(r, "p1").enHold && !pr(r, "p2").automatico && pr(r, "p2").como == ComoEntro::Go, "con hold no avanza solo");
    CHECK(pr(r, "p1").finMs == T0 + 90 * S && pr(r, "p2").frenteMs == T0 + 90 * S, "y empuja el pronóstico");
    entra = std::any_of(r.proximosEventos.begin(), r.proximosEventos.end(),
                        [](const ProximoEvento& p) { return p.que == ProximoEvento::Que::EntraPaso; });
    CHECK(!entra, "con hold nada entra solo");
    e.holds = {{"principal", ""}};
    r = resolver(l, e);
    CHECK(!pr(r, "p2").automatico, "hold de la salida entera");

    // Salteados, próximo movido hacia adelante y hacia atrás.
    Linea l4 = lineaBase();
    l4.bloques = {bloque("b1", {paso("p1", 60), paso("p2", 60), paso("p3", 60), paso("p4", 60), paso("p5", 60)})};
    e = entrada(T0 + 70 * S);
    e.instancias = {{"principal", "p1", T0, T0 + 60 * S, ComoEntro::Go}, {"principal", "p2", T0 + 60 * S, 0, ComoEntro::Go}};
    e.salteados = {{"principal", "p3"}};
    r = resolver(l4, e);
    CHECK(pr(r, "p3").estado == EstadoPaso::Salteado && pr(r, "p4").estado == EstadoPaso::Proximo, "saltar p3");
    CHECK(pr(r, "p4").frenteMs == T0 + 120 * S, "el salteado no ocupa tiempo");
    e.salteados.clear();
    e.proximos = {{"principal", "p5"}};
    r = resolver(l4, e);
    CHECK(pr(r, "p5").estado == EstadoPaso::Proximo && pr(r, "p3").estado == EstadoPaso::Salteado &&
              pr(r, "p4").estado == EstadoPaso::Salteado,
          "el próximo movido a p5 saltea lo del medio");
    e.proximos = {{"principal", "p1"}};
    r = resolver(l4, e);
    CHECK(pr(r, "p1").estado == EstadoPaso::Proximo && pr(r, "p1").frenteMs == T0 + 120 * S, "volver a p1: se repite");
    CHECK(pr(r, "p3").estado == EstadoPaso::Futuro, "y sigue de ahí en adelante");
    // Una instancia con otra «al aire» vieja se cierra cuando entró la siguiente.
    e = entrada(T0 + 70 * S);
    e.instancias = {{"principal", "p1", T0, 0, ComoEntro::Go}, {"principal", "p2", T0 + 60 * S, 0, ComoEntro::Go}};
    r = resolver(l4, e);
    CHECK(pr(r, "p1").estado == EstadoPaso::Pasado && pr(r, "p1").finMs == T0 + 60 * S, "instancia vieja cerrada");
}

// --- 7. Estado en t y próximos eventos ------------------------------------------------------

void testEstadoYProximos() {
    Linea l = tresPasos();
    auto& p1 = l.bloques[0].pasos[0];
    auto& p2 = l.bloques[0].pasos[1];
    p1.piezas.push_back(pieza("zA", "graficos", "grafico", json{{"fuente", "F"}, {"grafico", "a"}}, 0, Vida::Evento));
    p1.piezas.push_back(pieza("zM", "audio", "musica", json{{"archivo", "m.wav"}}, 0, Vida::Bloque));
    p1.piezas.push_back(pieza("zQ", "notas", "nota", json::object(), 0, Vida::HastaQueSeSaque));
    p2.piezas.push_back(pieza("zB", "graficos", "grafico", json{{"fuente", "F"}, {"grafico", "b"}}, 10 * S));
    Pieza zN = pieza("zN", "audio", "musica", json{{"archivo", "n.wav"}});
    zN.prioridad = -1;
    p2.piezas.push_back(zN);
    Pieza zF = pieza("zF", "dsk", "grafico", json{{"fuente", "D"}, {"grafico", "f"}});
    zF.desde.modo = Ancla::Modo::AntesDelFin;
    zF.desde.ms = 10 * S;
    p2.piezas.push_back(zF);
    Pieza zMan = pieza("zMan", "dsk", "grafico", json{{"fuente", "D"}, {"grafico", "m"}});
    zMan.desde.modo = Ancla::Modo::Manual;
    zMan.prioridad = 9;
    p2.piezas.push_back(zMan);
    Pieza zC = pieza("zC", "notas", "nota", json::object(), 0, Vida::Paso);
    zC.desde.modo = Ancla::Modo::Cuando;
    zC.desde.cuando = "tecla:F5";
    p2.piezas.push_back(zC);

    Resolucion r = resolver(l, entrada(T0));
    CHECK(enPista(r, T0 + 80 * S, "graficos") == "zB", "en la misma pista gana la que arrancó más tarde");
    CHECK(enPista(r, T0 + 80 * S, "audio") == "zM", "pero la prioridad manda");
    CHECK(enPista(r, T0 + 80 * S, "notas") == "zQ", "hasta que se saque");
    CHECK(enPista(r, T0 + 80 * S, "dsk").empty(), "antes del fin todavía no; manual nunca");
    CHECK(enPista(r, T0 + 115 * S, "dsk") == "zF", "10 s antes del fin");
    CHECK(enPista(r, T0 + 130 * S, "graficos") == "zA", "cuando se va la de su paso, vuelve la de vida evento");
    CHECK(enPista(r, T0 + 130 * S, "audio").empty(), "la de vida bloque se fue con el bloque");
    CHECK(enPista(r, T0 + 30 * H, "notas") == "zQ", "hasta que se saque es para siempre");
    CHECK(std::none_of(r.piezas.begin(), r.piezas.end(), [](const PiezaResuelta& z) { return z.pieza == "zC"; }),
          "una pieza «cuando» sin saber no se programa");
    Entrada e = entrada(T0);
    e.condiciones["tecla:F5"] = {true, T0 + 70 * S};
    r = resolver(l, e);
    CHECK(enPista(r, T0 + 75 * S, "notas") == "zC", "con la condición cumplida entra (y gana por más nueva)");

    // Próximos eventos: lo que entra solo desde el aire, sin GO en el medio.
    Linea lp = tresPasos();
    auto& q1 = lp.bloques[0].pasos[0];
    q1.continuar = Continuar::Sigue;
    q1.piezas.push_back(pieza("z0", "graficos", "grafico", json{{"fuente", "F"}, {"grafico", "x"}}));
    Pieza z1 = pieza("z1", "dsk", "grafico", json{{"fuente", "D"}, {"grafico", "y"}}, 10 * S);
    z1.dura.modo = Dura::Modo::Ms;
    z1.dura.ms = 5 * S;
    q1.piezas.push_back(z1);
    lp.bloques[0].pasos[1].piezas.push_back(pieza("z2", "graficos", "grafico", json{{"fuente", "F"}, {"grafico", "z"}}, 3 * S));
    lp.bloques[1].pasos[0].piezas.push_back(pieza("z3", "graficos", "grafico", json{{"fuente", "F"}, {"grafico", "w"}}));
    e = entrada(T0 + 5 * S);
    e.instancias = {{"principal", "p1", T0, 0, ComoEntro::Go}};
    r = resolver(lp, e);
    const auto& pe = r.proximosEventos;
    CHECK(pe.size() == 4, "entra z1, sale z1, entra p2 y su z2");
    if (pe.size() == 4) {
        CHECK(pe[0].que == ProximoEvento::Que::EntraPieza && pe[0].pieza == "z1" && pe[0].ms == T0 + 10 * S, "z1 a los 10 s");
        CHECK(pe[1].que == ProximoEvento::Que::SalePieza && pe[1].pieza == "z1" && pe[1].ms == T0 + 15 * S, "z1 se va sola");
        CHECK(pe[2].que == ProximoEvento::Que::EntraPaso && pe[2].paso == "p2" && pe[2].ms == T0 + 60 * S, "p2 entra solo");
        CHECK(pe[3].pieza == "z2" && pe[3].ms == T0 + 63 * S, "y su pieza");
    }
}

// --- 8. Varias salidas ------------------------------------------------------------------------

Linea dosSalidas() {
    Linea l;
    l.salidas = {"principal", "sala"};
    l.pistas = {pista("esc-p", "escena", "principal"), pista("esc-s", "escena", "sala"), pista("gra-p", "graficos", "principal"),
                pista("gra-s", "graficos", "sala"),     pista("ptz", "ptz"),               pista("notas", "notas")};
    Paso a1 = paso("a1", 60, "principal");
    a1.piezas = {pieza("a1e", "esc-p", "escena", json{{"escena", "Cam1"}}),
                 pieza("a1z", "ptz", "ptz", json{{"camara", "2"}}),
                 pieza("a1y", "ptz", "ptz", json{{"camara", "1"}}),
                 pieza("a1g", "gra-p", "grafico", json{{"fuente", "Zocalo"}, {"grafico", "nombre"}, {"valores", {{"t", "A"}}}})};
    Paso a2 = paso("a2", 60, "principal");
    a2.piezas = {pieza("a2e", "esc-p", "escena", json{{"escena", "General"}}), pieza("a2z", "ptz", "ptz", json{{"camara", "2"}})};
    Paso s0 = paso("s0", 60, "sala");
    s0.piezas = {pieza("s0e", "esc-s", "escena", json{{"escena", "Cam3"}}),
                 pieza("s0g", "gra-s", "grafico", json{{"fuente", "Zocalo"}, {"grafico", "nombre"}, {"valores", {{"t", "B"}}}})};
    Paso s1 = paso("s1", 60, "sala");
    s1.inicio.modo = ModoInicio::Ancla;
    s1.inicio.salida = "principal";
    s1.inicio.paso = "a2";
    s1.piezas = {pieza("s1e", "esc-s", "escena", json{{"escena", "Cam2"}})};
    Paso h = paso("h", 60, "*");
    h.piezas = {pieza("hp", "esc-p", "escena", json{{"escena", "Himno P"}}), pieza("hs", "esc-s", "escena", json{{"escena", "Himno S"}}),
                pieza("hn", "notas", "nota", json::object())};
    l.bloques = {bloque("b1", {a1, s0, s1, a2, h})};
    return l;
}

std::vector<std::string> camaras(const std::string& escena) {
    if (escena == "Cam1") return {"1"};
    if (escena == "Cam2") return {"2"};
    if (escena == "Cam3") return {"3"};
    if (escena == "General") return {"1", "2", "3"};
    return {};
}

void testVariasSalidas() {
    const Linea l = dosSalidas();
    Entrada e = entrada(T0);
    e.camarasDeEscena = camaras;
    const Resolucion r = resolver(l, e);
    CHECK(r.salidas.size() == 2, "dos salidas");
    CHECK(pr(r, "s1", "sala").frenteMs == pr(r, "a2").frenteMs && pr(r, "s1", "sala").como == ComoEntro::Ancla,
          "la sala entra con a2 de la principal");
    CHECK(pr(r, "s1", "sala").automatico, "un ancla entra sola");
    CHECK(pr(r, "h").frenteMs == T0 + 120 * S && pr(r, "h", "sala").frenteMs == T0 + 120 * S, "el momento compartido está en las dos");
    int hp = 0, hs = 0, hn = 0;
    for (const PiezaResuelta& z : r.piezas) {
        hp += z.pieza == "hp" && z.salida == "principal";
        hs += z.pieza == "hs" && z.salida == "sala";
        hn += z.pieza == "hn";
        if (z.pieza == "hn") CHECK(z.compartida, "una pista global en un paso * es compartida");
        if (z.pieza == "hp") CHECK(z.salida == "principal", "la escena de cada salida va solo en la suya");
    }
    CHECK(hp == 1 && hs == 1 && hn == 1, "cada pieza del momento compartido una sola vez");

    // Conflictos: la PTZ de a1 mueve la 2, armada en la sala (s1 viene después de s0);
    // la de a2 mueve la 2, al aire en la sala. La de la cámara 1 es de la propia salida.
    int alAire = 0, armada = 0, graficos = 0;
    for (const Conflicto& c : r.conflictos) {
        if (c.tipo == Conflicto::Tipo::Ptz) {
            CHECK(c.recurso == "2" && c.otraSalida == "sala", "el conflicto nombra cámara y salida");
            if (c.pieza == "a2z" && !c.armada && c.otroPaso == "s1") ++alAire;
            if (c.pieza == "a1z" && c.armada && c.otroPaso == "s1") ++armada;
            CHECK(c.pieza != "a1y", "mover la cámara de la propia salida no es conflicto");
        } else {
            CHECK(c.recurso == "Zocalo", "la fuente compartida");
            ++graficos;
        }
    }
    CHECK(alAire == 1 && armada == 1, "PTZ sobre cámara al aire y armada en otra salida");
    CHECK(graficos == 1, "la misma fuente con contenidos distintos en dos salidas");
    Entrada sinCamaras = entrada(T0);
    const Resolucion r2 = resolver(l, sinCamaras);
    CHECK(std::none_of(r2.conflictos.begin(), r2.conflictos.end(),
                       [](const Conflicto& c) { return c.tipo == Conflicto::Tipo::Ptz; }),
          "sin saber las cámaras de cada escena no se chequea la PTZ");
    // Mismo contenido: no hay conflicto.
    Linea l3 = l;
    l3.bloques[0].pasos[1].piezas[1].datos["valores"]["t"] = "A";
    CHECK(resolver(l3, sinCamaras).conflictos.empty(), "la misma fuente con lo mismo no choca");
}

// --- 9. Revisión ---------------------------------------------------------------------------

bool hay(const std::vector<Reparo>& rs, const std::string& paso, const std::string& pieza, bool grave, const char* que = "") {
    for (const Reparo& r : rs)
        if (r.paso == paso && r.pieza == pieza && r.grave == grave && r.texto.find(que) != std::string::npos) return true;
    return false;
}

void testRevisar() {
    CHECK(revisar(Linea{}, {}).size() == 1 && revisar(Linea{}, {})[0].grave, "sin pasos es grave");

    Linea l = lineaBase();
    Paso a = paso("a", 60);
    a.piezas = {pieza("e1", "escena", "escena", json{{"escena", "Nada"}}), pieza("g1", "graficos", "grafico", json::object()),
                pieza("x1", "inexistente", "nota", json::object()), pieza("p1", "notas", "ptz", json::object()),
                pieza("as", "asistente", "asistente", json::object())};
    Paso dup = paso("a", 10);
    Paso t = paso("t", 10);
    t.inicio.modo = ModoInicio::Tras;
    t.inicio.de = "zz";
    Paso h1 = paso("h1", 10);
    h1.inicio.modo = ModoInicio::Hora;
    h1.inicio.hora = "19:30";
    Paso h2 = paso("h2", 10);
    h2.inicio.modo = ModoInicio::Hora;
    h2.inicio.hora = "19:20";
    Paso hm = paso("hm", 10);
    hm.inicio.modo = ModoInicio::Hora;
    hm.inicio.hora = "25:99";
    Paso an = paso("an", 10);
    an.inicio.modo = ModoInicio::Ancla;
    an.inicio.salida = "principal";
    an.inicio.paso = "nadie";
    l.bloques = {bloque("b1", {a, dup, t, h1, h2, hm, an})};
    OpcionesRevision o;
    o.contexto = entrada(T0);
    o.existeEscena = [](const std::string& e) { return e != "Nada"; };
    const auto rs = revisar(l, o);
    CHECK(hay(rs, "a", "", true, "dos pasos"), "id de paso repetido");
    CHECK(hay(rs, "a", "e1", true, "Nada"), "escena que no existe");
    CHECK(hay(rs, "a", "g1", true), "gráfico incompleto (la regla del v1)");
    CHECK(hay(rs, "a", "x1", true, "inexistente"), "pista que no existe");
    CHECK(hay(rs, "a", "p1", true), "PTZ sin cámara");
    CHECK(hay(rs, "a", "as", true), "asistente sin herramienta");
    CHECK(hay(rs, "t", "", true, "zz"), "tras un paso que no existe");
    CHECK(hay(rs, "h2", "", true), "hora fija anterior a la de antes");
    CHECK(hay(rs, "hm", "", true, "25:99"), "hora mal escrita");
    CHECK(hay(rs, "an", "", true, "nadie"), "ancla a un paso que no existe");

    // Anclas en círculo entre dos salidas.
    Linea lc = lineaBase();
    lc.salidas = {"principal", "sala"};
    lc.pistas.push_back(pista("esc-s", "escena", "sala"));
    Paso x = paso("x", 10, "principal");
    x.inicio.modo = ModoInicio::Ancla;
    x.inicio.salida = "sala";
    x.inicio.paso = "y";
    Paso y = paso("y", 10, "sala");
    y.inicio.modo = ModoInicio::Ancla;
    y.inicio.salida = "principal";
    y.inicio.paso = "x";
    lc.bloques = {bloque("b", {x, y})};
    CHECK(hay(revisar(lc, o), "x", "", true, "círculo"), "anclas en círculo");

    // Cadena larga de pasos que entran solos.
    Linea ll = lineaBase();
    std::vector<Paso> ps;
    for (int i = 0; i < 8; ++i) {
        Paso p = paso("c" + std::to_string(i), 10);
        p.continuar = Continuar::Sigue;
        ps.push_back(p);
    }
    ll.bloques = {bloque("b", ps)};
    const auto rl = revisar(ll, o);
    CHECK(hay(rl, "c6", "", false), "seis pasos seguidos entrando solos: aviso");

    // Hard start al que no se llega: aviso; si corta, grave.
    Linea lh = lineaBase();
    Paso hh = paso("hh", 10);
    hh.inicio.modo = ModoInicio::Hora;
    hh.inicio.hora = "19:05";
    lh.bloques = {bloque("b", {paso("largo", 600), hh})};
    CHECK(hay(revisar(lh, o), "hh", "", false, "300"), "no se llega a la hora: faltan 300 s");
    lh.bloques[0].pasos[1].inicio.politica = Politica::Cortar;
    CHECK(hay(revisar(lh, o), "hh", "", true), "con cortar es grave");

    // Conflicto de PTZ: grave.
    OpcionesRevision oc;
    oc.contexto = entrada(T0);
    oc.contexto.camarasDeEscena = camaras;
    const auto rc = revisar(dosSalidas(), oc);
    CHECK(hay(rc, "s1", "a2z", true, "al aire"), "PTZ sobre una cámara al aire en otra salida: grave");
    CHECK(hay(rc, "s1", "a1z", true, "armada"), "PTZ sobre una cámara armada: grave");
    CHECK(hay(rc, "s0", "a1g", false, "Zocalo"), "gráfico compartido con otro contenido: aviso");
}

// --- 10. Load to time y retoma ---------------------------------------------------------------

void testCargarEn() {
    Linea l = lineaBase();
    Paso p1 = paso("p1", 60);
    p1.piezas = {pieza("zM", "audio", "musica", json{{"archivo", "m.wav"}}, 0, Vida::Evento),
                 pieza("zG", "graficos", "grafico", json{{"fuente", "F"}, {"grafico", "g"}}, 0, Vida::Bloque)};
    Paso p2 = paso("p2", 60);
    p2.piezas = {pieza("zZ", "dsk", "grafico", json{{"fuente", "D"}, {"grafico", "z"}})};
    Paso p3 = paso("p3", 60);
    p3.piezas = {pieza("zA", "asistente", "asistente", json{{"herramienta", "qr_mostrar"}}, 0, Vida::HastaQueSeSaque)};
    Paso p4 = paso("p4", 60);
    l.bloques = {bloque("b1", {p1, p2}), bloque("b2", {p3, p4})};

    const Carga c = cargarEn(l, entrada(T0), "principal", "p4", {"zZ", "zM", "zX"});
    auto accion = [&](const std::string& id) -> int {
        for (const Reposicion& r : c.deberianEstar)
            if (r.pieza.pieza == id) return static_cast<int>(r.accion);
        return -1;
    };
    CHECK(accion("zM") == static_cast<int>(SiSePerdio::Ejecutar), "la música de vida evento se repone");
    CHECK(accion("zA") == static_cast<int>(SiSePerdio::Preguntar), "la del asistente pregunta");
    CHECK(accion("zG") < 0 && accion("zZ") < 0, "lo de vida bloque o paso de antes no va");
    CHECK(c.sobran == std::vector<std::string>({"zZ", "zX"}), "sobran las que no van");

    // Si el destino trae su propia música, la vieja no se repone.
    l.bloques[1].pasos[1].piezas.push_back(pieza("zM2", "audio", "musica", json{{"archivo", "n.wav"}}));
    const Carga c2 = cargarEn(l, entrada(T0), "principal", "p4", {"zM"});
    CHECK(std::none_of(c2.deberianEstar.begin(), c2.deberianEstar.end(),
                       [](const Reposicion& r) { return r.pieza.pieza == "zM"; }),
          "el destino la reemplaza");
    CHECK(c2.sobran == std::vector<std::string>({"zM"}), "y la vieja sobra");
    CHECK(cargarEn(l, entrada(T0), "principal", "nadie").deberianEstar.empty(), "un destino que no existe no carga nada");
}

void testRetoma() {
    Linea l = lineaBase();
    Paso p1 = paso("p1", 60);
    p1.continuar = Continuar::Sigue;
    Pieza zZoc = pieza("zZoc", "graficos", "grafico", json{{"fuente", "F"}, {"grafico", "z"}}, 5 * S);
    zZoc.dura.modo = Dura::Modo::Ms;
    zZoc.dura.ms = 10 * S;
    p1.piezas = {pieza("zMus", "audio", "musica", json{{"archivo", "m.wav"}}, 0, Vida::Evento), zZoc,
                 pieza("zAsis", "asistente", "asistente", json{{"herramienta", "stream_titulo"}}, 30 * S)};
    l.bloques = {bloque("b1", {p1, paso("p2", 60), paso("p3", 60)})};

    EstadoAire g;
    g.escritoMs = T0 + 20 * S;
    g.salidas = {{"principal", "p1", T0, "p2", false}};
    g.piezas = {{"zMus", "principal", T0}, {"zZoc", "principal", T0 + 5 * S}};
    g.instancias = {{"principal", "p1", T0, 0, ComoEntro::Go}};
    const json gj = g.aJson();
    CHECK(EstadoAire::deJson(gj).aJson() == gj, "linea-aire.json ida y vuelta");

    const Retoma rt = retomarTrasCorte(l, g, entrada(T0 + 100 * S));
    CHECK(rt.estado.pideDescripcion && rt.estado.salidas.size() == 1 && rt.estado.salidas[0].hold,
          "vuelve en HOLD y pide qué está pasando");
    CHECK(rt.reponer.size() == 1 && rt.reponer[0].pieza.pieza == "zMus", "la música vuelve como estaba");
    CHECK(rt.sacar == std::vector<std::string>({"zZoc"}), "el zócalo que ya se fue no vuelve");
    // p1 sigue al aire (pasado de tiempo, en HOLD): lo de su vida sigue activo.
    CHECK(rt.perdidas.size() == 1 && rt.perdidas[0].pieza.pieza == "zAsis" && rt.perdidas[0].accion == SiSePerdio::Preguntar &&
              rt.perdidas[0].sigueActiva,
          "la del asistente que tocaba durante el corte pregunta");
    const json& rs = rt.resumen;
    CHECK(rs["corte"]["seg"] == 80 && rs["pideDescripcion"] == true && rs["hold"] == true, "el resumen dice el corte");
    const json& s0 = rs["salidas"][0];
    CHECK(s0["alAire"]["paso"] == "p1" && s0["alAire"]["llevaSeg"] == 100 && s0["alAire"]["excesoSeg"] == 40, "lo que está al aire");
    CHECK(s0["proximo"]["paso"] == "p2" && s0["proximo"]["entra"] == "go", "el próximo espera el GO (HOLD)");
    CHECK(s0["segunElPlan"]["paso"] == "p2" && s0["segunElPlan"]["soloConAutomaticos"] == true,
          "según el plan ya iría por p2, que entraba solo");
    CHECK(s0["overUnderSeg"] == 40, "over del evento");
    CHECK(rs["perdidas"][0]["accion"] == "preguntar" && rs["reponer"][0]["tipo"] == "musica", "las piezas en el resumen");

    // El estado retomado vuelve a resolver en HOLD.
    Entrada e = entrada(T0 + 100 * S);
    e.instancias = rt.estado.instancias;
    for (const SalidaAire& s : rt.estado.salidas)
        if (s.hold) e.holds.push_back({s.salida, {}});
    const Resolucion r = resolver(l, e);
    CHECK(!pr(r, "p2").automatico, "con el HOLD puesto nada entra solo");
}


Linea lineaCorrida(); // la de la corrida (14.)

// Un corte con el paso al aire pasado de tiempo: lo suyo vuelve, no se saca.
void testRetomaPasadoDeTiempo() {
    EstadoAire g;
    g.escritoMs = T0 + 40 * S;
    g.salidas = {{"principal", "p2", T0 + 30 * S, "p3", false}};
    g.piezas = {{"zM", "principal", T0}, {"zE2", "principal", T0 + 30 * S}};
    g.instancias = {{"principal", "p1", T0, T0 + 30 * S, ComoEntro::Go}, {"principal", "p2", T0 + 30 * S, 0, ComoEntro::Auto}};
    const Retoma rt = retomarTrasCorte(lineaCorrida(), g, entrada(T0 + 90 * S));
    CHECK(std::find(rt.sacar.begin(), rt.sacar.end(), "zE2") == rt.sacar.end(), "la escena del paso al aire no se saca");
    CHECK(std::any_of(rt.reponer.begin(), rt.reponer.end(), [](const Reposicion& x) { return x.pieza.pieza == "zE2"; }),
          "la escena del paso al aire vuelve");
}

// --- 11. La fachada del guion de cues (proyectar y reconciliar) ---------------------------

/// El acto de la migración (2.), como v1.
v1::Evento actoV1() {
    v1::Evento e;
    v1::Bloque a;
    a.id = "b1";
    a.nombre = "Apertura";
    a.cues = {cue(v1::Tipo::Escena, "c1", 30, json{{"escena", "Placa"}}, true),
              cue(v1::Tipo::Musica, "c2", 90, json{{"archivo", "Medios/himno.wav"}}),
              cue(v1::Tipo::Grafico, "c3", 10, json{{"fuente", "Z"}, {"grafico", "g"}, {"seVa", "al-avanzar"}}, true),
              cue(v1::Tipo::Pausa, "c4", 5),
              cue(v1::Tipo::Grafico, "c5", 0, json{{"fuente", "Z"}, {"grafico", "g"}, {"seVa", "sola"}, {"valores", {{"salidaEn", 4000}}}}),
              cue(v1::Tipo::Espera, "c6", 0, json::object(), true)};
    v1::Bloque b;
    b.id = "b2";
    b.nombre = "Cierre";
    b.cues = {cue(v1::Tipo::Musica, "c7", 20, json{{"archivo", "Medios/cierre.wav"}}),
              cue(v1::Tipo::Orador, "c8", 300, json{{"participante", "p-ana"}})};
    e.bloques = {a, b};
    return e;
}

bool mismoCue(const v1::Cue& a, const v1::Cue& b) {
    return a.id == b.id && a.tipo == b.tipo && a.texto == b.texto && a.datos == b.datos && a.duracionSeg == b.duracionSeg &&
           a.automatico == b.automatico;
}

/// Una línea escrita a mano, con todo lo que el v1 no sabe decir.
Linea lineaNativa() {
    Linea l = lineaBase();
    l.salidas.push_back("sala");
    l.pistas.push_back(pista("escenaS", "escena", "sala"));
    l.pistas.push_back(pista("ptz", "ptz"));
    l.pistas.push_back(pista("sala", "sala"));
    Paso a = paso("a", 60);
    a.nombre = "Apertura";
    a.inicio.modo = ModoInicio::Hora;
    a.inicio.hora = "19:30";
    a.inicio.politica = Politica::Esperar;
    a.preEsperaMs = 500;
    a.guion = "Buenas noches";
    a.etiquetas = {"recortable"};
    a.transicion = json{{"tipo", "fundido"}, {"ms", 800}};
    Pieza zB = pieza("zB", "graficos", "grafico", json{{"fuente", "Z"}, {"grafico", "g"}});
    zB.desde.modo = Ancla::Modo::AntesDelFin;
    zB.desde.ms = 10 * S;
    Pieza zN = pieza("zN", "notas", "nota", json::object(), 4 * S, Vida::HastaQueSeSaque);
    zN.desde.modo = Ancla::Modo::Manual;
    zN.texto = "aplausos";
    a.piezas = {pieza("zA", "escena", "escena", json{{"escena", "Cámara 1"}}), zB,
                pieza("zP", "ptz", "ptz", json{{"camara", "2"}, {"posicion", "atril"}}, 2 * S), zN};
    Paso b = paso("b", 120);
    b.nombre = "Intervalo";
    b.continuar = Continuar::Con;
    b.fin.modo = ModoFin::Abierto;
    b.piezas = {pieza("zM", "audio", "musica", json{{"archivo", "m.wav"}}, 0, Vida::Evento)};
    Paso c = paso("c", 30, "sala");
    c.inicio.modo = ModoInicio::Ancla;
    c.inicio.salida = "principal";
    c.inicio.paso = "a";
    c.piezas = {pieza("zS", "escenaS", "escena", json{{"escena", "Sala"}})};
    Paso d = paso("d", 300);
    d.nombre = "Habla Ana";
    d.piezas = {pieza("d-orador", "sala", "orador", json{{"participante", "p-ana"}})};
    Bloque b1 = bloque("b1", {a, b});
    Bloque b2 = bloque("b2", {c, d});
    b2.presupuestoSeg = 300;
    l.bloques = {b1, b2};
    ItemEstante it;
    it.id = "corte";
    it.paso = paso("corte", 20);
    l.estante.push_back(it);
    return l;
}

void testProyeccion() {
    // Lo migrado se lee igual que el v1 (la pausa siempre sigue sola: se ve automática).
    const v1::Evento e = actoV1();
    const Linea l = migrar(e);
    const Proyeccion pr = proyectar(l);
    CHECK(pr.bloques.size() == 2 && pr.bloques[0].cues.size() == 6 && pr.bloques[1].cues.size() == 2, "un cue por pieza o paso");
    for (std::size_t b = 0; b < e.bloques.size(); ++b)
        for (std::size_t c = 0; c < e.bloques[b].cues.size() && c < pr.bloques[b].cues.size(); ++c) {
            v1::Cue esperado = e.bloques[b].cues[c];
            if (esperado.tipo == v1::Tipo::Pausa) esperado.automatico = true;
            if (esperado.tipo == v1::Tipo::Espera) esperado.automatico = false; // una espera nunca se va sola
            CHECK(mismoCue(pr.bloques[b].cues[c], esperado), ("el cue " + esperado.id + " se lee igual").c_str());
        }
    CHECK(pr.refs[0][0].paso == "c1" && pr.refs[0][0].pieza == "c1-escena", "el cue que abre el paso es su escena");
    CHECK(pr.refs[0][3].paso == "c4" && pr.refs[0][3].pieza.empty(), "la pausa es el paso mismo");
    CHECK(pr.cueDe("c1", "c3") == std::make_pair(0, 2) && pr.cueDe("b2-inicio") == std::make_pair(1, 0), "y se ubica cada uno");
    CHECK(pr.ref(9, 0) == nullptr, "fuera de rango, nada");
    CHECK(reconciliar(l, pr.bloques).aJson() == l.aJson(), "sin tocar nada, la línea no cambia");

    // Una línea escrita a mano vuelve EXACTA si no se toca el guion de cues.
    const Linea nat = lineaNativa();
    const Proyeccion pn = proyectar(nat);
    CHECK(pn.bloques[0].cues[0].id == "a" && pn.bloques[0].cues[0].tipo == v1::Tipo::Escena, "la escena del principio abre el paso");
    CHECK(pn.bloques[0].cues[2].tipo == v1::Tipo::Nota && pn.bloques[0].cues[2].texto == "ptz",
          "lo que el v1 no conoce (PTZ) se ve como nota que dice qué es");
    CHECK(pn.bloques[0].cues[4].id == "b" && pn.bloques[0].cues[4].tipo == v1::Tipo::Espera, "un paso abierto sin escena es una espera");
    CHECK(reconciliar(nat, pn.bloques).aJson() == nat.aJson(), "ida y vuelta exacta: hard start, salida, PTZ, ancla, estante");

    // Tocar un cue cambia esa pieza y nada más.
    std::vector<v1::Bloque> bs = pn.bloques;
    bs[0].cues[3].texto = "aplausos largos";
    Linea r = reconciliar(nat, bs);
    const json d = diferencias(nat, r);
    CHECK(d.size() == 1 && d[0]["id"] == "zN" && d[0]["que"] == "cambiado", "solo cambia la nota tocada");
    CHECK(r.paso("a")->piezas[3].vida == Vida::HastaQueSeSaque && r.paso("a")->inicio.modo == ModoInicio::Hora,
          "y conserva su vida y el hard start del paso");
    // Tocar la PTZ (vista como nota) no le cambia el tipo.
    bs = pn.bloques;
    bs[0].cues[2].texto = "a la banca 3";
    r = reconciliar(nat, bs);
    CHECK(r.paso("a")->piezas[2].tipo == "ptz" && r.paso("a")->piezas[2].pista == "ptz" &&
              r.paso("a")->piezas[2].texto == "a la banca 3",
          "la PTZ sigue siendo PTZ");
    // Cambiarle solo la duración a la PTZ no le pone «ptz» de texto.
    bs = pn.bloques;
    bs[0].cues[2].duracionSeg += 1;
    r = reconciliar(nat, bs);
    CHECK(r.paso("a")->piezas[2].texto.empty(), "lo que no cambió en el cue no se copia");
    // Alargar un cue corre lo que viene en su paso y el paso dura más.
    bs = pn.bloques;
    bs[0].cues[2].duracionSeg += 10;
    r = reconciliar(nat, bs);
    CHECK(r.paso("a")->piezas[3].desde.ms == 14 * S && r.paso("a")->fin.duracionSeg == 70, "lo de después se corre");
    CHECK(r.paso("a")->piezas[1].desde.modo == Ancla::Modo::AntesDelFin, "lo anclado al fin queda anclado");
    CHECK(r.paso("b")->continuar == Continuar::Con && r.paso("b")->fin.modo == ModoFin::Abierto, "el otro paso, intacto");
    // Agregar un gráfico al final del bloque: pieza nueva del último paso; una escena: paso nuevo.
    bs = pn.bloques;
    bs[1].cues.push_back(cue(v1::Tipo::Grafico, "zNuevo", 5, json{{"fuente", "Z"}, {"grafico", "g"}}));
    bs[1].cues.push_back(cue(v1::Tipo::Escena, "pNuevo", 10, json{{"escena", "Placa"}}));
    r = reconciliar(nat, bs);
    CHECK(r.paso("d")->piezas.size() == 2 && r.paso("d")->piezas[1].id == "zNuevo", "el gráfico va al paso que estaba");
    CHECK(r.paso("pNuevo") && r.paso("pNuevo")->piezas[0].pista == "escena-principal" && r.pista("escena-principal"),
          "la escena abre un paso nuevo (con su pista)");
    CHECK(r.paso("c")->salida == "sala" && r.paso("c")->inicio.modo == ModoInicio::Ancla, "lo de la sala no se pierde");
    // Sacar un cue saca su pieza.
    bs = pn.bloques;
    bs[0].cues.erase(bs[0].cues.begin() + 1);
    r = reconciliar(nat, bs);
    CHECK(r.paso("a")->piezas.size() == 3 && r.paso("a")->piezas[0].id == "zA", "la pieza sale y la escena queda con su id");
}

// --- 12. Editar la línea ---------------------------------------------------------------

void testEditar() {
    const Linea l = lineaNativa();
    Edicion e = editar(l, json::parse(R"([
        {"op":"paso_agregar","bloque":"b1","despuesDe":"a","paso":{"id":"x","nombre":"Video","fin":{"duracion":45}}},
        {"op":"pieza_agregar","paso":"x","pieza":{"id":"zx","pista":"graficos","tipo":"grafico","datos":{"fuente":"Z","grafico":"g"}}},
        {"op":"cambiar","id":"b","campos":{"nombre":"Intervalo corto","fin":{"duracion":60}}},
        {"op":"mover","id":"d","bloque":"b1","despuesDe":""},
        {"op":"quitar","id":"zN"}])"));
    CHECK(e.ok, e.error.c_str());
    CHECK(e.linea.bloques[0].pasos.size() == 4 && e.linea.bloques[0].pasos[0].id == "d" && e.linea.bloques[0].pasos[2].id == "x",
          "agregar y mover en su lugar");
    CHECK(e.linea.paso("b")->nombre == "Intervalo corto" && e.linea.paso("b")->fin.modo == ModoFin::Duracion &&
              e.linea.paso("b")->continuar == Continuar::Con,
          "cambiar reemplaza solo lo que dice");
    int agregados = 0, quitados = 0, cambiados = 0, movidos = 0;
    for (const json& x : e.diff) {
        agregados += x["que"] == "agregado";
        quitados += x["que"] == "quitado";
        cambiados += x["que"] == "cambiado";
        movidos += x["que"] == "movido";
    }
    CHECK(agregados == 2 && quitados == 1 && cambiados == 1 && movidos >= 1, "el diff cuenta lo que pasó");

    // Todo o nada: una que falla no deja nada a medias.
    e = editar(l, json::parse(R"([{"op":"quitar","id":"zN"},{"op":"cambiar","id":"nadie","campos":{}}])"));
    CHECK(!e.ok && e.error.find("2") != std::string::npos, "dice cuál falló");
    CHECK(!editar(l, json::parse(R"([{"op":"paso_agregar","bloque":"b1","paso":{"id":"a"}}])")).ok, "un id repetido no entra");
    CHECK(!editar(l, json::parse(R"([{"op":"cambiar","id":"a","campos":{"id":"otro"}}])")).ok, "el id no se cambia");
    CHECK(!editar(l, json::array()).ok, "sin operaciones no hay edición");
    // En vivo, lo emitido no se toca.
    const auto emitido = [](const std::string& p) { return p == "a"; };
    CHECK(!editar(l, json::parse(R"([{"op":"quitar","id":"zA"}])"), emitido).ok, "una pieza de lo que salió no se toca");
    CHECK(!editar(l, json::parse(R"([{"op":"quitar","id":"b1"}])"), emitido).ok, "ni el bloque que lo tiene");
    CHECK(editar(l, json::parse(R"([{"op":"quitar","id":"zM"}])"), emitido).ok, "lo que viene sí");
    CHECK(!editar(l, json::parse(R"([{"op":"quitar","id":"graficos"}])")).ok, "una pista en uso no se quita");
    CHECK(editar(l, json::parse(R"([{"op":"estante_agregar","item":{"id":"aplauso","pieza":{"id":"zAp","pista":"audio","tipo":"musica"}}}])")).ok,
          "el estante se carga por acá");
}

// --- 13. Ver y ajustar -----------------------------------------------------------------

void testVistaYAjuste() {
    Linea l = tresPasos();
    l.bloques[0].pasos[1].etiquetas = {"recortable"};
    l.bloques[1].pasos[0].etiquetas = {"opcional"};
    l.bloques[0].pasos[0].piezas = {pieza("z1", "escena", "escena", json{{"escena", "Cam 1"}})};
    Entrada en = entrada(T0);
    en.finPlaneadoMs = T0 + 120 * S; // planeado: dos minutos; previsto: tres
    const Resolucion r = resolver(l, en);
    const json v = vista(l, r, "principal", 0, 0, "piezas", T0, [](int64_t ms) { return std::to_string((ms - T0) / S) + "s"; });
    CHECK(v["bloques"].size() == 2 && v["bloques"][0]["pasos"].size() == 2, "bloques y pasos");
    CHECK(v["bloques"][0]["pasos"][0]["piezas"][0]["id"] == "z1" && v["bloques"][0]["pasos"][0]["piezas"][0]["entra"] == "0s",
          "con nivel piezas, las piezas y su hora");
    CHECK(v["overUnderSeg"] == 60 && v["fin"] == "180s", "el over del evento");
    const json vb = vista(l, r, "principal", T0 + 130 * S, 0, "bloques", T0);
    CHECK(vb["bloques"].size() == 1 && !vb["bloques"][0].contains("pasos"), "la ventana y el nivel bloques");
    CHECK(vista(l, r, "nadie", 0, 0, "pasos", T0).is_null(), "una salida que no existe");

    const json a = proponerAjuste(l, r, "principal", T0 + 120 * S);
    CHECK(a["overSeg"] == 60 && a["recortaSeg"] == 30 && a["faltaSeg"] == 0, "recorta la mitad de lo recortable y saltea lo opcional");
    CHECK(a["ops"].size() == 1 && a["ops"][0]["id"] == "p2" && a["saltear"].size() == 1 && a["saltear"][0]["paso"] == "p3",
          "las operaciones para linea_editar");
    const Edicion e = editar(l, a["ops"]);
    CHECK(e.ok && e.linea.paso("p2")->duracionPlanMs() == 30 * S, "y se pueden aplicar tal cual");
    CHECK(proponerAjuste(l, r, "principal", T0 + 600 * S)["ops"].empty(), "si sobra tiempo no propone nada");
}

// --- 14. La corrida (el ejecutor, con el reloj inyectado) ----------------------------------

bool hay(const std::vector<Accion>& acc, Accion::Que q, const std::string& id) {
    return std::any_of(acc.begin(), acc.end(), [&](const Accion& a) {
        return a.que == q && (a.pieza == id || (a.pieza.empty() && (a.paso == id || a.escena == id)));
    });
}

Linea lineaCorrida() {
    Linea l = lineaBase();
    Paso p1 = paso("p1", 30);
    p1.continuar = Continuar::Sigue;
    Pieza zN = pieza("zN", "notas", "nota");
    zN.desde.modo = Ancla::Modo::Manual;
    p1.piezas = {pieza("zE1", "escena", "escena", json{{"escena", "Cam 1"}}),
                 pieza("zG", "graficos", "grafico", json{{"fuente", "F"}, {"grafico", "g"}}, 5 * S),
                 pieza("zM", "audio", "musica", json{{"archivo", "m.wav"}}, 0, Vida::Evento), zN};
    Paso p2 = paso("p2", 20);
    p2.piezas = {pieza("zE2", "escena", "escena", json{{"escena", "Cam 2"}})};
    Paso p3 = paso("p3", 60);
    p3.piezas = {pieza("zE3", "escena", "escena", json{{"escena", "Placa"}})};
    l.bloques = {bloque("b1", {p1, p2}), bloque("b2", {p3})};
    ItemEstante ap;
    ap.id = "aplauso";
    Pieza zAp = pieza("zAp", "audio", "musica", json{{"archivo", "aplausos.wav"}});
    zAp.dura.modo = Dura::Modo::Ms;
    zAp.dura.ms = 3 * S;
    ap.pieza = zAp;
    ItemEstante corte;
    corte.id = "corte";
    corte.paso = paso("corte", 15);
    corte.paso->piezas = {pieza("zC", "escena", "escena", json{{"escena", "Placa corte"}})};
    l.estante = {ap, corte};
    return l;
}

void testCorrida() {
    Corrida c(lineaCorrida(), entrada(T0));
    CHECK(c.estado() == EstadoCorrida::Parada && c.proximoMs(T0) == -1, "parada no hace nada");
    auto acc = c.arrancar(T0);
    CHECK(c.estado() == EstadoCorrida::Corriendo && c.alAire("principal") && c.alAire("principal")->paso == "p1", "arranca en p1");
    CHECK(hay(acc, Accion::Que::EntraPaso, "p1") && hay(acc, Accion::Que::Pieza, "zE1") && hay(acc, Accion::Que::Pieza, "zM"),
          "entra con su escena y su música");
    CHECK(!hay(acc, Accion::Que::Pieza, "zG") && !hay(acc, Accion::Que::Pieza, "zN"), "el zócalo a su hora; la manual, nunca sola");
    CHECK(hay(acc, Accion::Que::Armar, "Cam 2"), "el próximo queda armado en el PVW");
    CHECK(c.proximoMs(T0) == 5 * S, "el timer va justo al zócalo");
    acc = c.tick(T0 + 5 * S);
    CHECK(hay(acc, Accion::Que::Pieza, "zG") && c.proximoMs(T0 + 5 * S) == 25 * S, "el zócalo y después el paso que sigue solo");
    CHECK(c.entraSoloEnSeg("principal", T0 + 24 * S) == 0 && c.entraSoloEnSeg("principal", T0 + 25 * S + 1) == 5 &&
              c.entraSoloEnSeg("principal", T0 + 29500) == 1,
          "el aviso de los últimos 5 s");
    acc = c.tick(T0 + 30 * S);
    CHECK(hay(acc, Accion::Que::EntraPaso, "p2") && hay(acc, Accion::Que::Saca, "zG") && hay(acc, Accion::Que::Pieza, "zE2"),
          "p2 entra solo y el zócalo se va con su paso");
    CHECK(!hay(acc, Accion::Que::Saca, "zM"), "la música sigue");
    CHECK(c.instancias().size() == 2 && c.instancias()[0].hastaMs == T0 + 30 * S && c.instancias()[1].comoEntro == ComoEntro::Auto,
          "lo emitido, con cómo entró");
    CHECK(c.proximoMs(T0 + 30 * S) == -1, "p3 espera el GO: no hay timer");
    acc = c.tick(T0 + 60 * S);
    CHECK(!hay(acc, Accion::Que::Saca, "zE2"), "p2 pasado de tiempo esperando el GO: su escena sigue");
    acc = c.go("principal", T0 + 70 * S);
    CHECK(hay(acc, Accion::Que::EntraPaso, "p3") && c.alAire("principal")->paso == "p3", "GO");
    acc = c.go("principal", T0 + 80 * S);
    CHECK(hay(acc, Accion::Que::Termina, "") && c.estado() == EstadoCorrida::Terminada && !c.alAire("principal"),
          "un GO sin próximo termina");
    const auto rec = c.sacarRecorrido();
    CHECK(rec.size() > 8 && rec.front()["que"] == "arranca" && rec.back()["que"] == "fin", "el recorrido anota todo");
    CHECK(c.sacarRecorrido().empty(), "y se saca una vez");
}

void testCorridaHoldYPausa() {
    Linea l = tresPasos();
    l.bloques[0].pasos[0].continuar = Continuar::Sigue;
    l.bloques[0].pasos[1].continuar = Continuar::Sigue;
    l.bloques[0].pasos[1].fin.duracionSeg = 10;
    Corrida c(l, entrada(T0));
    c.arrancar(T0);
    CHECK(c.setHold("principal", true, T0 + S) && c.hold("principal"), "HOLD del paso al aire");
    CHECK(c.tick(T0 + 90 * S).empty() && c.entraSoloEnSeg("principal", T0 + 58 * S) == 0 && c.proximoMs(T0 + 90 * S) == -1,
          "en HOLD no entra nada ni se avisa");
    c.setHold("principal", false, T0 + 90 * S);
    auto acc = c.tick(T0 + 90 * S);
    CHECK(hay(acc, Accion::Que::EntraPaso, "p2") && !c.hold("principal"), "suelto y pasado, entra ya");
    c.pausar(T0 + 95 * S);
    CHECK(c.estado() == EstadoCorrida::Pausada && c.tick(T0 + 150 * S).empty() && c.proximoMs(T0 + 150 * S) == -1,
          "en pausa nada pasa solo");
    c.reanudar(T0 + 150 * S);
    CHECK(c.proximoMs(T0 + 150 * S) == 5 * S, "lo pausado no cuenta: a p2 le quedaban 5 s");
    CHECK(c.pausadoMs(T0 + 150 * S) == 55 * S, "y se lleva la cuenta de lo pausado (para el desvío)");
    acc = c.tick(T0 + 155 * S);
    CHECK(hay(acc, Accion::Que::EntraPaso, "p3"), "y sigue solo");
}

void testCorridaSaltosEIr() {
    Linea l = lineaBase();
    Paso p1 = paso("p1", 60);
    p1.piezas = {pieza("zM", "audio", "musica", json{{"archivo", "m.wav"}}, 0, Vida::Evento),
                 pieza("zG", "graficos", "grafico", json{{"fuente", "F"}, {"grafico", "g"}}, 0, Vida::Bloque)};
    Paso p2 = paso("p2", 60);
    p2.piezas = {pieza("zZ", "dsk", "grafico", json{{"fuente", "D"}, {"grafico", "z"}})};
    Paso p3 = paso("p3", 60);
    p3.piezas = {pieza("zA", "asistente", "asistente", json{{"herramienta", "qr_mostrar"}}, 0, Vida::HastaQueSeSaque)};
    Paso p4 = paso("p4", 60);
    l.bloques = {bloque("b1", {p1, p2}), bloque("b2", {p3, p4})};

    Corrida c(l, entrada(T0));
    c.arrancar(T0);
    c.go("principal", T0 + 60 * S);
    std::string err;
    auto acc = c.irA("principal", "p4", T0 + 70 * S, &err);
    CHECK(err.empty() && c.alAire("principal")->paso == "p4", "ir a p4");
    CHECK(hay(acc, Accion::Que::Saca, "zZ") && hay(acc, Accion::Que::Saca, "zG"), "saca lo que ahí no va");
    CHECK(!hay(acc, Accion::Que::Pieza, "zM"), "la música que ya suena no se repite");
    CHECK(hay(acc, Accion::Que::Avisar, "zA"), "la del asistente se pregunta");
    // Volver atrás repone.
    acc = c.irA("principal", "p2", T0 + 80 * S);
    CHECK(hay(acc, Accion::Que::Pieza, "zZ") && c.alAire("principal")->paso == "p2", "volver a p2");
    CHECK(c.irA("principal", "nadie", T0, &err).empty() && !err.empty(), "un paso que no existe");
    // Saltar y mover el próximo.
    CHECK(c.saltar("principal", T0 + 81 * S) && c.resolver(T0 + 81 * S).salida("principal")->proximo == "p4", "saltar p3");
    CHECK(c.moverProximo("principal", -1, T0 + 82 * S) && c.resolver(T0 + 82 * S).salida("principal")->proximo == "p1",
          "el próximo para atrás (sin p2, que está al aire)");
    CHECK(!c.moverProximo("principal", -1, T0 + 83 * S), "antes del primero no hay");
    CHECK(c.alDia(T0 + 84 * S).empty(), "el próximo movido no tiene escena: nada que armar");
    acc = c.go("principal", T0 + 90 * S);
    CHECK(c.alAire("principal")->paso == "p1", "y el GO toma el movido");
    acc = c.repetir("principal", T0 + 95 * S);
    CHECK(hay(acc, Accion::Que::EntraPaso, "p1") && hay(acc, Accion::Que::Pieza, "zG"), "repetir vuelve a disparar sus piezas");
}

void testCorridaAdlibEInsertar() {
    Corrida c(lineaCorrida(), entrada(T0));
    c.arrancar(T0);
    std::string err;
    auto acc = c.adlib("principal", "zN", T0 + S, &err);
    CHECK(err.empty() && hay(acc, Accion::Que::Pieza, "zN") && c.disparada("principal", "zN"), "el adlib del paso");
    acc = c.adlib("principal", "zG", T0 + 2 * S);
    CHECK(hay(acc, Accion::Que::Pieza, "zG") && !hay(c.tick(T0 + 5 * S), Accion::Que::Pieza, "zG"),
          "disparada antes de su hora, no se repite a su hora");
    acc = c.adlib("principal", "aplauso", T0 + 6 * S);
    CHECK(hay(acc, Accion::Que::Pieza, "zAp") && c.proximoMs(T0 + 6 * S) == 3 * S, "del estante, con su duración");
    CHECK(hay(c.tick(T0 + 9 * S), Accion::Que::Saca, "zAp"), "y se va sola");
    CHECK(c.adlib("principal", "nada", T0, &err).empty() && !err.empty(), "lo que no está, no");
    err.clear();
    acc = c.insertar("principal", "corte", false, T0 + 10 * S, &err);
    CHECK(err.empty() && hay(acc, Accion::Que::Armar, "Placa corte"), "el paso del estante queda de próximo (y armado)");
    const std::string ins = c.resolver(T0 + 10 * S).salida("principal")->proximo;
    CHECK(ins.rfind("ins1-corte", 0) == 0 && c.linea().paso(ins) && c.linea().paso(ins)->piezas[0].id == ins + "-zC",
          "con ids propios");
    c.setLinea(lineaCorrida());
    CHECK(c.linea().paso(ins) != nullptr, "editar la línea no se lo lleva");
    acc = c.go("principal", T0 + 12 * S);
    CHECK(c.alAire("principal")->paso == ins && hay(acc, Accion::Que::Pieza, ins + "-zC"), "y entra con el GO");
    CHECK(c.resolver(T0 + 12 * S).salida("principal")->proximo == "p2", "después sigue el plan");
}

void testCorridaRetoma() {
    Linea l = lineaBase();
    Paso p1 = paso("p1", 60);
    p1.continuar = Continuar::Sigue;
    Pieza zZoc = pieza("zZoc", "graficos", "grafico", json{{"fuente", "F"}, {"grafico", "z"}}, 5 * S);
    zZoc.dura.modo = Dura::Modo::Ms;
    zZoc.dura.ms = 10 * S;
    p1.piezas = {pieza("zMus", "audio", "musica", json{{"archivo", "m.wav"}}, 0, Vida::Evento), zZoc,
                 pieza("zAsis", "asistente", "asistente", json{{"herramienta", "stream_titulo"}}, 30 * S)};
    l.bloques = {bloque("b1", {p1, paso("p2", 60), paso("p3", 60)})};

    Corrida antes(l, entrada(T0));
    antes.arrancar(T0);
    antes.tick(T0 + 5 * S);
    const json vivo = antes.aJsonVivo(T0 + 12 * S);
    CHECK(vivo["salidas"][0]["alAire"] == "p1" && vivo["piezas"].size() == 2 && vivo["arranqueMs"] == T0, "linea-aire.json");

    // Se cortó la luz a las T0+12 y volvió a las T0+100.
    Corrida c(l, entrada(T0));
    auto acc = c.retomar(vivo, T0 + 100 * S);
    CHECK(c.retomando() && c.pideDescripcion() && c.hold("principal"), "vuelve en HOLD y pide descripción");
    CHECK(c.alAire("principal") && c.alAire("principal")->paso == "p1", "con lo que estaba al aire");
    CHECK(hay(acc, Accion::Que::Pieza, "zMus") && hay(acc, Accion::Que::Saca, "zZoc") && hay(acc, Accion::Que::Avisar, "zAsis"),
          "repone la música, saca el zócalo vencido y pregunta por la del asistente");
    CHECK(c.resumenRetoma()["salidas"][0]["segunElPlan"]["paso"] == "p2", "el resumen para el asistente");
    CHECK(c.tick(T0 + 200 * S).empty() && c.proximoMs(T0 + 200 * S) == -1, "sin respuesta, sigue en HOLD");
    c.describir("el orador sigue hablando", T0 + 210 * S);
    CHECK(!c.pideDescripcion() && c.retomando() && c.descripcion() == "el orador sigue hablando", "la descripción no suelta nada");
    acc = c.soltar("seguimos como estaba", T0 + 220 * S);
    CHECK(!c.retomando() && !c.hold("principal") && hay(acc, Accion::Que::EntraPaso, "p2"), "soltar: p1 ya se había pasado y p2 entra");
    CHECK(!hay(acc, Accion::Que::Pieza, "zAsis"), "lo perdido que pregunta no se hace solo");
    bool corte = false, descripcion = false, decision = false;
    for (const json& x : c.sacarRecorrido()) {
        corte = corte || x["que"] == "corte";
        descripcion = descripcion || (x["que"] == "descripcion" && x["texto"] == "el orador sigue hablando");
        decision = decision || (x["que"] == "decision" && x["texto"] == "seguimos como estaba");
    }
    CHECK(corte && descripcion && decision, "el corte, la descripción y la decisión quedan en el recorrido");

    // Con GO se suelta tal cual: entra el próximo una sola vez.
    Corrida g(l, entrada(T0));
    g.retomar(vivo, T0 + 100 * S);
    g.go("principal", T0 + 101 * S);
    CHECK(!g.retomando() && g.alAire("principal")->paso == "p2" && g.instancias().size() == 2, "GO en la retoma");
    const json est = g.estadoJson(T0 + 101 * S);
    CHECK(est["estado"] == "corriendo" && est["salidas"][0]["alAire"]["paso"] == "p2" && est["retoma"]["activa"] == false,
          "el estado para el MCP");

    // F1-03: linea-aire.json volvió dañado (las dos generaciones rotas). No se sabe en
    // qué iba: igual vuelve en HOLD en todas las salidas y pregunta, sin reponer nada.
    Corrida d(l, entrada(T0));
    acc = d.retomar(json{{"estado", "corriendo"}, {"escritoMs", T0 + 100 * S}, {"arranqueMs", T0 + 100 * S}}, T0 + 100 * S);
    CHECK(d.retomando() && d.pideDescripcion() && d.hold("principal"), "dañado: HOLD y la pregunta");
    CHECK(!hay(acc, Accion::Que::Pieza, "zMus") && !hay(acc, Accion::Que::Pieza, "zAsis") &&
              !hay(acc, Accion::Que::Avisar, "zAsis"),
          "dañado: no repone ni dispara nada");
    CHECK(d.tick(T0 + 200 * S).empty(), "dañado: sigue en HOLD hasta que alguien cuente");
}

} // namespace

int main() {
    testJson();
    testMigracion();
    testPronostico();
    testHardStarts();
    testContinuarYCondiciones();
    testHoldsYSaltos();
    testEstadoYProximos();
    testVariasSalidas();
    testRevisar();
    testCargarEn();
    testRetoma();
    testRetomaPasadoDeTiempo();
    testProyeccion();
    testEditar();
    testVistaYAjuste();
    testCorrida();
    testCorridaHoldYPausa();
    testCorridaSaltosEIr();
    testCorridaAdlibEInsertar();
    testCorridaRetoma();
    if (g_fallos == 0) std::printf("test_linea: todo OK\n");
    return g_fallos == 0 ? 0 : 1;
}
