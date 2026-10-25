// ============================================================================
// src/app/tests/test_grilla_personas.cpp — el reparto y la disposición de la grilla de personas
//
//   1. Disposición: las columnas que dejan las celdas más grandes, la última fila
//      centrada, la proporción, nada afuera del lienzo.
//   2. Filtros y orden: origen, cámara, al aire; llegada, quién habla, prioridad.
//   3. Una grilla sola: tope con «+N», lugares estables (el que llega va al final,
//      el que se va deja el hueco al último), resaltar a quien habla.
//   4. Grupos: «en orden» concentra, «parejo» reparte, sin repetir a nadie; el
//      «+N» en la última.
//   5. Ajustes: los de fábrica, validar, el pedido al gateway.
//   6. Al aire en otra grilla: las grillas al aire se reparten primero y las demás
//      no repiten a quien ya se ve en una (sin vaivén).
//   7. A mano: fijar (primero, aunque los filtros digan otra cosa), sacar (de la
//      grilla y su grupo), mover, devolver.
//   8. Lo que se ve: escenas anidadas y capas, sin recorrer dos veces una vuelta.
// ============================================================================
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <string>

#include "escenas/GrillaPersonas.hpp"

using namespace ba::app::grilla;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (false)

Persona sala(const std::string& id, std::int64_t llegada) {
    Persona p;
    p.clave = "sala:" + id;
    p.nombre = id;
    p.deSala = true;
    p.llegadaMs = llegada;
    return p;
}

Persona asamblea(const std::string& id, std::int64_t llegada, int prioridad = 0) {
    Persona p = sala(id, llegada);
    p.clave = "asamblea:" + id;
    p.deSala = false;
    p.prioridad = prioridad;
    return p;
}

std::vector<Persona> gente(int n, std::int64_t desde = 0) {
    std::vector<Persona> out;
    for (int i = 0; i < n; ++i) out.push_back(sala("p" + std::to_string(i), desde + i * 10));
    return out;
}

Config grilla(const std::string& fuente, int tope = 16, const std::string& grupo = "", int orden = 1) {
    Config c = Config::desdeAjustes(fuente, Config::defectos());
    c.canal = 1;
    c.tope = tope;
    c.grupo = grupo;
    c.ordenEnGrupo = orden;
    return c;
}

bool dentro(const Rect& r, int w, int h) { return r.x >= 0 && r.y >= 0 && r.x + r.w <= w && r.y + r.h <= h && r.w > 0 && r.h > 0; }

void disposicion() {
    int cols = 0;
    CHECK(disponer(0, 1280, 720, 8, 0, 16.0 / 9, &cols).empty(), "nadie: ninguna celda");
    auto r = disponer(1, 1280, 720, 8, 0, 16.0 / 9, &cols);
    CHECK(r.size() == 1 && cols == 1 && r[0].w == 1280 && r[0].h == 720, "uno: el lienzo entero");
    r = disponer(4, 1280, 720, 8, 0, 16.0 / 9, &cols);
    CHECK(cols == 2 && r.size() == 4, "cuatro en 16:9: 2x2");
    r = disponer(5, 1280, 720, 8, 0, 16.0 / 9, &cols);
    CHECK(cols == 3, "cinco: tres columnas");
    // La última fila (dos) centrada: su centro coincide con el del lienzo.
    CHECK(r.size() == 5 && std::abs((r[3].x + r[4].x + r[4].w) / 2 - 640) <= 1, "la última fila, centrada");
    CHECK(r[3].y > r[0].y, "la segunda fila, abajo");
    for (int n : {1, 2, 3, 7, 13, 25, 50, 64}) {
        const auto rs = disponer(n, 1280, 720, 8, 10, 16.0 / 9, &cols);
        bool ok = static_cast<int>(rs.size()) == n;
        for (const Rect& x : rs) ok = ok && dentro(x, 1280, 720) && std::abs(x.w * 9 - x.h * 16) <= 16;
        CHECK(ok, "todas adentro del lienzo y en 16:9");
        // Sin superposición entre vecinas.
        for (std::size_t i = 1; i < rs.size(); ++i)
            ok = ok && (rs[i].x >= rs[i - 1].x + rs[i - 1].w || rs[i].y >= rs[i - 1].y + rs[i - 1].h);
        CHECK(ok, "sin pisarse");
    }
    // Una tira vertical (9:16 de lienzo) con celdas 16:9: una columna.
    disponer(3, 360, 1280, 8, 0, 16.0 / 9, &cols);
    CHECK(cols == 1, "lienzo vertical: una columna");
    // «Llenar»: las celdas ocupan todo el lienzo (sin barras).
    r = disponer(2, 1000, 500, 0, 0, 0, &cols);
    CHECK(r.size() == 2 && r[0].w * r[0].h + r[1].w * r[1].h == 1000 * 500, "llenar: sin barras");
}

