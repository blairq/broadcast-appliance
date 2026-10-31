// ============================================================================
// src/app/tests/test_linea_vista.cpp — la línea lista para dibujar (L4/L5)
//
//   1. La escena de un paso y el presupuesto de miniaturas (programa, PVW, dos
//      vivas, el resto fotos; las repetidas comparten).
//   2. La escaleta: numeración, entrada, lo real contra lo previsto, chips, lo que
//      no se edita y lo pendiente.
//   3. La conducción completa el estado de la corrida (escena, adlibs, bloque).
//   4. El estante con F1…F8 y la tecla que dispara cada ítem.
//   5. La multipista: cajas de pasos con su fantasma y el over, piezas en su
//      carril, hard starts con su nivel y la regla alineada a la hora.
//
// Sin motor y con el reloj inyectado, como test_linea.
// ============================================================================

#include <cstdio>
#include <string>

#include "evento/Corrida.hpp"
#include "evento/Linea.hpp"
#include "evento/LineaVista.hpp"

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
constexpr int64_t T0 = 19 * H; // a las 19:00 (medianoche = 0)

Pieza escena(const std::string& id, const std::string& nombre) {
    Pieza z;
    z.id = id;
    z.pista = "escena-principal";
    z.tipo = "escena";
    z.datos = {{"escena", nombre}};
    return z;
}

Paso paso(const std::string& id, int seg, const std::string& esc) {
    Paso p;
    p.id = id;
    p.nombre = "Paso " + id;
    p.fin.duracionSeg = seg;
    p.duracionPrevistaSeg = seg;
    if (!esc.empty()) p.piezas.push_back(escena(id + "-e", esc));
    return p;
}

/// Cinco pasos de un minuto: A, B, C, D y otra vez A; el p2 con un zócalo y un adlib.
Linea ejemplo() {
    Linea l;
    l.pistas = {{"escena-principal", "escena", "principal", "Escena", "", false},
                {"graficos-principal", "graficos", "principal", "Gráficos", "", false},
                {"notas", "notas", "", "Notas", "", false}};
    Bloque b;
    b.id = "b1";
    b.nombre = "Apertura";
    b.pasos = {paso("p1", 60, "A"), paso("p2", 60, "B"), paso("p3", 60, "C"), paso("p4", 60, "D"), paso("p5", 60, "A")};
    Pieza zocalo;
    zocalo.id = "z1";
    zocalo.pista = "graficos-principal";
    zocalo.tipo = "grafico";
    zocalo.texto = "Zócalo";
    zocalo.desde.ms = 5 * S;
    zocalo.dura.modo = Dura::Modo::Ms;
    zocalo.dura.ms = 20 * S;
    Pieza aplauso;
    aplauso.id = "z2";
    aplauso.pista = "notas";
    aplauso.tipo = "nota";
    aplauso.texto = "Aplausos";
    aplauso.desde.modo = Ancla::Modo::Manual;
    b.pasos[1].piezas.push_back(zocalo);
    b.pasos[1].piezas.push_back(aplauso);
    b.pasos[3].inicio.modo = ModoInicio::Hora;
    b.pasos[3].inicio.hora = "19:04";
    l.bloques.push_back(b);
    ItemEstante e1;
    e1.id = "e1";
    e1.boton = "Risas";
    e1.pieza = aplauso;
    ItemEstante e2;
    e2.id = "e2";
    e2.tecla = "F1";
    e2.pieza = zocalo;
    l.estante = {e1, e2};
    return l;
}

Entrada base() {
    Entrada e;
    e.inicioPlaneadoMs = T0;
    e.medianocheMs = 0;
    return e;
}

const json* pasoDe(const json& esc, const std::string& id) {
    for (const json& b : esc)
        for (const json& p : b["pasos"])
            if (p["id"] == id) return &p;
    return nullptr;
}

} // namespace

