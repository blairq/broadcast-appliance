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
//   9. El orador (MaquinaOrador): voz sostenida (una tos no alcanza), tiempo mínimo en
//      pantalla, el último que habló se queda, la palabra, a mano gana en el acto, el
//      siguiente para precargar.
//  10. Disposiciones con un lugar grande: destacar (franja abajo, al costado, a la
//      izquierda; en orden; «+N» si no entran) y superponer (nadie tapado; pantalla
//      completa).
//  11. En la grilla: resaltar (nadie se mueve, el orador llena su lugar), destacar (el
//      orden de los demás no cambia y la memoria de lugares tampoco), la escena del
//      orador (una grilla que muestra solo al orador de otra).
//  12. Capas con histéresis y de a una subida; el pedido al gateway (capa, rótulo,
//      precarga, fundido) y los rótulos.
//  13. Sin cambios (la grilla no se toca, el orador en alta), agrandar (no / encima /
//      achicando) y el pase a la escena del orador (a mano, automático, con el asistente).
//  14. Alta definición hasta N personas: con N, cada una por su tamaño; con N + 1, todas en
//      baja menos el orador y el precargado; el borde con histéresis; el que deja de ser
//      orador no se queda en alta por la histéresis del tamaño; el invitado del cupo en una
//      celda chica, baja.
//  15. Alta definición como mucho N a la vez («alta_max», de fábrica 4): con 5, 10 y 40
//      invitados y 0, 1 o 2 oradores, presentador y precargado, nunca más de 4 en alta; la
//      prioridad (orador, presentador, precargado, fijado, tamaño) y sin parpadeo en el borde.
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

Persona hablando(Persona p, std::int64_t desde) {
    p.hablando = true;
    p.hablaDesdeMs = desde;
    return p;
}

std::vector<const Persona*> punteros(const std::vector<Persona>& v) {
    std::vector<const Persona*> out;
    for (const Persona& p : v) out.push_back(&p);
    return out;
}