void filtrosYOrden() {
    Config c = grilla("g");
    Persona s = sala("a", 0), a = asamblea("b", 0);
    c.origen = Origen::Sala;
    CHECK(acepta(c, s) && !acepta(c, a), "origen sala");
    c.origen = Origen::Asamblea;
    CHECK(!acepta(c, s) && acepta(c, a), "origen asamblea");
    c.origen = Origen::Ambas;
    s.conCamara = false;
    CHECK(acepta(c, s), "sin cámara entra si no se filtra");
    c.soloConCamara = true;
    CHECK(!acepta(c, s), "solo con cámara");
    s.conCamara = true;
    s.alAire = true;
    CHECK(!acepta(c, s), "sin los que están al aire (de fábrica)");
    c.sinAlAire = false;
    CHECK(acepta(c, s), "con los que están al aire si se pide");

    Persona x = sala("x", 100), y = sala("y", 50);
    CHECK(antes(Orden::Llegada, y, x), "llegada: el primero que entró");
    x.vozMs = 900;
    y.vozMs = 500;
    CHECK(antes(Orden::Habla, x, y), "habla: el que habló hace menos");
    y.hablando = true;
    CHECK(antes(Orden::Habla, y, x), "habla: el que habla ahora, antes que todos");
    Persona p1 = asamblea("p1", 100, 5), p2 = asamblea("p2", 50, 1);
    CHECK(antes(Orden::Prioridad, p1, p2), "prioridad: mayor primero");
    p2.prioridad = 5;
    CHECK(antes(Orden::Prioridad, p2, p1), "prioridad igual: por llegada");
}

void solaConTope() {
    Repartidor rep;
    std::vector<Config> gs{grilla("g", 4)};
    auto res = rep.calcular(gs, gente(3));
    CHECK(res.size() == 1 && res[0].personas.size() == 3 && res[0].mas == 0, "tres en tope cuatro: entran todos");
    CHECK(res[0].celdas.size() == 3, "una celda por persona");

    res = rep.calcular(gs, gente(6));
    CHECK(res[0].personas.size() == 3 && res[0].mas == 3, "seis en tope cuatro: tres y «+3»");
    CHECK(res[0].rMas.w > 0, "el «+N» tiene lugar");
    CHECK(res[0].personas[0] == "sala:p0" && res[0].personas[2] == "sala:p2", "por llegada: los primeros");

    // Lugares estables: llega uno nuevo (con lugar) → los demás no se mueven.
    Repartidor r2;
    std::vector<Config> g8{grilla("g", 8)};
    auto v = gente(4);
    auto a = r2.calcular(g8, v);
    v.push_back(sala("nuevo", 1));          // llegó «antes» por reloj, pero llega ahora
    auto b = r2.calcular(g8, v);
    CHECK(b[0].personas.size() == 5, "cinco");
    bool quietos = true;
    for (int i = 0; i < 4; ++i) quietos = quietos && a[0].personas[i] == b[0].personas[i];
    CHECK(quietos && b[0].personas[4] == "sala:nuevo", "el que llega va al final; nadie salta");

    // Se va el segundo: el último ocupa su hueco, el resto queda igual.
    v.erase(v.begin() + 1); // p1
    auto c = r2.calcular(g8, v);
    CHECK(c[0].personas.size() == 4, "cuatro");
    CHECK(c[0].personas[0] == "sala:p0" && c[0].personas[1] == "sala:nuevo" && c[0].personas[2] == "sala:p2" &&
              c[0].personas[3] == "sala:p3",
          "el hueco lo toma el último; los demás quietos");

    // Se va uno y llega otro en la misma vuelta: el nuevo toma el hueco, nadie se mueve.
    v.erase(v.begin()); // p0
    v.push_back(sala("otro", 999));
    auto d = r2.calcular(g8, v);
    CHECK(d[0].personas[0] == "sala:otro" && d[0].personas[1] == "sala:nuevo", "el que llega ocupa el hueco del que se fue");

    // Resaltar a quien habla.
    v[1].hablando = true;
    auto e = r2.calcular(g8, v);
    int resaltadas = 0;
    for (const auto& celda : e[0].celdas) resaltadas += celda.resaltar ? 1 : 0;
    CHECK(resaltadas == 1, "una resaltada");
    g8[0].resaltar = false;
    e = r2.calcular(g8, v);
    resaltadas = 0;
    for (const auto& celda : e[0].celdas) resaltadas += celda.resaltar ? 1 : 0;
    CHECK(resaltadas == 0, "sin resaltar si se apagó");

    // Por quién habla y con tope: el que habla entra aunque haya llegado último.
    Repartidor r3;
    std::vector<Config> gh{grilla("g", 3)};
    gh[0].orden = Orden::Habla;
    auto w = gente(5);
    w[4].hablando = true;
    auto f = r3.calcular(gh, w);
    std::set<std::string> visibles(f[0].personas.begin(), f[0].personas.end());
    CHECK(visibles.count("sala:p4") == 1 && f[0].mas == 3, "el que habla entra; «+3»");
}

