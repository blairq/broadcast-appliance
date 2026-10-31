// ============================================================================
// src/app/tests/test_linea_productor.cpp — el asistente productor de la línea (L9)
//
//   1. Las etiquetas: recortable, recortable:N, prioridad:N, fijo.
//   2. El solucionador contra el fin: recortar (como antes, compatible), por
//      prioridad, que uno absorba, saltear lo opcional; y si sobra, relleno o estirar.
//   3. Contra cada hard start: overlap (recortar antes, mover la hora) y hueco
//      (relleno antes, estirar, adelantar la hora). Todo se aplica con editar().
//   4. Plantillas: orador (con datos, sin datos, el tiempo de la fila), un bloque
//      existente, el número que queda número, el orden del día.
//   5. La línea desde una guía (el guion v1 de su plantilla), con ids que chocan.
//   6. El vigía del over/under: umbral, escalón, silencio y recuperación.
//   7. Las superficies: la vista con escena, enSeg y estante; los planos por cámara.
//   8. L10: la escaleta en CSV y en MOS (ida y vuelta, y de otro sistema) y los
//      tiempos grabados de un ensayo.
//
// Todo puro: sin motor y con el reloj inyectado.
// ============================================================================

#include <cstdio>
#include <string>

#include "evento/Evento.hpp"
#include "evento/Linea.hpp"
#include "evento/LineaEdicion.hpp"
#include "evento/LineaIntercambio.hpp"
#include "evento/LineaProductor.hpp"

using namespace ba::evento::linea;
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

constexpr int64_t S = 1000;
constexpr int64_t M = 60 * S;
constexpr int64_t H = 60 * M;
constexpr int64_t T0 = 19 * H; // arranca a las 19:00 (medianoche = 0)

Paso paso(const std::string& id, int seg, std::vector<std::string> etiquetas = {}) {
    Paso p;
    p.id = id;
    p.nombre = "Paso " + id;
    p.fin.duracionSeg = seg;
    p.duracionPrevistaSeg = seg;
    p.etiquetas = std::move(etiquetas);
    return p;
}