bool seTocan(const Rect& a, const Rect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

void orador() {
    Config c = grilla("g");
    c.disparo = Disparo::Voz;
    c.vozMs = 1000;
    c.minimoMs = 3000;
    MaquinaOrador m;
    std::vector<Persona> v{sala("a", 0), sala("b", 0), sala("c", 0)};
    v[0] = hablando(v[0], 10000);
    CHECK(m.paso(c, punteros(v), "", 10500).empty(), "medio segundo de voz: todavía no");
    CHECK(MaquinaOrador::siguiente(c, punteros(v), m.actual(), 10500) == "sala:a", "pero ya se precarga (que su alta vaya llegando)");
    CHECK(m.paso(c, punteros(v), "", 11000) == "sala:a", "voz sostenida: orador");
    v[0].hablando = false;
    v[0].hablaDesdeMs = 0;
    v[1] = hablando(v[1], 11500);
    CHECK(m.paso(c, punteros(v), "", 12600) == "sala:a", "otro habla, pero «a» no cumplió su tiempo mínimo");
    v[2] = hablando(v[2], 13500); // una tos
    CHECK(m.paso(c, punteros(v), "", 13800) == "sala:a", "una tos no alcanza (y el mínimo sigue)");
    v[2].hablando = false;
    v[2].hablaDesdeMs = 0;
    CHECK(m.paso(c, punteros(v), "", 14000) == "sala:b", "pasado el mínimo, el que habla");
    v[1].hablando = false;
    v[1].hablaDesdeMs = 0;
    CHECK(m.paso(c, punteros(v), "", 30000) == "sala:b", "nadie habla: se queda el último (no vuelve a la grilla con cada silencio)");
    CHECK(m.paso(c, punteros(v), "sala:c", 30010) == "sala:c", "a mano gana en el acto (aunque no pasó el mínimo)");
    CHECK(m.paso(c, punteros(v), "", 30020) == "sala:c", "suelto a mano: el mínimo lo sostiene");
    std::vector<Persona> sinC{v[0], v[1]};
    CHECK(m.paso(c, punteros(sinC), "", 30030).empty(), "el orador se fue: nadie (sin esperar el mínimo)");

    // La palabra (asamblea).
    Config p = grilla("g");
    p.disparo = Disparo::Palabra;
    p.minimoMs = 3000;
    MaquinaOrador mp;
    std::vector<Persona> w{asamblea("p1", 0), asamblea("p2", 0), asamblea("p3", 0)};
    w[0].palabra = true;
    w[2].siguiente = true;
    CHECK(mp.paso(p, punteros(w), "", 1000) == "asamblea:p1", "con la palabra: orador en el acto");
    CHECK(MaquinaOrador::siguiente(p, punteros(w), mp.actual(), 1000) == "asamblea:p3", "el siguiente de la fila se precarga");
    p.precarga = false;
    CHECK(MaquinaOrador::siguiente(p, punteros(w), mp.actual(), 1000).empty(), "sin precarga, nadie");
    w[0].palabra = false;
    w[1].palabra = true;
    CHECK(mp.paso(p, punteros(w), "", 2000) == "asamblea:p1", "la palabra pasó, pero el mínimo sostiene");
    CHECK(mp.paso(p, punteros(w), "", 4000) == "asamblea:p2", "pasado el mínimo, el que tiene la palabra");
    w[1].palabra = false;
    CHECK(mp.paso(p, punteros(w), "", 8000).empty(), "sin palabra para nadie: vuelve a la grilla");
}

void disposicionesConGrande() {
    Config c = grilla("g");
    c.ancho = 1280;
    c.alto = 720;
    c.separacion = 8;
    c.margen = 0;
    c.destacarTamano = 70;
    // Franja abajo, al centro.
    Disposicion d = disponerDestacar(5, c);
    CHECK(d.resto.size() == 5 && !d.hayMas, "cinco en la franja");
    CHECK(std::abs(d.grande.x + d.grande.w / 2 - 640) <= 1 && d.grande.h <= 504 && d.grande.h >= 480, "el grande, centrado arriba");
    bool ok = true;
    for (std::size_t i = 0; i < d.resto.size(); ++i) {
        ok = ok && dentro(d.resto[i], 1280, 720) && d.resto[i].y >= 504 && d.resto[i].h >= kMinCelda && !seTocan(d.resto[i], d.grande);
        if (i > 0) ok = ok && d.resto[i].x > d.resto[i - 1].x; // en orden, de izquierda a derecha
    }
    CHECK(ok, "la franja: abajo, adentro, en orden, sin tocar al grande");
    // Muchos: «+N».
    d = disponerDestacar(80, c);
    CHECK(d.hayMas && static_cast<int>(d.resto.size()) < 80 && d.rMas.w > 0, "no entran: «+N»");
    ok = true;
    for (const Rect& r : d.resto) ok = ok && r.h >= kMinCelda && dentro(r, 1280, 720);
    CHECK(ok, "las que entran no bajan del mínimo");
    // Al costado, al centro: la franja de la izquierda se llena primero.
    c.franjaCostado = true;
    d = disponerDestacar(4, c);
    CHECK(d.resto.size() == 4 && d.resto[0].x + d.resto[0].w <= d.grande.x && d.resto[1].x + d.resto[1].w <= d.grande.x &&
              d.resto[2].x >= d.grande.x + d.grande.w && d.resto[3].x >= d.grande.x + d.grande.w,
          "al costado: dos a la izquierda (los primeros) y dos a la derecha");
    // A la izquierda.
    c.destacarIzquierda = true;
    d = disponerDestacar(4, c);
    ok = d.grande.x == 0;
    for (const Rect& r : d.resto) ok = ok && r.x >= d.grande.x + d.grande.w;
    CHECK(ok, "a la izquierda: el grande pegado al borde y la franja a la derecha");
    CHECK(disponerDestacar(0, c).grande.w > 0, "solo el orador: el grande igual");

    // Superponer: nadie tapado.
    Config s = grilla("g");
    s.ancho = 1280;
    s.alto = 720;
    s.superponerTamano = 60;
    for (int n : {1, 4, 8, 12, 20, 30}) {
        const Disposicion x = disponerSuperponer(n, s);
        ok = static_cast<int>(x.resto.size()) + (x.hayMas ? 1 : 0) >= std::min(n, 1) && x.grande.w > 0;
        for (const Rect& r : x.resto) ok = ok && !seTocan(r, x.grande) && dentro(r, 1280, 720);
        for (std::size_t i = 1; i < x.resto.size(); ++i) ok = ok && !seTocan(x.resto[i], x.resto[i - 1]);
        CHECK(ok, "superponer: el orador en su hueco, nadie tapado");
    }
    const Disposicion x12 = disponerSuperponer(12, s);
    CHECK(x12.resto.size() == 12 && std::abs(x12.grande.x + x12.grande.w / 2 - 640) <= 8, "doce alrededor del hueco del medio");
    s.superponerCompleta = true;
    const Disposicion completa = disponerSuperponer(12, s);
    CHECK(completa.grande.w == 1280 && completa.grande.h == 720 && completa.resto.empty(), "pantalla completa: el orador y nadie más");
    CHECK(achicar(Rect{0, 0, 100, 50}, 0.5) == (Rect{25, 12, 50, 25}), "achicar al centro");
}

void enLaGrilla() {
    // Resaltar: nadie se mueve; el orador llena su lugar.
    {
        Repartidor rep;
        std::vector<Config> gs{grilla("g", 8)};
        gs[0].vozMs = 1000;
        gs[0].agrandar = Agrandar::Encima;
        auto v = gente(4);
        auto antes = rep.calcular(gs, v, {}, 1000);
        CHECK(antes[0].orador.empty(), "nadie habla: sin orador");
        v[2] = hablando(v[2], 2000);
        auto res = rep.calcular(gs, v, {}, 3500);
        CHECK(res[0].orador == "sala:p2", "habló sostenido: orador");
        std::map<std::string, Rect> ahora, previo;
        for (const auto& x : res[0].celdas) ahora[x.clave] = x.r;
        for (const auto& x : antes[0].celdas) previo[x.clave] = x.r;
        bool quietos = ahora.size() == previo.size();
        for (const auto& [k, r] : ahora)
            if (k != "sala:p2") quietos = quietos && r == previo[k];
        CHECK(quietos, "resaltar: los demás no se mueven ni cambian de tamaño");
        const Rect suya = ahora["sala:p2"], antesSuya = previo["sala:p2"];
        // Contiene su lugar (crece hacia adentro del lienzo si está en un borde).
        CHECK(suya.w > antesSuya.w && suya.h > antesSuya.h && suya.x <= antesSuya.x && suya.y <= antesSuya.y &&
                  suya.x + suya.w >= antesSuya.x + antesSuya.w && suya.y + suya.h >= antesSuya.y + antesSuya.h &&
                  dentro(suya, 1280, 720),
              "el orador, un poco más grande, sobre su mismo lugar y sin salirse del lienzo");
        CHECK(res[0].celdas.back().clave == "sala:p2", "el orador, al final (se pinta encima de las vecinas)");
        CHECK(antes[0].celdas[0].r == disponer(4, 1280, 720, 8, 0, 16.0 / 9)[0], "sin orador, la grilla de siempre (nada achicado)");
        bool resaltado = false;
        for (const auto& x : res[0].celdas) resaltado = resaltado || (x.clave == "sala:p2" && x.resaltar && x.capa == "alta");
        CHECK(resaltado, "con borde y en alta");
    }
    // Destacar: el orden de los demás no cambia, y al volver cada uno está donde estaba.
    {
        Repartidor rep;
        std::vector<Config> gs{grilla("g", 16)};
        gs[0].alHablar = AlHablar::Destacar;
        gs[0].disparo = Disparo::Palabra;
        gs[0].minimoMs = 2000;
        auto v = gente(6);
        auto a = rep.calcular(gs, v, {}, 0);
        v[3].palabra = true;
        auto b = rep.calcular(gs, v, {}, 1000);
        CHECK(b[0].orador == "sala:p3" && b[0].personas == a[0].personas, "destacar: la memoria de lugares no cambia");
        std::map<std::string, Rect> r;
        for (const auto& x : b[0].celdas) r[x.clave] = x.r;
        CHECK(r["sala:p3"].w * r["sala:p3"].h > 4 * r["sala:p0"].w * r["sala:p0"].h, "el orador, grande");
        CHECK(r["sala:p0"].x < r["sala:p1"].x && r["sala:p1"].x < r["sala:p2"].x && r["sala:p2"].x < r["sala:p4"].x &&
                  r["sala:p4"].x < r["sala:p5"].x,
              "los demás en la franja, en el mismo orden (no se mezclan)");
        v[3].palabra = false;
        auto c = rep.calcular(gs, v, {}, 1500);
        CHECK(c[0].orador == "sala:p3", "sin la palabra, el mínimo lo sostiene");
        auto d = rep.calcular(gs, v, {}, 3500);
        bool igual = d[0].orador.empty() && d[0].celdas.size() == a[0].celdas.size();
        for (std::size_t i = 0; igual && i < d[0].celdas.size(); ++i)
            igual = d[0].celdas[i].clave == a[0].celdas[i].clave && d[0].celdas[i].r == achicar(a[0].celdas[i].r, 1.0);
        CHECK(igual, "al terminar, cada uno vuelve a su lugar");
    }
    // El orador entra aunque no haya lugar (tope chico), sin desplazar a los fijados.
    {
        Repartidor rep;
        std::vector<Config> gs{grilla("g", 3)};
        gs[0].alHablar = AlHablar::Destacar;
        gs[0].disparo = Disparo::Palabra;
        auto v = gente(6);
        v[5].palabra = true;
        auto res = rep.calcular(gs, v, {}, 0);
        const std::set<std::string> vis(res[0].personas.begin(), res[0].personas.end());
        CHECK(vis.count("sala:p5") && res[0].orador == "sala:p5", "el orador entra aunque llegó último");
    }
    // En un grupo, el orador de una grilla queda en ESA grilla (no se lo lleva el reparto).
    {
        Repartidor rep;
        std::vector<Config> gs{grilla("A", 3, "g", 1), grilla("B", 3, "g", 2)};
        gs[1].canal = 2;
        gs[1].alHablar = AlHablar::Destacar;
        gs[1].disparo = Disparo::Palabra;
        gs[0].alHablar = AlHablar::Nada;
        auto v = gente(5);
        v[0].palabra = true; // llegó primero: el reparto «en orden» lo pondría en A
        auto res = rep.calcular(gs, v, {}, 0);
        CHECK(res[1].orador == "sala:p0", "B tiene su orador");
        CHECK(std::find(res[1].personas.begin(), res[1].personas.end(), "sala:p0") != res[1].personas.end() &&
                  std::find(res[0].personas.begin(), res[0].personas.end(), "sala:p0") == res[0].personas.end(),
              "y está en B, no en A");
    }
    // La escena del orador: otra grilla muestra solo al orador de esta.
    {
        Repartidor rep;
        std::vector<Config> gs{grilla("G", 8), grilla("O", 8)};
        gs[0].alHablar = AlHablar::Escena;
        gs[0].disparo = Disparo::Palabra;
        gs[1].canal = 2;
        gs[1].soloOrador = true;
        gs[1].sigue = "G";
        gs[1].nombres = Nombres::NombreCargo;
        std::vector<Persona> v{asamblea("p1", 0), asamblea("p2", 10), asamblea("p3", 20)};
        v[1].palabra = true;
        v[1].nombre = "Ana Pérez";
        v[1].cargo = "Consejera · banca 12";
        auto res = rep.calcular(gs, v, {}, 0);
        CHECK(res[0].personas.size() == 3 && res[0].orador == "asamblea:p2", "la grilla completa sigue completa");
        CHECK(res[1].celdas.size() == 1 && res[1].celdas[0].clave == "asamblea:p2" && res[1].celdas[0].r.w == 1280,
              "la del orador: solo él, grande");
        CHECK(res[1].celdas[0].capa == "alta" && res[1].celdas[0].rotulo == idRotulo(Nombres::NombreCargo, "Ana Pérez", "Consejera · banca 12"),
              "en alta y con su nombre y cargo");
        v[1].palabra = false;
        res = rep.calcular(gs, v, {}, 10000);
        CHECK(res[1].celdas.empty(), "sin orador: vacía");
    }
}

void capasYPedido() {
    CapasGrilla cg;
    auto a = cg.decidir({{"a", {300, false}}, {"b", {300, false}}, {"c", {100, false}}});
    CHECK(a["a"] != a["b"] && !a["c"], "dos para subir: una por vuelta (un cuadro clave a la vez)");
    a = cg.decidir({{"a", {300, false}}, {"b", {300, false}}, {"c", {100, false}}});
    CHECK(a["a"] && a["b"], "a la vuelta siguiente, la otra");
    a = cg.decidir({{"a", {200, false}}, {"b", {300, false}}});
    CHECK(a["a"], "se achicó un poco (200): sigue en alta (histéresis)");
    a = cg.decidir({{"a", {170, false}}, {"b", {300, false}}});
    CHECK(!a["a"], "más chica que el umbral de bajada: baja");
    a = cg.decidir({{"a", {250, false}}, {"b", {300, false}}});
    CHECK(!a["a"], "250 no alcanza para volver a subir (hace falta pasar 260)");
    a = cg.decidir({{"a", {50, true}}, {"b", {300, false}}});
    CHECK(a["a"], "el orador: alta aunque su celda sea chica");

    CHECK(idRotulo(Nombres::No, "Ana", "x").empty(), "sin nombres: sin rótulo");
    CHECK(idRotulo(Nombres::Nombre, "Ana", "x") == idRotulo(Nombres::Nombre, "Ana", "y"), "solo el nombre: el cargo no cuenta");
    CHECK(idRotulo(Nombres::NombreCargo, "Ana", "x") != idRotulo(Nombres::NombreCargo, "Ana", "y"), "con cargo: cuenta");
    CHECK(idRotulo(Nombres::Nombre, "", "x").empty(), "sin nombre: nada");

    Repartidor rep;
    std::vector<Config> gs{grilla("g", 8)};
    gs[0].canal = 3;
    gs[0].nombres = Nombres::Nombre;
    gs[0].disparo = Disparo::Palabra;
    gs[0].fundidoMs = 250;
    auto v = gente(3);
    v[1].palabra = true;
    v[2].siguiente = true;
    auto res = rep.calcular(gs, v, {}, 0);
    const json pedido = pedidoGateway(gs, res);
    CHECK(pedido["fundido_ms"] == 250, "el fundido va al gateway");
    CHECK(pedido["precarga"] == json::array({"sala:p2"}), "el siguiente, precargado");
    const json& g0 = pedido["grillas"][0];
    CHECK(g0["rotulos"]["tamano"].get<double>() > 0.1 && g0["rotulos"]["lugar"] == "abajo", "el estilo de los rótulos");
    bool capaYRotulo = true;
    for (const json& x : g0["celdas"]) capaYRotulo = capaYRotulo && x.contains("capa") && x.contains("rotulo");
    CHECK(capaYRotulo, "cada celda con su capa y su rótulo");
    const auto rot = rotulosDe(gs, res, v);
    CHECK(rot.size() == 3 && rot.at(idRotulo(Nombres::Nombre, "p1", "")).first == "p1", "lo que dice cada rótulo");

    // Los ajustes nuevos.
    CHECK(Config::validar(json{{"al_hablar", "destacar"}, {"disparo", "palabra"}, {"franja", "costado"}, {"nombres", "nombre_cargo"},
                               {"muestra", "orador"}, {"sigue", "Grilla"}, {"fundido_ms", 300}})
              .empty(),
          "ajustes nuevos buenos");
    CHECK(!Config::validar(json{{"al_hablar", "bailar"}}).empty(), "al_hablar raro");
    CHECK(!Config::validar(json{{"destacar_tamano", 99}}).empty(), "tamaño fuera de rango");
    const Config d = Config::desdeAjustes("g", Config::defectos());
    CHECK(d.alHablar == AlHablar::Resaltar && d.disparo == Disparo::Voz && d.precarga && d.nombres == Nombres::No &&
              d.fundidoMs == 300 && d.nombreColor == "#FFFFFFFF" && d.nombreFondo == "#A0000000",
          "los de fábrica");
}

void sinCambiosAgrandarYPase() {
    const auto plano = disponer(4, 1280, 720, 8, 0, 16.0 / 9);
    // Por lugar: la i-ésima persona de la grilla (el orden de los lugares; el orador entró
    // primero porque tiene prioridad) → su rectángulo, con el nombre «L<i>».
    const auto rects = [](const Resultado& r) {
        std::map<std::string, Rect> m;
        for (std::size_t i = 0; i < r.personas.size(); ++i)
            for (const auto& x : r.celdas)
                if (x.clave == r.personas[i]) m["L" + std::to_string(i)] = x.r;
        for (const auto& x : r.celdas) m[x.clave] = x.r;
        return m;
    };
    const auto lugarDe = [](const Resultado& r, const std::string& k) {
        return static_cast<std::size_t>(std::find(r.personas.begin(), r.personas.end(), k) - r.personas.begin());
    };
    auto v = gente(4);
    v[1].palabra = true;
    // Sin cambios: nadie se mueve ni crece, sin borde de orador, pero el orador en alta.
    {
        Repartidor rep;
        std::vector<Config> gs{grilla("g", 8)};
        gs[0].alHablar = AlHablar::SinCambios;
        gs[0].disparo = Disparo::Palabra;
        auto res = rep.calcular(gs, v, {}, 0);
        auto m = rects(res[0]);
        CHECK(res[0].orador == "sala:p1", "sin cambios: igual hay orador");
        CHECK(m["L0"] == plano[0] && m["L1"] == plano[1] && m["L2"] == plano[2] && m["L3"] == plano[3], "sin cambios: la grilla de siempre");
        bool alta = false, borde = false;
        for (const auto& x : res[0].celdas)
            if (x.clave == "sala:p1") {
                alta = x.capa == "alta";
                borde = x.resaltar;
            }
        CHECK(alta && !borde, "el orador en alta (para el fundido), sin borde de orador");
    }
    // Resaltar, agrandar «no» (de fábrica): la grilla de siempre, el orador con borde.
    {
        Repartidor rep;
        std::vector<Config> gs{grilla("g", 8)};
        gs[0].disparo = Disparo::Palabra;
        CHECK(gs[0].agrandar == Agrandar::No, "de fábrica no se agranda");
        auto m = rects(rep.calcular(gs, v, {}, 0)[0]);
        CHECK(m["L0"] == plano[0] && m["L1"] == plano[1] && m["L3"] == plano[3], "agrandar no: nadie cambia de tamaño");
    }
    // Agrandar «achicando»: las demás al 94 %, el orador entero; sin orador, todas al 94 % (nadie salta).
    {
        Repartidor rep;
        std::vector<Config> gs{grilla("g", 8)};
        gs[0].disparo = Disparo::Palabra;
        gs[0].agrandar = Agrandar::Achicando;
        const Resultado r0 = rep.calcular(gs, v, {}, 0)[0];
        auto m = rects(r0);
        const std::size_t lo = lugarDe(r0, "sala:p1"), l0 = lugarDe(r0, "sala:p0");
        CHECK(lo < 4 && l0 < 4 && m["sala:p1"] == plano[lo] && m["sala:p0"] == achicar(plano[l0], kResaltarResto),
              "achicando: el orador llena su lugar, los demás al 94 %");
        auto w = v;
        w[1].palabra = false;
        auto n = rects(rep.calcular(gs, w, {}, 10000)[0]);
        CHECK(n["sala:p1"] == achicar(plano[lo], kResaltarResto) && n["sala:p0"] == m["sala:p0"], "sin orador: todas al 94 %, los demás quietos");
    }
    // El pase a la escena del orador.
    {
        PaseEscenaOrador p;
        using O = PaseEscenaOrador::Orden;
        CHECK(p.paso(PasarEscena::AMano, "sala:a", "Orador · G", "Principal").tipo == O::Nada, "a mano: la app no corta");
        PaseEscenaOrador q;
        auto o = q.paso(PasarEscena::Automatico, "sala:a", "Orador · G", "Principal");
        CHECK(o.tipo == O::Poner && o.escena == "Orador · G" && o.orador == "sala:a" && !o.volver, "automático: a la escena del orador");
        CHECK(q.paso(PasarEscena::Automatico, "sala:a", "Orador · G", "Orador · G").tipo == O::Nada, "el mismo orador: nada");
        CHECK(q.paso(PasarEscena::Automatico, "sala:b", "Orador · G", "Orador · G").tipo == O::Nada,
              "otro orador estando en su escena: nada (la grilla del orador lo sigue sola)");
        o = q.paso(PasarEscena::Automatico, "", "Orador · G", "Orador · G");
        CHECK(o.tipo == O::Poner && o.escena == "Principal" && o.volver, "sin orador: vuelve a la de antes");
        CHECK(q.paso(PasarEscena::Automatico, "", "Orador · G", "Principal").tipo == O::Nada, "y no repite");
        // El operador ya cortó a otra cosa: sin orador, no se la pisa.
        q.paso(PasarEscena::Automatico, "sala:c", "Orador · G", "Principal");
        CHECK(q.paso(PasarEscena::Automatico, "", "Orador · G", "Créditos").tipo == O::Nada, "si el operador cortó a otra, no se vuelve");
        // Con el asistente: se propone (no se corta acá).
        PaseEscenaOrador r;
        o = r.paso(PasarEscena::Asistente, "sala:a", "Orador · G", "Principal");
        CHECK(o.tipo == O::Proponer && o.escena == "Orador · G", "asistente: se propone");
        o = r.paso(PasarEscena::Asistente, "", "Orador · G", "Orador · G");
        CHECK(o.tipo == O::Proponer && o.volver && o.escena == "Principal", "asistente: propone volver");
        PaseEscenaOrador s;
        CHECK(s.paso(PasarEscena::Automatico, "sala:a", "", "Principal").tipo == O::Nada, "sin escena del orador armada: nada");
    }
    CHECK(Config::validar(json{{"al_hablar", "sin_cambios"}, {"agrandar", "achicando"}, {"pasar_a_escena", "asistente"},
                               {"salida_orador", "s2"}, {"escena_orador", "Orador · G"}})
              .empty(),
          "ajustes de las opciones nuevas");
    CHECK(!Config::validar(json{{"agrandar", "mucho"}}).empty() && !Config::validar(json{{"pasar_a_escena", "ya"}}).empty() &&
              !Config::validar(json{{"escena_orador", 3}}).empty(),
          "las opciones nuevas, mal");
    const Config d = Config::desdeAjustes("g", Config::defectos());
    CHECK(d.agrandar == Agrandar::No && d.pasarEscena == PasarEscena::AMano && d.salidaOrador.empty() && d.escenaOrador.empty(),
          "de fábrica: sin agrandar, pase a mano");
}

} // namespace