void grupos() {
    // En orden: concentra en la primera hasta su tope.
    {
        Repartidor rep;
        std::vector<Config> gs{grilla("B", 4, "g", 2), grilla("A", 4, "g", 1)};
        gs[1].canal = 2;
        auto res = rep.calcular(gs, gente(6));
        // res viene en el orden de `gs`: B, A.
        CHECK(res[1].personas.size() == 4 && res[0].personas.size() == 2, "en orden: A llena (4), B el resto (2)");
        std::set<std::string> todas(res[0].personas.begin(), res[0].personas.end());
        todas.insert(res[1].personas.begin(), res[1].personas.end());
        CHECK(todas.size() == 6, "sin repetir a nadie");
        res = rep.calcular(gs, gente(10));
        CHECK(res[1].personas.size() == 4 && res[0].personas.size() == 3 && res[0].mas == 3 && res[1].mas == 0,
              "desborda: el «+N» en la última del grupo");
    }
    // Parejo: la misma cantidad en cada una.
    {
        Repartidor rep;
        std::vector<Config> gs{grilla("A", 8, "g", 1), grilla("B", 8, "g", 2), grilla("C", 8, "g", 3)};
        gs[0].reparto = Reparto::Parejo; // el modo lo manda la primera
        auto res = rep.calcular(gs, gente(7));
        CHECK(res[0].personas.size() == 3 && res[1].personas.size() == 2 && res[2].personas.size() == 2, "parejo: 3/2/2");
        // Se van dos de A: no se mudan todos, se equilibra moviendo a los últimos.
        auto v = gente(7);
        std::vector<std::string> antesB = res[1].personas;
        v.erase(std::remove_if(v.begin(), v.end(),
                               [&](const Persona& p) { return p.clave == res[0].personas[0] || p.clave == res[0].personas[1]; }),
                v.end());
        auto r2 = rep.calcular(gs, v);
        const int total = static_cast<int>(r2[0].personas.size() + r2[1].personas.size() + r2[2].personas.size());
        CHECK(total == 5, "cinco en total");
        CHECK(std::abs(static_cast<int>(r2[0].personas.size()) - static_cast<int>(r2[2].personas.size())) <= 1, "sigue parejo");
        CHECK(r2[1].personas[0] == antesB[0], "el primero de B no se movió");
    }
    // Grillas del grupo con distinto origen: cada uno a la que lo acepta.
    {
        Repartidor rep;
        std::vector<Config> gs{grilla("S", 8, "g", 1), grilla("A", 8, "g", 2)};
        gs[0].origen = Origen::Sala;
        gs[1].origen = Origen::Asamblea;
        std::vector<Persona> v{sala("s1", 1), asamblea("a1", 2), sala("s2", 3), asamblea("a2", 4)};
        auto res = rep.calcular(gs, v);
        bool ok = res[0].personas.size() == 2 && res[1].personas.size() == 2;
        for (const auto& k : res[0].personas) ok = ok && k.rfind("sala:", 0) == 0;
        for (const auto& k : res[1].personas) ok = ok && k.rfind("asamblea:", 0) == 0;
        CHECK(ok, "cada uno a la grilla de su origen");
    }
    // Sin grupo: dos grillas muestran a los mismos (cada una es independiente).
    {
        Repartidor rep;
        std::vector<Config> gs{grilla("X", 8), grilla("Y", 8)};
        auto res = rep.calcular(gs, gente(3));
        CHECK(res[0].personas == res[1].personas && res[0].personas.size() == 3, "sin grupo: cada una con todos");
    }
}