int main() {
    const Linea l = ejemplo();

    // --- 1. Escena y presupuesto ----------------------------------------------------------
    {
        CHECK(escenaDe(l, l.bloques[0].pasos[0], "principal") == "A", "la escena del paso sale de su pieza escena");
        Corrida c(l, base());
        c.arrancar(T0);
        const Resolucion r = c.resolver(T0 + 10 * S);
        const SalidaResuelta* sr = r.salida("principal");
        CHECK(sr != nullptr, "hay salida principal");
        const auto pm = presupuestoMiniaturas(l, *sr);
        CHECK(pm.at("A") == FuenteMini::Programa, "al aire: el programa");
        CHECK(pm.at("B") == FuenteMini::Pvw, "el próximo: el PVW");
        CHECK(pm.at("C") == FuenteMini::Viva && pm.at("D") == FuenteMini::Viva, "las dos que siguen, vivas");
        CHECK(pm.size() == 4, "la A repetida comparte la del programa");
        const auto chico = presupuestoMiniaturas(l, *sr, 1);
        CHECK(chico.at("D") == FuenteMini::Foto, "con una sola viva, la D va en foto");
    }

    // --- 2. La escaleta ----------------------------------------------------------------------
    {
        Corrida c(l, base());
        c.arrancar(T0);
        const int64_t t = T0 + 90 * S; // p1 se pasa: lleva 1:30 de 1:00
        const Resolucion r = c.resolver(t);
        ContextoVista ctx;
        ctx.ahoraMs = t;
        ctx.salida = "principal";
        ctx.instancias = c.instancias();
        ctx.editable = [&](const std::string& p, std::string& porque) {
            if (p != "p1") return true;
            porque = "al aire";
            return false;
        };
        ctx.pendientes = {"p3"};
        const json esc = escaleta(l, r, ctx);
        CHECK(esc.size() == 1 && esc[0]["pasos"].size() == 5, "un bloque con sus cinco pasos");
        const json* p1 = pasoDe(esc, "p1");
        const json* p2 = pasoDe(esc, "p2");
        const json* p4 = pasoDe(esc, "p4");
        CHECK(p1 && (*p1)["estado"] == "alAire" && (*p1)["numero"] == 1, "p1 al aire, número 1");
        CHECK(p1 && (*p1)["realSeg"] == 90 && (*p1)["overSeg"] == 30, "lo real y el over de p1");
        CHECK(p1 && (*p1)["editable"] == false && (*p1)["porque"] == "al aire", "lo emitido no se edita, con el porqué");
        CHECK(p1 && (*p1)["mini"]["fuente"] == "programa", "p1 con el programa");
        CHECK(p2 && (*p2)["estado"] == "proximo" && (*p2)["entra"] == "go", "p2 próximo, entra con GO");
        CHECK(p2 && (*p2)["chips"].size() == 2 && (*p2)["chips"][1]["manual"] == true, "los chips de p2, el adlib manual");
        CHECK(p4 && (*p4)["entra"] == "hora" && (*p4)["entraHora"] == "19:04", "p4 es un hard start");
        const json* p3 = pasoDe(esc, "p3");
        CHECK(p3 && (*p3)["pendiente"] == true, "lo pendiente se marca");
    }

    // --- 3. La conducción ---------------------------------------------------------------------
    {
        Corrida c(l, base());
        c.arrancar(T0);
        c.go("principal", T0 + 60 * S); // p2 al aire
        const int64_t t = T0 + 70 * S;
        const Resolucion r = c.resolver(t);
        ContextoVista ctx;
        ctx.ahoraMs = t;
        ctx.salida = "principal";
        const json est = c.estadoJson(t);
        json sal;
        for (const json& x : est["salidas"])
            if (x["salida"] == "principal") sal = x;
        const json cond = conduccion(l, r, ctx, sal);
        CHECK(cond["alAire"]["escena"] == "B", "al aire, con su escena");
        CHECK(cond["alAire"]["adlibs"].size() == 1 && cond["alAire"]["adlibs"][0]["id"] == "z2", "sus adlibs: las manuales");
        CHECK(cond["proximo"]["escena"] == "C" && cond["proximo"]["mini"]["fuente"] == "pvw", "el próximo con su PVW");
        CHECK(cond["bloque"]["nombre"] == "Apertura", "el bloque con su nombre");
        CHECK(cond["proximoHardStart"]["nombre"] == "Paso p4", "el hard start con el nombre de su paso");
    }

    // --- 4. El estante -----------------------------------------------------------------------
    {
        const json e = estante(l);
        CHECK(e.size() == 2, "dos ítems");
        CHECK(e[0]["tecla"] == "F2" && e[1]["tecla"] == "F1", "F1 ya es de e2: e1 toma la F2");
        CHECK(e[0]["texto"] == "Risas", "el botón es el texto");
        CHECK(itemDeTecla(l, "F2") == "e1" && itemDeTecla(l, "F9").empty(), "la tecla dispara su ítem");
    }

    // --- 5. La multipista ---------------------------------------------------------------------
    {
        Corrida c(l, base());
        c.arrancar(T0);
        const int64_t t = T0 + 90 * S;
        const Resolucion r = c.resolver(t);
        ContextoVista ctx;
        ctx.ahoraMs = t;
        ctx.salida = "principal";
        Ventana v{t - 2 * M, t + 4 * M};
        const json mp = multipista(l, r, ctx, v, 0);
        CHECK(mp["ahora"].get<double>() > 0.33 && mp["ahora"].get<double>() < 0.34, "«ahora» a un tercio");
        CHECK(mp["salidas"].size() == 1, "una salida");
        const json& carriles = mp["salidas"][0]["carriles"];
        CHECK(carriles[0]["escena"] == true, "el primer carril es el de escena");
        const json& caja = carriles[0]["items"][0];
        CHECK(caja["paso"] == "p1" && caja["emitido"] == true, "la caja de p1, emitida");
        CHECK(caja["over"] == "+0:30" && caja["xPrevisto"].get<double>() < caja["x1"].get<double>(), "el fantasma y el over");
        CHECK(carriles[0]["items"][1]["manuales"].size() == 1, "el adlib de p2 va como botón de su caja");
        CHECK(carriles[1]["tipo"] == "graficos" && carriles[1]["items"].size() == 1, "el zócalo en su carril");
        CHECK(mp["globales"].size() == 1 && mp["globales"][0]["tipo"] == "notas", "las globales aparte");
        CHECK(mp["hardStarts"].size() == 1 && mp["hardStarts"][0]["nivel"] == "justo", "el hard start de las 19:04, justo");
        const json rg = regla(v, 0, [](int64_t ms) {
            char b[16];
            std::snprintf(b, sizeof b, "%02lld:%02lld:%02lld", static_cast<long long>(ms / H), static_cast<long long>(ms % H / M),
                          static_cast<long long>(ms % M / S));
            return std::string(b);
        });
        bool hay1902 = false;
        for (const json& m : rg)
            if (m.value("texto", std::string()) == "19:02") hay1902 = true;
        CHECK(hay1902, "la regla marca 19:02 (minutos redondos, sin segundos)");
        CHECK(conSigno(-3725) == "-1:02:05" && conSigno(42) == "+0:42", "conSigno");
    }

    if (g_fallos == 0) std::printf("test_linea_vista: todo bien\n");
    return g_fallos == 0 ? 0 : 1;
}