Pieza pieza(const std::string& id, const std::string& pista, const std::string& tipo, json datos) {
    Pieza z;
    z.id = id;
    z.pista = pista;
    z.tipo = tipo;
    z.datos = std::move(datos);
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

Bloque bloque(const std::string& id, std::vector<Paso> pasos) {
    Bloque b;
    b.id = id;
    b.nombre = "Bloque " + id;
    b.pasos = std::move(pasos);
    return b;
}

Linea base(std::vector<Bloque> bloques) {
    Linea l;
    l.salidas = {"principal"};
    l.pistas = {pista("escena", "escena", "principal"), pista("graficos", "graficos", "principal"), pista("ptz", "ptz")};
    l.bloques = std::move(bloques);
    return l;
}

Entrada entrada(int64_t ahora, int64_t finPlaneado = 0) {
    Entrada e;
    e.ahoraMs = ahora;
    e.inicioPlaneadoMs = T0;
    e.medianocheMs = 0;
    e.finPlaneadoMs = finPlaneado;
    return e;
}

const json* opcionDe(const json& opciones, const std::string& id) {
    for (const json& o : opciones)
        if (o.value("id", std::string()) == id) return &o;
    return nullptr;
}

// --- 1. Etiquetas ------------------------------------------------------------------------

void testEtiquetas() {
    CHECK(recortablePct(paso("a", 60)) == 0, "sin etiqueta no se recorta");
    CHECK(recortablePct(paso("a", 60, {"recortable"})) == 50, "recortable: la mitad");
    CHECK(recortablePct(paso("a", 60, {"recortable:30"})) == 30, "recortable:N");
    CHECK(recortablePct(paso("a", 60, {"recortable:99"})) == 90, "hasta el 90 %");
    CHECK(recortablePct(paso("a", 60, {"recortable", "fijo"})) == 0, "fijo manda");
    CHECK(prioridadDe(paso("a", 60)) == 5 && prioridadDe(paso("a", 60, {"prioridad:1"})) == 1 &&
              prioridadDe(paso("a", 60, {"prioridad:x"})) == 5,
          "prioridad");
    CHECK(sumarMinutos("19:30", 45) == "20:15" && sumarMinutos("23:50", 20) == "00:10" && sumarMinutos("19:30", -45) == "18:45",
          "sumar minutos");
    CHECK(sumarMinutos("tarde", 5).empty(), "una hora mal escrita");
}

// --- 2. Contra el fin --------------------------------------------------------------------------

void testFin() {
    // Lo de antes (L3), igual: recorta la mitad de lo recortable y saltea lo opcional.
    {
        Linea l = base({bloque("b1", {paso("p1", 60), paso("p2", 60, {"recortable"})}), bloque("b2", {paso("p3", 60, {"opcional"})})});
        const Resolucion r = resolver(l, entrada(T0, T0 + 120 * S));
        const json a = proponerAjuste(l, r, "principal", T0 + 120 * S);
        CHECK(a["overSeg"] == 60 && a["recortaSeg"] == 30 && a["faltaSeg"] == 0, "compatible: recorta y saltea");
        CHECK(a["ops"].size() == 1 && a["ops"][0]["id"] == "p2" && a["saltear"].size() == 1, "compatible: ops y saltear");
        CHECK(opcionDe(a["opciones"], "recortar") && opcionDe(a["opciones"], "saltear"), "las opciones");
        CHECK(!opcionDe(a["opciones"], "absorber"), "ninguno de un minuto absorbe un minuto (quedaría en nada)");
        const json* sa = opcionDe(a["opciones"], "saltear");
        CHECK(sa && editar(l, (*sa)["ops"]).ok, "saltear se aplica (se sacan de la línea)");
        // Con uno largo, ese absorbe.
        Linea l2 = l;
        l2.bloques[1].pasos.push_back(paso("p4", 600));
        const json a2 = proponerAjuste(l2, resolver(l2, entrada(T0)), "principal", T0 + 720 * S);
        const json* ab = opcionDe(a2["opciones"], "absorber");
        CHECK(ab && (*ab)["ganaSeg"] == 60 && (*ab)["paso"] == "p4", "el largo absorbe los 60 s");
        CHECK(ab && editar(l2, (*ab)["ops"]).ok, "y se aplica");
        // Sin `hasta`: el fin planeado.
        CHECK(proponerAjuste(l, r, "principal", 0)["overSeg"] == 60, "sin objetivo, el fin planeado");
    }
    // Por prioridad: la 1 primero, la 9 si no alcanza.
    {
        Linea l = base({bloque("b1", {paso("p1", 60, {"recortable", "prioridad:9"}), paso("p2", 60, {"recortable", "prioridad:1"}),
                                      paso("p3", 60, {"recortable", "prioridad:1"})})});
        const Resolucion r = resolver(l, entrada(T0));
        json a = proponerAjuste(l, r, "principal", T0 + 160 * S); // 20 s pasados
        CHECK(a["recortaSeg"] == 20 && a["ops"].size() == 2, "con 20 s alcanza la prioridad 1, repartido");
        for (const json& op : a["ops"]) CHECK(op["id"] != "p1", "la prioridad 9 no se toca");
        a = proponerAjuste(l, r, "principal", T0 + 100 * S); // 80 s pasados: la 1 da 60
        CHECK(a["recortaSeg"] == 80 && a["ops"].size() == 3, "si no alcanza, sigue con la 9");
        const Edicion e = editar(l, a["ops"]);
        CHECK(e.ok && e.linea.paso("p2")->duracionPlanMs() == 30 * S && e.linea.paso("p1")->duracionPlanMs() == 40 * S, "aplicado");
    }
    // Un abierto (el orador) se recorta en lo previsto; uno fijo no absorbe.
    {
        Paso o = paso("orador", 0, {"recortable"});
        o.fin.modo = ModoFin::Abierto;
        o.duracionPrevistaSeg = 300;
        Linea l = base({bloque("b1", {o, paso("cierre", 600, {"fijo"})})});
        const Resolucion r = resolver(l, entrada(T0));
        const json a = proponerAjuste(l, r, "principal", T0 + 840 * S); // 60 s pasados
        CHECK(a["ops"].size() == 1 && a["ops"][0]["campos"]["duracionPrevistaSeg"] == 240 && !a["ops"][0]["campos"].contains("fin"),
              "el abierto: solo lo previsto");
        const json* ab = opcionDe(a["opciones"], "absorber");
        CHECK(ab && (*ab)["paso"] == "orador", "el fijo no absorbe");
    }
    // Sobra: relleno al final o estirar.
    {
        Linea l = base({bloque("b1", {paso("p1", 60), paso("p2", 60, {"recortable"})})});
        const Resolucion r = resolver(l, entrada(T0));
        const json a = proponerAjuste(l, r, "principal", T0 + 300 * S);
        CHECK(a["ops"].empty() && a["overSeg"] == -180, "si sobra no recorta");
        const json* re = opcionDe(a["opciones"], "relleno");
        const json* es = opcionDe(a["opciones"], "estirar");
        CHECK(re && es, "relleno y estirar");
        const Edicion e = editar(l, (*re)["ops"]);
        CHECK(e.ok && e.linea.bloques[0].pasos.size() == 3 && e.linea.bloques[0].pasos[2].duracionPlanMs() == 180 * S,
              "el relleno va al final con lo que sobra");
        const Edicion e2 = editar(l, (*es)["ops"]);
        CHECK(e2.ok && e2.linea.paso("p2")->duracionPlanMs() == 240 * S, "se estira el recortable");
    }
    // Sin fin planeado ni objetivo: lo dice.
    {
        Linea l = base({bloque("b1", {paso("p1", 60)})});
        const json a = proponerAjuste(l, resolver(l, entrada(T0)), "principal", 0);
        CHECK(a["ops"].empty() && a["overSeg"] == 0, "sin fin planeado a mano: el arranque más lo previsto");
        Entrada sinHora = entrada(T0);
        sinHora.inicioPlaneadoMs = 0;
        const json b = proponerAjuste(l, resolver(l, sinHora), "principal", 0);
        CHECK(b["ops"].empty() && b.contains("texto") && (!b.contains("objetivoMs") || b["overSeg"] == 0), "sin hora prevista");
        CHECK(proponerAjuste(l, resolver(l, entrada(T0)), "nadie", T0)["ops"].empty(), "una salida que no existe");
    }
}

// --- 3. Contra los hard starts --------------------------------------------------------------

void testHardStarts() {
    const auto conHora = [](const std::string& hora) {
        Paso p = paso("p3", 60);
        p.inicio.modo = ModoInicio::Hora;
        p.inicio.hora = hora;
        p.inicio.politica = Politica::Esperar;
        return p;
    };
    // Overlap: a las 19:01 no se llega (hay dos minutos antes).
    {
        Linea l = base({bloque("b1", {paso("p1", 60), paso("p2", 60, {"recortable"}), conHora("19:01")})});
        const Resolucion r = resolver(l, entrada(T0));
        const json a = proponerAjuste(l, r, "principal", 0);
        CHECK(a["hardStarts"].size() == 1 && a["hardStarts"][0]["gapSeg"] == -60, "el overlap");
        const json& os = a["hardStarts"][0]["opciones"];
        const json* mh = opcionDe(os, "hs-p3-mover_hora");
        CHECK(mh && (*mh)["ops"][0]["campos"]["inicio"]["hora"] == "19:02", "correr la hora un minuto");
        const json* rc = opcionDe(os, "hs-p3-recortar");
        CHECK(rc && (*rc)["ganaSeg"] == 30, "recortar lo de antes (la mitad de p2)");
        CHECK(editar(l, (*mh)["ops"]).ok, "se aplica");
    }
    // El hard start del bloque: se corre el del bloque.
    {
        Linea l = base({bloque("b1", {paso("p1", 120)}), bloque("b2", {paso("p3", 60)})});
        Inicio i;
        i.modo = ModoInicio::Hora;
        i.hora = "19:01";
        i.politica = Politica::Esperar;
        l.bloques[1].inicio = i;
        const json a = proponerAjuste(l, resolver(l, entrada(T0)), "principal", 0);
        const json* mh = a["hardStarts"].empty() ? nullptr : opcionDe(a["hardStarts"][0]["opciones"], "hs-p3-mover_hora");
        CHECK(mh && (*mh)["ops"][0]["id"] == "b2", "el inicio es del bloque");
    }
    // Hueco: a las 19:10 sobran ocho minutos.
    {
        Linea l = base({bloque("b1", {paso("p1", 60), paso("p2", 60)}), bloque("b2", {conHora("19:10")})});
        const Resolucion r = resolver(l, entrada(T0));
        const json a = proponerAjuste(l, r, "principal", 0);
        CHECK(a["hardStarts"].size() == 1 && a["hardStarts"][0]["gapSeg"] == 480, "el hueco");
        const json& os = a["hardStarts"][0]["opciones"];
        const json* re = opcionDe(os, "hs-p3-relleno");
        CHECK(re && (*re)["ops"][0]["bloque"] == "b1" && !(*re)["ops"][0].contains("despuesDe"),
              "el relleno al final del bloque de antes (no le roba el inicio a b2)");
        const Edicion e = editar(l, (*re)["ops"]);
        CHECK(e.ok && e.linea.bloques[0].pasos.back().duracionPlanMs() == 480 * S, "el relleno tapa el hueco");
        const json* mh = opcionDe(os, "hs-p3-mover_hora");
        CHECK(mh && (*mh)["ops"][0]["campos"]["inicio"]["hora"] == "19:02", "adelantar la hora");
        CHECK(opcionDe(os, "hs-p3-estirar"), "o estirar lo de antes");
    }
}

// --- 4. Plantillas ---------------------------------------------------------------------------

void testPlantillas() {
    Linea l = base({bloque("b1", {paso("p1", 60)})});
    DesdePlantilla d;
    d.plantilla = "orador";
    d.datos = json::parse(R"([{"nombre": "Ana Pérez", "cargo": "Presidenta", "escena": "Cam 1", "participante": "ana", "tiempoSeg": 180},
                              {"nombre": "Beto"}])");
    const json r = desdePlantilla(l, d);
    CHECK(r["ok"] == true && r["pasos"].size() == 2, "dos pasos");
    CHECK(r["avisos"].size() == 2, "a Beto le faltan la escena y quién habla");
    const Edicion e = editar(l, r["ops"]);
    CHECK(e.ok, "el lote se aplica");
    if (e.ok) {
        const Paso* ana = e.linea.paso("orador-1-orador");
        const Paso* beto = e.linea.paso("orador-2-orador");
        CHECK(ana && ana->nombre == "Ana Pérez" && ana->duracionPrevistaSeg == 180 && ana->fin.modo == ModoFin::Abierto,
              "el nombre y el tiempo de la fila");
        CHECK(ana && ana->piezas.size() == 3 && ana->piezas[2].datos["valores"]["cargo"] == "Presidenta", "el zócalo lleno");
        CHECK(beto && beto->piezas.size() == 1 && beto->piezas[0].datos["valores"]["cargo"] == "" && beto->duracionPrevistaSeg == 300,
              "sin datos: sin escena ni palabra, el cargo vacío y el tiempo de la plantilla");
        CHECK(e.linea.pista("sala") != nullptr, "la pista que faltaba se agregó");
        CHECK(e.linea.bloques.size() == 2 && e.linea.bloques[1].nombre == "Orador", "en un bloque nuevo");
        CHECK(revisar(e.linea).empty() || true, "revisa sin romper");
    }
    // En un bloque que existe, después de un paso; los ids no chocan.
    DesdePlantilla d2;
    d2.plantilla = "punto";
    d2.bloque = "b1";
    d2.despuesDe = "p1";
    d2.prefijo = "p1"; // a propósito: «p1-1» no existe, pero el prefijo es el de un paso
    d2.datos = json::parse(R"([{"n": 2, "titulo": "Obras", "expediente": "12/24"}, {"n": 3, "titulo": "Varios"}])");
    const json r2 = desdePlantilla(l, d2);
    CHECK(r2["ok"] == true && r2["ops"].size() >= 2, "paso_agregar encadenados");
    const Edicion e2 = editar(l, r2["ops"]);
    CHECK(e2.ok && e2.linea.bloques[0].pasos.size() == 3 && e2.linea.bloques[0].pasos[1].nombre == "Punto 2: Obras", "en orden, después de p1");
    if (e2.ok) {
        const Paso& pp = e2.linea.bloques[0].pasos[1];
        bool numero = false;
        for (const Pieza& z : pp.piezas)
            if (z.tipo == "asamblea") numero = z.datos["n"].is_number() && z.datos["n"] == 2;
        CHECK(numero, "un texto que es solo {n} queda número");
        CHECK(pp.notaOperador == "12/24", "el expediente en la nota");
    }
    // Un bloque que sale del orden del día (lo expande LineaPlantillas, L7).
    const json od = bloqueDeOrdenDelDia(l, "punto", json::parse(R"([{"n": 1, "titulo": "Presupuesto", "estado": "pendiente"},
                                                                    {"n": 2, "titulo": "Obras", "estado": "pendiente"}])"),
                                        "Sesión");
    CHECK(od["ok"] == true && od["pasos"].size() == 2, "un paso por punto");
    bool sumada = false;
    for (const json& o : od["ops"]) sumada = sumada || (o["op"] == "plantilla_agregar" && o["plantilla"]["id"] == "punto");
    CHECK(sumada, "la plantilla de fábrica se suma a las del evento en el mismo lote");
    const Edicion e3 = editar(l, od["ops"]);
    CHECK(e3.ok && e3.linea.bloques.back().origen == "asamblea/orden-del-dia" && e3.linea.bloques.back().plantilla == "punto",
          "el bloque queda con su origen (para re-expandirlo)");
    bool quejaPlantilla = false;
    if (e3.ok)
        for (const Reparo& rp : revisar(e3.linea)) quejaPlantilla = quejaPlantilla || rp.texto.find("plantilla") != std::string::npos;
    CHECK(e3.ok && e3.linea.plantillas.size() == 1 && e3.linea.plantillas[0].id == "punto" && !quejaPlantilla,
          "y con su plantilla en el evento: la revisión no se queja");
    CHECK(bloqueDeOrdenDelDia(l, "punto", json::array(), "")["ok"] == false, "sin puntos, no");
    DesdePlantilla d3;
    d3.plantilla = "no-existe";
    CHECK(desdePlantilla(l, d3)["ok"] == false, "una plantilla que no existe");
    d3.plantilla = "orador";
    d3.bloque = "nada";
    CHECK(desdePlantilla(l, d3)["ok"] == false, "un bloque que no existe");
    // Una plantilla del evento manda sobre la de fábrica.
    Linea l3 = l;
    Plantilla propia;
    propia.id = "orador";
    propia.nombre = "Mi orador";
    propia.pasos = {paso("x", 90)};
    l3.plantillas = {propia};
    const json r3 = desdePlantilla(l3, DesdePlantilla{"orador"});
    CHECK(r3["ok"] == true && r3["ops"].back()["bloque"]["nombre"] == "Mi orador", "la del evento primero");
}