void ajustes() {
    const json d = Config::defectos();
    const Config c = Config::desdeAjustes("g", d);
    CHECK(c.tope == 16 && c.origen == Origen::Ambas && c.orden == Orden::Llegada && c.sinAlAire && c.resaltar, "de fábrica");
    CHECK(c.fondo == "#00000000" && c.colorResaltar == "#FFFFC107", "colores de fábrica (entero de OBS → #AARRGGBB)");
    CHECK(Config::validar(json{{"origen", "sala"}, {"tope", 9}}).empty(), "ajustes buenos");
    CHECK(!Config::validar(json{{"origen", "marte"}}).empty(), "origen raro");
    CHECK(!Config::validar(json{{"tope", 500}}).empty(), "tope fuera de rango");
    CHECK(!Config::validar(json{{"cualquiera", 1}}).empty(), "ajuste desconocido");
    CHECK(!Config::validar(json{{"resaltar", "si"}}).empty(), "booleano mal");
    const Config p = Config::desdeAjustes("g", json{{"proporcion", "4:3"}, {"orden", "prioridad"}, {"reparto", "parejo"}});
    CHECK(std::abs(p.aspecto - 4.0 / 3) < 1e-9 && p.orden == Orden::Prioridad && p.reparto == Reparto::Parejo, "lee los textos");

    Repartidor rep;
    std::vector<Config> gs{grilla("g", 3)};
    gs[0].canal = 2;
    auto res = rep.calcular(gs, gente(5));
    const json pedido = pedidoGateway(gs, res);
    CHECK(pedido["grillas"].size() == 1 && pedido["grillas"][0]["canal"] == 2, "una grilla, su canal");
    CHECK(pedido["grillas"][0]["celdas"].size() == 2 && pedido["grillas"][0]["mas"]["n"] == 3, "dos celdas y «+3»");
    gs[0].canal = 0;
    CHECK(pedidoGateway(gs, res)["grillas"].empty(), "sin canal asignado no va al gateway");
}

void escala() {
    // 50 personas repartidas en un grupo de 3 grillas de 16: 47 + «+N», y la cuenta cierra.
    Repartidor rep;
    std::vector<Config> gs{grilla("A", 16, "g", 1), grilla("B", 16, "g", 2), grilla("C", 16, "g", 3)};
    auto res = rep.calcular(gs, gente(50));
    const std::size_t vistos = res[0].personas.size() + res[1].personas.size() + res[2].personas.size();
    CHECK(vistos == 47 && res[2].mas == 3, "47 a la vista y «+3»");
    // Sin tope: hasta el techo de celdas.
    std::vector<Config> una{grilla("U", 0)};
    auto r = rep.calcular(una, gente(50));
    CHECK(r[0].personas.size() == 50 && r[0].mas == 0, "sin tope: los 50");
}