// Las capas de una vuelta: clave → "alta" | "baja".
std::map<std::string, std::string> capasDe(const Resultado& r) {
    std::map<std::string, std::string> m;
    for (const auto& x : r.celdas) m[x.clave] = x.capa;
    return m;
}

int cuantasAlta(const Resultado& r) {
    int n = 0;
    for (const auto& x : r.celdas) n += x.capa == "alta" ? 1 : 0;
    return n;
}

void altaHasta() {
    // La regla pura del umbral de cantidad.
    CHECK(!todasEnBaja(10, 10, false), "10 de 10: por tamaño");
    CHECK(todasEnBaja(11, 10, false), "11 de 10: todas en baja");
    CHECK(todasEnBaja(10, 10, true), "vuelve a 10 desde 11: sigue en baja (histéresis, uno que sale y entra no la hace saltar)");
    CHECK(!todasEnBaja(9, 10, true), "con 9 vuelve a la regla por tamaño");
    CHECK(!todasEnBaja(64, 0, false) && !todasEnBaja(64, 0, true), "0 = sin límite");
    {
        const Config d = Config::desdeAjustes("g", Config::defectos());
        CHECK(d.altaHasta == 10, "de fábrica, alta hasta 10 personas");
        CHECK(Config::validar(json{{"alta_hasta", 0}}).empty() && Config::validar(json{{"alta_hasta", 20}}).empty(), "alta_hasta bueno");
        CHECK(!Config::validar(json{{"alta_hasta", -1}}).empty() && !Config::validar(json{{"alta_hasta", "diez"}}).empty(),
              "alta_hasta malo");
        CHECK(Config::desdeAjustes("g", json{{"alta_hasta", 3}}).altaHasta == 3, "se lee");
    }

    // La grilla de la prueba de sala: 1920x1080, llenar, margen 16, separación 8, sin tope.
    const auto config = [] {
        Config c = grilla("g", 0);
        c.ancho = 1920;
        c.alto = 1080;
        c.margen = 16;
        c.aspecto = 0;
        c.disparo = Disparo::Palabra;
        return c;
    };
    std::vector<Config> gs{config()};
    Repartidor rep;
    auto v = gente(11);
    v[3].palabra = true; // el orador
    std::vector<Persona> diez(v.begin(), v.begin() + 10);
    std::vector<Resultado> res;
    for (int i = 0; i < 12; ++i) res = rep.calcular(gs, diez, {}, 1000 + i);
    CHECK(!res[0].enBaja && res[0].celdas.size() == 10, "10 personas: por tamaño");
    CHECK(res[0].celdas.front().r.h > kSubirAlta, "las celdas de 10 son grandes");
    CHECK(cuantasAlta(res[0]) == kAltaMaxDefecto, "10 personas en celdas grandes: en alta solo las del tope (alta_max)");

    res = rep.calcular(gs, v, {}, 2000);
    auto capas = capasDe(res[0]);
    CHECK(res[0].enBaja, "11 personas: la grilla en baja");
    CHECK(capas["sala:p3"] == "alta", "el orador sigue en alta");
    CHECK(cuantasAlta(res[0]) == 1, "11 personas: todas en baja menos el orador");

    // El borde: sale uno (quedan 10) y la grilla sigue en baja; vuelve a entrar, igual.
    res = rep.calcular(gs, diez, {}, 3000);
    CHECK(res[0].enBaja && cuantasAlta(res[0]) == 1, "vuelve a 10: sigue en baja (histéresis)");
    res = rep.calcular(gs, v, {}, 3100);
    CHECK(res[0].enBaja && cuantasAlta(res[0]) == 1, "y a 11: nada cambió");
    std::vector<Persona> nueve(v.begin(), v.begin() + 9);
    res = rep.calcular(gs, nueve, {}, 3200);
    CHECK(!res[0].enBaja && cuantasAlta(res[0]) == 2, "9: por tamaño otra vez (sube de a una por vuelta)");

    // El precargado (el siguiente) también en alta, aunque sean más de N.
    {
        Repartidor r2;
        auto w = v;
        w[5].siguiente = true;
        const auto x = r2.calcular(gs, w, {}, 0);
        const auto c = capasDe(x[0]);
        CHECK(x[0].enBaja && c.at("sala:p3") == "alta" && c.at("sala:p5") == "alta" && cuantasAlta(x[0]) == 2,
              "11 personas: el orador y el precargado en alta, el resto en baja");
    }
    // Sin límite (0): 20 personas en celdas de 256 px, la regla por tamaño de siempre.
    {
        Repartidor r2;
        std::vector<Config> g0{config()};
        g0[0].altaHasta = 0;
        auto w = gente(20);
        const auto x = r2.calcular(g0, w, {}, 0);
        CHECK(!x[0].enBaja && x[0].celdas.front().r.h < kSubirAlta && cuantasAlta(x[0]) == 0,
              "sin límite: 20 en celdas medianas no suben (hace falta pasar kSubirAlta)");
    }
    // El que deja de ser orador no se queda en alta por la histéresis del tamaño (medido el
    // 2026-11-01: cada orador que pasaba quedaba en alta en su celda de ~200 px).
    {
        Repartidor r2;
        std::vector<Config> g0{config()};
        g0[0].altaHasta = 0; // solo la regla por tamaño
        g0[0].minimoMs = 0;
        auto w = gente(30);
        w[0].palabra = true;
        auto x = r2.calcular(g0, w, {}, 0);
        const int h = x[0].celdas.front().r.h;
        CHECK(h >= kBajarAlta && h <= kSubirAlta, "30 personas: celdas en la franja de la histéresis");
        CHECK(capasDe(x[0])["sala:p0"] == "alta", "el orador, en alta");
        w[0].palabra = false;
        w[1].palabra = true;
        x = r2.calcular(g0, w, {}, 10000);
        auto c = capasDe(x[0]);
        CHECK(c["sala:p1"] == "alta" && c["sala:p0"] == "baja", "el orador anterior vuelve a la baja (no ganó la alta por su lugar)");
        CHECK(cuantasAlta(x[0]) == 1, "solo el orador en alta");
    }
    // Un invitado del cupo (en el programa, no al aire) en una celda chica de la grilla: baja.
    // La grilla no mira el cupo: pide por lo que se ve de él (y el gateway lo respeta, test_capas).
    {
        Repartidor r2;
        auto w = gente(30);
        const auto x = r2.calcular(gs, w, {}, 0);
        CHECK(capasDe(x[0])["sala:p0"] == "baja", "invitado del cupo en una celda chica: baja");
    }
}

