// ============================================================================
// src/gateway/tests/test_grillas.cpp — el mosaico de la grilla de personas
//
//   1. Lo que manda la app se valida entero: si algo está mal, no cambia nada.
//   2. Cada celda pinta el retrato de su persona; sin retrato, la silueta; el
//      fondo queda como se pidió (transparente de fábrica).
//   3. El «+N» se dibuja (sin fuentes del sistema) adentro de su cuadro.
//   4. Los lugares se animan: a mitad de la transición la cara está a mitad de
//      camino; la que sale se achica y después se va.
//   5. La calidad: avisa el alto del lugar más grande de cada uno (y 0 al salir).
//   6. Quién habla: entra con voz sostenida, sale después del silencio.
//   7. Solo se guardan cuadros de quien está en alguna grilla.
//   8. La calidad que se le pide a cada uno según dónde se lo ve: al aire, la de
//      siempre; en una grilla, la de su lugar; en la fila sin grilla, baja (y vuelve
//      al pasar a un lugar o a una grilla).
// ============================================================================

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Grillas.hpp"
#include "Log.hpp"

using ba::gateway::Grillas;
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

std::uint32_t pixel(const std::vector<std::uint8_t>& l, int w, int x, int y) {
    std::uint32_t v = 0;
    std::memcpy(&v, l.data() + (static_cast<std::size_t>(y) * w + x) * 4, 4);
    return v;
}

std::vector<std::uint8_t> liso(int w, int h, std::uint32_t c) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < px.size(); i += 4) std::memcpy(px.data() + i, &c, 4);
    return px;
}

json celda(const std::string& p, int x, int y, int w, int h, bool resaltar = false) {
    return {{"persona", p}, {"x", x}, {"y", y}, {"w", w}, {"h", h}, {"resaltar", resaltar}};
}

} // namespace