void alAireEnOtraGrilla() {
    // Dos grillas sin grupo: X al aire, Y en la escena que se prepara. Quien está en X ya
    // se ve: Y no lo repite (y no hay vaivén de una vuelta a la otra).
    {
        Repartidor rep;
        std::vector<Config> gs{grilla("Y", 8), grilla("X", 2)};
        gs[1].alAire = true;
        auto v = gente(4);
        auto res = rep.calcular(gs, v);
        // X (tope 2 con «+N»): uno y «+3». Y: los otros tres.
        CHECK(res[1].personas.size() == 1 && res[1].personas[0] == "sala:p0", "la grilla al aire se reparte primero");
        CHECK(res[0].personas.size() == 3 &&
                  std::find(res[0].personas.begin(), res[0].personas.end(), "sala:p0") == res[0].personas.end(),
              "la otra no repite al que ya está al aire en una grilla");
        auto otra = rep.calcular(gs, v);
        CHECK(otra[0].personas == res[0].personas && otra[1].personas == res[1].personas, "estable: la vuelta siguiente, igual");
        // Con «sin_al_aire» apagado, Y muestra a todos.
        gs[0].sinAlAire = false;
        res = rep.calcular(gs, v);
        CHECK(res[0].personas.size() == 4, "sin el filtro: todos");
    }
    // Las dos al aire y sin grupo: no se repiten; gana la que se reparte primero (el nombre).
    {
        Repartidor rep;
        std::vector<Config> gs{grilla("B", 8), grilla("A", 8)};
        gs[0].alAire = gs[1].alAire = true;
        auto res = rep.calcular(gs, gente(3));
        CHECK(res[1].personas.size() == 3 && res[0].personas.empty(), "dos al aire: nadie repetido");
    }
    // Ninguna al aire: cada una con todos (como antes).
    {
        Repartidor rep;
        std::vector<Config> gs{grilla("B", 8), grilla("A", 8)};
        auto res = rep.calcular(gs, gente(3));
        CHECK(res[0].personas.size() == 3 && res[1].personas.size() == 3, "ninguna al aire: cada una con todos");
    }
    // Una grilla al aire de un grupo cuenta para las grillas de otros grupos.
    {
        Repartidor rep;
        std::vector<Config> gs{grilla("Z", 8), grilla("G1", 2, "g", 1), grilla("G2", 2, "g", 2)};
        gs[2].alAire = true; // solo G2 se ve; G1 está en otra escena
        auto res = rep.calcular(gs, gente(4));
        // El grupo g (en orden): G1 p0,p1; G2 p2,p3. En el aire solo lo de G2.
        CHECK(res[1].personas.size() == 2 && res[2].personas.size() == 2 && res[2].personas[0] == "sala:p2",
              "el grupo se reparte como siempre");
        CHECK(res[0].personas == std::vector<std::string>({"sala:p0", "sala:p1"}),
              "solo lo de la grilla al aire cuenta como al aire");
    }
}