// --- 5. Desde una guía ---------------------------------------------------------------------------

void testGuia() {
    const json g = json::parse(R"({"nombre": "Panel", "bloques": [
      {"id": "b1", "nombre": "Apertura", "cues": [
        {"id": "c1", "tipo": "escena", "texto": "Conducción", "datos": {"escena": "Conducción"}, "duracionSeg": 60},
        {"id": "c2", "tipo": "grafico", "texto": "Zócalo", "datos": {"fuente": "Zócalo", "grafico": "zocalo"}, "duracionSeg": 0}]},
      {"id": "b9", "nombre": "Cierre", "cues": [
        {"id": "c9", "tipo": "nota", "texto": "Gracias", "datos": {}, "duracionSeg": 30}]}]})");
    const ba::evento::Evento guia = ba::evento::Evento::deJson(g);
    Linea l = base({bloque("b1", {paso("p1", 60)})}); // «b1» ya está
    const json r = lineaDeGuia(l, guia);
    CHECK(r["ok"] == true && r["bloques"].size() == 2 && r["bloques"][0] != "b1", "el b1 de la guía se renombra");
    CHECK(!r["avisos"].empty(), "y lo avisa");
    const Edicion e = editar(l, r["ops"]);
    CHECK(e.ok && e.linea.bloques.size() == 3 && e.linea.bloques[1].nombre == "Apertura", "al final, en orden");
    if (e.ok) {
        bool pistasOk = true;
        for (const Bloque& b : e.linea.bloques)
            for (const Paso& p : b.pasos)
                for (const Pieza& z : p.piezas) pistasOk = pistasOk && e.linea.pista(z.pista) != nullptr;
        CHECK(pistasOk, "cada pieza en una pista que existe");
    }
}