std::set<std::string> enAlta(const std::vector<Resultado>& res) {
    std::set<std::string> s;
    for (const auto& r : res)
        for (const auto& x : r.celdas)
            if (x.capa == "alta") s.insert(x.clave);
    return s;
}

void altaMax() {
    {
        const Config d = Config::desdeAjustes("g", Config::defectos());
        CHECK(d.altaMax == 4, "de fábrica, alta como mucho 4");
        CHECK(Config::validar(json{{"alta_max", 1}}).empty() && Config::validar(json{{"alta_max", 64}}).empty(), "alta_max bueno");
        CHECK(!Config::validar(json{{"alta_max", 0}}).empty() && !Config::validar(json{{"alta_max", "cuatro"}}).empty(),
              "alta_max malo (sin «sin tope»)");
        CHECK(Config::desdeAjustes("g", json{{"alta_max", 3}}).altaMax == 3, "se lee");
        CHECK(Config::desdeAjustes("g", json{{"alta_max", 0}}).altaMax == 1, "fuera de rango: al borde");
    }
    // La prioridad, pura.
    {
        using P = CapasGrilla::Pedido;
        const std::map<std::string, P> pedidos{
            {"o1", {50, true}},  {"o2", {50, true}},
            {"pres", {50, false, false, true}},
            {"prec", {50, false, true}},
            {"fijo", {300, false, false, false, true}},
            {"t", {400, false}}};
        CapasGrilla cg;
        auto a = cg.decidir(pedidos, 1, 4);
        CHECK(a["o1"] && a["o2"] && a["pres"] && a["prec"] && !a["fijo"] && !a["t"],
              "tope 4: los dos oradores, el presentador y el precargado; el fijado y el grande, en baja");
        CapasGrilla c3;
        a = c3.decidir(pedidos, 1, 3);
        CHECK(a["o1"] && a["o2"] && a["pres"] && !a["prec"], "tope 3: el precargado es el que se queda afuera");
        CapasGrilla c6;
        a = c6.decidir(pedidos, 1, 6);
        CHECK(a["fijo"] && !a["t"], "tope 6: sube primero el fijado (aunque el otro sea más grande)");
        a = c6.decidir(pedidos, 1, 6);
        CHECK(a["fijo"] && a["t"], "y a la vuelta siguiente, el otro");
        CapasGrilla c0;
        a = c0.decidir({{"o", {50, true}}, {"pres", {10, false, false, true}}}, 1, 0);
        CHECK(a["o"] && a["pres"], "el presentador pide la alta aunque su celda sea chica");
    }
    // Sin parpadeo en el borde del tope: los que ya están no los saca uno nuevo del mismo grupo.
    {
        using P = CapasGrilla::Pedido;
        std::map<std::string, P> pedidos{{"a", {300, false}}, {"b", {300, false}}, {"c", {300, false}}, {"d", {300, false}}};
        CapasGrilla cg;
        for (int i = 0; i < 4; ++i) cg.decidir(pedidos, 1, 4);
        pedidos["e"] = {500, false};
        bool firme = true;
        for (int i = 0; i < 10; ++i) {
            auto a = cg.decidir(pedidos, 1, 4);
            firme = firme && a["a"] && a["b"] && a["c"] && a["d"] && !a["e"];
        }
        CHECK(firme, "tope lleno: uno más grande que llega no saca a nadie (10 vueltas sin cambios)");
        pedidos["x"] = {50, true};
        auto a = cg.decidir(pedidos, 1, 4);
        int n = 0;
        for (const auto& [k, v] : a) n += v ? 1 : 0;
        CHECK(a["x"] && n == 4 && !a["e"], "llega un orador: alta en el acto y sale uno por tamaño (siguen siendo 4)");
        bool estable = true;
        const auto antes = a;
        for (int i = 0; i < 10; ++i) estable = estable && cg.decidir(pedidos, 1, 4) == antes;
        CHECK(estable, "con el orador: 10 vueltas sin cambios");
    }

    // En la grilla de verdad: 5, 10, 40 invitados; 0, 1 o 2 oradores; presentador y precargado.
    const auto config = [](const std::string& f, int canal) {
        Config c = grilla(f, 0);
        c.canal = canal;
        c.ancho = 1920;
        c.alto = 1080;
        c.margen = 16;
        c.aspecto = 0;
        c.disparo = Disparo::Palabra;
        c.minimoMs = 0;
        return c;
    };
    for (int n : {5, 10, 40}) {
        for (int oradores : {0, 1, 2}) {
            std::vector<Config> gs{config("g", 1), config("h", 2)};
            Repartidor rep;
            auto v = gente(n);
            v[0].presentador = true;
            v[3].siguiente = true;
            Manual manual;
            if (oradores == 1) v[1].palabra = true;
            if (oradores == 2) {
                manual.destacados["g"] = "sala:p1";
                manual.destacados["h"] = "sala:p2";
            }
            bool tope = true, siempre = true, firme = true;
            std::set<std::string> ultimo;
            for (int i = 0; i < 30; ++i) {
                const auto res = rep.calcular(gs, v, manual, 1000 + i * 100);
                const auto alta = enAlta(res);
                tope = tope && alta.size() <= 4;
                siempre = siempre && alta.count("sala:p0") && alta.count("sala:p3");
                if (oradores >= 1) siempre = siempre && alta.count("sala:p1");
                if (oradores == 2) siempre = siempre && alta.count("sala:p2") && alta.size() == 4;
                if (i >= 10) firme = firme && alta == ultimo;
                ultimo = alta;
            }
            const std::string caso = std::to_string(n) + " invitados, " + std::to_string(oradores) + " oradores";
            CHECK(tope, ("nunca más de 4 en alta: " + caso).c_str());
            CHECK(siempre, ("los oradores, el presentador y el precargado siempre en alta: " + caso).c_str());
            CHECK(firme, ("sin parpadeo (sin cambios de capa con todo quieto): " + caso).c_str());
            if (n == 40) CHECK(ultimo.size() == static_cast<std::size_t>(2 + oradores), ("con 40, nadie más por tamaño: " + caso).c_str());
            if (n == 10 && oradores == 0) CHECK(ultimo.size() == 4, "10 en celdas grandes sin orador: el tope se llena (presentador, precargado y 2 por tamaño)");
        }
    }
    // alta_max de dos grillas distintas: manda el más chico.
    {
        std::vector<Config> gs{config("g", 1), config("h", 2)};
        gs[1].altaMax = 2;
        Repartidor rep;
        auto v = gente(10);
        v[0].presentador = true;
        v[1].palabra = true;
        v[3].siguiente = true;
        std::set<std::string> alta;
        for (int i = 0; i < 10; ++i) alta = enAlta(rep.calcular(gs, v, {}, 1000 + i * 100));
        CHECK(alta == std::set<std::string>({"sala:p1", "sala:p0"}), "alta_max 2 en una grilla: el orador y el presentador");
    }
}