int main() {
    ba::gateway::Log log;
    // 1. Validación entera.
    {
        Grillas g(log);
        CHECK(!g.aplicar(json::object(), 0).empty(), "sin «grillas»");
        CHECK(!g.aplicar({{"grillas", {{{"canal", 9}}}}}, 0).empty(), "canal fuera de rango");
        CHECK(!g.aplicar({{"grillas", {{{"canal", 1}}, {{"canal", 1}}}}}, 0).empty(), "canal repetido");
        CHECK(!g.aplicar({{"grillas", {{{"canal", 1}, {"celdas", {celda("a", 0, 0, 10, 10), celda("a", 10, 0, 10, 10)}}}}}}, 0)
                   .empty(),
              "la misma persona dos veces en una grilla");
        CHECK(g.estado()["grillas"].empty(), "lo malo no dejó nada a medias");
        CHECK(g.aplicar({{"grillas", {{{"canal", 2}, {"ancho", 320}, {"alto", 180}}}}}, 0).empty(), "una vacía, bien");
        CHECK(g.estado()["grillas"].size() == 1, "una grilla");
        CHECK(g.aplicar({{"grillas", json::array()}}, 0).empty() && g.estado()["grillas"].empty(), "la que no viene se apaga");
    }
    // 2 y 3. Pintar.
    {
        Grillas g(log);
        std::vector<std::pair<std::string, int>> calidad;
        g.setAlCambiarCalidad([&](const std::string& p, int alto) { calidad.emplace_back(p, alto); });
        const json pedido = {{"grillas",
                              {{{"canal", 1},
                                {"ancho", 320},
                                {"alto", 180},
                                {"transicion_ms", 0},
                                {"celdas", {celda("sala:a", 0, 0, 160, 90, true), celda("sala:b", 160, 0, 160, 90)}},
                                {"mas", {{"n", 7}, {"x", 0}, {"y", 90}, {"w", 160}, {"h", 90}}}}}}};
        CHECK(g.aplicar(pedido, 1000).empty(), "aplica");
        // 5. Calidad: el alto del lugar de cada uno.
        CHECK(calidad.size() == 2 && calidad[0].second == 90 && calidad[1].second == 90, "avisa el alto de cada uno");
        // Un cuadro rojo de «a» (más grande que su lugar: se guarda achicado).
        const std::uint32_t rojo = 0xFFFF0000u; // AARRGGBB = en memoria B G R A
        const auto px = liso(640, 360, rojo);
        g.cuadro("sala:a", px.data(), 640, 360, 1000);
        g.cuadro("sala:nadie", px.data(), 640, 360, 1000); // 7. no está en ninguna grilla: se ignora
        std::vector<std::uint8_t> l;
        int w = 0, h = 0;
        CHECK(g.pintar(1, 1000, l, w, h) && w == 320 && h == 180, "pinta la grilla 1");
        CHECK(pixel(l, w, 80, 45) == rojo, "la celda de «a» tiene su cuadro");
        CHECK(pixel(l, w, 1, 45) != rojo, "y el borde de quien habla");
        CHECK(pixel(l, w, 240, 45) != rojo && (pixel(l, w, 240, 45) >> 24) == 0xFF, "«b» sin cuadro: la silueta (opaca)");
        CHECK(pixel(l, w, 240, 135) == 0, "el fondo, transparente de fábrica");
        // El «+7»: hay blanco adentro de su cuadro y su fondo es oscuro.
        int blancos = 0;
        for (int y = 90; y < 180; ++y)
            for (int x = 0; x < 160; ++x) blancos += pixel(l, w, x, y) == 0xFFFFFFFFu ? 1 : 0;
        CHECK(blancos > 50, "el «+N» se dibuja");
        CHECK(pixel(l, w, 2, 92) == 0xE0202020u, "sobre un cuadro oscuro");
        const json e = g.estado();
        CHECK(e["grillas"][0]["celdas"][0]["imagen"] == true && e["grillas"][0]["celdas"][1]["imagen"] == false,
              "el estado dice quién tiene imagen");
        // 5. Al salir, calidad 0.
        calidad.clear();
        CHECK(g.aplicar({{"grillas", {{{"canal", 1}, {"ancho", 320}, {"alto", 180}, {"celdas", {celda("sala:a", 0, 0, 320, 180)}}}}}},
                        2000)
                  .empty(),
              "otra disposición");
        bool b0 = false, aGrande = false;
        for (const auto& [p, a] : calidad) {
            b0 = b0 || (p == "sala:b" && a == 0);
            aGrande = aGrande || (p == "sala:a" && a == 180);
        }
        CHECK(b0 && aGrande, "«b» sale (0) y «a» crece (180)");
    }
    // 4. Animación.
    {
        Grillas g(log);
        auto pedido = [](int x) {
            return json{{"grillas",
                         {{{"canal", 1}, {"ancho", 400}, {"alto", 100}, {"transicion_ms", 1000}, {"celdas", {celda("p", x, 0, 100, 100)}}}}}};
        };
        const std::uint32_t verde = 0xFF00FF00u;
        CHECK(g.aplicar(pedido(0), 0).empty(), "aplica");
        const auto px = liso(100, 100, verde);
        g.cuadro("p", px.data(), 100, 100, 0);
        std::vector<std::uint8_t> l;
        int w = 0, h = 0;
        g.pintar(1, 2000, l, w, h); // terminó de entrar
        CHECK(pixel(l, w, 50, 50) == verde && pixel(l, w, 150, 50) != verde, "quieta en x=0");
        CHECK(g.aplicar(pedido(300), 3000).empty(), "se muda a x=300");
        g.pintar(1, 3500, l, w, h); // a mitad de camino (curva suave: en 0.5 vale 0.5)
        CHECK(pixel(l, w, 200, 50) == verde && pixel(l, w, 50, 50) != verde, "a mitad de camino, x≈150");
        g.pintar(1, 4100, l, w, h);
        CHECK(pixel(l, w, 350, 50) == verde, "llegó");
        // Sale: se achica hacia el centro y después se va.
        CHECK(g.aplicar(json{{"grillas", {{{"canal", 1}, {"ancho", 400}, {"alto", 100}, {"transicion_ms", 1000}}}}}, 5000).empty(),
              "sin nadie");
        g.pintar(1, 5500, l, w, h);
        CHECK(pixel(l, w, 350, 50) == verde && pixel(l, w, 305, 5) != verde, "saliendo: más chica, centrada");
        g.pintar(1, 6100, l, w, h);
        CHECK(pixel(l, w, 350, 50) != verde, "ya se fue");
        CHECK(Grillas::suavizar(0) == 0 && Grillas::suavizar(1) == 1 && Grillas::suavizar(0.5f) == 0.5f, "la curva");
    }
    // 6. Quién habla.
    {
        Grillas g(log);
        std::vector<std::vector<std::string>> avisos;
        g.setAlCambiarHablan([&](const std::vector<std::string>& ids) { avisos.push_back(ids); });
        std::vector<float> voz(960, 0.2f), silencio(960, 0.0f); // 10 ms estéreo a 48 kHz
        for (int t = 0; t < 150; t += 10) g.audio("sala:x", voz.data(), voz.size(), 2, t);
        CHECK(avisos.empty(), "un golpe corto no es hablar");
        for (int t = 150; t < 400; t += 10) g.audio("sala:x", voz.data(), voz.size(), 2, t);
        CHECK(avisos.size() == 1 && avisos[0] == std::vector<std::string>{"sala:x"}, "voz sostenida: habla");
        for (int t = 400; t < 1200; t += 10) g.audio("sala:x", silencio.data(), silencio.size(), 2, t);
        CHECK(avisos.size() == 1, "una pausa entre frases no lo apaga");
        for (int t = 1200; t < 2000; t += 10) g.audio("sala:x", silencio.data(), silencio.size(), 2, t);
        CHECK(avisos.size() == 2 && avisos[1].empty(), "callado: deja de hablar");
    }
    // Colores.
    {
        std::uint32_t c = 0;
        CHECK(Grillas::color("#FFC107", c) && c == 0xFFFFC107u, "#RRGGBB opaco");
        CHECK(Grillas::color("#80112233", c) && c == 0x80112233u, "#AARRGGBB");
        CHECK(!Grillas::color("rojo", c) && !Grillas::color("#12345", c), "lo que no es un color");
    }
    // 8. La calidad según dónde se lo ve.
    {
        using ba::gateway::MediaRoute;
        using ba::gateway::nivelCalidad;
        CHECK(nivelCalidad(MediaRoute::program(1), 0) == "libre", "al aire: la de siempre");
        CHECK(nivelCalidad(MediaRoute::program(1), 90) == "libre", "al aire aunque esté en una grilla: la de siempre");
        CHECK(nivelCalidad(MediaRoute::backstage(), 90) == "180p", "en un cuadrito de una grilla: 180p");
        CHECK(nivelCalidad(MediaRoute::backstage(), 300) == "360p", "en un lugar mediano: 360p");
        CHECK(nivelCalidad(MediaRoute::backstage(), 720) == "libre", "en un lugar grande: la de siempre");
        CHECK(nivelCalidad(MediaRoute::backstage(), 0) == "180p", "en la fila y en ninguna grilla: baja");
        CHECK(nivelCalidad(MediaRoute::backstage(2), 0) == "360p", "en la fila con vista previa: la del cuadrito del operador");
        CHECK(nivelCalidad(MediaRoute::backstage(2), 90) == "360p", "con vista previa y en un cuadrito: manda el más grande");
        CHECK(nivelCalidad(MediaRoute::backstage(2), 720) == "libre", "con vista previa y en un lugar grande: la de siempre");
        CHECK(nivelCalidad(MediaRoute::lobby(1, false), 0) == "libre", "en el lobby: la de siempre");
        CHECK(nivelCalidad(MediaRoute{}, 0) == "libre", "sin ruta: nada que bajar");
    }
    if (g_fallos == 0) std::printf("test_grillas: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