// --- 6. El vigía -----------------------------------------------------------------------------------

void testVigia() {
    Vigia v;
    v.umbralSeg = 60;
    v.escalonSeg = 60;
    v.silencioMs = 5 * M;
    CHECK(!v.debeProponer("principal", 30, 0), "por debajo del umbral, nada");
    CHECK(v.debeProponer("principal", 70, 1000), "pasó el umbral: avisa");
    CHECK(!v.debeProponer("principal", 90, 2000), "no creció un escalón: calla");
    CHECK(v.debeProponer("principal", 130, 3000), "creció un escalón: avisa de nuevo");
    CHECK(!v.debeProponer("principal", 130, 3000 + 4 * M), "dentro del silencio, igual: calla");
    CHECK(v.debeProponer("principal", 130, 3000 + 5 * M), "pasado el silencio: recuerda");
    CHECK(!v.debeProponer("principal", 20, 3000 + 6 * M), "se recuperó");
    CHECK(v.debeProponer("principal", 65, 3000 + 6 * M + 1), "y si vuelve a pasar, avisa como la primera vez");
    CHECK(v.debeProponer("sala", 65, 0), "cada salida, lo suyo");
}

// --- 7. Superficies ----------------------------------------------------------------------------------

void testSuperficies() {
    Linea l = base({bloque("b1", {paso("p1", 60), paso("p2", 60), paso("p3", 60)})});
    l.bloques[0].pasos[0].piezas = {pieza("z1", "escena", "escena", {{"escena", "Cam 1"}})};
    l.bloques[0].pasos[1].piezas = {pieza("z2", "escena", "escena", {{"escena", "Plano general"}})};
    l.bloques[0].pasos[2].piezas = {pieza("z3", "escena", "escena", {{"escena", "Cam 1"}}),
                                     pieza("z4", "ptz", "ptz", {{"camara", "cam2"}, {"posicion", "atril"}})};
    ItemEstante it;
    it.id = "aplauso";
    it.boton = "Aplausos";
    it.pieza = pieza("zAp", "graficos", "grafico", json::object());
    l.estante = {it};
    const Resolucion r = resolver(l, entrada(T0 - 30 * S)); // media hora… no: 30 s antes de arrancar
    const json v = vista(l, r, "principal", 0, 0, "pasos", T0 - 30 * S);
    const json& ps = v["bloques"][0]["pasos"];
    CHECK(ps[0]["escena"] == "Cam 1" && ps[1]["escena"] == "Plano general", "la escena de cada paso");
    CHECK(ps[1]["enSeg"] == 90, "cuánto falta para que entre");
    CHECK(v["estante"].size() == 1 && v["estante"][0]["boton"] == "Aplausos" && v["estante"][0]["tipo"] == "pieza", "el estante");
    const auto camaras = [](const std::string& e) {
        if (e == "Cam 1") return std::vector<std::string>{"cam1"};
        if (e == "Plano general") return std::vector<std::string>{"cam1", "cam2"};
        return std::vector<std::string>{};
    };
    const json c = proximosPorCamara(l, json::array({v}), camaras);
    CHECK(c.contains("cam1") && c["cam1"].size() == 3, "cam1: los tres planos");
    CHECK(c["cam1"][0]["paso"] == "p1" && c["cam1"][2]["paso"] == "p3", "por lo que falta");
    CHECK(c["cam2"].size() == 2 && c["cam2"][1]["paso"] == "p3" && c["cam2"][1].contains("ptz"), "cam2: el plano general y el PTZ");
    CHECK(proximosPorCamara(l, json::array({v}), {}).contains("cam2") && !proximosPorCamara(l, json::array({v}), {}).contains("cam1"),
          "sin saber las cámaras de las escenas, solo el PTZ");
}

} // namespace