void aMano() {
    // Fijar: entra primero aunque haya llegado último y no haya lugar.
    {
        Repartidor rep;
        Manual m;
        std::vector<Config> gs{grilla("g", 3)};
        auto v = gente(5);
        m.fijar("g", "sala:p4");
        auto res = rep.calcular(gs, v, m);
        CHECK(std::find(res[0].personas.begin(), res[0].personas.end(), "sala:p4") != res[0].personas.end() && res[0].mas == 3,
              "la fijada entra (y el «+N» cuenta al resto)");
        // Fijada aunque los filtros digan otra cosa (al aire en otra fuente, sin cámara).
        v[4].alAire = true;
        v[4].conCamara = false;
        gs[0].soloConCamara = true;
        res = rep.calcular(gs, v, m);
        CHECK(std::find(res[0].personas.begin(), res[0].personas.end(), "sala:p4") != res[0].personas.end(),
              "fijada: los filtros no la sacan");
        // Ya en su lugar, una vuelta más no la mueve.
        const auto antes = res[0].personas;
        res = rep.calcular(gs, v, m);
        CHECK(res[0].personas == antes, "estable con fijados");
    }
    // Fijar en una grilla del grupo: va en esa, no en la primera.
    {
        Repartidor rep;
        Manual m;
        std::vector<Config> gs{grilla("A", 4, "g", 1), grilla("B", 4, "g", 2)};
        m.fijar("B", "sala:p0");
        auto res = rep.calcular(gs, gente(3), m);
        CHECK(res[1].personas.size() == 1 && res[1].personas[0] == "sala:p0", "fijada en B");
        CHECK(res[0].personas.size() == 2 &&
                  std::find(res[0].personas.begin(), res[0].personas.end(), "sala:p0") == res[0].personas.end(),
              "y no repetida en A");
        res = rep.calcular(gs, gente(3), m);
        CHECK(res[1].personas.size() == 1 && res[1].personas[0] == "sala:p0" && res[0].personas.size() == 2,
              "en orden con un fijado en B: nadie la larga en la vuelta siguiente");
        // Mover: sacar de B y fijar en A (mismo grupo: fijar alcanza).
        m.fijar("A", "sala:p0");
        res = rep.calcular(gs, gente(3), m);
        CHECK(std::find(res[0].personas.begin(), res[0].personas.end(), "sala:p0") != res[0].personas.end() &&
                  std::find(res[1].personas.begin(), res[1].personas.end(), "sala:p0") == res[1].personas.end(),
              "movida a A");
    }
    // Sacar: no aparece en esa ni en su grupo; devolver la trae de vuelta.
    {
        Repartidor rep;
        Manual m;
        std::vector<Config> gs{grilla("A", 2, "g", 1), grilla("B", 8, "g", 2), grilla("Sola", 8)};
        m.sacar("A", "sala:p0");
        auto res = rep.calcular(gs, gente(3), m);
        bool enGrupo = false;
        for (int i : {0, 1})
            enGrupo = enGrupo || std::find(res[i].personas.begin(), res[i].personas.end(), "sala:p0") != res[i].personas.end();
        CHECK(!enGrupo, "sacada: ni en A ni en B (su grupo)");
        CHECK(std::find(res[2].personas.begin(), res[2].personas.end(), "sala:p0") != res[2].personas.end(),
              "en otra grilla, fuera del grupo, sigue");
        m.devolver("A", "sala:p0");
        res = rep.calcular(gs, gente(3), m);
        CHECK(std::find(res[0].personas.begin(), res[0].personas.end(), "sala:p0") != res[0].personas.end() ||
                  std::find(res[1].personas.begin(), res[1].personas.end(), "sala:p0") != res[1].personas.end(),
              "devuelta: vuelve al grupo");
    }
    // El Manual: fijar suelta el fijo anterior y la saca de «fuera»; sacar suelta el fijo.
    {
        Manual m;
        m.sacar("A", "k");
        m.fijar("A", "k");
        CHECK(m.fijo("A", "k") && !m.sacado("A", "k"), "fijar la devuelve");
        m.fijar("B", "k");
        CHECK(m.fijo("B", "k") && !m.fijo("A", "k"), "un fijo por persona");
        m.sacar("B", "k");
        CHECK(!m.fijo("B", "k") && m.sacado("B", "k"), "sacar suelta el fijo");
        m.fijar("C", "j");
        m.olvidarFuentesSalvo({"C"});
        CHECK(m.fijo("C", "j") && !m.sacado("B", "k"), "se olvida de las grillas que no están");
    }
    // Fijados en un grupo al aire cuentan como al aire para los demás.
    {
        Repartidor rep;
        Manual m;
        std::vector<Config> gs{grilla("Aire", 2), grilla("Otra", 8)};
        gs[0].alAire = true;
        m.fijar("Aire", "sala:p2");
        auto res = rep.calcular(gs, gente(3), m);
        CHECK(res[0].personas.size() == 1 && res[0].personas[0] == "sala:p2", "la fijada, en la grilla al aire");
        CHECK(std::find(res[1].personas.begin(), res[1].personas.end(), "sala:p2") == res[1].personas.end(),
              "y la otra no la repite");
    }
}

void loQueSeVe() {
    // Programa: «Principal» con una cámara, la escena «Panel» adentro (que tiene una grilla y a
    // su vez «Fondo»), y una capa DSK con la escena «Zócalo». «Fondo» contiene a «Panel» (una
    // vuelta): no se recorre dos veces.
    const std::map<std::string, std::vector<std::string>> escenas = {
        {"Principal", {"Cam 1", "Panel"}},
        {"Panel", {"Grilla", "Fondo"}},
        {"Fondo", {"Imagen", "Panel"}},
        {"Zócalo", {"Texto", "Cam 1"}},
        {"Otra", {"Grilla 2"}}};
    const auto itemsDe = [&](const std::string& n) -> std::optional<std::vector<std::string>> {
        const auto it = escenas.find(n);
        if (it == escenas.end()) return std::nullopt;
        return it->second;
    };
    const auto v = fuentesQueSeVen({"Principal", "Zócalo", "Logo"}, itemsDe);
    const std::vector<std::string> quiero{"Cam 1", "Grilla", "Imagen", "Texto", "Logo"};
    CHECK(v == quiero, "las fuentes de las escenas anidadas y de las capas, una vez cada una");
    CHECK(std::find(v.begin(), v.end(), "Grilla 2") == v.end(), "lo de una escena que no está al aire, no");
    CHECK(fuentesQueSeVen({}, itemsDe).empty(), "sin programa: nada");
}

} // namespace

int main() {
    disposicion();
    filtrosYOrden();
    solaConTope();
    grupos();
    ajustes();
    escala();
    alAireEnOtraGrilla();
    aMano();
    loQueSeVe();
    if (g_fallos == 0) std::printf("test_grilla_personas: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
