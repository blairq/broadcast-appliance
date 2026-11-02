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
//   9. La capa de cada celda y los precargados: se juntan por persona (alta si alguna
//      grilla la pide) y se avisa a quién le cambió.
//  10. De la baja a la HD: con `fundir`, el primer cuadro en vivo arranca un fundido
//      desde lo último que se veía; a mitad, mezcla; al final, solo la HD. Nunca negro.
//  11. Los rótulos: se validan; la celda que nombra uno lo pinta en una banda abajo con
//      el color pedido; los que nadie nombra se olvidan.
//  15. Uno nuevo por vuelta: una cámara a 30 que tiembla se ve entera y en orden; una a
//      60, a 30 sin atrasarse; una a 15, sus 15.
// ============================================================================

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Grillas.hpp"
#include "HttpIo.hpp"
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
    // 9. Capas y precarga.
    {
        using ba::gateway::Capa;
        Grillas g(log);
        std::vector<std::string> tocadas;
        g.setAlCambiarCapa([&](const std::string& p) { tocadas.push_back(p); });
        json a = celda("sala:a", 0, 0, 800, 450);
        a["capa"] = "alta";
        json b = celda("sala:b", 800, 0, 100, 56);
        b["capa"] = "baja";
        json b2 = celda("sala:b", 0, 0, 640, 360);
        b2["capa"] = "alta";
        const json pedido = {{"precarga", {"sala:c"}},
                             {"fundido_ms", 250},
                             {"grillas",
                              {{{"canal", 1}, {"ancho", 1280}, {"alto", 720}, {"celdas", {a, b}}},
                               {{"canal", 2}, {"ancho", 1280}, {"alto", 720}, {"celdas", {b2}}}}}};
        CHECK(g.aplicar(pedido, 0).empty(), "aplica con capas");
        CHECK(g.capaDe("sala:a") == Capa::Alta, "«a» en grande: alta");
        CHECK(g.capaDe("sala:b") == Capa::Alta, "«b»: chica en una, grande en otra: alta (la mayor)");
        CHECK(g.capaDe("sala:z") == Capa::Nada, "quien no está: nada dicho");
        CHECK(g.precargada("sala:c") && !g.precargada("sala:a"), "«c» precargada (el siguiente)");
        CHECK(g.fundidoMs() == 250, "el fundido que mandó la app");
        CHECK(tocadas.size() == 3, "se avisa a los tres (a, b, c)");
        tocadas.clear();
        CHECK(g.aplicar(pedido, 100).empty() && tocadas.empty(), "lo mismo otra vez: nada que avisar");
        const json sinC = {{"grillas", {{{"canal", 1}, {"ancho", 1280}, {"alto", 720}, {"celdas", {a, b}}}}}};
        CHECK(g.aplicar(sinC, 200).empty(), "sin la segunda ni la precarga");
        CHECK(g.capaDe("sala:b") == Capa::Baja && !g.precargada("sala:c"), "«b» vuelve a la baja; «c» ya no se precarga");
        CHECK(tocadas.size() == 2, "se avisa a «b» y a «c»");
    }
    // 10. El fundido de la baja a la HD en el retrato.
    {
        Grillas g(log);
        const json pedido = {{"grillas",
                              {{{"canal", 1}, {"ancho", 320}, {"alto", 180}, {"transicion_ms", 0}, {"celdas", {celda("sala:a", 0, 0, 320, 180)}}}}}};
        CHECK(g.aplicar(pedido, 0).empty(), "aplica");
        const std::uint32_t rojo = 0xFFFF0000u, azul = 0xFF0000FFu;
        const auto baja = liso(320, 180, rojo);
        const auto alta = liso(1280, 720, azul);
        g.cuadro("sala:a", baja.data(), 320, 180, 1000); // la baja
        std::vector<std::uint8_t> l;
        int w = 0, h = 0;
        g.pintar(1, 1010, l, w, h);
        CHECK(pixel(l, w, 160, 90) == rojo, "antes: la baja");
        g.fundir("sala:a", 400);
        g.cuadro("sala:a", alta.data(), 1280, 720, 1100); // el primer cuadro HD
        g.pintar(1, 1100, l, w, h);
        CHECK(pixel(l, w, 160, 90) == rojo, "al empezar el fundido se ve la baja (nada de salto)");
        g.cuadroAnterior("sala:a", baja.data(), 320, 180); // la baja sigue llegando durante el fundido
        g.pintar(1, 1300, l, w, h);
        const std::uint32_t mitad = pixel(l, w, 160, 90);
        const unsigned r = (mitad >> 16) & 0xFF, bl = mitad & 0xFF;
        CHECK(r > 60 && r < 200 && bl > 60 && bl < 200 && (mitad >> 24) == 0xFF, "a mitad: la mezcla de las dos (y opaca: nunca negro)");
        CHECK(g.estado()["grillas"][0]["celdas"][0]["fundiendo"] == true, "el estado dice que está fundiendo");
        g.pintar(1, 1600, l, w, h);
        CHECK(pixel(l, w, 160, 90) == azul, "al final, la HD");
        CHECK(g.estado()["grillas"][0]["celdas"][0]["imagen_ancho"] == 320, "el retrato de la HD, achicado al lugar");
        // Sin fundir: un cuadro HD entra de una (comportamiento de antes).
        g.cuadro("sala:a", baja.data(), 320, 180, 2000);
        g.cuadro("sala:a", alta.data(), 1280, 720, 2100);
        g.pintar(1, 2100, l, w, h);
        CHECK(pixel(l, w, 160, 90) == azul, "sin pedido de fundido, sin fundido");
    }
    // 11. Rótulos.
    {
        Grillas g(log);
        CHECK(!g.rotulos(json::object()).empty(), "sin «rotulos»");
        CHECK(!g.rotulos({{"rotulos", {{{"id", "r1"}, {"w", 4}, {"h", 2}, {"a8", "AAAA"}}}}}).empty(), "la máscara no mide w*h");
        // 40x10 todo a 255 (base64 de 400 bytes 0xFF).
        std::string unos(400, '\xFF');
        const std::string b64 = ba::gateway::http::base64(reinterpret_cast<const std::uint8_t*>(unos.data()), unos.size());
        CHECK(g.rotulos({{"rotulos", {{{"id", "r1"}, {"w", 40}, {"h", 10}, {"a8", b64}}}}}).empty(), "un rótulo");
        CHECK(g.tieneRotulo("r1"), "guardado");
        json c = celda("sala:a", 0, 0, 320, 180);
        c["rotulo"] = "r1";
        const json pedido = {{"grillas",
                              {{{"canal", 1},
                                {"ancho", 320},
                                {"alto", 180},
                                {"transicion_ms", 0},
                                {"rotulos", {{"color", "#FF00FF00"}, {"fondo", "#FF000000"}, {"tamano", 0.2}}},
                                {"celdas", {c}}}}}};
        CHECK(g.aplicar(pedido, 0).empty(), "aplica con rótulo");
        const auto px = liso(320, 180, 0xFFFF0000u);
        g.cuadro("sala:a", px.data(), 320, 180, 0);
        std::vector<std::uint8_t> l;
        int w = 0, h = 0;
        g.pintar(1, 10, l, w, h);
        // La banda: los últimos 36 px (0,2 x 180), fondo negro y el texto (todo 255) en verde a la izquierda.
        CHECK(pixel(l, w, 160, 40) == 0xFFFF0000u, "arriba, la cara");
        CHECK(pixel(l, w, 300, 176) == 0xFF000000u, "la banda del rótulo, con su fondo");
        CHECK(pixel(l, w, 20, 162) == 0xFF00FF00u, "y el nombre, del color pedido");
        CHECK(g.estado()["grillas"][0]["celdas"][0]["rotulo_listo"] == true, "el estado dice que el rótulo está");
        CHECK(g.aplicar({{"grillas", {{{"canal", 1}, {"ancho", 320}, {"alto", 180}}}}}, 100).empty() && !g.tieneRotulo("r1"),
              "nadie lo nombra: se olvida");
    }
    {
        // 14. Desde I420 del decodificador (PlanosYuv): rojo en BT.601 (Y 82, U 90, V 240), más
        // grande que su lugar: se achica en I420 y se convierte lo chico; al mismo tamaño, solo
        // se convierte.
        Grillas g(log);
        CHECK(g.aplicar({{"grillas", {{{"canal", 1}, {"ancho", 320}, {"alto", 180}, {"transicion_ms", 0},
                                        {"celdas", {celda("sala:a", 0, 0, 160, 90), celda("sala:b", 160, 0, 160, 90)}}}}}},
                        1000)
                  .empty(),
              "aplica (I420)");
        std::vector<std::uint8_t> py(1280 * 720, 82), pu(640 * 360, 90), pv(640 * 360, 240);
        g.cuadro("sala:a", ba::gateway::PlanosYuv{py.data(), pu.data(), pv.data(), 1280, 640, 640, 1280, 720}, 1000);
        g.cuadro("sala:b", ba::gateway::PlanosYuv{py.data(), pu.data(), pv.data(), 1280, 640, 640, 160, 90}, 1000);
        std::vector<std::uint8_t> l;
        int w = 0, h = 0;
        CHECK(g.pintar(1, 1000, l, w, h), "pinta (I420)");
        for (int x : {80, 240}) {
            const std::uint32_t c = pixel(l, w, x, 45);
            CHECK(((c >> 16) & 0xFF) > 220 && ((c >> 8) & 0xFF) < 40 && (c & 0xFF) < 40 && (c >> 24) == 0xFF,
                  "desde I420: rojo (achicado o del mismo tamaño)");
        }
    }
    {
        // 13. El ritmo del pintor: a mitad de camino entre dos cuadros del lienzo, al ritmo del
        // lienzo; sin lienzo (o quieto más de 1 s), el propio, nunca antes de ahora.
        using ba::gateway::Grillas;
        constexpr std::uint64_t ms = 1'000'000ULL, p30 = 1'000'000'000ULL / 30;
        const std::uint64_t envio = 1000 * 1000 * ms; // el lienzo publicó a los 1000 s
        CHECK(Grillas::proximoPintado(envio + 1 * ms, envio, p30, 0) == envio + p30 / 2, "mitad de camino");
        CHECK(Grillas::proximoPintado(envio + p30 / 2, envio, p30, 0) == envio + p30 / 2 + p30, "ya pasó: el siguiente");
        {
            // Desde otra fase, el lazo la corre de a poco a la mitad de camino, sin saltos.
            std::uint64_t ant = envio + 2 * ms, peor = 0, ahora = ant + 3 * ms;
            for (int i = 0; i < 200; ++i) {
                const std::uint64_t ultimo = envio + ((ahora - envio) / p30) * p30;
                const std::uint64_t x = Grillas::proximoPintado(ahora, ultimo, p30, 0, ant);
                peor = std::max(peor, x - ant > p30 ? x - ant - p30 : p30 - (x - ant));
                ant = x;
                ahora = x + 3 * ms;
            }
            const std::uint64_t fase = (ant - envio) % p30;
            CHECK(fase > p30 / 2 - 1 * ms && fase < p30 / 2 + 1 * ms, "el lazo llega a la mitad de camino");
            CHECK(peor <= p30 / 32 + 1, "el lazo corre de a poco (ningún intervalo se aparta más de 1 ms)");
        }
        CHECK(Grillas::proximoPintado(envio + 3 * p30, envio, p30, 0) == envio + 3 * p30 + p30 / 2,
              "el lienzo no publicó todavía el último: sigue su grilla");
        std::uint64_t t = envio + 2 * ms, anterior = 0;
        int intervalosMal = 0;
        for (int i = 0; i < 300; ++i) { // pintando 5 ms cada vez, contra un lienzo parejo a 30
            const std::uint64_t ultimo = envio + ((t - envio) / p30) * p30;
            const std::uint64_t x = Grillas::proximoPintado(t, ultimo, p30, 0);
            if (anterior != 0 && x - anterior != p30) ++intervalosMal;
            anterior = x;
            t = x + 5 * ms;
        }
        CHECK(intervalosMal == 0, "contra un lienzo a 30, cada 33,3 ms exactos (sin batido)");
        // Un lienzo que tiembla (±8 ms, como el motor con carga): ni dos mosaicos en un mismo
        // cuadro del lienzo ni de más; el ritmo medio, el del lienzo.
        {
            std::uint32_t semilla = 99;
            std::vector<std::uint64_t> envios;
            for (int k = 0; k < 400; ++k) {
                semilla = semilla * 1664525u + 1013904223u;
                const std::int64_t temblor = static_cast<std::int64_t>((semilla >> 8) % 17) - 8;
                envios.push_back(envio + static_cast<std::uint64_t>(k) * p30 + static_cast<std::uint64_t>(temblor + 8) * ms);
            }
            std::uint64_t ahora = envio + 9 * ms, ant = 0;
            int pintados = 0, cortos = 0, desparejos = 0;
            while (ahora < envios.back()) {
                std::uint64_t ultimo = 0;
                for (std::uint64_t e : envios)
                    if (e <= ahora) ultimo = e;
                const std::uint64_t x = Grillas::proximoPintado(ahora, ultimo, p30, 0, ant);
                if (ant != 0 && x - ant < p30 / 2) ++cortos;
                if (ant != 0 && (x - ant > p30 + p30 / 32 + 1 || x - ant + p30 / 32 + 1 < p30)) ++desparejos;
                ant = x;
                ++pintados;
                ahora = x + 4 * ms; // lo que tarda en pintar
            }
            const double porSegundo = pintados / ((envios.back() - envio) / 1e9);
            std::printf("pintor contra un lienzo que tiembla: %.2f por segundo, %d a menos de medio período, %d desparejos\n",
                        porSegundo, cortos, desparejos);
            CHECK(cortos == 0, "lienzo que tiembla: nunca dos a menos de medio período");
            CHECK(desparejos == 0, "lienzo que tiembla: intervalos parejos (33,3 ± 1 ms)");
            CHECK(porSegundo > 28.5 && porSegundo < 30.5, "lienzo que tiembla: al ritmo del lienzo");
        }
        CHECK(Grillas::proximoPintado(envio + 2000 * ms, envio, p30, envio + 2010 * ms) == envio + 2010 * ms,
              "lienzo quieto: el ritmo propio");
        CHECK(Grillas::proximoPintado(envio + 2000 * ms, envio, p30, envio + 1990 * ms) == envio + 2000 * ms,
              "atrasado: ahora, sin ráfaga");
        CHECK(Grillas::proximoPintado(envio, 0, 0, envio + 5 * ms) == envio + 5 * ms, "sin lienzo: el ritmo propio");
    }
    // 15. Una cámara a 30 que tiembla contra un pintor a 30 en la misma fase: se ven TODOS
    // sus cuadros, en orden (antes, con el último que llegó, unos se pisaban y otros se
    // repetían: 30 que llegaban se veían a 24-27). Una a 60: se ve a 30, sin atrasarse.
    {
        const auto probar = [&](double cps, int temblorMs, int& vistos, int& fueraDeOrden, int& atrasoMax) {
            Grillas g(log);
            g.aplicar({{"grillas", {{{"canal", 1}, {"ancho", 64}, {"alto", 36}, {"transicion_ms", 0},
                                      {"celdas", {celda("sala:a", 0, 0, 64, 36)}}}}}},
                      0);
            const double periodoCam = 1000.0 / cps, periodoPintor = 1000.0 / 30;
            std::vector<std::uint8_t> l;
            int w = 0, h = 0, k = 0, ultimo = -1;
            std::uint32_t semilla = 12345;
            vistos = fueraDeOrden = atrasoMax = 0;
            for (int j = 0; j < 300; ++j) {
                const double t = 1000 + j * periodoPintor;
                // Los cuadros que llegaron hasta esta vuelta (cada uno con su temblor).
                for (;;) {
                    semilla = semilla * 1103515245u + 12345u;
                    const int temblor = temblorMs > 0 ? static_cast<int>((semilla >> 8) % (2 * temblorMs + 1)) - temblorMs : 0;
                    const double llega = 1000 + k * periodoCam + temblor;
                    if (llega > t) break;
                    // El número del cuadro en el canal rojo (0..255) y en el verde (de a 256).
                    const auto px = liso(64, 36, 0xFF000000u | (static_cast<std::uint32_t>(k & 0xFF) << 16) |
                                                     (static_cast<std::uint32_t>(k >> 8) << 8));
                    g.cuadro("sala:a", px.data(), 64, 36, static_cast<std::int64_t>(std::lround(llega)));
                    ++k;
                }
                g.pintar(1, static_cast<std::int64_t>(std::lround(t)), l, w, h);
                const std::uint32_t p = pixel(l, w, 32, 18);
                const int n = static_cast<int>(((p >> 16) & 0xFF) | (((p >> 8) & 0xFF) << 8));
                if (j > 10) { // ya andando
                    if (n != ultimo) ++vistos;
                    if (n < ultimo) ++fueraDeOrden;
                    atrasoMax = std::max(atrasoMax, k - 1 - n);
                }
                ultimo = n;
            }
        };
        int vistos = 0, fuera = 0, atraso = 0;
        probar(30, 12, vistos, fuera, atraso);
        CHECK(vistos >= 286, "cámara a 30 que tiembla ±12 ms: se ven todos sus cuadros");
        CHECK(fuera == 0, "cámara a 30: en orden");
        CHECK(atraso <= 2, "cámara a 30: lo que espera no pasa de dos cuadros");
        probar(30, 0, vistos, fuera, atraso);
        CHECK(vistos >= 286 && atraso <= 1, "cámara a 30 pareja: todos, sin esperar");
        probar(60, 4, vistos, fuera, atraso);
        CHECK(vistos >= 286 && fuera == 0 && atraso <= 4, "cámara a 60: uno nuevo por vuelta, sin atrasarse (dos vueltas a lo sumo)");
        probar(15, 12, vistos, fuera, atraso);
        CHECK(vistos >= 140 && vistos <= 146 && fuera == 0, "cámara a 15: sus 15");
    }
    if (g_fallos == 0) std::printf("test_grillas: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