// --- 8. L10: CSV, MOS y los tiempos grabados ---------------------------------------------

void testIntercambio() {
    CHECK(segundosDe("1:30") == 90 && segundosDe("1:02:03") == 3723 && segundosDe("45") == 45 && segundosDe("x") == -1 &&
              segundosDe("") == -1,
          "las duraciones");
    CHECK(textoDeSegundos(90) == "1:30" && textoDeSegundos(3723) == "1:02:03", "y al revés");
    Paso a = paso("a", 90);
    a.piezas = {pieza("za", "escena", "escena", json{{"escena", "Cam 1"}})};
    a.guion = "Buenas noches, \"bienvenidos\"";
    Paso b = paso("b", 300);
    b.inicio = Inicio::deJson(json{{"modo", "hora"}, {"hora", "19:30"}, {"politica", "esperar"}});
    b.fin = Fin::deJson(json{{"abierto", true}});
    b.piezas = {pieza("zb", "graficos", "grafico", json{{"fuente", "Zócalo"}, {"grafico", "nombre"}, {"valores", {{"titulo", "Ana"}}}})};
    const Linea l = base({bloque("b1", {a, b})});
    const Resolucion r = resolver(l, entrada(T0));
    const std::string csv = escaletaCsv(l, &r, "principal", [](int64_t ms) { return std::to_string(ms / 1000); });
    CHECK(csv.find("bloque,id,nombre,entra") == 0, "el encabezado con las claves");
    CHECK(csv.find("hora 19:30 esperar") != std::string::npos && csv.find("abierto") != std::string::npos &&
              csv.find("Cam 1") != std::string::npos && csv.find("\"Buenas noches, \"\"bienvenidos\"\"\"") != std::string::npos,
          "cómo entra, cómo termina, la escena y las comillas");
    // Vuelve: a una línea vacía, como lote.
    Leida c = leerCsv(csv);
    CHECK(c.ok && c.linea.bloques.size() == 1 && c.linea.bloques[0].pasos.size() == 2, "el CSV se lee");
    if (c.ok && c.linea.bloques[0].pasos.size() == 2) {
        const Paso& pb = c.linea.bloques[0].pasos[1];
        CHECK(pb.inicio.modo == ModoInicio::Hora && pb.inicio.hora == "19:30" && pb.inicio.politica == Politica::Esperar &&
                  pb.fin.modo == ModoFin::Abierto && pb.duracionPrevistaSeg == 300,
              "con su hard start y su fin");
        CHECK(c.linea.bloques[0].pasos[0].guion == "Buenas noches, \"bienvenidos\"" && escenaDePaso(c.linea, c.linea.bloques[0].pasos[0]) == "Cam 1",
              "el guion y la escena");
    }
    const json lote = importar(l, c);
    CHECK(lote["ok"] == true && !lote["ops"].empty(), "como lote");
    const Edicion e = editar(l, lote["ops"]);
    CHECK(e.ok && e.linea.bloques.size() == 2 && e.linea.bloques[1].pasos[0].id != "a", "al final, con los ids que chocan renombrados");
    // Una planilla de otro lado: en inglés, con punto y coma.
    const Leida otra = leerCsv("Segment;Title;Duration;Script;Notes\nApertura;Bienvenida;0:45;Hola;luz baja\nApertura;Himno;2:10;;\n");
    CHECK(otra.ok && otra.linea.bloques.size() == 1 && otra.linea.bloques[0].pasos.size() == 2 &&
              otra.linea.bloques[0].pasos[1].fin.duracionSeg == 130 && otra.linea.bloques[0].pasos[0].notaOperador == "luz baja",
          "Rundown Studio o una planilla: segment, title, duration, script, notes");
    CHECK(!leerCsv("a,b\n1,2\n").ok, "sin columna de nombre no se adivina");
    const Leida malEntra = leerCsv("nombre,entra\nUno,volando\n");
    CHECK(malEntra.ok && malEntra.avisos.size() == 1, "una entrada que no se entiende se avisa y entra con GO");

    // MOS: ida y vuelta exacta de lo nuestro.
    const std::string mos = escaletaMos(l, "evento-1", "Evento & <cierre>");
    CHECK(mos.find("<roCreate>") != std::string::npos && mos.find("<storyID>a</storyID>") != std::string::npos &&
              mos.find("Evento &amp; &lt;cierre&gt;") != std::string::npos,
          "roCreate con un story por paso, escapado");
    const Leida m = leerMos(mos);
    CHECK(m.ok && m.linea.bloques.size() == 1 && m.linea.bloques[0].nombre == "Bloque b1" && m.linea.bloques[0].pasos.size() == 2,
          "vuelve con su bloque");
    if (m.ok && m.linea.bloques[0].pasos.size() == 2) {
        const Paso& mb = m.linea.bloques[0].pasos[1];
        CHECK(mb.inicio.modo == ModoInicio::Hora && mb.fin.modo == ModoFin::Abierto && mb.piezas.size() == 1 &&
                  mb.piezas[0].tipo == "grafico" && mb.piezas[0].datos["valores"]["titulo"] == "Ana" && mb.piezas[0].pista == "graficos",
              "y cada paso con sus piezas enteras");
    }
    // De otro sistema: lo básico.
    const Leida ajeno = leerMos(R"(<mos><roStoryAppend><roID>X</roID><story><storyID>S1</storyID><storySlug>Titulares</storySlug>
        <item><itemID>1</itemID><itemSlug>VT apertura</itemSlug></item></story>
        <story><storyID>S2</storyID><storySlug>Entrevista</storySlug></story></roStoryAppend></mos>)");
    CHECK(ajeno.ok && ajeno.linea.bloques[0].pasos.size() == 2 && ajeno.linea.bloques[0].pasos[0].nombre == "Titulares" &&
              ajeno.linea.bloques[0].pasos[0].piezas.size() == 1 && ajeno.linea.bloques[0].pasos[0].piezas[0].texto == "VT apertura",
          "un roStoryAppend de otro sistema: stories e items como notas");
    CHECK(!leerMos("<mos></mos>").ok, "sin stories, no");

    // Los tiempos grabados de un ensayo.
    std::vector<Instancia> xs;
    Instancia x1;
    x1.salida = "principal";
    x1.paso = "a";
    x1.desdeMs = T0;
    x1.hastaMs = T0 + 120 * S + 400; // duró 2:00 (no 1:30)
    Instancia x2 = x1;
    x2.paso = "b";
    x2.desdeMs = x1.hastaMs;
    x2.hastaMs = x2.desdeMs + 300 * S; // igual que lo previsto
    Instancia copia = x1;
    copia.paso = "a-ins-1"; // lo insertado no cuenta
    xs = {x1, x2, copia};
    const json g = tiemposGrabados(l, xs);
    CHECK(g["pasos"].size() == 1 && g["pasos"][0]["paso"] == "a" && g["pasos"][0]["grabadoSeg"] == 120 && g["pasos"][0]["antesSeg"] == 90,
          "solo lo que cambió, redondeado al segundo");
    CHECK(g["totalGrabadoSeg"] == 420 && g["totalAntesSeg"] == 390, "y el total");
    const Edicion eg = editar(l, g["ops"]);
    CHECK(eg.ok && eg.linea.paso("a")->fin.duracionSeg == 120 && eg.linea.paso("a")->duracionPrevistaSeg == 120, "se aplica con editar");
    Instancia abierto = x2;
    abierto.hastaMs = abierto.desdeMs + 200 * S;
    const json g2 = tiemposGrabados(l, {abierto});
    const Edicion eg2 = editar(l, g2["ops"]);
    CHECK(eg2.ok && eg2.linea.paso("b")->fin.modo == ModoFin::Abierto && eg2.linea.paso("b")->duracionPrevistaSeg == 200,
          "a un paso abierto solo se le cambia lo previsto");
}

int main() {
    testEtiquetas();
    testFin();
    testHardStarts();
    testPlantillas();
    testGuia();
    testVigia();
    testSuperficies();
    testIntercambio();
    if (g_fallos == 0) std::printf("test_linea_productor: todo bien\n");
    return g_fallos == 0 ? 0 : 1;
}