// 16. Las entradas se juntan (grilla.reacomodoMinMs): los que llegan dentro del intervalo
//     esperan y entran todos juntos (una sola disposición nueva), en orden; el orador y los
//     fijados no esperan; los que se van, en el acto; 64 entradas seguidas → pocos reacomodos.
void entradasJuntas() {
    Repartidor rep;
    rep.reacomodoMinMs(1500);
    std::vector<Config> gs{grilla("g", 64)};
    std::vector<Persona> v{sala("a", 0)};
    auto r = rep.calcular(gs, v, {}, 10000);
    CHECK(r[0].personas.size() == 1 && r[0].entrando == 0, "el primero entra en el acto");
    const Rect r0 = r[0].celdas.at(0).r;
    for (int i = 0; i < 5; ++i) {
        v.push_back(sala("n" + std::to_string(i), 100 + i));
        r = rep.calcular(gs, v, {}, 10100 + i * 200);
        CHECK(r[0].personas.size() == 1, "los que llegan dentro del intervalo esperan");
        CHECK(r[0].entrando == i + 1, "la grilla dice cuántos esperan");
        CHECK(r[0].mas == 0, "los que esperan no cuentan en el «+N»");
        CHECK(r[0].celdas.size() == 1 && r[0].celdas[0].r == r0, "la grilla no se mueve");
        CHECK(rep.proximoReacomodoMs() == 11500, "entran al cumplirse el intervalo");
    }
    r = rep.calcular(gs, v, {}, 11500);
    CHECK(r[0].personas.size() == 6 && r[0].entrando == 0 && rep.proximoReacomodoMs() == 0, "entran los cinco juntos");
    CHECK(r[0].personas[0] == "sala:a", "el que estaba sigue primero");
    CHECK(r[0].personas[1] == "sala:n0" && r[0].personas[5] == "sala:n4", "los nuevos, en orden de llegada");

    // El que se va sale en el acto aunque haya gente esperando.
    v.push_back(sala("z", 200));
    r = rep.calcular(gs, v, {}, 11600);
    CHECK(r[0].entrando == 1, "z espera");
    v.erase(v.begin()); // se va «a»
    r = rep.calcular(gs, v, {}, 11700);
    CHECK(r[0].personas.size() == 5 && std::find(r[0].personas.begin(), r[0].personas.end(), "sala:a") == r[0].personas.end(),
          "el que se va sale en el acto");
    CHECK(r[0].entrando == 1 && rep.proximoReacomodoMs() == 13000, "y no demora a los que esperan");

    // El orador (y el fijado a mano) no esperan: el fundido al dar la palabra es inmediato.
    Repartidor ro;
    ro.reacomodoMinMs(1500);
    std::vector<Config> go{grilla("g", 16)};
    go[0].disparo = Disparo::Palabra;
    std::vector<Persona> u{sala("a", 0)};
    ro.calcular(go, u, {}, 0);
    u.push_back(sala("b", 1));
    auto y = ro.calcular(go, u, {}, 100);
    CHECK(y[0].entrando == 1, "b espera");
    Persona c = sala("c", 2);
    c.palabra = true;
    u.push_back(c);
    y = ro.calcular(go, u, {}, 200);
    const auto esta = [](const Resultado& x, const std::string& k) {
        return std::find(x.personas.begin(), x.personas.end(), k) != x.personas.end();
    };
    CHECK(esta(y[0], "sala:c") && y[0].orador == "sala:c", "el orador entra en el acto");
    CHECK(!esta(y[0], "sala:b") && y[0].entrando == 1, "el otro sigue esperando");
    Manual m;
    m.fijar("g", "sala:d");
    u.push_back(sala("d", 3));
    y = ro.calcular(go, u, m, 300);
    CHECK(esta(y[0], "sala:d"), "el fijado a mano entra en el acto");

    // 64 que entran de a uno cada 100 ms, con una vuelta por entrada: con el intervalo, la
    // disposición cambia pocas veces; sin él (0), una por cada uno.
    const auto reacomodos = [](int minMs, int& adentro) {
        Repartidor rj;
        rj.reacomodoMinMs(minMs);
        std::vector<Config> g64{grilla("g", 64)};
        std::vector<Persona> w;
        std::vector<Rect> ultimo;
        int n = 0;
        for (int t = 0; t <= 10000; t += 100) {
            if (w.size() < 64) w.push_back(sala("q" + std::to_string(w.size()), t));
            const auto x = rj.calcular(g64, w, {}, t);
            std::vector<Rect> rs;
            for (const Celda& celda : x[0].celdas) rs.push_back(celda.r);
            if (rs != ultimo) {
                ++n;
                ultimo = rs;
            }
            adentro = static_cast<int>(x[0].personas.size());
        }
        return n;
    };
    int adentro = 0;
    const int juntos = reacomodos(1500, adentro);
    CHECK(adentro == 64, "con el intervalo, al final entraron todos");
    CHECK(juntos <= 6, "64 entradas en 6,4 s: a lo sumo un reacomodo cada 1,5 s");
    const int sueltos = reacomodos(0, adentro);
    CHECK(adentro == 64 && sueltos == 64, "sin el intervalo, un reacomodo por cada uno (como antes)");
}

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
    orador();
    disposicionesConGrande();
    enLaGrilla();
    capasYPedido();
    sinCambiosAgrandarYPase();
    altaHasta();
    altaMax();
    entradasJuntas();
    if (g_fallos == 0) std::printf("test_grilla_personas: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
